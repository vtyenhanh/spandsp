/*
 * SpanDSP - a series of DSP components for telephony
 *
 * raw_v21_generator.c - Generate V.21 preamble (HDLC flags) and write to raw PCM file
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define SAMPLE_RATE 8000
#define SAMPLES_PER_CHUNK 160

/* FSK parameters for V.21 channel 2 (high-band, used for fax) */
#define V21CH2_FREQ_0 1650
#define V21CH2_FREQ_1 1850
#define FSK_BAUD_RATE 300

/* Generate a single bit of FSK modulated data */
static void generate_fsk_bit(FILE *file, int bit, float amplitude, int samples_per_bit)
{
    float freq = bit ? V21CH2_FREQ_1 : V21CH2_FREQ_0;
    int i;
    short sample;
    static float phase = 0.0f;  /* Maintain phase continuity */
    
    for (i = 0; i < samples_per_bit; i++) {
        /* Use continuous phase to avoid artifacts */
        phase += 2.0f * M_PI * freq / SAMPLE_RATE;
        /* Keep phase between 0 and 2π */
        if (phase >= 2.0f * M_PI)
            phase -= 2.0f * M_PI;
            
        sample = (short)(amplitude * 32767.0f * sin(phase));
        fwrite(&sample, sizeof(short), 1, file);
    }
}

/* Generate a byte of data as FSK */
static void generate_fsk_byte(FILE *file, unsigned char byte, float amplitude, int samples_per_bit)
{
    int i;
    /* HDLC format - LSB first */
    for (i = 0; i < 8; i++) {
        generate_fsk_bit(file, (byte >> i) & 0x01, amplitude, samples_per_bit);
    }
}

/* Generate V.21 preamble (series of HDLC flags: 0x7E) and write to raw file */
static int create_v21_preamble(const char *filename, float amplitude, int duration_ms)
{
    FILE *file;
    int flags_to_generate;
    int i;
    int samples_per_bit = SAMPLE_RATE / FSK_BAUD_RATE;
    
    if ((file = fopen(filename, "wb")) == NULL) {
        fprintf(stderr, "Error: Cannot open output file %s\n", filename);
        return -1;
    }
    
    printf("Generating V.21 preamble (HDLC flags) for %d ms...\n", duration_ms);
    printf("Samples per bit: %d\n", samples_per_bit);
    
    /* Calculate how many flags we need for the requested duration */
    flags_to_generate = (duration_ms * FSK_BAUD_RATE / 1000) / 8 + 1;
    
    printf("Generating %d HDLC flags (0x7E)...\n", flags_to_generate);
    
    /* Generate repeated HDLC flags (0x7E = 01111110) */
    for (i = 0; i < flags_to_generate; i++) {
        generate_fsk_byte(file, 0x7E, amplitude, samples_per_bit);
    }
    
    fclose(file);
    printf("V.21 preamble raw data written to %s\n", filename);
    return 0;
}

int main(int argc, char *argv[])
{
    float amplitude = 0.5;  /* 50% of full scale */
    int duration_ms = 1000; /* 1 second by default */
    char *output_file = "v21_preamble.raw";
    
    if (argc >= 2) {
        output_file = argv[1];
    }
    
    if (argc >= 3) {
        duration_ms = atoi(argv[2]);
        if (duration_ms <= 0) {
            duration_ms = 1000;
        }
    }
    
    if (argc >= 4) {
        amplitude = atof(argv[3]);
        if (amplitude <= 0.0f || amplitude > 1.0f) {
            amplitude = 0.5f;
        }
    }
    
    printf("Generating %dms V.21 preamble to raw file: %s (amplitude: %.2f)\n", 
           duration_ms, output_file, amplitude);
    create_v21_preamble(output_file, amplitude, duration_ms);
    
    printf("\nDone!\n");
    printf("The file is in raw 16-bit PCM format, 8000Hz, mono.\n");
    printf("File format info for reference:\n");
    printf("- Sample rate: 8000 Hz\n");
    printf("- Sample format: 16-bit signed little-endian\n");
    printf("- Channels: 1 (mono)\n");
    
    return 0;
}