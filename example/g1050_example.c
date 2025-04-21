/*
 * G.1050 Network Model Example
 *
 * This example demonstrates how to use the g1050 network simulator
 * from the SpanDSP library to simulate packet transmission across
 * a network with configurable impairments.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "spandsp.h"
#include "spandsp/g1050.h"

/* Define packet parameters */
#define PACKET_SIZE     200  /* Size of test packets in bytes */
#define PACKET_RATE     50   /* Packets per second */
#define SIMULATION_TIME 10.0 /* Duration in seconds */

int main(int argc, char *argv[])
{
    g1050_state_t *s;
    int model = 1;           /* Model A = 1, B = 2, C = 3, etc. */
    int speed_pattern = 1;   /* Speed pattern 1-168 */
    uint8_t packet[1024];    /* Buffer for packets */
    int seq_no;
    double current_time;
    double departure_time;
    double arrival_time;
    int i;
    int packets_sent = 0;
    int packets_received = 0;
    int len;

    /* Print header */
    printf("G.1050 Network Simulation Example\n");
    printf("=================================\n\n");
    
    /* Allow user to specify model and speed pattern from command line */
    if (argc >= 2)
        model = atoi(argv[1]);
    if (argc >= 3)
        speed_pattern = atoi(argv[2]);

    /* Print chosen configuration */
    printf("Using model: %d (%c), speed pattern: %d\n", model, 'A' + model - 1, speed_pattern);
    
    /* Dump the parameters for the chosen model */
    g1050_dump_parms(model, speed_pattern);
    
    /* Initialize the g1050 simulator */
    if ((s = g1050_init(model, speed_pattern, PACKET_SIZE, PACKET_RATE)) == NULL) {
        fprintf(stderr, "Failed to initialize G.1050 simulator\n");
        exit(1);
    }

    /* Fill packet with test pattern */
    memset(packet, 0, sizeof(packet));
    for (i = 0; i < PACKET_SIZE; i++)
        packet[i] = i & 0xFF;

    printf("\nSimulating packet transmission for %.1f seconds...\n", SIMULATION_TIME);
    printf("Sending %d packets per second of size %d bytes\n\n", PACKET_RATE, PACKET_SIZE);

    /* Send packets into the simulator */
    current_time = 0.0;
    for (i = 0; i < PACKET_RATE * SIMULATION_TIME; i++) {
        departure_time = i * 1.0 / PACKET_RATE;
        
        /* Sequence number in first 4 bytes */
        packet[0] = (i >> 24) & 0xFF;
        packet[1] = (i >> 16) & 0xFF;
        packet[2] = (i >> 8) & 0xFF;
        packet[3] = i & 0xFF;
        
        /* Send packet into the simulator */
        if (g1050_put(s, packet, PACKET_SIZE, i, departure_time) > 0)
            packets_sent++;
    }

    printf("Sent %d packets into the network\n", packets_sent);
    printf("\nReceived packets:\n");
    printf("SEQ    DEPARTURE    ARRIVAL     DELAY (ms)\n");
    printf("------------------------------------------\n");

    /* Receive packets from the simulator */
    while ((len = g1050_get(s, packet, sizeof(packet), SIMULATION_TIME, &seq_no, &departure_time, &arrival_time)) >= 0) {
        packets_received++;
        
        /* Print packet information */
        printf("%4d    %8.3f    %8.3f    %8.1f\n", 
               seq_no, 
               departure_time, 
               arrival_time, 
               (arrival_time - departure_time) * 1000.0);
        
        /* For brevity, only show first 10 packets */
        if (packets_received >= 10) {
            printf("... (showing only first 10 packets)\n");
            break;
        }
    }

    /* Summarize results */
    printf("\nSummary:\n");
    printf("- Packets sent: %d\n", packets_sent);
    printf("- Packets received: %d\n", packets_received);
    printf("- Packet loss rate: %.2f%%\n", 
           (packets_sent > 0) ? 100.0 * (packets_sent - packets_received) / packets_sent : 0.0);

    /* Clean up */
    g1050_free(s);
    
    return 0;
}