/*
 * SpanDSP - a series of DSP components for telephony
 *
 * simple_v21_generator.c - Generate V.21 preamble (HDLC flags) and write to WAV file
 */

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <math.h>
#include <stdint.h>

#define SAMPLE_RATE 8000
#define SAMPLES_PER_CHUNK 160

/* FSK parameters for V.21 channel 2 (high-band, used for fax) */
#define V21CH2_FREQ_0 1650
#define V21CH2_FREQ_1 1850
#define FSK_BAUD_RATE 300

/* Simple WAV header structure */
typedef struct {
    /* RIFF header */
    char riff_header[4];    /* "RIFF" */
    uint32_t wav_size;      /* Total file size - 8 */
    char wave_header[4];    /* "WAVE" */
    
    /* Format chunk */
    char fmt_header[4];     /* "fmt " */
    uint32_t fmt_chunk_size;/* Size of format chunk */
    uint16_t audio_format;  /* Audio format type (1 = PCM) */
    uint16_t num_channels;  /* Number of channels */
    uint32_t sample_rate;   /* Sample rate */
    uint32_t byte_rate;     /* Bytes per second */
    uint16_t block_align;   /* Bytes per sample * num_channels */
    uint16_t bits_per_sample;/* Bits per sample */
    
    /* Data chunk */
    char data_header[4];    /* "data" */
    uint32_t data_bytes;    /* Size of data chunk */
} wav_header_t;

/* Function to write WAV header */
static void write_wav_header(FILE *file, int data_size) 
{
    wav_header_t header;
    
    strncpy(header.riff_header, "RIFF", 4);
    header.wav_size = data_size + 36;  /* Total size - 8 bytes for RIFF & size */
    strncpy(header.wave_header, "WAVE", 4);
    
    strncpy(header.fmt_header, "fmt ", 4);
    header.fmt_chunk_size = 16;
    header.audio_format = 1;  /* PCM format */
    header.num_channels = 1;  /* Mono */
    header.sample_rate = SAMPLE_RATE;
    header.bits_per_sample = 16;
    header.block_align = header.num_channels * header.bits_per_sample / 8;
    header.byte_rate = header.sample_rate * header.block_align;
    
    strncpy(header.data_header, "data", 4);
    header.data_bytes = data_size;
    
    fwrite(&header, sizeof(header), 1, file);
}

/* Generate sine wave of specified frequency */
static void generate_tone(FILE *file, float freq, float amplitude, int duration_ms)
{
    int samples = duration_ms * SAMPLE_RATE / 1000;
    int i;
    float t;
    short sample;
    
    printf("Generating %d samples of %.1f Hz tone...\n", samples, freq);
    
    for (i = 0; i < samples; i++) {
        t = (float) i / SAMPLE_RATE;
        sample = (short)(amplitude * 32767.0f * sin(2.0f * 3.14159f * freq * t));
        fwrite(&sample, sizeof(short), 1, file);
    }
}

/* Generate a single bit of FSK modulated data */
static void generate_fsk_bit(FILE *file, int bit, float amplitude, int samples_per_bit)
{
    float freq = bit ? V21CH2_FREQ_1 : V21CH2_FREQ_0;
    int i;
    float t;
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

/* Generate V.21 preamble (series of HDLC flags: 0x7E) and write to WAV file */
static int create_v21_preamble(const char *filename, float amplitude, int duration_ms)
{
    FILE *file;
    int flags_to_generate;
    int i;
    int samples_per_bit = SAMPLE_RATE / FSK_BAUD_RATE;
    long data_pos, end_pos;
    
    if ((file = fopen(filename, "wb")) == NULL) {
        fprintf(stderr, "Error: Cannot open output file %s\n", filename);
        return -1;
    }
    
    /* Write placeholder WAV header - will update later */
    write_wav_header(file, 0);
    
    /* Remember position where data starts */
    data_pos = ftell(file);
    
    printf("Generating V.21 preamble (HDLC flags) for %d ms...\n", duration_ms);
    printf("Samples per bit: %d\n", samples_per_bit);
    
    /* Calculate how many flags we need for the requested duration */
    flags_to_generate = (duration_ms * FSK_BAUD_RATE / 1000) / 8 + 1;
    
    printf("Generating %d HDLC flags (0x7E)...\n", flags_to_generate);
    
    /* Generate repeated HDLC flags (0x7E = 01111110) */
    for (i = 0; i < flags_to_generate; i++) {
        generate_fsk_byte(file, 0x7E, amplitude, samples_per_bit);
    }
    
    /* Measure size of data written */
    end_pos = ftell(file);
    
    /* Update the WAV header with the correct data size */
    fseek(file, 0, SEEK_SET);
    write_wav_header(file, end_pos - data_pos);
    
    fclose(file);
    printf("V.21 preamble written to %s\n", filename);
    return 0;
}

int main(int argc, char *argv[])
{
    float amplitude = 0.5;  /* 50% of full scale */
    int duration_ms = 1000; /* 1 second by default */
    char *output_file = "v21_preamble.wav";
    
    if (argc >= 2) {
        output_file = argv[1];
    }
    
    if (argc >= 3) {
        duration_ms = atoi(argv[2]);
        if (duration_ms <= 0) {
            duration_ms = 1000;
        }
    }
    
    printf("Generating %dms V.21 preamble to WAV file: %s\n", duration_ms, output_file);
    create_v21_preamble(output_file, amplitude, duration_ms);
    
    printf("\nDone! Generated WAV file with V.21 preamble.\n");
    
    return 0;
}