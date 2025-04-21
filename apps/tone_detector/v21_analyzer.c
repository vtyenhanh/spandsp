/*
 * Simple V.21 tone analyzer - detects V.21 channel 2 frequencies in audio files
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <sndfile.h>

#define SAMPLE_RATE 8000
#define GOERTZEL_SAMPLES 102  /* About 12.75ms at 8kHz - good for detecting tones */

/* Goertzel algorithm state for frequency detection */
typedef struct {
    float coef;
    float q1;
    float q2;
    float energy;
    int count;
    int total_samples;
    float threshold;
    const char *name;
} goertzel_state_t;

/* Initialize Goertzel detector for a specific frequency */
static void goertzel_init(goertzel_state_t *s, float freq, int samples, float threshold, const char *name)
{
    s->coef = 2.0f * cosf(2.0f * M_PI * freq / SAMPLE_RATE);
    s->q1 = 0.0f;
    s->q2 = 0.0f;
    s->energy = 0.0f;
    s->count = 0;
    s->total_samples = 0;
    s->threshold = threshold;
    s->name = name;
}

/* Add a sample to the Goertzel calculation */
static void goertzel_update(goertzel_state_t *s, float sample)
{
    float q0;
    
    q0 = s->coef * s->q1 - s->q2 + sample;
    s->q2 = s->q1;
    s->q1 = q0;
    s->count++;
    s->total_samples++;
    
    /* If we've reached our sample window, calculate energy */
    if (s->count >= GOERTZEL_SAMPLES) {
        s->energy = s->q1 * s->q1 + s->q2 * s->q2 - s->coef * s->q1 * s->q2;
        
        /* If energy is above threshold, report detection */
        if (s->energy > s->threshold) {
            printf("Detected %s frequency at sample %d, energy: %.4f\n", 
                   s->name, s->total_samples, s->energy);
        }
        
        /* Reset for next window */
        s->q1 = 0.0f;
        s->q2 = 0.0f;
        s->count = 0;
    }
}

int main(int argc, char *argv[])
{
    SNDFILE *infile;
    SF_INFO info;
    short *buffer;
    int readcount, i;
    float sample;
    goertzel_state_t mark_detector, space_detector;
    
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <audio_file>\n", argv[0]);
        return 1;
    }
    
    /* Open the input file */
    memset(&info, 0, sizeof(info));
    if (!(infile = sf_open(argv[1], SFM_READ, &info))) {
        fprintf(stderr, "Error opening file: %s\n", argv[1]);
        return 1;
    }
    
    /* Print file info */
    printf("File: %s\n", argv[1]);
    printf("Channels: %d\n", info.channels);
    printf("Sample rate: %d Hz\n", info.samplerate);
    printf("Format: 0x%08x\n", info.format);
    printf("Frames: %ld\n", (long)info.frames);
    
    /* Check sample rate */
    if (info.samplerate != SAMPLE_RATE) {
        printf("Warning: Sample rate is %d Hz, expected %d Hz\n", 
               info.samplerate, SAMPLE_RATE);
    }
    
    /* Initialize tone detectors */
    goertzel_init(&mark_detector, 1850.0f, GOERTZEL_SAMPLES, 0.1f, "V.21 MARK (1850 Hz)");
    goertzel_init(&space_detector, 1650.0f, GOERTZEL_SAMPLES, 0.1f, "V.21 SPACE (1650 Hz)");
    
    /* Allocate buffer for audio data */
    buffer = malloc(1024 * sizeof(short));
    if (!buffer) {
        fprintf(stderr, "Failed to allocate memory\n");
        sf_close(infile);
        return 1;
    }
    
    /* Process the file */
    printf("Analyzing file for V.21 channel 2 tones (MARK=1850Hz, SPACE=1650Hz)...\n");
    
    while ((readcount = sf_read_short(infile, buffer, 1024)) > 0) {
        for (i = 0; i < readcount; i++) {
            /* Convert to normalized float (-1.0 to 1.0) */
            sample = buffer[i] / 32768.0f;
            
            /* Process sample through both detectors */
            goertzel_update(&mark_detector, sample);
            goertzel_update(&space_detector, sample);
        }
    }
    
    /* Report results */
    printf("Analysis complete.\n");
    printf("Total samples processed: %d\n", mark_detector.total_samples);
    
    /* Clean up */
    free(buffer);
    sf_close(infile);
    
    return 0;
}