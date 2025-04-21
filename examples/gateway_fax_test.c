/*
 * SpanDSP - a series of DSP components for telephony
 *
 * gateway_fax_test.c - A simplified test for audio FAX to T38 gateway to T38 FAX
 *
 * Written by GitHub Copilot, based on code by Steve Underwood <steveu@coppice.org>
 *
 * Copyright (C) 2005, 2006, 2009, 2010 Steve Underwood
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*! \file */

/*! \page gateway_fax_test_page Gateway FAX test
\section gateway_fax_test_page_sec_1 What does it do?
This test exercises the FAX path:

Audio FAX <---Audio----> T38 Gateway <---T38/UDPTL----> T38 FAX

The test simulates a complete FAX call between an audio FAX endpoint and a T.38 FAX endpoint,
communicating through a T.38 gateway.
*/

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <stdlib.h>
#include <inttypes.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <sndfile.h>
#include <tiffio.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#include "spandsp.h"
#include "spandsp-sim.h"

#define SAMPLES_PER_CHUNK       160

#define INPUT_TIFF_FILE_NAME    "/workspaces/spandsp/test-data/itu/fax/itutests.tif"
#define OUTPUT_TIFF_FILE_NAME   "gateway_fax_test.tif"
#define OUTPUT_WAVE_FILE_NAME   "gateway_fax_test.wav"

/* Structure to hold our fax endpoint details */
typedef struct {
    int type;                   /* 0: audio fax, 1: t38 fax, 2: t38 gateway */
    union {
        struct {
            fax_state_t *fax_state;
        } f;
        struct {
            t38_terminal_state_t *t38_state;
            t38_core_state_t *t38_core_state;
        } t;
        struct {
            t38_gateway_state_t *t38_gateway_state;
            t38_core_state_t *t38_core_state;
        } g;
    } state;
    t30_state_t *t30_state;     /* Valid only for fax terminals */
    int16_t audio_buf[SAMPLES_PER_CHUNK];
    int audio_buf_len;
    bool phase_e_reached;
    bool completed;
    bool succeeded;
    awgn_state_t *awgn_state;
    g1050_state_t *g1050_path;  /* Network path model for T.38 packets */
    int t38_subst_seq;          /* T.38 packet sequence number */
    int packet_rate;            /* T.38 packet repeat count (redundancy) */
    char tag[10];
} endpoint_t;

/* Global test settings */
bool use_ecm = false;
bool use_tep = false;
bool use_transmit_on_idle = true;
int supported_modems = T30_SUPPORT_V27TER | T30_SUPPORT_V29 | T30_SUPPORT_V17;
float signal_scaling = 1.0f;
SNDFILE *wave_handle = NULL;
bool log_audio = false;
int noise_level = -99;
int t38_version = 1;
double when = 0.0;

/* Forward declaration of endpoints array */
endpoint_t endpoints[3];  /* [0]=audio fax, [1]=t38 gateway, [2]=t38 fax */

static int phase_b_handler(void *user_data, int result)
{
    endpoint_t *ep;
    
    ep = (endpoint_t *) user_data;
    printf("%s: Phase B handler - (0x%X) %s\n", ep->tag, result, t30_frametype(result));
    return T30_ERR_OK;
}

static int phase_d_handler(void *user_data, int result)
{
    endpoint_t *ep;
    
    ep = (endpoint_t *) user_data;
    printf("%s: Phase D handler - (0x%X) %s\n", ep->tag, result, t30_frametype(result));
    return T30_ERR_OK;
}

static void phase_e_handler(void *user_data, int result)
{
    endpoint_t *ep;
    t30_stats_t t;
    
    ep = (endpoint_t *) user_data;
    printf("%s: Phase E handler - (%d) %s\n", ep->tag, result, t30_completion_code_to_str(result));
    t30_get_transfer_statistics(ep->t30_state, &t);
    ep->succeeded = (result == T30_ERR_OK);
    ep->phase_e_reached = true;
}

static void real_time_frame_handler(void *user_data,
                                    bool incoming,
                                    const uint8_t *msg,
                                    int len)
{
    endpoint_t *ep;
    
    ep = (endpoint_t *) user_data;
    printf("%s: %s frame: %s, len=%d\n", 
           ep->tag, 
           incoming ? "Rx" : "Tx", 
           t30_frametype(msg[2]), 
           len);
}

/* Handle T.38 packets between endpoints */
static int tx_packet_handler(t38_core_state_t *s __attribute__((unused)), void *user_data, const uint8_t *buf, int len, int count)
{
    endpoint_t *ep;
    int i;
    
    /* This routine queues messages between two instances of T.38 processing */
    ep = (endpoint_t *) user_data;
    
    /* Simulate sending the packet over a network with the G.1050 model */
    for (i = 0; i < count; i++) {
        if (g1050_put(ep->g1050_path, buf, len, ep->t38_subst_seq, when) < 0)
            printf("%s: Lost packet %d\n", ep->tag, ep->t38_subst_seq);
        ep->t38_subst_seq = (ep->t38_subst_seq + 1) & 0xFFFF;
    }
    
    return 0;
}

static void real_time_gateway_frame_handler(void *user_data,
                                           bool incoming,
                                           const uint8_t *msg,
                                           int len)
{
    endpoint_t *ep;
    
    ep = (endpoint_t *) user_data;
    printf("%s: Real time gateway frame - %s, %s, length = %d\n",
           ep->tag,
           (incoming) ? "PSTN->T.38" : "T.38->PSTN",
           t30_frametype(msg[2]),
           len);
}

static void set_t30_callbacks(t30_state_t *t30, endpoint_t *ep)
{
    t30_set_phase_b_handler(t30, phase_b_handler, ep);
    t30_set_phase_d_handler(t30, phase_d_handler, ep);
    t30_set_phase_e_handler(t30, phase_e_handler, ep);
    t30_set_real_time_frame_handler(t30, real_time_frame_handler, ep);
}

static void init_audio_fax(endpoint_t *ep, bool calling_party)
{
    logging_state_t *logging;
    
    ep->type = 0;  /* Audio FAX */
    
    if ((ep->state.f.fax_state = fax_init(NULL, calling_party)) == NULL)
    {
        fprintf(stderr, "Cannot start FAX instance\n");
        exit(2);
    }
    
    ep->t30_state = fax_get_t30_state(ep->state.f.fax_state);
    
    logging = fax_get_logging_state(ep->state.f.fax_state);
    span_log_set_level(logging, SPAN_LOG_DEBUG | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_SHOW_TAG | SPAN_LOG_SHOW_SAMPLE_TIME);
    span_log_set_tag(logging, ep->tag);
    
    logging = t30_get_logging_state(ep->t30_state);
    span_log_set_level(logging, SPAN_LOG_DEBUG | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_SHOW_TAG | SPAN_LOG_SHOW_SAMPLE_TIME);
    span_log_set_tag(logging, ep->tag);
    
    set_t30_callbacks(ep->t30_state, ep);
    
    ep->awgn_state = NULL;
    if (noise_level > -99)
        ep->awgn_state = awgn_init_dbm0(NULL, 1234567, noise_level);
    
    fax_set_transmit_on_idle(ep->state.f.fax_state, use_transmit_on_idle);
    fax_set_tep_mode(ep->state.f.fax_state, use_tep);
    
    t30_set_supported_modems(ep->t30_state, supported_modems);
    t30_set_supported_t30_features(ep->t30_state,
                                   T30_SUPPORT_IDENTIFICATION
                                   | T30_SUPPORT_SELECTIVE_POLLING
                                   | T30_SUPPORT_SUB_ADDRESSING);
    t30_set_supported_image_sizes(ep->t30_state,
                                  T4_SUPPORT_WIDTH_215MM
                                  | T4_SUPPORT_WIDTH_255MM
                                  | T4_SUPPORT_WIDTH_303MM
                                  | T4_SUPPORT_LENGTH_US_LETTER
                                  | T4_SUPPORT_LENGTH_US_LEGAL
                                  | T4_SUPPORT_LENGTH_UNLIMITED);
    t30_set_supported_bilevel_resolutions(ep->t30_state,
                                          T4_RESOLUTION_R8_STANDARD
                                          | T4_RESOLUTION_R8_FINE
                                          | T4_RESOLUTION_R8_SUPERFINE
                                          | T4_RESOLUTION_R16_SUPERFINE);
    
    t30_set_ecm_capability(ep->t30_state, use_ecm);
    t30_set_supported_compressions(ep->t30_state,
                                  T4_COMPRESSION_T4_1D
                                  | T4_COMPRESSION_T4_2D
                                  | T4_COMPRESSION_T6);
    
    /* Set distinct sender IDs */
    char ident[21];
    snprintf(ident, sizeof(ident), "%s fax", ep->tag);
    t30_set_tx_ident(ep->t30_state, ident);
}

static void init_t38_fax(endpoint_t *ep, bool calling_party)
{
    logging_state_t *logging;
    
    ep->type = 1;  /* T.38 FAX */
    
    if ((ep->state.t.t38_state = t38_terminal_init(NULL, calling_party, 
                                                  tx_packet_handler, 
                                                  (void *) ep)) == NULL)
    {
        fprintf(stderr, "Cannot start the T.38 terminal instance\n");
        exit(2);
    }
    
    ep->t30_state = t38_terminal_get_t30_state(ep->state.t.t38_state);
    ep->state.t.t38_core_state = t38_terminal_get_t38_core_state(ep->state.t.t38_state);
    ep->t38_subst_seq = 0;
    
    logging = t38_terminal_get_logging_state(ep->state.t.t38_state);
    span_log_set_level(logging, SPAN_LOG_DEBUG | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_SHOW_TAG | SPAN_LOG_SHOW_SAMPLE_TIME);
    span_log_set_tag(logging, ep->tag);
    
    logging = t38_core_get_logging_state(ep->state.t.t38_core_state);
    span_log_set_level(logging, SPAN_LOG_DEBUG | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_SHOW_TAG | SPAN_LOG_SHOW_SAMPLE_TIME);
    span_log_set_tag(logging, ep->tag);
    
    logging = t30_get_logging_state(ep->t30_state);
    span_log_set_level(logging, SPAN_LOG_DEBUG | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_SHOW_TAG | SPAN_LOG_SHOW_SAMPLE_TIME);
    span_log_set_tag(logging, ep->tag);
    
    set_t30_callbacks(ep->t30_state, ep);
    
    t38_set_t38_version(ep->state.t.t38_core_state, t38_version);
    t38_terminal_set_fill_bit_removal(ep->state.t.t38_state, false);
    t38_terminal_set_tep_mode(ep->state.t.t38_state, use_tep);
    
    t30_set_supported_modems(ep->t30_state, supported_modems);
    t30_set_supported_t30_features(ep->t30_state,
                                   T30_SUPPORT_IDENTIFICATION
                                   | T30_SUPPORT_SELECTIVE_POLLING
                                   | T30_SUPPORT_SUB_ADDRESSING);
    t30_set_supported_image_sizes(ep->t30_state,
                                  T4_SUPPORT_WIDTH_215MM
                                  | T4_SUPPORT_WIDTH_255MM
                                  | T4_SUPPORT_WIDTH_303MM
                                  | T4_SUPPORT_LENGTH_US_LETTER
                                  | T4_SUPPORT_LENGTH_US_LEGAL
                                  | T4_SUPPORT_LENGTH_UNLIMITED);
    t30_set_supported_bilevel_resolutions(ep->t30_state,
                                          T4_RESOLUTION_R8_STANDARD
                                          | T4_RESOLUTION_R8_FINE
                                          | T4_RESOLUTION_R8_SUPERFINE
                                          | T4_RESOLUTION_R16_SUPERFINE);
    
    t30_set_ecm_capability(ep->t30_state, use_ecm);
    t30_set_supported_compressions(ep->t30_state,
                                  T4_COMPRESSION_T4_1D
                                  | T4_COMPRESSION_T4_2D
                                  | T4_COMPRESSION_T6);
    
    /* Set distinct sender IDs */
    char ident[21];
    snprintf(ident, sizeof(ident), "%s fax", ep->tag);
    t30_set_tx_ident(ep->t30_state, ident);
}

static void init_t38_gateway(endpoint_t *ep)
{
    logging_state_t *logging;
    
    ep->type = 2;  /* T.38 Gateway */
    
    if ((ep->state.g.t38_gateway_state = t38_gateway_init(NULL, tx_packet_handler, (void *) ep)) == NULL)
    {
        fprintf(stderr, "Cannot start T.38 gateway instance\n");
        exit(2);
    }
    
    ep->state.g.t38_core_state = t38_gateway_get_t38_core_state(ep->state.g.t38_gateway_state);
    ep->t38_subst_seq = 0;
    
    logging = t38_gateway_get_logging_state(ep->state.g.t38_gateway_state);
    span_log_set_level(logging, SPAN_LOG_DEBUG | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_SHOW_TAG | SPAN_LOG_SHOW_SAMPLE_TIME);
    span_log_set_tag(logging, ep->tag);
    
    logging = t38_core_get_logging_state(ep->state.g.t38_core_state);
    span_log_set_level(logging, SPAN_LOG_DEBUG | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_SHOW_TAG | SPAN_LOG_SHOW_SAMPLE_TIME);
    span_log_set_tag(logging, ep->tag);
    
    t38_gateway_set_transmit_on_idle(ep->state.g.t38_gateway_state, use_transmit_on_idle);
    t38_gateway_set_supported_modems(ep->state.g.t38_gateway_state, supported_modems);
    t38_gateway_set_fill_bit_removal(ep->state.g.t38_gateway_state, false);
    t38_gateway_set_real_time_frame_handler(ep->state.g.t38_gateway_state, real_time_gateway_frame_handler, (void *) ep);
    t38_gateway_set_ecm_capability(ep->state.g.t38_gateway_state, use_ecm);
    t38_set_t38_version(ep->state.g.t38_core_state, t38_version);
}

/* Count number of pages in a TIFF file */
static int count_tiff_pages(const char *file_name)
{
    TIFF *tiff_file;
    int pages;
    
    if ((tiff_file = TIFFOpen(file_name, "r")) == NULL)
        return 0;
    
    pages = 0;
    do
    {
        pages++;
    }
    while (TIFFReadDirectory(tiff_file));
    
    TIFFClose(tiff_file);
    return pages;
}

int main(int argc, char *argv[])
{
    int16_t audio_log[SAMPLES_PER_CHUNK*4]; /* For 4 channels */
    int opt;
    const char *input_tiff_file_name = INPUT_TIFF_FILE_NAME;
    const char *output_tiff_file_name = OUTPUT_TIFF_FILE_NAME;
    int outframes;
    int j;
    bool completed;
    int expected_pages;
    t30_stats_t t30_stats;
    t38_stats_t t38_stats;
    SF_INFO sfinfo;
    int seq_no;
    double tx_when;
    double rx_when;
    uint8_t msg[1024];
    int msg_len;
    
    /* Parse command line options */
    while ((opt = getopt(argc, argv, "eli:o:tn:")) != -1)
    {
        switch (opt)
        {
            case 'e':
                use_ecm = true;
                break;
            case 'l':
                log_audio = true;
                break;
            case 'i':
                input_tiff_file_name = optarg;
                break;
            case 'o':
                output_tiff_file_name = optarg;
                break;
            case 't':
                use_tep = true;
                break;
            case 'n':
                noise_level = atoi(optarg);
                break;
            default:
                fprintf(stderr, "Usage: %s [-e] [-l] [-i input.tif] [-o output.tif] [-t] [-n noise_level]\n", argv[0]);
                fprintf(stderr, "  -e : Use ECM (Error Correction Mode)\n");
                fprintf(stderr, "  -l : Log audio to file\n");
                fprintf(stderr, "  -i : Input TIFF file name\n");
                fprintf(stderr, "  -o : Output TIFF file name\n");
                fprintf(stderr, "  -t : Use TEP (Talker Echo Protection)\n"); 
                fprintf(stderr, "  -n : Noise level in dBm0\n");
                exit(2);
        }
    }
    
    printf("Gateway FAX test: Audio FAX to T.38 Gateway to T.38 FAX\n");
    printf("Using %s ECM\n", use_ecm ? "with" : "without");
    printf("Using T.38 version %d\n", t38_version);
    
    /* Initialize the audio logging if requested */
    if (log_audio)
    {
        /* Set up an audio file with 4 channels:
           1: Audio FAX output
           2: Audio FAX input 
           3: Gateway audio output (to FAX)
           4: Gateway audio input (from FAX) */
        memset(&sfinfo, 0, sizeof(sfinfo));
        sfinfo.samplerate = 8000;
        sfinfo.channels = 4;
        sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
        
        if ((wave_handle = sf_open(OUTPUT_WAVE_FILE_NAME, SFM_WRITE, &sfinfo)) == NULL)
        {
            fprintf(stderr, "Cannot create audio file '%s': %s\n", 
                    OUTPUT_WAVE_FILE_NAME, sf_strerror(NULL));
            exit(2);
        }
    }
    
    /* Set up the endpoint names */
    strcpy(endpoints[0].tag, "A");     /* Audio FAX */
    strcpy(endpoints[1].tag, "GW");    /* T.38 Gateway */
    strcpy(endpoints[2].tag, "T38");   /* T.38 FAX */
    
    /* Initialize the endpoints */
    init_audio_fax(&endpoints[0], true);     /* Calling party */
    init_t38_gateway(&endpoints[1]);
    init_t38_fax(&endpoints[2], false);      /* Called party */
    
    /* Initialize the G.1050 network models for T.38 packets */
    endpoints[1].g1050_path = g1050_init(1, 1, 100, 33);   /* Gateway to T.38 FAX */
    endpoints[2].g1050_path = g1050_init(1, 1, 100, 33);   /* T.38 FAX to Gateway */
    
    /* Set up the file transfer */
    t30_set_tx_file(endpoints[0].t30_state, input_tiff_file_name, -1, -1);
    t30_set_rx_file(endpoints[2].t30_state, output_tiff_file_name, -1);
    
    completed = false;
    
    /* Run the simulation */
    while (!completed)
    {
        memset(audio_log, 0, sizeof(audio_log));
        
        /* Update T.30 timing for the endpoints */
        span_log_bump_samples(t30_get_logging_state(endpoints[0].t30_state), SAMPLES_PER_CHUNK);
        span_log_bump_samples(fax_get_logging_state(endpoints[0].state.f.fax_state), SAMPLES_PER_CHUNK);
        
        span_log_bump_samples(t38_gateway_get_logging_state(endpoints[1].state.g.t38_gateway_state), SAMPLES_PER_CHUNK);
        span_log_bump_samples(t38_core_get_logging_state(endpoints[1].state.g.t38_core_state), SAMPLES_PER_CHUNK);
        
        span_log_bump_samples(t30_get_logging_state(endpoints[2].t30_state), SAMPLES_PER_CHUNK);
        span_log_bump_samples(t38_terminal_get_logging_state(endpoints[2].state.t.t38_state), SAMPLES_PER_CHUNK);
        span_log_bump_samples(t38_core_get_logging_state(endpoints[2].state.t.t38_core_state), SAMPLES_PER_CHUNK);
        
        /* Gateway processes audio from audio FAX */
        if (t38_gateway_rx(endpoints[1].state.g.t38_gateway_state, endpoints[0].audio_buf, endpoints[0].audio_buf_len))
        {
            printf("Gateway audio processing error\n");
            break;
        }
        
        /* Audio FAX processes audio from gateway */
        fax_rx(endpoints[0].state.f.fax_state, endpoints[1].audio_buf, endpoints[1].audio_buf_len);
        if (!t30_call_active(endpoints[0].t30_state))
        {
            endpoints[0].completed = true;
        }
        
        /* Audio FAX generates new audio */
        endpoints[0].audio_buf_len = fax_tx(endpoints[0].state.f.fax_state, endpoints[0].audio_buf, SAMPLES_PER_CHUNK);
        if (!use_transmit_on_idle && endpoints[0].audio_buf_len < SAMPLES_PER_CHUNK)
        {
            vec_zeroi16(&endpoints[0].audio_buf[endpoints[0].audio_buf_len], SAMPLES_PER_CHUNK - endpoints[0].audio_buf_len);
            endpoints[0].audio_buf_len = SAMPLES_PER_CHUNK;
        }
        
        /* Apply noise if needed */
        if (endpoints[0].awgn_state)
        {
            for (j = 0; j < endpoints[0].audio_buf_len; j++)
                endpoints[0].audio_buf[j] = ((int16_t)(endpoints[0].audio_buf[j]*signal_scaling)) + awgn(endpoints[0].awgn_state);
        }
        
        /* Gateway generates new audio */
        endpoints[1].audio_buf_len = t38_gateway_tx(endpoints[1].state.g.t38_gateway_state, endpoints[1].audio_buf, SAMPLES_PER_CHUNK);
        if (!use_transmit_on_idle && endpoints[1].audio_buf_len < SAMPLES_PER_CHUNK)
        {
            vec_zeroi16(&endpoints[1].audio_buf[endpoints[1].audio_buf_len], SAMPLES_PER_CHUNK - endpoints[1].audio_buf_len);
            endpoints[1].audio_buf_len = SAMPLES_PER_CHUNK;
        }
        
        /* Process any T.38 packets from gateway to T.38 FAX */
        while ((msg_len = g1050_get(endpoints[1].g1050_path, msg, sizeof(msg), when, &seq_no, &tx_when, &rx_when)) >= 0)
        {
            t38_core_rx_ifp_packet(endpoints[2].state.t.t38_core_state, msg, msg_len, seq_no);
        }
        
        /* Process any T.38 packets from T.38 FAX to gateway */
        while ((msg_len = g1050_get(endpoints[2].g1050_path, msg, sizeof(msg), when, &seq_no, &tx_when, &rx_when)) >= 0)
        {
            t38_core_rx_ifp_packet(endpoints[1].state.g.t38_core_state, msg, msg_len, seq_no);
        }
        
        /* Process T.38 terminal timing */
        endpoints[2].completed = t38_terminal_send_timeout(endpoints[2].state.t.t38_state, SAMPLES_PER_CHUNK);
        
        /* Log audio if required */
        if (log_audio)
        {
            /* Four channels:
               1: Audio FAX output
               2: Audio FAX input 
               3: Gateway audio output (to FAX)
               4: Gateway audio input (from FAX) */
            for (j = 0; j < SAMPLES_PER_CHUNK; j++)
            {
                audio_log[4*j + 0] = endpoints[0].audio_buf[j];                /* FAX TX */
                audio_log[4*j + 1] = (j < endpoints[1].audio_buf_len) ? endpoints[1].audio_buf[j] : 0;  /* FAX RX */
                audio_log[4*j + 2] = endpoints[1].audio_buf[j];                /* Gateway TX */
                audio_log[4*j + 3] = (j < endpoints[0].audio_buf_len) ? endpoints[0].audio_buf[j] : 0;  /* Gateway RX */
            }
            outframes = sf_writef_short(wave_handle, audio_log, SAMPLES_PER_CHUNK);
            if (outframes != SAMPLES_PER_CHUNK)
            {
                fprintf(stderr, "Error writing audio file\n");
                break;
            }
        }
        
        when += (float) SAMPLES_PER_CHUNK/(float) SAMPLE_RATE;
        
        /* Check if both sides have completed */
        if (endpoints[0].completed && endpoints[2].completed)
        {
            completed = true;
        }
    }
    
    /* Close audio log if needed */
    if (log_audio && wave_handle)
    {
        if (sf_close(wave_handle))
        {
            fprintf(stderr, "Cannot close audio file '%s': %s\n", 
                    OUTPUT_WAVE_FILE_NAME, sf_strerror(NULL));
            exit(2);
        }
    }
    
    /* Get gateway statistics */
    t38_gateway_get_transfer_statistics(endpoints[1].state.g.t38_gateway_state, &t38_stats);
    printf("\nGateway statistics:\n");
    printf("  Pages transferred:  %d\n", t38_stats.pages_transferred);
    printf("  Bit rate:          %d bps\n", t38_stats.bit_rate);
    printf("  ECM used:          %s\n", (t38_stats.error_correcting_mode) ? "yes" : "no");
    
    /* Check how many pages should have been transferred */
    expected_pages = count_tiff_pages(input_tiff_file_name);
    printf("Expected %d pages to be transferred\n", expected_pages);
    
    /* Print results */
    t30_get_transfer_statistics(endpoints[0].t30_state, &t30_stats);
    printf("\nAudio FAX statistics (Sender):\n");
    printf("  Pages transferred:  %d\n", t30_stats.pages_tx);
    printf("  Image size:         %d x %d pixels\n", t30_stats.width, t30_stats.length);
    printf("  Image resolution:   %d x %d DPI\n", t30_stats.x_resolution, t30_stats.y_resolution);
    printf("  Transfer Rate:      %d bps\n", t30_stats.bit_rate);
    printf("  ECM used:           %s\n", (t30_stats.error_correcting_mode) ? "yes" : "no");
    
    t30_get_transfer_statistics(endpoints[2].t30_state, &t30_stats);
    printf("\nT.38 FAX statistics (Receiver):\n");
    printf("  Pages transferred:  %d\n", t30_stats.pages_rx);
    printf("  Image size:         %d x %d pixels\n", t30_stats.width, t30_stats.length);
    printf("  Image resolution:   %d x %d DPI\n", t30_stats.x_resolution, t30_stats.y_resolution);
    printf("  Transfer Rate:      %d bps\n", t30_stats.bit_rate);
    printf("  ECM used:           %s\n", (t30_stats.error_correcting_mode) ? "yes" : "no");
    
    /* Cleanup */
    fax_free(endpoints[0].state.f.fax_state);
    t38_gateway_free(endpoints[1].state.g.t38_gateway_state);
    t38_terminal_free(endpoints[2].state.t.t38_state);
    
    if (endpoints[0].awgn_state)
        awgn_free(endpoints[0].awgn_state);
    
    g1050_free(endpoints[1].g1050_path);
    g1050_free(endpoints[2].g1050_path);
    
    if (!endpoints[0].phase_e_reached || !endpoints[2].phase_e_reached || !endpoints[0].succeeded || !endpoints[2].succeeded)
    {
        printf("\nTests failed\n");
        exit(2);
    }
    
    printf("\nTest completed successfully\n");
    return 0;
}