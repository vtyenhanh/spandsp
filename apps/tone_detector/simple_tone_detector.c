/*
 * SpanDSP - a series of DSP components for telephony
 *
 * simple_tone_detector.c - Detect V.21 preamble, CNG and CED tones in raw PCM files
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define SAMPLE_RATE 8000
#define SAMPLES_PER_CHUNK 160

/* Goertzel algorithm implementation for tone detection */
typedef struct {
    float coef;
    float q1;
    float q2;
    float energy;
    int current_sample;
    int samples;
} goertzel_state_t;

/* Initialize Goertzel detector for a specific frequency */
static void goertzel_init(goertzel_state_t *s, float freq, int samples)
{
    s->coef = 2.0f * cosf(2.0f * M_PI * freq / SAMPLE_RATE);
    s->q1 = 0.0f;
    s->q2 = 0.0f;
    s->energy = 0.0f;
    s->current_sample = 0;
    s->samples = samples;
}

/* Process samples through the Goertzel detector */
static void goertzel_update(goertzel_state_t *s, const int16_t *samples, int count)
{
    int i;
    float q0;
    
    for (i = 0; i < count; i++) {
        q0 = s->coef * s->q1 - s->q2 + (float)samples[i] / 32768.0f;
        s->q2 = s->q1;
        s->q1 = q0;
        s->current_sample++;
        
        /* If we've processed enough samples, calculate energy and reset */
        if (s->current_sample >= s->samples) {
            s->energy = s->q1 * s->q1 + s->q2 * s->q2 - s->coef * s->q1 * s->q2;
            s->q1 = 0.0f;
            s->q2 = 0.0f;
            s->current_sample = 0;
            
            /* Report detection if strong enough */
            if (s->energy > 0.001f) {
                printf("Tone detection: Energy=%.4f\n", s->energy);
            }
        }
    }
}

/* Detect specific tone based on frequency */
static void process_for_tone(const char *tone_name, float freq, const int16_t *samples, int count)
{
    static goertzel_state_t detector;
    static int initialized = 0;
    
    if (!initialized) {
        goertzel_init(&detector, freq, 102);  /* about 100 samples is good for tone detection */
        initialized = 1;
    }
    
    goertzel_update(&detector, samples, count);
}

/* Process raw PCM file for tone detection */
static int detect_tones_in_raw_file(const char *filename)
{
    FILE *file;
    int16_t buffer[SAMPLES_PER_CHUNK];
    size_t samples_read;
    int total_samples = 0;
    
    printf("Processing file: %s\n", filename);
    
    if ((file = fopen(filename, "rb")) == NULL) {
        fprintf(stderr, "Error: Cannot open input file %s\n", filename);
        return -1;
    }
    
    /* Process the file in chunks */
    while ((samples_read = fread(buffer, sizeof(int16_t), SAMPLES_PER_CHUNK, file)) > 0) {
        /* Check for V.21 channel 2 mark frequency (1850 Hz) */
        process_for_tone("V.21 CH2 Mark", 1850.0f, buffer, samples_read);
        
        /* Check for V.21 channel 2 space frequency (1650 Hz) */
        process_for_tone("V.21 CH2 Space", 1650.0f, buffer, samples_read);
        
        /* Check for CNG tone (1100 Hz) */
        process_for_tone("CNG", 1100.0f, buffer, samples_read);
        
        /* Check for CED/ANS tone (2100 Hz) */
        process_for_tone("CED/ANS", 2100.0f, buffer, samples_read);
        
        total_samples += samples_read;
    }
    
    fclose(file);
    printf("Processed %d samples\n", total_samples);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <raw-pcm-file>\n", argv[0]);
        fprintf(stderr, "Detects V.21 preamble, CNG and CED tones in raw PCM files\n");
        fprintf(stderr, "File format must be: 16-bit signed, 8000Hz, mono\n");
        return 1;
    }
    
    detect_tones_in_raw_file(argv[1]);
    return 0;
}