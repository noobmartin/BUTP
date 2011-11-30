/* Author: Alexander Rajula
 * Contact: alexander@rajula.org
 */

enum{
  FL_WIN = 0x8000,
  FL_FIN = 0x4000,
  FL_TME = 0x2000,
  FL_SYN = 0x1000,
  FL_ACK = 0x800,
  FL_DAT = 0x400,
  FL_MTM = 0x200,
  FL_ABO = 0x100,
  FL_FAC = 0x80,
  NORMAL_HEADER_SIZE = 0xC,
  FULL_HEADER_SIZE = 0x14,
  EMPTY_PACKET = 0x4D80
};

enum{
  CLOSED,
  ESTABLISHED,
  SYN_INIT,
  SYN_INIT_WAIT,
  SYN_LISTEN,
  SYN_LISTEN_WAIT
};

/* Header description
 Bit number
 0                                31
  ___________________________________
 |        Sequence number            |
 |___________________________________|
 |        Ack.     number            |
 |___________________________________|
 | Checksum     |W|F|T|S|A|D|M|B|Z|R |
 |______________|_|_|________________|
 |******* Window size *************  | <- Optional
 |___________________________________|
 |******* Timestamp   *************  | <- Optional
 |___________________________________|
  
  The W flag indicates the prescence of the window size field.
  The F flag indicates that this is the last data packet in this transmission.
  The T flag indicates that the header contains a timestamp.
  The S flag indicates that this is a SYNCHRONIZATION packet.
  The A flag indicates that this packet contains an ACKNOWLEDGEMENT.
  The D flag indicates that this packet contains data for upper layers.
  The M flag indicates that if the timestamp is present, then the source of this packet set this timestamp. Otherwise, the timestamp is a copy of the value injected by the other host, which is now being transmitted back to the source.
  The B flag indicates that the connection should be terminated and any received data should be deleted. B for aBort.
  The Z flag indicates that a previous FIN flag is now acknowledged (FIN-ACK).
  The RESERVED bits are all set to zero and ignored.

 */

typedef struct{
  uint32_t seq; // Sequence number
  uint32_t ack; // Acknowledgement number
  uint16_t chk; // Datum portion checksum
  uint16_t opt; // Options field
}butp_header;

typedef struct{
  uint32_t seq; // Sequence number
  uint32_t ack; // Acknowledgement number
  uint16_t chk; // Datum portion checksum
  uint16_t opt; // Options field
  uint32_t window;
}butp_wheader;

typedef struct{
  uint32_t seq; // Sequence number
  uint32_t ack; // Acknowledgement number
  uint16_t chk; // Datum portion checksum
  uint16_t opt; // Options field
  uint32_t timestamp;
}butp_theader;

typedef struct{
  uint32_t seq; // Sequence number
  uint32_t ack; // Acknowledgement number
  uint16_t chk; // Datum portion checksum
  uint16_t opt; // Options field
  uint32_t window;
  uint32_t timestamp;
}butp_wtheader;

/* Packet abstraction */
typedef struct{
  butp_wtheader header;         // Packet header
  uint16_t      data_size;      // Size of packet data
  uint8_t       no_trans;       // Store the number of times this packet has been transmitted
  struct timespec tv;		// Store time of departure.
  char* datum;                  // Packet data pointer
  void* next;                   // Next packet which is in transit
}butp_packet;

// External protocol interface
int set_parameters(struct addrinfo* dest, uint8_t packet_loss, uint8_t corruption_ratio, uint8_t transmission_type, uint32_t run_time);      // Used by application program to set parameters
int syn_init();
int syn_init_wait();
int syn_listen();
int syn_listen_wait();
void loop();                                    // Protocol loop

// Network helper methods.
uint16_t calculate_checksum(const char* buf, uint32_t buflen); // Calculation of data checksum
void network_header_to_host(butp_wtheader* packet);        // Used to swap bytes for correct network order
void host_header_to_network(butp_wtheader* packet); // See above
int same_user(const struct addrinfo* userA, const struct addrinfo* userB);
void clear_lists();                                       // Frees all memory, closes the socket and exists

butp_packet* already_transmitted(const uint32_t seq);            // Check if seq (requested by client) has already been sent.
int already_received(const uint32_t seq);
butp_packet* get_packet_in_transit(const uint32_t seq);
int push_data_to_output_buffer(const char* buf, const int buflen);    // Used by application program to push data to the transport layer
int pull_data_from_input_buffer(char* buf, uint32_t buflen);          // Used by application to get data from transport layer buffer
void insert_packet_in_input_buffer(butp_packet* packet);
void link_packet_to_in_transit(butp_packet* packet);
void unlink_packet_from_in_transit(butp_packet* packet);

// Helper methods to quickly process packet header information without cluttering code.
int get_data_length(butp_wtheader* packet, const int packlen);
int get_header_length(const butp_wtheader* packet);
int checksum_ok(char* packet, const int data_len);
int seq_ok(butp_wtheader* packet, const int datasize);
int ack_ok(butp_wtheader* packet);
int has_data(const butp_wtheader* packet);
int has_ack(const butp_wtheader* packet);
int has_window(const butp_wtheader* packet);
int has_timestamp(const butp_wtheader* packet);
int has_syn(const butp_wtheader* packet);
int has_fin(const butp_wtheader* packet);
int has_abort(const butp_wtheader* packet);
int has_fin_ack(butp_wtheader* packet);
int is_your_timestamp(const butp_wtheader* packet);

// Internal protocol functions.
void process_incoming(char* buf, const int len, butp_packet* outgoing_packet);
void build_outgoing(butp_wtheader* packet, uint32_t packet_data_size, butp_packet* outgoing_packet);
void fill_output_buffer(const butp_packet* outgoing_packet);
void packet_timeout_function();
int time_exceeded(struct timespec* tv);
void adjust_packet_timeout();
