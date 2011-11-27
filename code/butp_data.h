/* Author: Alexander Rajula
* Contact: alexander@rajula.org
 * Info:
 *   This is a library header for the BUTP (BTH UDP-based Transport Protocol).
 *   It is a lightweight protocol for reliable transmissin of application data
 *   over unreliable transmission media.
 *   This implementation is simple, and the assumption is that it runs over either
 *   IPv4 or IPv6, but this assumption is not critical to the protocol operations.
 *   Also, please note that this protocol does not implement MTU discovery as specified
 *   by RFC 1191 for IPv4 and RFC 1981 for IPv6.
 */

#include <linux/types.h>
#include <stdint.h>
#include <signal.h>
#include <sys/time.h>
#include "butp_functions.h"

uint32_t SIMULATION_RUNTIME = 0x0;
uint8_t INSTANT_BITRATE = 0x0;
uint8_t	AVERAGE_BITRATE = 0x0;
uint8_t AVERAGE_THROUGHPUT = 0x0;
uint8_t CONTINUOUS_TRANSMISSION = 0x0;
uint8_t SIMULATE_LOSSY_MEDIUM = 0x0;
uint8_t SIMULATE_CORRUPTION = 0x0;
uint8_t PACKET_LOSS_RATIO = 75;
uint8_t CORRUPTION_RATIO = 75;
uint16_t RTT_RAND_MODULUS = 0x6;
uint16_t RTT_RAND_VALUE = 0x3;
uint16_t RTT = 0x0;
//uint16_t MTU = 0x240;
uint16_t MTU = 0x500;
uint16_t MSN = 0xFFFF;
uint32_t my_win = 0x0;
uint32_t my_seq = 0x0;
uint32_t your_win = 0x0;
uint32_t your_seq = 0x0;
uint32_t bytes_in_transit = 0x0;
uint32_t packet_timeout_window_shrink = 0x9;
uint32_t packet_ack_window_increase = 0x20;
uint32_t receiver_window_min = 0x500;
uint32_t receiver_window_max = 0x1000;
uint32_t packet_timeout_min = 900; // 0.9ms
int	 sock = 0x0;
int	 reception_finished = 0x0;
int	 transmission_finished = 0x0;
int	 round_trip_time_factor = 0x400;
int	 data_written = 0x0;
int	 state = 0x0;

// Packet ack timeout. Start with 500ms
struct timespec packet_timeout_data = {0,500000};
struct itimerval packet_timeout = {{0,500000},{0,500000}};
struct timespec packet_loop_interval_sleep = {0, 100000};

char sbuf[0xFFFF];
char rbuf[0xFFFF];

struct addrinfo* destination = NULL;

butp_packet* in_transit_first = NULL;
butp_packet* first_in_buffer = NULL;
butp_packet* received_ready_first = NULL;
