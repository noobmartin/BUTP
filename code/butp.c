/* Author: Alexander Rajula
 * Contact: alexander@rajula.org
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <sys/time.h>
#include <fcntl.h>
#include "butp_data.h"

int set_parameters(struct addrinfo* dest, uint8_t packet_loss, uint8_t corruption_ratio, uint8_t transmission_type){
	memset(rbuf, 0, MTU);
	memset(sbuf, 0, MTU);

	if(dest == NULL)
	  return -1;
	destination = dest;
	
	first_in_buffer = NULL;
	in_transit_first = NULL;
	received_ready_first = NULL;
	
	my_win = receiver_window_min;
	your_win = receiver_window_min;

	transmission_finished = 0;
	reception_finished = 0;
	
	srand(time(NULL));

	if(packet_loss != 0)
	  SIMULATE_LOSSY_MEDIUM = 1;
	else
	  SIMULATE_LOSSY_MEDIUM = 0;
	PACKET_LOSS_RATIO = packet_loss;

	if(corruption_ratio != 0)
	  SIMULATE_CORRUPTION = 1;
	else
	  SIMULATE_CORRUPTION = 0;
	CORRUPTION_RATIO = corruption_ratio;

	if(transmission_type != 0)
	  CONTINUOUS_TRANSMISSION = 1;
	else
	  CONTINUOUS_TRANSMISSION = 0;

	AVERAGE_BITRATE = 0;
	AVERAGE_THROUGHPUT = 0;

	state = CLOSED;

	return 1;
}

int syn_init(){
	state = SYN_INIT;

	my_seq = rand() % (MSN+1);

	if(destination == NULL)
	  return -1;

	sock = socket(destination->ai_family, destination->ai_socktype, destination->ai_protocol);
	if(sock == -1)
	  return -1;
	
	butp_wtheader packet;
	memset(&packet, 0, sizeof(butp_wtheader));
	packet.seq = my_seq;
	packet.opt |=FL_WIN;
	packet.opt |=FL_TME;
	packet.opt |=FL_SYN;
	packet.window = my_win;

	struct timespec ti;
	clock_gettime(CLOCK_REALTIME, &ti);
	packet.timestamp |= (uint32_t)(ti.tv_nsec);

	host_header_to_network(&packet);

	int rval = sendto(sock, &packet, sizeof(butp_wtheader), 0, (struct sockaddr*)destination->ai_addr, destination->ai_addrlen); 
	if(rval == -1)
	  return -1;
	
	return syn_init_wait();
}

int syn_init_wait(){
	state = SYN_INIT_WAIT;

	struct addrinfo source;
	memset(&source, 0, sizeof(struct addrinfo));

	int rval = recvfrom(sock, rbuf, MTU, 0, source.ai_addr, &source.ai_addrlen);
	if(rval == -1)
	  return -1;

	if(!same_user(&source, destination))
	  return -1;

	butp_wtheader* hdr = (butp_wtheader*)rbuf;
	network_header_to_host(hdr);

	your_seq = hdr->seq;
	if(has_window(hdr))
	  your_win = hdr->window;
	
	if(!has_syn(hdr))
	  return -1;

	if(!has_ack(hdr))
	  return -1;

	if((hdr->ack) != my_seq)
	  return -1;

	struct timespec ti;
	clock_gettime(CLOCK_REALTIME, &ti);
	RTT = (uint32_t)(ti.tv_nsec) - hdr->timestamp;
	adjust_packet_timeout();

	butp_wtheader packet;
	packet.ack = your_seq;
	packet.opt = FL_ACK;

	host_header_to_network(&packet);

	rval = sendto(sock, &packet, sizeof(butp_header), 0, (struct sockaddr*)destination->ai_addr, destination->ai_addrlen);
	if(rval == -1)
	  return -1;

	if(fcntl(sock, F_SETFL, O_NONBLOCK) == -1)
	  return -1;

	return 1;
}

int syn_listen(){
	state = SYN_LISTEN;

	srand(time(NULL));
	my_seq = rand() % (MSN+1);
	
	sock = socket(destination->ai_family, destination->ai_socktype, destination->ai_protocol);
	if(sock == -1)
	  return -1;

        if(bind(sock, destination->ai_addr, destination->ai_addrlen) != 0){
            close(sock);
            return -1;
        }

	int rval = recvfrom(sock, rbuf, MTU, 0, destination->ai_addr, &(destination->ai_addrlen));
	if(rval == -1)
	  return -1;

	butp_wtheader* hdr = (butp_wtheader*)rbuf;
	network_header_to_host(hdr);
	if(!(hdr->opt&FL_SYN))
	  return -1;
	
	your_seq = hdr->seq;

	if(hdr->opt&FL_WIN && (!(hdr->opt&FL_TME))){
	  butp_wheader* whdr = (butp_wheader*)rbuf;
	  your_win = whdr->window;
	}

	butp_wtheader packet;
	packet.seq = my_seq;
	packet.ack = your_seq;
	packet.opt = FL_SYN;
	packet.opt |= FL_ACK;
	int psize = sizeof(butp_header);
	if(has_window(hdr) && (!has_timestamp(hdr))){
	  packet.opt |= FL_WIN;
	  packet.window = my_win;
	  psize+=0x4;
	}
	else if((!has_window(hdr)) && has_timestamp(hdr)){
	  packet.opt |= FL_TME;
	  packet.window = hdr->timestamp;
	  psize+=0x4;
	}
	else if(has_window(hdr) && has_timestamp(hdr)){
	  packet.opt |= FL_WIN;
	  packet.opt |= FL_TME;
	  packet.window = my_win;
	  packet.timestamp = hdr->timestamp;
	  psize+=0x8;
	}

	host_header_to_network(&packet);

	rval = sendto(sock, &packet, psize, 0, destination->ai_addr, destination->ai_addrlen);
	if(rval == -1)
	  return -1;

	return syn_listen_wait();
}

int syn_listen_wait(){
	state = SYN_LISTEN_WAIT;

	struct addrinfo source;
	memset(&source, 0, sizeof(struct addrinfo));

	butp_wtheader* hdr = (butp_wtheader*)rbuf;
	memset(rbuf, 0, MTU);
	int rval = recvfrom(sock, rbuf, MTU, 0, source.ai_addr, &source.ai_addrlen);
	if(rval == -1)
	  return -1;

	if(!same_user(&source, destination))
	  return -1;

	network_header_to_host(hdr);

	if(!has_ack(hdr))
	  return -1;

	if(hdr->ack != my_seq)
	  return -1;

	if(fcntl(sock, F_SETFL, O_NONBLOCK) == -1)
	  return -1;

	return 1;
}

// Continuously listen for and send data.
void loop(){
 struct timeval tv;
 tv.tv_sec = 0;
 tv.tv_usec = 100;
 if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv))){
    perror("Error with setsockopt:");
 }

 struct timespec start_time;
 clock_gettime(CLOCK_REALTIME, &start_time);
 uint32_t bytes_sent = 0;
 uint32_t data_bytes_sent = 0;

 // Open a logfile
 FILE* logfile = fopen("logfile.dat", "w");
 fprintf(logfile, "runtime yourwin byterate throughput inst_byterate inst_throughput\n");

 int packet_count = 0;
 int cumulative_bytes = 0;
 int cumulative_data_bytes = 0;
 struct timespec instant_rate_time;

 do{
 	struct addrinfo source;
	memset(&source, 0, sizeof(struct addrinfo));
	int rval = recvfrom(sock, rbuf, MTU, 0, source.ai_addr, &source.ai_addrlen);

	butp_wtheader* packet = (butp_wtheader*)rbuf;
	network_header_to_host(packet);

	butp_packet outgoing_packet;
	
	if(rval == -1){
	  if(errno != EAGAIN){
	    perror("Error in underlying networking");
	    printf("errno: %i\n",errno);
	    clear_lists();
	    return;
	  }
	}
	else{
	  if(!same_user(&source, destination))
	    continue;
	
	  int skip = 0;
	  if(SIMULATE_LOSSY_MEDIUM){
	    skip = rand() % 100;
	    if(skip < PACKET_LOSS_RATIO){
	      memset(packet, 0, sizeof(butp_header));
	    }
	    else{
	      process_incoming(rbuf, rval, &outgoing_packet);
	    }
	  }
	  else{
	    process_incoming(rbuf, rval, &outgoing_packet);
	  }
	}

	build_outgoing(packet, get_data_length(packet, rval), &outgoing_packet);
	
	packet_timeout_function();
	
	uint16_t options = outgoing_packet.header.opt;
	if((options&EMPTY_PACKET) == 0){
	  continue;
	}

	uint32_t outgoing_size = outgoing_packet.data_size + get_header_length(&outgoing_packet.header);

	fill_output_buffer(&outgoing_packet);
	if(has_data(&outgoing_packet.header))
	  bytes_in_transit+=outgoing_packet.data_size;

	host_header_to_network((butp_wtheader*)sbuf);

	if(SIMULATE_CORRUPTION){
	  int corrupt = rand() % 100;
	  if(corrupt < CORRUPTION_RATIO){
	    *(sbuf+sizeof(butp_wtheader)) = rand();
	    *(sbuf+sizeof(butp_wtheader) + 4) = rand();
	    *(sbuf+sizeof(butp_wtheader) + 8) = rand();
	  }
	}

	rval = sendto(sock, sbuf, outgoing_size, 0, (struct sockaddr*)destination->ai_addr, destination->ai_addrlen);

	struct timespec send_time;
	clock_gettime(CLOCK_REALTIME, &send_time);

	bytes_sent+=outgoing_size;

	if(has_data(&outgoing_packet.header))
	  data_bytes_sent+=outgoing_packet.data_size;

	// Bitrate measurements
	float run_time = send_time.tv_sec - start_time.tv_sec;
	run_time += (float)((float)(send_time.tv_nsec - start_time.tv_nsec) / 1000000000);
	float byte_rate = bytes_sent / run_time;
	float data_byte_rate = data_bytes_sent / run_time;

	packet_count++;
	cumulative_bytes+=outgoing_size;
	cumulative_data_bytes+=outgoing_packet.data_size;

	float inst_byte_rate;
	float inst_data_byte_rate;
	if(packet_count == 100){
	  float short_run_time = send_time.tv_sec - instant_rate_time.tv_sec;
	  short_run_time += (float)((float)(send_time.tv_nsec - instant_rate_time.tv_nsec) / 1000000000);

	  inst_byte_rate = cumulative_bytes / short_run_time;
	  inst_data_byte_rate = cumulative_data_bytes / short_run_time;

	  cumulative_bytes = 0;
	  cumulative_data_bytes = 0;
	  packet_count = 0;

	  instant_rate_time.tv_sec = send_time.tv_sec;
	  instant_rate_time.tv_nsec = send_time.tv_nsec;
	}
	

	fprintf(logfile, "%f\t%i\t%f\t%f\t%f\t%f\n", run_time, your_win, byte_rate, data_byte_rate, inst_byte_rate, inst_data_byte_rate);

 }while(1);
 fclose(logfile);
}

void packet_timeout_function(){
	if(in_transit_first == NULL)
	  return;
	
	struct timespec tv;
	struct timespec time_in_transit;
	clock_gettime(CLOCK_REALTIME, &tv);

	// Don't bother with re-transmissions if the first packet in transmission hasn't timed out.
	butp_packet* ptr = in_transit_first;
	time_in_transit.tv_sec = tv.tv_sec - ptr->tv.tv_sec;
	time_in_transit.tv_nsec = tv.tv_nsec - ptr->tv.tv_nsec;
	if(!time_exceeded(&time_in_transit))
	  return;
	
	do{
	  // Shrink window for first re-transmission.
	  if(ptr->no_trans == 1){
	    if(your_win - packet_timeout_window_shrink > receiver_window_min){
	      your_win -= packet_timeout_window_shrink;
	    }
	    else
	      your_win = receiver_window_min;
	  }

	  ptr->header.opt=FL_DAT;
	  ptr->tv.tv_sec = tv.tv_sec;
	  ptr->tv.tv_nsec = tv.tv_nsec;

	  memcpy(sbuf, &ptr->header, sizeof(butp_header));
	  memcpy(sbuf+sizeof(butp_header), ptr->datum, ptr->data_size);

	  ((butp_header*)sbuf)->chk = calculate_checksum(sbuf, sizeof(butp_header)+ptr->data_size);
	   
	  host_header_to_network((butp_wtheader*)sbuf);
	  int rval = sendto(sock, sbuf, sizeof(butp_header)+ptr->data_size, 0, (struct sockaddr*)destination->ai_addr, destination->ai_addrlen);
	  if(rval != -1)
	    ptr->no_trans++;

	  ptr = ptr->next;
	}while(ptr != NULL);
}

void process_incoming(char* buf, const int len, butp_packet* outgoing_packet){
	butp_wtheader* inbound = (butp_wtheader*)buf;

	// Process ack.
	if(has_ack(inbound)){
	  butp_packet* packet_in_transit = get_packet_in_transit(inbound->ack);
	  if(packet_in_transit != NULL){
	    your_win += packet_ack_window_increase;
	    if(your_win > receiver_window_max)
	      your_win = receiver_window_max;
	    bytes_in_transit -= packet_in_transit->data_size;
	    unlink_packet_from_in_transit(packet_in_transit);
	  }
	}

	// Process sequence number.
	uint32_t data_length = get_data_length(inbound, len);
	uint16_t header_length = get_header_length(inbound);
	if(has_data(inbound) && checksum_ok(buf, len)){
	  if(CONTINUOUS_TRANSMISSION == 0){
	    butp_packet* new_data = malloc(sizeof(butp_packet));
	    memset(new_data, 0, sizeof(butp_packet));
	    new_data->header.seq = inbound->seq;
	    new_data->data_size = data_length;
	    new_data->datum = malloc(data_length);
	    memcpy(new_data->datum, buf+header_length, data_length);
	    insert_packet_in_input_buffer(new_data);
	  }

	  // Set ACK flag and ACK value in outgoing packet.
	  outgoing_packet->header.ack = inbound->seq;
	  outgoing_packet->header.opt|=FL_ACK;
	}

	// Process FIN flag.
	if(has_fin(inbound))
	  reception_finished = 1;

	// Process FIN-ACK flag.
	if(has_fin_ack(inbound))
	  transmission_finished = 1;

	// Process timestamp.
	if(!is_your_timestamp(inbound)){
	    struct timespec tv;
	    clock_gettime(CLOCK_REALTIME, &tv);
	    RTT = tv.tv_nsec - inbound->timestamp;
 	    adjust_packet_timeout();
	}

	// Process window information.
	if(has_window(inbound))
	    your_win = inbound->window;

	// Process FL_SYN flag.

	// Process FL_ABO flag.
}

void build_outgoing(butp_wtheader* packet, uint32_t packet_data_size, butp_packet* outgoing_packet){
	// Copy timestamp information, if necessary.
	if(has_timestamp(packet) && is_your_timestamp(packet)){
	  outgoing_packet->header.timestamp = packet->timestamp;
	  outgoing_packet->header.opt|=FL_TME;
	}
	else{
 	  // Don't insert RTT measurements in every packet.
	  if((rand() % RTT_RAND_MODULUS) == RTT_RAND_VALUE){
	    (outgoing_packet->header.opt)|=FL_TME;
	    (outgoing_packet->header.opt)|=FL_MTM;
	    struct timespec tv;
	    clock_gettime(CLOCK_REALTIME, &tv);
	    outgoing_packet->header.timestamp = tv.tv_nsec;
	  }
	}

	// Fill in FIN-ACK flag, if needed.
	if(reception_finished)
	  outgoing_packet->header.opt|=FL_FAC;

	// This just applies for continuous transmission of random data - create packets when there are none in the queue.
	if((first_in_buffer == NULL) && CONTINUOUS_TRANSMISSION){
	  butp_packet* packet = malloc(sizeof(butp_packet));
	  memset(packet, 0, sizeof(butp_packet));
	  first_in_buffer = packet;
	  packet->header.seq = my_seq+MTU-FULL_HEADER_SIZE;
	  packet->data_size = MTU-FULL_HEADER_SIZE;
	  packet->datum = malloc(packet->data_size);
	  my_seq += packet->data_size;
	}
	
	// Pull packet from output buffer.
	butp_packet* next_data_out = first_in_buffer;
	if((next_data_out == NULL) && (in_transit_first == NULL)){
	  outgoing_packet->header.opt|=FL_FIN;
	  return;
	}

	if((next_data_out == NULL))
	  return;

	// We must not ignore the receiver window size!	
	if(bytes_in_transit+next_data_out->data_size > your_win)
	  return;

	// Set packet outgoing time, used for checking packet in-transit time expiration based on current value of round-trip time.
	clock_gettime(CLOCK_REALTIME, &first_in_buffer->tv);

	first_in_buffer = first_in_buffer->next;

	outgoing_packet->header.opt|=FL_DAT;
	outgoing_packet->header.seq = next_data_out->header.seq;
	outgoing_packet->data_size = next_data_out->data_size;
	outgoing_packet->datum = next_data_out->datum;
	outgoing_packet->header.chk = next_data_out->header.chk;

	next_data_out->no_trans = 1;

	link_packet_to_in_transit(next_data_out);
}

int pull_data_from_input_buffer(char* buf, uint32_t buflen){
	if(!reception_finished)
	  return -1;

	if(received_ready_first == NULL)
	  return -1;

	if(buflen <= 0)
	  return -1;

	if(buf == NULL)
	  return -1;

	int pushed_bytes = 0;
	int buffer_free = buflen;
	int data_size = 0;
	do{
	  data_size = received_ready_first->data_size;
	  if(data_size > buffer_free)
	    break;
	  memcpy(buf+pushed_bytes, received_ready_first->datum, data_size);
	  buffer_free-=data_size;
	  received_ready_first = received_ready_first->next;
	  free(received_ready_first);
	  pushed_bytes+=data_size;
	}while(received_ready_first!=NULL);

	return pushed_bytes;
}

int push_data_to_output_buffer(const char* buf, const int buflen){
	if(buf == NULL)
	  return -1;

	if(buflen <= 0)
	  return -1;

	int buffer_left = buflen;
	int buffer_offset = 0;
	butp_packet* packet = NULL;
	do{
	  butp_packet* prev = packet;
	  packet = malloc(sizeof(butp_packet));
	  memset(packet, 0, sizeof(butp_packet));
	  if(prev != NULL)
	    prev->next = packet;
	  else
	    first_in_buffer = packet;

	  uint32_t data_size = MTU-FULL_HEADER_SIZE;
	  if((int)(buffer_left - data_size) < 0)
	    data_size = buffer_left;

	  buffer_left -= data_size;	

	  packet->data_size = data_size;
	  packet->header.seq = my_seq;
	  my_seq+=data_size;
	  packet->header.opt&=FL_DAT;
	  packet->datum = (char*)(buf+buffer_offset);
	  buffer_offset+=data_size;
	  packet->header.chk = 0;
	}while(buffer_left > 0);
	packet->next = NULL;

	return 0;
}

int time_exceeded(struct timespec* tv){
	if(tv->tv_sec > packet_timeout_data.tv_sec)
	  return 1;
	else if(tv->tv_nsec >= packet_timeout_data.tv_nsec)
	  return 1;
	return 0;
}

uint16_t calculate_checksum(const char* buf, uint32_t buflen){
	if(buf == NULL)
	  return 0;
	uint16_t checksum = 0;
	uint32_t done = 0;
	do{
	  uint16_t dbyte = 0;
	  dbyte = *(buf+done);
	  dbyte = ~dbyte;	  
	  checksum += dbyte;
	  done+=2;
	}while(done < buflen);
	checksum = ~checksum;
	return checksum;
}

void network_header_to_host(butp_wtheader* packet){
	if(packet == NULL)
	  return;
	packet->seq = ntohl(packet->seq);
	packet->ack = ntohl(packet->ack);
	packet->chk = ntohs(packet->chk);
	packet->opt = ntohs(packet->opt);
	if(packet->opt&FL_WIN || packet->opt&FL_TME){
	  packet->window = ntohl(packet->window);
	}
	else if(packet->opt&FL_TME && packet->opt&FL_WIN){
	  packet->window = ntohl(packet->window);
	  packet->timestamp = ntohl(packet->timestamp);
	}
}

void host_header_to_network(butp_wtheader* packet){
	if(packet == NULL)
	  return;
	packet->seq = htonl(packet->seq);
	packet->ack = htonl(packet->ack);
	packet->chk = htons(packet->chk);
	if(packet->opt&FL_WIN || packet->opt&FL_TME){
	  packet->window = htonl(packet->window);
	}
	else if(packet->opt&FL_TME && packet->opt&FL_WIN){
	  packet->window = htonl(packet->window);
	  packet->timestamp = htonl(packet->timestamp);
	}
	packet->opt = htons(packet->opt);
}

int seq_ok(butp_wtheader* packet, const int datasize){
	if(packet == NULL)
	  return -1;

	// Make sure we haven't already received this packet and its data!
	butp_packet* ptr = received_ready_first;
	if(received_ready_first != NULL){
	  do{
	    if(ptr->header.seq == packet->seq)
	      return -1;
	    ptr = ptr->next;
	  }while(ptr != NULL);
	}

	// Since we have not seen this packet before, it means it is new to us, but only accept it if it fits within our window!
	if(received_ready_first != NULL){
	  uint32_t sequence_high = received_ready_first->header.seq + my_win;
	  if((packet->seq+datasize) > sequence_high)
	    return -1;
	}
	return 1;
}

void clear_lists(){
	// Free in-transit memory.
	butp_packet* ptr = in_transit_first;
	butp_packet* tp = NULL;
	while(ptr != NULL){
	  tp = ptr->next;
	  free(ptr);
	  ptr = tp;
	}
	// Free output buffer memory.
	ptr = first_in_buffer;
	while(ptr != NULL){
	  tp = ptr->next;
	  free(ptr);
	  ptr = tp;
	}
}

int last_packet(butp_wtheader* packet){
	return ((packet->opt)&FL_FIN);
}

int has_data(const butp_wtheader* packet){
	return ((packet->opt)&FL_DAT);
}

int has_ack(const butp_wtheader* packet){
	return ((packet->opt)&FL_ACK);
}

int has_window(const butp_wtheader* packet){
	return ((packet->opt)&FL_WIN);
}

int has_timestamp(const butp_wtheader* packet){
	return ((packet->opt)&FL_TME);
}

int get_header_length(const butp_wtheader* packet){
	int header_length = NORMAL_HEADER_SIZE;
	if(has_window(packet) && has_timestamp(packet))
          header_length+=0x8;
        else if(has_window(packet) || has_timestamp(packet))
          header_length+=0x4;
	return header_length;
}

int get_data_length(butp_wtheader* packet, const int packlen){
	return packlen-get_header_length(packet);
}

int checksum_ok(char* packet, const int packet_length){
	butp_wtheader* pack = (butp_wtheader*)packet;
	uint16_t packet_checksum = pack->chk;
	pack->chk = 0;
	uint16_t checksum = calculate_checksum(packet, packet_length);
	if(checksum != packet_checksum){
	  pack->chk = packet_checksum;
	  return 0;
	}
	pack->chk = packet_checksum;
	return 1;
}

butp_packet* already_transmitted(const uint32_t seq){
	butp_packet* ptr = in_transit_first;
	while(ptr != NULL){
	  if(ptr->header.seq == seq)
	    return ptr;
	  ptr = ptr->next;
	}
	return NULL;
}

int already_received(const uint32_t seq){
	butp_packet * ptr = received_ready_first;
	if(ptr == NULL)
	  return -1;
	do{
	  if(seq == ptr->header.seq)
	    return 1;
	  ptr = ptr->next;
	}while(ptr != NULL);
	return -1;
}

void empty_in_transit(){
	butp_packet* ptr = in_transit_first;
	if(ptr == NULL)
	  return;
	do{
	  butp_packet* nxt = ptr->next;
	  free(ptr);
	  ptr = nxt;
	}while(ptr != NULL);
}

void adjust_packet_timeout(){
	uint32_t new_timeout = 0;
	uint32_t proposed_timeout = round_trip_time_factor*RTT;

	if(proposed_timeout < packet_timeout_min)
	  new_timeout = packet_timeout_min;
	else
	  new_timeout = proposed_timeout;

	packet_timeout_data.tv_sec = 0;
	packet_timeout_data.tv_nsec = new_timeout;
}

butp_packet* get_packet_in_transit(const uint32_t seq){
	if(in_transit_first == NULL)
	  return NULL;
	butp_packet* ptr = in_transit_first;
	do{
	  if(ptr->header.seq == seq)
	    return ptr;
	  if(ptr == ptr->next)
	    ptr->next = NULL;
	  ptr = ptr->next;
	}while(ptr != NULL);
	return NULL;
}

void insert_packet_in_input_buffer(butp_packet* packet){
	if(received_ready_first == NULL){
	  received_ready_first = packet;
	  received_ready_first->next = NULL;
	  return;
	}

	// Don't insert duplicates.
	butp_packet* ptr = received_ready_first;	
	do{
	  if(packet->header.seq == ptr->header.seq)
	    return;
	  ptr = ptr->next;
	}while(ptr != NULL);

	ptr = received_ready_first;
	butp_packet* last = NULL;

	do{
	  if(packet->header.seq < ptr->header.seq){
	    if(ptr == received_ready_first)
	      received_ready_first = packet;
	    packet->next = ptr;
	    if(last != NULL){
	      last->next = packet;
	    }
	    return;
	  }
	  if(ptr->next == NULL){
	    ptr->next = packet;
	    packet->next = NULL;
	    return;
	  }
	  last = ptr;
	  ptr = ptr->next;
	}while(ptr != NULL);
}

int is_your_timestamp(const butp_wtheader* packet){
	return ((packet->opt)&FL_MTM);
}

int has_fin_ack(butp_wtheader* packet){
	return ((packet->opt)&FL_FAC);
}

int has_syn(const butp_wtheader* packet){
	return ((packet->opt)&FL_SYN);
}

int has_fin(const butp_wtheader* packet){
	return ((packet->opt)&FL_FIN);
}

int has_abort(const butp_wtheader* packet){
	return ((packet->opt)&FL_ABO);
}

void fill_output_buffer(const butp_packet* outgoing_packet){
	if(outgoing_packet == NULL)
	  return;
	uint8_t header_length = get_header_length(&outgoing_packet->header);
	uint32_t data_length = outgoing_packet->data_size;

	memcpy(sbuf, &outgoing_packet->header, header_length);
	memcpy(sbuf+header_length, outgoing_packet->datum, data_length);

	butp_wtheader* pheader = (butp_wtheader*)sbuf;
	pheader->chk = 0;
	pheader->chk = calculate_checksum(sbuf, header_length+data_length);
}

void link_packet_to_in_transit(butp_packet* packet){
	if(packet == NULL)
	  return;
	
	if(in_transit_first == NULL){
	  in_transit_first = packet;
	  in_transit_first->next = NULL;
	  return;
	}

	butp_packet* ptr = in_transit_first;
	do{
	  if(ptr->next == NULL){
	    ptr->next = packet;
	    packet->next = NULL;
	    break;
	  }
	  ptr = ptr->next;
	}while(ptr != NULL);
}

void unlink_packet_from_in_transit(butp_packet* packet){
	if(packet == NULL)
	  return;

	butp_packet* ptr = in_transit_first;
	butp_packet* prv = NULL;
	if(ptr == NULL)
	  return;

	do{
	  if(ptr == packet){
	    if(prv != NULL)
	      prv->next = packet->next;
	    else
	      in_transit_first = packet->next;
	    free(packet);
	    return;
	  }
	  prv = ptr;
	  ptr = ptr->next;
	}while(ptr != NULL);
}

int same_user(const struct addrinfo* userA, const struct addrinfo* userB){
        if(userA == NULL || userB == NULL)
          return -1;
        if(userA->ai_family != userB->ai_family)
          return -1;
        if(userA->ai_addr == NULL || userB->ai_addr == NULL)
          return -1;
        uint16_t sport = *(userA->ai_addr->sa_data);
        uint16_t dport = *(userB->ai_addr->sa_data);
        if(sport != dport);
          return -1;
        int family = userA->ai_family;
        if(family == AF_INET){
          uint32_t saddr = *(userA->ai_addr->sa_data + 2);
          uint32_t daddr = *(userB->ai_addr->sa_data + 2);
          if(saddr != daddr)
            return -1;
          return 1;
        }
        else if(family == AF_INET6){
          uint64_t saddr = *(userA->ai_addr->sa_data + 2);
          uint64_t daddr = *(userB->ai_addr->sa_data + 2);
          if(saddr != daddr)
            return -1;
          return 1;
        }
        else
          return -1;
}
