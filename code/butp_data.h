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

uint32_t BYTES_IN_INPUT_QUEUE = 0x0;
uint32_t INPUT_QUEUE_SIZE = 0xF4240;
uint8_t ACTIVE_QUEUE_MANAGEMENT = 0x0;
uint8_t TRANSMISSION_MODE = 0x0;
uint8_t WIRELESS_CONNECTION = 0x0;
uint32_t ACKED_BYTE_COUNT = 0x0;
uint32_t TRANSMISSION_COUNT = 0x0;
uint32_t GOODPUT_COUNTER = 0x0;
uint8_t PACKET_COUNTER = 0x0;
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
uint16_t MTU = 0x50F;
uint16_t MSN = 0xFFFF;
uint32_t my_win = 0x0;
uint32_t my_seq = 0x0;
uint32_t congestion_win = 0x0;
uint32_t your_seq = 0x0;
uint32_t your_win = 0x0;
uint32_t bytes_in_transit = 0x0;
uint32_t packet_timeout_window_shrink = 0x9;
uint32_t packet_ack_window_increase = 0xA00;
uint32_t receiver_window_min = 0xADC;
//uint32_t receiver_window_max = 0x66FF30;
uint32_t receiver_window_max = 0x7735940;
uint32_t packet_timeout_min = 900; // 0.9ms
int	 sock = 0x0;
int	 reception_finished = 0x0;
int	 transmission_finished = 0x0;
int	 round_trip_time_factor = 0xFFFFF;
int	 data_written = 0x0;
int	 state = 0x0;
FILE*	 goodput_logfile = 0;
FILE*	 raw_logfile = 0;
float	 boost_power = 2.72;
float	 boost_factor = -2;
float	 glide_oscillation_amplitude_factor = 0.009;
float	 glide_oscillation_frequency_factor = 2;
float	 sneak_division_factor = 7;
float	 sneak_transition_division_factor = 4;
float	 boost_transition_division_factor = 0.95;
float	 restore_factor = 1.5;
float	 sneak_timeout_congestion_reduce_factor = 0.9;
float	 restore_timeout_congestion_reduce_factor = 0.8;
float	 glide_timeout_congestion_reduce_factor = 0.8;
float	 wireless_restore_timeout_congestion_reduce_factor = 0.85;
float	 wireless_glide_timeout_congestion_reduce_factor = 0.95;
float	 wireless_boost_timeout_congestion_reduce_factor = 0.95;
float	 wireless_sneak_timeout_congestion_reduce_factor = 0.8;
float	 wireless_timeout_increase_factor = 0xAAAA20;
int	 wireless_consecutive_timeout_threshold = 5;
int	 wireless_consecutive_ack_threshold = 1;
int	 consecutive_timeout_threshold = 1;
int	 consecutive_ack_threshold = 3;
int	 packet_timeout_sneak_seconds = 1;
int	 wireless_packet_timeout_sneak_seconds = 2;

// Packet ack timeout. Start with 500ms
struct timespec packet_timeout_data = {0,500000};
struct itimerval packet_timeout = {{0,500000},{0,500000}};
struct timespec packet_loop_interval_sleep = {0, 100000};
struct timespec start_time;
struct timespec goodput_time_start;

char sbuf[0xFFFF];
char rbuf[0xFFFF];

struct addrinfo* destination = NULL;

butp_packet* in_transit_first = NULL;
butp_packet* first_in_buffer = NULL;
butp_packet* received_ready_first = NULL;
