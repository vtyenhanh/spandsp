/*
 * SpanDSP - a series of DSP components for telephony
 *
 * fax_tone_detector.c - Detect V.21 preamble, CNG and CED tones
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sndfile.h>

/* Include the main spandsp header */
#include "spandsp.h"

#define SAMPLES_PER_CHUNK   160

/* Tone detection flags */
#define DETECT_CNG          (1 << 0)    /* 1100Hz calling tone */
#define DETECT_CED          (1 << 1)    /* 2100Hz answer tone */
#define DETECT_V21          (1 << 2)    /* V.21 preamble */
#define DETECT_ALL          (DETECT_CNG | DETECT_CED | DETECT_V21)

static void tone_detected(void *user_data, int tone, int level, int delay)
{
    const char *name;
    
    switch (tone)
    {
    case MODEM_CONNECT_TONES_FAX_CNG:
        name = "FAX CNG (Calling)";
        break;
    case MODEM_CONNECT_TONES_ANS:
        name = "CED/ANS (Answer)";
        break;
    case MODEM_CONNECT_TONES_FAX_PREAMBLE:
        name = "V.21 Preamble";
        break;
    default:
        name = "Unknown";
        break;
    }
    
    printf("Tone detected: %s (code=%d) at level %ddBm0, delay %d samples\n", name, tone, level, delay);
}

static int scan_audio_file(const char *file_name, int detect_flags)
{
    SNDFILE *inhandle;
    SF_INFO info;
    int16_t amp[SAMPLES_PER_CHUNK];
    int len;
    modem_connect_tones_rx_state_t *cng_rx = NULL;
    modem_connect_tones_rx_state_t *ced_rx = NULL;
    modem_connect_tones_rx_state_t *v21_rx = NULL;
    int hit;
    int total_samples = 0;
    
    printf("Processing '%s'\n", file_name);
    
    /* Open the audio file */
    memset(&info, 0, sizeof(info));
    if ((inhandle = sf_open(file_name, SFM_READ, &info)) == NULL)
    {
        fprintf(stderr, "Error opening audio file '%s' for reading\n", file_name);
        exit(2);
    }
    
    printf("File info: %d Hz, %d channels, %ld total frames\n", 
           info.samplerate, info.channels, (long)info.frames);
    
    /* Initialize selected tone detectors */
    if (detect_flags & DETECT_CNG)
        cng_rx = modem_connect_tones_rx_init(NULL, MODEM_CONNECT_TONES_FAX_CNG, tone_detected, NULL);
    
    if (detect_flags & DETECT_CED)
        ced_rx = modem_connect_tones_rx_init(NULL, MODEM_CONNECT_TONES_ANS, tone_detected, NULL);
    
    if (detect_flags & DETECT_V21)
        v21_rx = modem_connect_tones_rx_init(NULL, MODEM_CONNECT_TONES_FAX_PREAMBLE, tone_detected, NULL);
    
    if ((detect_flags & DETECT_CNG && !cng_rx) || 
        (detect_flags & DETECT_CED && !ced_rx) || 
        (detect_flags & DETECT_V21 && !v21_rx))
    {
        fprintf(stderr, "Failed to initialize tone detectors\n");
        sf_close(inhandle);
        exit(2);
    }
    
    printf("Tone detectors initialized successfully\n");
    printf("Detecting: %s%s%s\n",
           (detect_flags & DETECT_CNG) ? "CNG " : "",
           (detect_flags & DETECT_CED) ? "CED " : "",
           (detect_flags & DETECT_V21) ? "V.21 " : "");
    
    /* Process audio samples in chunks */
    while ((len = sf_readf_short(inhandle, amp, SAMPLES_PER_CHUNK)) > 0)
    {
        total_samples += len;
        
        /* Every 8000 samples (1 second), print a progress update */
        if (total_samples % 8000 == 0) {
            printf("Processed %d samples (%d seconds)...\n", total_samples, total_samples/8000);
        }
        
        /* Check for CNG tone (1100Hz) */
        if (cng_rx) {
            modem_connect_tones_rx(cng_rx, amp, len);
            hit = modem_connect_tones_rx_get(cng_rx);
            if (hit) printf("CNG detection hit at sample %d\n", total_samples);
        }
        
        /* Check for CED/ANS tone (2100Hz) */
        if (ced_rx) {
            modem_connect_tones_rx(ced_rx, amp, len);
            hit = modem_connect_tones_rx_get(ced_rx);
            if (hit) printf("CED detection hit at sample %d\n", total_samples);
        }
        
        /* Check for V.21 preamble */
        if (v21_rx) {
            modem_connect_tones_rx(v21_rx, amp, len);
            hit = modem_connect_tones_rx_get(v21_rx);
            if (hit) printf("V.21 preamble detection hit at sample %d\n", total_samples);
        }
    }
    
    printf("Processing complete. %d total samples processed.\n", total_samples);
    
    /* Free tone detectors */
    if (cng_rx) modem_connect_tones_rx_free(cng_rx);
    if (ced_rx) modem_connect_tones_rx_free(ced_rx);
    if (v21_rx) modem_connect_tones_rx_free(v21_rx);
    
    /* Close the audio file */
    sf_close(inhandle);
    
    return 0;
}

int main(int argc, char *argv[])
{
    int detect_flags = DETECT_ALL;
    int opt;
    int file_index = 1;
    
    /* Parse command line options */
    while ((opt = getopt(argc, argv, "cgva")) != -1) {
        switch (opt) {
            case 'c':
                detect_flags = DETECT_CNG;
                break;
            case 'g':
                detect_flags = DETECT_CED;
                break;
            case 'v':
                detect_flags = DETECT_V21;
                break;
            case 'a':
                detect_flags = DETECT_ALL;
                break;
            default:
                fprintf(stderr, "Unknown option: %c\n", opt);
                exit(2);
        }
        file_index++;
    }
    
    if (file_index >= argc)
    {
        fprintf(stderr, "Usage: %s [-c|-g|-v|-a] <audio-file> [<audio-file>...]\n", argv[0]);
        fprintf(stderr, "Detects fax tones in audio files\n");
        fprintf(stderr, "  -c  Detect CNG (Calling) tone only\n");
        fprintf(stderr, "  -g  Detect CED (Answer) tone only\n");
        fprintf(stderr, "  -v  Detect V.21 preamble only\n");
        fprintf(stderr, "  -a  Detect all tones (default)\n");
        exit(2);
    }
    
    for (int i = file_index; i < argc; i++)
    {
        scan_audio_file(argv[i], detect_flags);
    }
    
    return 0;
}