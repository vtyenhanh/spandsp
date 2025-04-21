/*
 * SpanDSP - a series of DSP components for telephony
 *
 * fax_tone_generator.c - Generate V.21 preamble, CNG and CED tones and write to WAV file
 *
 * Copyright (C) 2025 Your Name
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sndfile.h>

#include "spandsp.h"

#define SAMPLE_RATE 8000
#define SAMPLES_PER_CHUNK 160

/* Generate V.21 preamble HDLC flags (0x7E) for a specified duration */
static int create_v21_preamble(const char *filename, float level_dbm0, int duration_ms)
{
    SNDFILE *outhandle;
    SF_INFO info;
    int16_t amp[SAMPLES_PER_CHUNK];
    int len;
    int i;
    int preamble_samples;
    int total_samples = 0;
    fsk_tx_state_t fsk_tx;
    
    /* Initialize the FSK transmitter for V.21 (channel 2, 1650Hz/1850Hz) */
    fsk_tx_init(&fsk_tx, &preset_fsk_specs[FSK_V21CH2], FSK_FRAME_MODE_SYNC, async_tx_get_bit, NULL);
    fsk_tx_set_modem_status_handler(&fsk_tx, NULL, NULL);
    fsk_tx_power(&fsk_tx, level_dbm0);
    
    /* Setup the output WAV file */
    memset(&info, 0, sizeof(info));
    info.samplerate = SAMPLE_RATE;
    info.channels = 1;
    info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    
    if ((outhandle = sf_open(filename, SFM_WRITE, &info)) == NULL)
    {
        fprintf(stderr, "Error creating audio file '%s' for writing\n", filename);
        return -1;
    }
    
    /* Calculate total number of samples needed */
    preamble_samples = duration_ms * (SAMPLE_RATE/1000);
    
    printf("Generating %d ms of V.21 preamble at level %0.1f dBm0...\n", duration_ms, level_dbm0);
    
    /* Generate the HDLC flags as V.21 audio and write to file */
    while (total_samples < preamble_samples)
    {
        len = fsk_tx(&fsk_tx, amp, SAMPLES_PER_CHUNK);
        if (len < 0)
            break;
        
        sf_writef_short(outhandle, amp, len);
        total_samples += len;
    }
    
    printf("Generated %d samples of V.21 preamble to '%s'\n", total_samples, filename);
    
    /* Clean up and close the output file */
    sf_close(outhandle);
    return 0;
}

/* Functions to supply bits for the FSK modulator */
static int bit_stream_state = 0;
static int flag_bits = 0;

/* Supply bits that form HDLC flags (0x7E) */
static int async_tx_get_bit(void *user_data)
{
    int bit;
    
    /* Generate HDLC flags pattern: 0 1 1 1 1 1 1 0 (0x7E) */
    bit = (flag_bits & 0x01) ? 1 : 0;
    
    /* Advance to next bit */
    flag_bits = (flag_bits >> 1) | ((flag_bits & 0x01) << 7);
    
    /* Every 8 bits, reset to a flag pattern (0x7E) */
    if (++bit_stream_state >= 8)
    {
        bit_stream_state = 0;
        flag_bits = 0x7E;  /* HDLC flag */
    }
    
    return bit;
}

/* Display usage information */
static void show_usage(const char *prog_name)
{
    fprintf(stderr, "Usage: %s [options] <output-file.wav>\n", prog_name);
    fprintf(stderr, "Generate FAX tones and write to WAV file\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -p <duration>   Generate V.21 preamble (HDLC flags) [default: 1000ms]\n");
    fprintf(stderr, "  -l <level>      Signal level in dBm0 [default: -10.0]\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s -p 2000 -l -15 v21_preamble.wav   # 2 seconds of V.21 preamble at -15dBm0\n", prog_name);
    exit(2);
}

int main(int argc, char *argv[])
{
    int duration_ms = 1000;  /* Default to 1000 ms */
    float level_dbm0 = -10.0f;  /* Default to -10 dBm0 */
    char *output_file = NULL;
    int opt;
    
    /* Initialize the flag pattern */
    flag_bits = 0x7E;  /* HDLC flag */
    
    /* Parse command line arguments */
    while ((opt = getopt(argc, argv, "p:l:h")) != -1)
    {
        switch (opt)
        {
            case 'p':
                duration_ms = atoi(optarg);
                if (duration_ms <= 0)
                {
                    fprintf(stderr, "Error: Duration must be positive\n");
                    show_usage(argv[0]);
                }
                break;
                
            case 'l':
                level_dbm0 = atof(optarg);
                if (level_dbm0 > 3.0f || level_dbm0 < -50.0f)
                {
                    fprintf(stderr, "Error: Level should be between -50.0 and 3.0 dBm0\n");
                    show_usage(argv[0]);
                }
                break;
                
            case 'h':
            default:
                show_usage(argv[0]);
                break;
        }
    }
    
    /* Get the output filename */
    if (optind >= argc)
    {
        fprintf(stderr, "Error: No output file specified\n");
        show_usage(argv[0]);
    }
    
    output_file = argv[optind];
    
    /* Generate the V.21 preamble */
    return create_v21_preamble(output_file, level_dbm0, duration_ms);
}