/*
 * G.1050 Network Model Example with PCAP Capture
 *
 * This example demonstrates how to use the g1050 network simulator
 * from the SpanDSP library to simulate packet transmission across
 * a network with configurable impairments, and save the results to
 * a PCAP file for analysis in tools like Wireshark.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <inttypes.h>
#include <sys/time.h>
#include <arpa/inet.h>  /* For htons, htonl */

#include "spandsp.h"
#include "spandsp/g1050.h"

/* Define packet parameters */
#define PACKET_SIZE     200  /* Size of test packets in bytes */
#define PACKET_RATE     50   /* Packets per second */
#define SIMULATION_TIME 10.0 /* Duration in seconds */

/* PCAP file format structures */
typedef struct {
    uint32_t magic_number;   /* magic number */
    uint16_t version_major;  /* major version number */
    uint16_t version_minor;  /* minor version number */
    int32_t  thiszone;       /* GMT to local correction */
    uint32_t sigfigs;        /* accuracy of timestamps */
    uint32_t snaplen;        /* max length of captured packets, in octets */
    uint32_t network;        /* data link type */
} pcap_file_header_t;

typedef struct {
    uint32_t ts_sec;         /* timestamp seconds */
    uint32_t ts_usec;        /* timestamp microseconds */
    uint32_t incl_len;       /* number of octets of packet saved in file */
    uint32_t orig_len;       /* actual length of packet */
} pcap_packet_header_t;

/* Simple UDP/IP header for demo purposes */
typedef struct {
    /* IP header */
    uint8_t  ip_ver_ihl;     /* IP version and header length */
    uint8_t  ip_tos;         /* Type of service */
    uint16_t ip_len;         /* Total length */
    uint16_t ip_id;          /* Identification */
    uint16_t ip_frag_off;    /* Fragment offset */
    uint8_t  ip_ttl;         /* Time to live */
    uint8_t  ip_proto;       /* Protocol */
    uint16_t ip_checksum;    /* Header checksum */
    uint32_t ip_src;         /* Source address */
    uint32_t ip_dst;         /* Destination address */
    
    /* UDP header */
    uint16_t udp_src_port;   /* Source port */
    uint16_t udp_dst_port;   /* Destination port */
    uint16_t udp_len;        /* Length */
    uint16_t udp_checksum;   /* Checksum */
} udp_ip_header_t;

/* Write PCAP file header */
void write_pcap_header(FILE *pcap_file) {
    pcap_file_header_t header;
    
    /* PCAP file header with default values */
    header.magic_number = 0xa1b2c3d4;  /* Standard PCAP file */
    header.version_major = 2;          /* Major version */
    header.version_minor = 4;          /* Minor version */
    header.thiszone = 0;               /* GMT */
    header.sigfigs = 0;                /* Accuracy of timestamps - not used */
    header.snaplen = 65535;            /* Max packet length */
    header.network = 1;                /* Ethernet */
    
    fwrite(&header, sizeof(header), 1, pcap_file);
}

/* Write a packet to the PCAP file */
void write_pcap_packet(FILE *pcap_file, const uint8_t *packet, int packet_len, double timestamp) {
    pcap_packet_header_t header;
    uint8_t ethernet_header[14];
    udp_ip_header_t ip_udp_header;
    
    /* Extract timestamp components */
    header.ts_sec = (uint32_t)timestamp;
    header.ts_usec = (uint32_t)((timestamp - header.ts_sec) * 1000000);
    header.incl_len = packet_len + sizeof(ethernet_header) + sizeof(ip_udp_header);
    header.orig_len = header.incl_len;
    
    /* Write packet header */
    fwrite(&header, sizeof(header), 1, pcap_file);
    
    /* Create a simple Ethernet header - this is just for demonstration */
    memset(ethernet_header, 0, sizeof(ethernet_header));
    ethernet_header[0] = 0x00;  /* Destination MAC */
    ethernet_header[1] = 0x11;
    ethernet_header[2] = 0x22;
    ethernet_header[3] = 0x33;
    ethernet_header[4] = 0x44;
    ethernet_header[5] = 0x55;
    ethernet_header[6] = 0x66;  /* Source MAC */
    ethernet_header[7] = 0x77;
    ethernet_header[8] = 0x88;
    ethernet_header[9] = 0x99;
    ethernet_header[10] = 0xaa;
    ethernet_header[11] = 0xbb;
    ethernet_header[12] = 0x08; /* EtherType: IPv4 */
    ethernet_header[13] = 0x00;
    
    /* Write Ethernet header */
    fwrite(ethernet_header, sizeof(ethernet_header), 1, pcap_file);
    
    /* Create a simple IP/UDP header */
    memset(&ip_udp_header, 0, sizeof(ip_udp_header));
    /* IP header */
    ip_udp_header.ip_ver_ihl = 0x45;   /* IPv4, 5 DWORDS header length */
    ip_udp_header.ip_tos = 0;          /* Type of service */
    ip_udp_header.ip_len = htons(sizeof(ip_udp_header) - sizeof(ethernet_header) + packet_len);
    ip_udp_header.ip_id = htons(1234); /* ID number */
    ip_udp_header.ip_frag_off = 0;     /* No fragmentation */
    ip_udp_header.ip_ttl = 64;         /* Time to live */
    ip_udp_header.ip_proto = 17;       /* UDP protocol */
    ip_udp_header.ip_src = htonl(0xC0A80101);  /* Source IP 192.168.1.1 */
    ip_udp_header.ip_dst = htonl(0xC0A80102);  /* Destination IP 192.168.1.2 */
    
    /* UDP header */
    ip_udp_header.udp_src_port = htons(12345);  /* Source port */
    ip_udp_header.udp_dst_port = htons(5060);   /* Destination port (SIP) */
    ip_udp_header.udp_len = htons(sizeof(ip_udp_header.udp_src_port) + 
                                 sizeof(ip_udp_header.udp_dst_port) +
                                 sizeof(ip_udp_header.udp_len) +
                                 sizeof(ip_udp_header.udp_checksum) +
                                 packet_len);
    ip_udp_header.udp_checksum = 0;    /* No checksum for simplicity */
    
    /* Write IP/UDP header */
    fwrite(&ip_udp_header, sizeof(ip_udp_header), 1, pcap_file);
    
    /* Write packet payload */
    fwrite(packet, packet_len, 1, pcap_file);
}

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
    FILE *pcap_file;
    const char *pcap_filename = "g1050_simulation.pcap";

    /* Print header */
    printf("G.1050 Network Simulation Example with PCAP Capture\n");
    printf("=================================================\n\n");
    
    /* Allow user to specify model and speed pattern from command line */
    if (argc >= 2)
        model = atoi(argv[1]);
    if (argc >= 3)
        speed_pattern = atoi(argv[2]);
    if (argc >= 4)
        pcap_filename = argv[3];

    /* Print chosen configuration */
    printf("Using model: %d (%c), speed pattern: %d\n", model, 'A' + model - 1, speed_pattern);
    printf("PCAP output file: %s\n\n", pcap_filename);
    
    /* Dump the parameters for the chosen model */
    g1050_dump_parms(model, speed_pattern);
    
    /* Initialize the g1050 simulator */
    if ((s = g1050_init(model, speed_pattern, PACKET_SIZE, PACKET_RATE)) == NULL) {
        fprintf(stderr, "Failed to initialize G.1050 simulator\n");
        exit(1);
    }

    /* Create PCAP file */
    if ((pcap_file = fopen(pcap_filename, "wb")) == NULL) {
        fprintf(stderr, "Failed to create PCAP file\n");
        g1050_free(s);
        exit(1);
    }
    
    /* Write PCAP file header */
    write_pcap_header(pcap_file);

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
    current_time = SIMULATION_TIME + 1.0;  /* Make sure we get all packets */
    while ((len = g1050_get(s, packet, sizeof(packet), current_time, &seq_no, &departure_time, &arrival_time)) >= 0) {
        packets_received++;
        
        /* Print packet information */
        printf("%4d    %8.3f    %8.3f    %8.1f\n", 
               seq_no, 
               departure_time, 
               arrival_time, 
               (arrival_time - departure_time) * 1000.0);

        /* Write this packet to the PCAP file */
        write_pcap_packet(pcap_file, packet, len, arrival_time);
        
        /* For brevity, only show first 10 packets in console output */
        if (packets_received == 10) {
            printf("... (showing only first 10 packets)\n");
        }
    }

    /* Summarize results */
    printf("\nSummary:\n");
    printf("- Packets sent: %d\n", packets_sent);
    printf("- Packets received: %d\n", packets_received);
    printf("- Packet loss rate: %.2f%%\n", 
           (packets_sent > 0) ? 100.0 * (packets_sent - packets_received) / packets_sent : 0.0);
    printf("\nPackets saved to PCAP file: %s\n", pcap_filename);
    printf("You can open this file with Wireshark or tcpdump for analysis.\n");

    /* Close the PCAP file */
    fclose(pcap_file);
    
    /* Clean up */
    g1050_free(s);
    
    return 0;
}