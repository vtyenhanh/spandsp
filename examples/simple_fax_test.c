/*
 * SpanDSP - a series of DSP components for telephony
 *
 * simple_fax_test.c - A simplified version of fax_tests.c for audio FAX to audio FAX
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

/*! \page simple_fax_test_page Simple FAX test
\section simple_fax_test_page_sec_1 What does it do?
This test exercises the FAX to FAX path:

Audio fax <--------------------------TDM/RTP-------------------------> Audio fax

The test simulates a complete FAX call between two endpoints, with one transmitting
a TIFF file to the other.
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

#define SAMPLES_PER_CHUNK       160

#define INPUT_TIFF_FILE_NAME    "/workspaces/spandsp/test-data/etsi/fax/etsi_300_242_a4_error.tif"
#define OUTPUT_TIFF_FILE_NAME   "simple_fax_test.tif"
#define OUTPUT_WAVE_FILE_NAME   "simple_fax_test.wav"

/* Structure to hold our fax endpoint details */
typedef struct {
    fax_state_t *fax_state;
    t30_state_t *t30_state;
    int16_t audio_buf[SAMPLES_PER_CHUNK];
    int audio_buf_len;
    bool phase_e_reached;
    bool completed;
    bool succeeded;
    awgn_state_t *awgn_state;
    char tag[10];
} fax_node_t;

fax_node_t fax_node_a;
fax_node_t fax_node_b;
bool use_ecm = false;
bool use_tep = false;
bool use_transmit_on_idle = true;
int supported_modems = T30_SUPPORT_V27TER | T30_SUPPORT_V29 | T30_SUPPORT_V17;
float signal_scaling = 1.0f;
SNDFILE *wave_handle = NULL;
bool log_audio = false;
int noise_level = -99;

static int phase_b_handler(void *user_data, int result)
{
    fax_node_t *s;
    
    s = (fax_node_t *) user_data;
    printf("%s: Phase B handler - (0x%X) %s\n", s->tag, result, t30_frametype(result));
    return T30_ERR_OK;
}

static int phase_d_handler(void *user_data, int result)
{
    fax_node_t *s;
    
    s = (fax_node_t *) user_data;
    printf("%s: Phase D handler - (0x%X) %s\n", s->tag, result, t30_frametype(result));
    return T30_ERR_OK;
}

static void phase_e_handler(void *user_data, int result)
{
    fax_node_t *s;
    
    s = (fax_node_t *) user_data;
    printf("%s: Phase E handler - (%d) %s\n", s->tag, result, t30_completion_code_to_str(result));
    
    s->succeeded = (result == T30_ERR_OK);
    s->phase_e_reached = true;
}

static void real_time_frame_handler(void *user_data,
                                    bool incoming,
                                    const uint8_t *msg,
                                    int len)
{
    fax_node_t *s;
    
    s = (fax_node_t *) user_data;
    printf("%s: %s frame: %s, len=%d\n", 
           s->tag, 
           incoming ? "Rx" : "Tx", 
           t30_frametype(msg[2]), 
           len);
}

static void set_t30_callbacks(t30_state_t *t30, fax_node_t *node)
{
    t30_set_phase_b_handler(t30, phase_b_handler, node);
    t30_set_phase_d_handler(t30, phase_d_handler, node);
    t30_set_phase_e_handler(t30, phase_e_handler, node);
    t30_set_real_time_frame_handler(t30, real_time_frame_handler, node);
}

static void init_fax_node(fax_node_t *node, const char *tag, bool calling_party)
{
    logging_state_t *logging;
    
    memset(node, 0, sizeof(fax_node_t));
    strcpy(node->tag, tag);
    
    if ((node->fax_state = fax_init(NULL, calling_party)) == NULL)
    {
        fprintf(stderr, "Cannot start FAX instance\n");
        exit(2);
    }
    
    node->t30_state = fax_get_t30_state(node->fax_state);
    
    logging = fax_get_logging_state(node->fax_state);
    span_log_set_level(logging, SPAN_LOG_DEBUG | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_SHOW_TAG | SPAN_LOG_SHOW_SAMPLE_TIME);
    span_log_set_tag(logging, node->tag);
    
    logging = t30_get_logging_state(node->t30_state);
    span_log_set_level(logging, SPAN_LOG_DEBUG | SPAN_LOG_SHOW_PROTOCOL | SPAN_LOG_SHOW_TAG | SPAN_LOG_SHOW_SAMPLE_TIME);
    span_log_set_tag(logging, node->tag);
    
    set_t30_callbacks(node->t30_state, node);
    
    node->awgn_state = NULL;
    if (noise_level > -99)
        node->awgn_state = awgn_init_dbm0(NULL, 1234567, noise_level);
    
    fax_set_transmit_on_idle(node->fax_state, use_transmit_on_idle);
    fax_set_tep_mode(node->fax_state, use_tep);
    
    t30_set_supported_modems(node->t30_state, supported_modems);
    t30_set_supported_t30_features(node->t30_state,
                                   T30_SUPPORT_IDENTIFICATION
                                   | T30_SUPPORT_SELECTIVE_POLLING
                                   | T30_SUPPORT_SUB_ADDRESSING);
    t30_set_supported_image_sizes(node->t30_state,
                                  T4_SUPPORT_WIDTH_215MM
                                  | T4_SUPPORT_WIDTH_255MM
                                  | T4_SUPPORT_WIDTH_303MM
                                  | T4_SUPPORT_LENGTH_US_LETTER
                                  | T4_SUPPORT_LENGTH_US_LEGAL
                                  | T4_SUPPORT_LENGTH_UNLIMITED);
    t30_set_supported_bilevel_resolutions(node->t30_state,
                                          T4_RESOLUTION_R8_STANDARD
                                          | T4_RESOLUTION_R8_FINE
                                          | T4_RESOLUTION_R8_SUPERFINE
                                          | T4_RESOLUTION_R16_SUPERFINE);
    
    t30_set_ecm_capability(node->t30_state, use_ecm);
    t30_set_supported_compressions(node->t30_state,
                                  T4_COMPRESSION_T4_1D
                                  | T4_COMPRESSION_T4_2D
                                  | T4_COMPRESSION_T6);
    
    /* Set distinct sender IDs */
    char ident[21];
    snprintf(ident, sizeof(ident), "%s fax", tag);
    t30_set_tx_ident(node->t30_state, ident);
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
    int16_t audio_log[SAMPLES_PER_CHUNK*2];
    int opt;
    const char *input_tiff_file_name = INPUT_TIFF_FILE_NAME;
    const char *output_tiff_file_name = OUTPUT_TIFF_FILE_NAME;
    int outframes;
    int j;
    float when = 0.0f;
    bool completed;
    int expected_pages;
    t30_stats_t t30_stats;
    SF_INFO sfinfo;
    
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
    
    printf("Simple Audio FAX to Audio FAX test\n");
    printf("Using %s ECM\n", use_ecm ? "with" : "without");
    
    /* Initialize the audio logging if requested */
    if (log_audio)
    {
        /* Set up a mono audio file with 8000 samples/second */
        memset(&sfinfo, 0, sizeof(sfinfo));
        sfinfo.samplerate = 8000;
        sfinfo.channels = 2;
        sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
        
        if ((wave_handle = sf_open(OUTPUT_WAVE_FILE_NAME, SFM_WRITE, &sfinfo)) == NULL)
        {
            fprintf(stderr, "Cannot create audio file '%s': %s\n", 
                    OUTPUT_WAVE_FILE_NAME, sf_strerror(NULL));
            exit(2);
        }
    }
    
    /* Initialize the FAX nodes */
    init_fax_node(&fax_node_a, "A", true);  // Calling party
    init_fax_node(&fax_node_b, "B", false); // Called party
    
    /* Set up the file transfer */
    t30_set_tx_file(fax_node_a.t30_state, input_tiff_file_name, -1, -1);
    t30_set_rx_file(fax_node_b.t30_state, output_tiff_file_name, -1);
    
    completed = false;
    
    /* Run the simulation */
    while (!completed)
    {
        /* Update T.30 timing for both nodes */
        span_log_bump_samples(t30_get_logging_state(fax_node_a.t30_state), SAMPLES_PER_CHUNK);
        span_log_bump_samples(fax_get_logging_state(fax_node_a.fax_state), SAMPLES_PER_CHUNK);
        
        span_log_bump_samples(t30_get_logging_state(fax_node_b.t30_state), SAMPLES_PER_CHUNK);
        span_log_bump_samples(fax_get_logging_state(fax_node_b.fax_state), SAMPLES_PER_CHUNK);
        
        /* Node A processes audio from Node B */
        fax_rx(fax_node_a.fax_state, fax_node_b.audio_buf, fax_node_b.audio_buf_len);
        if (!t30_call_active(fax_node_a.t30_state))
        {
            fax_node_a.completed = true;
        }
        
        /* Node B processes audio from Node A */
        fax_rx(fax_node_b.fax_state, fax_node_a.audio_buf, fax_node_a.audio_buf_len);
        if (!t30_call_active(fax_node_b.t30_state))
        {
            fax_node_b.completed = true;
        }
        
        /* Node A generates audio */
        fax_node_a.audio_buf_len = fax_tx(fax_node_a.fax_state, fax_node_a.audio_buf, SAMPLES_PER_CHUNK);
        if (!use_transmit_on_idle && fax_node_a.audio_buf_len < SAMPLES_PER_CHUNK)
        {
            vec_zeroi16(&fax_node_a.audio_buf[fax_node_a.audio_buf_len], SAMPLES_PER_CHUNK - fax_node_a.audio_buf_len);
            fax_node_a.audio_buf_len = SAMPLES_PER_CHUNK;
        }
        
        /* Node B generates audio */
        fax_node_b.audio_buf_len = fax_tx(fax_node_b.fax_state, fax_node_b.audio_buf, SAMPLES_PER_CHUNK);
        if (!use_transmit_on_idle && fax_node_b.audio_buf_len < SAMPLES_PER_CHUNK)
        {
            vec_zeroi16(&fax_node_b.audio_buf[fax_node_b.audio_buf_len], SAMPLES_PER_CHUNK - fax_node_b.audio_buf_len);
            fax_node_b.audio_buf_len = SAMPLES_PER_CHUNK;
        }
        
        /* Apply noise if needed */
        if (fax_node_a.awgn_state)
        {
            for (j = 0; j < fax_node_a.audio_buf_len; j++)
                fax_node_a.audio_buf[j] = ((int16_t) (fax_node_a.audio_buf[j]*signal_scaling)) + awgn(fax_node_a.awgn_state);
        }
        
        if (fax_node_b.awgn_state)
        {
            for (j = 0; j < fax_node_b.audio_buf_len; j++)
                fax_node_b.audio_buf[j] = ((int16_t) (fax_node_b.audio_buf[j]*signal_scaling)) + awgn(fax_node_b.awgn_state);
        }
        
        /* Log audio if required */
        if (log_audio)
        {
            for (j = 0; j < SAMPLES_PER_CHUNK; j++)
            {
                audio_log[2*j] = fax_node_a.audio_buf[j];
                audio_log[2*j + 1] = fax_node_b.audio_buf[j];
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
        if (fax_node_a.completed && fax_node_b.completed)
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
    
    /* Check how many pages should have been transferred */
    expected_pages = count_tiff_pages(input_tiff_file_name);
    printf("Expected %d pages to be transferred\n", expected_pages);
    
    /* Print results */
    t30_get_transfer_statistics(fax_node_a.t30_state, &t30_stats);
    printf("FAX call statistics for node A (Caller/Sender):\n");
    printf("  Pages transferred:  %d\n", t30_stats.pages_tx);
    printf("  Image size:         %d x %d pixels\n", t30_stats.width, t30_stats.length);
    printf("  Image resolution:   %d x %d DPI\n", t30_stats.x_resolution, t30_stats.y_resolution);
    printf("  Transfer Rate:      %d bps\n", t30_stats.bit_rate);
    printf("  ECM used:           %s\n", (t30_stats.error_correcting_mode) ? "yes" : "no");
    
    t30_get_transfer_statistics(fax_node_b.t30_state, &t30_stats);
    printf("FAX call statistics for node B (Called/Receiver):\n");
    printf("  Pages transferred:  %d\n", t30_stats.pages_rx);
    printf("  Image size:         %d x %d pixels\n", t30_stats.width, t30_stats.length);
    printf("  Image resolution:   %d x %d DPI\n", t30_stats.x_resolution, t30_stats.y_resolution);
    printf("  Transfer Rate:      %d bps\n", t30_stats.bit_rate);
    printf("  ECM used:           %s\n", (t30_stats.error_correcting_mode) ? "yes" : "no");
    
    /* Cleanup */
    fax_free(fax_node_a.fax_state);
    fax_free(fax_node_b.fax_state);
    
    if (fax_node_a.awgn_state)
        awgn_free(fax_node_a.awgn_state);
    if (fax_node_b.awgn_state)
        awgn_free(fax_node_b.awgn_state);
    
    if (!fax_node_a.phase_e_reached || !fax_node_b.phase_e_reached || !fax_node_a.succeeded || !fax_node_b.succeeded)
    {
        printf("Tests failed\n");
        exit(2);
    }
    
    printf("Test completed successfully\n");
    return 0;
}