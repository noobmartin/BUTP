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
#include <math.h>
#include "butp_data.h"

int set_parameters(struct addrinfo* dest, uint8_t packet_loss, uint8_t corruption_ratio, uint8_t transmission_type, uint32_t run_time, uint32_t transmission_count, uint8_t wireless, uint8_t active_queue_management){
	memset(rbuf, 0, MTU);
	memset(sbuf, 0, MTU);

	if(dest == NULL)
	  return -1;
	destination = dest;
	
	first_in_buffer = NULL;
	in_transit_first = NULL;
	received_ready_first = NULL;
	
	my_win = receiver_window_max;
	congestion_win = receiver_window_max;

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

	SIMULATION_RUNTIME = run_time;

	TRANSMISSION_COUNT = transmission_count;

	WIRELESS_CONNECTION = wireless;

	TRANSMISSION_MODE = BOOST;

	ACTIVE_QUEUE_MANAGEMENT = active_queue_management;

	state = CLOSED;
 
 	raw_logfile = fopen("raw_output.dat", "w");
	fprintf(raw_logfile, "time\twindow\tbyterate\tdatarate\tinstbyte\tinstdata\tRTT\n");
 	goodput_logfile = fopen("goodput.dat", "w");

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
	if(!has_syn(hdr))
	  return -1;

	if(!has_ack(hdr))
	  return -1;

	if((hdr->ack) != my_seq)
	  return -1;

	if(has_wireless(hdr))
	  WIRELESS_CONNECTION = 1;

	struct timespec ti;
	clock_gettime(CLOCK_REALTIME, &ti);
	RTT = (uint32_t)(ti.tv_nsec) - hdr->timestamp;
	adjust_packet_timeout();
	
	if(has_window(hdr))
	  receiver_window_max = hdr->window;

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
	  receiver_window_max = whdr->window;
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

	if(has_wireless(hdr))
	  WIRELESS_CONNECTION = 1;

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
int i,avg = 0;
int count = -1;
int p_b,pb_avg=0;
float init_wq,wq = 0.002;
float pa,maxp, pb,alpha,beta,gama,delta1,delta2 =0;
int size =INPUT_QUEUE_SIZE ;
int m,pm,minth,maxth,Q,init_maxTh,k,kl,kh,km,C1,C2,G1,G2 = 0;
C1 = 12;
C2 = 15;
G1 = 54;
G2 = 18;
maxp = 0.8;
minth = 5;
maxth = 3* minth;

pb = 0.1;
gama = 0.1;

init_maxTh = Q = size;
init_wq = 0.02;
k= 4;
unsigned int random_value = 0x0;
struct timeval now;

time_t ptime,qtime,freeze_time;



gettimeofday(&now, NULL);
ptime = now.tv_usec;
qtime = now.tv_usec-10; //has to be done at when the que

 struct timeval tv;
 tv.tv_sec = 0;
 tv.tv_usec = 100;
 if(setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&tv, sizeof(tv))){
    perror("Error with setsockopt:");
 }

 clock_gettime(CLOCK_REALTIME, &start_time);
 uint32_t bytes_sent = 0;
 uint32_t data_bytes_sent = 0;

 int packet_count = 0;
 int cumulative_bytes = 0;
 int cumulative_data_bytes = 0;
 struct timespec instant_rate_time = {0,0};

 do{
 	struct timespec st = {0,400};
	nanosleep(&st,NULL);
	
 	struct addrinfo source;
	memset(&source, 0, sizeof(struct addrinfo));
	int rval = recvfrom(sock, rbuf, MTU, 0, source.ai_addr, &source.ai_addrlen);

	butp_wtheader* packet = (butp_wtheader*)rbuf;
	network_header_to_host(packet);

	butp_packet outgoing_packet;
	memset(&outgoing_packet, 0, sizeof(butp_packet));

	if(rval == -1){
	  if(errno != EAGAIN){
	    perror("Error in underlying networking");
	    printf("errno: %i\n",errno);
	    clear_lists();
	    return;
	  }
	}
	else{
	  if(!same_user(&source, destination)){
	    continue;
	  }
	
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
else if(ACTIVE_QUEUE_MANAGEMENT == RED){
        // Number of bytes in input queue is called BYTES_IN_INPUT_QUEUE.
        // Max size of input buffer is called INPUT_QUEUE_SIZE.

        for( i=0; i<BYTES_IN_INPUT_QUEUE;i++)//packet arrival
        {
          do{
            avg = size/BYTES_IN_INPUT_QUEUE;
            if (avg)// queue
            {
                avg = (1-wq)*avg +wq;
            }
            else
            {
                m = ptime-qtime;
                avg = pow((1-wq),m) * avg;
            }
            if((minth <= avg)&&(avg < maxth))
            {
                count++;
                pb = maxp * ((avg-minth)/(maxth-minth));
                pa = pb/(1-count * pb);
                count = 0;
                process_incoming(rbuf,rval,&outgoing_packet);
            }
            else if(maxth < avg)
            {
                count = 0;
                memset(packet,0,sizeof(butp_header));
            }
            else
            {
                count = -1;
                process_incoming(rbuf,rval,&outgoing_packet);
            }
	   }while( BYTES_IN_INPUT_QUEUE == 0);
           qtime = ptime;
         }
	 if(BYTES_IN_INPUT_QUEUE == 0)
	   process_incoming(rbuf, rval, &outgoing_packet);
}
else if(ACTIVE_QUEUE_MANAGEMENT == BLUE){
        delta1 = 0.00025;
        delta2 = 0.000025;
        freeze_time = 0.01;//micro seconds
        pm = 0.05;
        if(Q > BYTES_IN_INPUT_QUEUE )
        {
		printf("Q: %i bytes_in_input: %i\n", Q, BYTES_IN_INPUT_QUEUE);
		printf("ptime: %i qtime: %i freeze: %i\n", ptime, qtime, freeze_time);
                if((ptime - qtime ) >= freeze_time)
                {
                        pm += delta1;
                        qtime = ptime;
                        process_incoming(rbuf,rval,&outgoing_packet);
                }
        }
        else
        {
                if((ptime - qtime ) > freeze_time)
                {
                        pm -= delta2;
                        qtime = ptime;
                        memset(packet,0,sizeof(butp_header));

                }
        }
}
else if(ACTIVE_QUEUE_MANAGEMENT == DSRED){
        for(i = 0; i<BYTES_IN_INPUT_QUEUE;i++)
        {
                avg = (1-wq) *avg +wq;
                kl = maxth;
                kh = minth;
                km = 0.5 *(kl-kh);

                alpha = 2*(1-gama)/(kh-kl);
                beta = 2*gama/(kh-kl);

        	if(avg < kl)
        	{
                  pb_avg = 0;
                  process_incoming(rbuf,rval,&outgoing_packet);
        	}
        	else if((kl<=avg) &&(avg <=km))
        	{
                  pb_avg = alpha *(avg - kl);
                  random_value = random();
                  memset(packet,0,sizeof(butp_header));
        	}
        	else if((kh <= avg)&&(kh <= size))
        	{
                  pb_avg = 1;
                  memset(packet,0,sizeof(butp_header));
        	}
        }
        if(pb_avg > 0 && (size/pb_avg > random_value/pb_avg))
        {       
		pb_avg = 1;
                memset(packet,0,sizeof(butp_header));
        }
	if(BYTES_IN_INPUT_QUEUE == 0)
	  process_incoming(rbuf, rval, &outgoing_packet);
}
else if(ACTIVE_QUEUE_MANAGEMENT == SDRED){
        for(i = 0; i<BYTES_IN_INPUT_QUEUE;i++)
        {
		avg = size/BYTES_IN_INPUT_QUEUE;
         	if (avg)// queue
        	{
                  avg = (1-wq) *avg +wq;
        	}
        	else
        	{
                  m = ptime-qtime;
                  avg = pow((1-wq),m) * avg;
        	}
        	if(minth > avg){
                  count = -1;
                  process_incoming(rbuf,rval,&outgoing_packet);
        	}
        	else if(init_maxTh  > avg)
        	{
                  maxth = init_maxTh ;
                  wq = init_wq;
                  pb = 1 ;
        	}
        	else if(0.7 *Q > avg)
        	{
                  maxth = init_maxTh +0.1*Q;
                  wq = k * init_wq;
                  pb = 1;
        	}
        	else if(0.8*Q > avg)
        	{
                  maxth = init_maxTh +0.2*Q;
                  wq = k*k * init_wq;
                  pb = 1;
        	}
        	else if(0.9*Q > avg)
        	{
                  maxth = init_maxTh +0.3*Q;
                  wq = k*k*k * init_wq;
                  pb = 1 ;
        	}
		else
                  memset(packet,0,sizeof(butp_header));

        	if(pb)
        	{
                  p_b = ((C1 * avg ) >> BYTES_IN_INPUT_QUEUE) - C2;
                  if(p_b)
                    memset(packet,0,sizeof(butp_header));
        	}
        	else{
                  p_b = ((G1 * avg ) >> BYTES_IN_INPUT_QUEUE) - G2;
                  process_incoming(rbuf,rval,&outgoing_packet);
        	}

        	count++;
        	if(count > 0 && p_b >0 && count > random_value/p_b)
        	{
                  count = 0;
                  random_value = (random() >> 5) & 0xFFFF;
                  p_b = 1;
        	}
       		if(count == 0)
        	{
                  random_value = (random() >> 5) & 0xFFFF;
                  p_b = 0;
        	}
        }
	if(BYTES_IN_INPUT_QUEUE == 0)
	  process_incoming(rbuf, rval, &outgoing_packet);
}

	  else{
	    process_incoming(rbuf, rval, &outgoing_packet);
	  }

	  if(rval == -1){
	    break;
	  }
	}

	if( (TRANSMISSION_COUNT != 0) && (ACKED_BYTE_COUNT >= TRANSMISSION_COUNT) ){
	  clear_lists();
	  send_abort();
	  state = KILL;
	  break;
	}

	packet_timeout_function();

	build_outgoing(packet, get_data_length(packet, rval), &outgoing_packet);
	
	if(!has_ack(&outgoing_packet.header) && !has_data(&outgoing_packet.header))
	  continue;
	
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

	float inst_byte_rate = 0;
	float inst_data_byte_rate = 0;
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
	  
	  fprintf(raw_logfile, "%f\t%i\t%f\t%f\t%f\t%f\t%i\n", run_time, congestion_win, byte_rate, data_byte_rate, inst_byte_rate, inst_data_byte_rate, RTT);
	}
	

	if( (SIMULATION_RUNTIME != 0) && ( (send_time.tv_sec - start_time.tv_sec) >= SIMULATION_RUNTIME) ){
	  clear_lists();
	  send_abort();
	  state = KILL;
	  break;
	}

 }while(1);
   switch(state){
	case KILL:
	  fclose(raw_logfile);
	  fclose(goodput_logfile);
	  break;
   }

   return;
}

void send_abort(){
	butp_header* outgoing = (butp_header*)sbuf;

	memset(outgoing, 0, sizeof(butp_header));
	outgoing->opt = FL_ABO;

	outgoing->opt = htons(outgoing->opt);
	
	int i;
	for(i = 0; i < 10; i++){
	 sendto(sock, outgoing, sizeof(butp_header), 0, (struct sockaddr*)destination->ai_addr, destination->ai_addrlen);
	}
}

void packet_timeout_function(){
	if(in_transit_first == NULL)
	  return;
	
	struct timespec tv;
	struct timespec time_in_transit;
	static struct timespec last_time = {0,0};
	static struct timespec last_run_diff = {0,0};
	clock_gettime(CLOCK_REALTIME, &tv);

	// Don't bother with re-transmissions if the first packet in transmission hasn't timed out.
	butp_packet* ptr = in_transit_first;
	time_in_transit.tv_sec = tv.tv_sec - ptr->tv.tv_sec;
	time_in_transit.tv_nsec = tv.tv_nsec - ptr->tv.tv_nsec;
	if(!time_exceeded(&time_in_transit))
	  return;

	last_run_diff.tv_sec = tv.tv_sec - last_time.tv_sec;
	last_run_diff.tv_nsec = tv.tv_nsec - last_time.tv_nsec;

	// Only re-transmit if at least a timeout interval has passed since the last re-transmission.
	//if(tv.tv_sec - last_time.tv_sec < 1)
	  //return;
	if(!time_exceeded(&last_run_diff))
	  return;

	modify_congestion_window(RETRANSMISSION_TIMEOUT);

	do{
	ptr->header.opt=FL_DAT;

	memcpy(sbuf, &ptr->header, sizeof(butp_header));
	memcpy(sbuf+sizeof(butp_header), ptr->datum, ptr->data_size);

	((butp_header*)sbuf)->chk = calculate_checksum(sbuf, sizeof(butp_header)+ptr->data_size);
	   
	host_header_to_network((butp_wtheader*)sbuf);
	int rval = sendto(sock, sbuf, sizeof(butp_header)+ptr->data_size, 0, (struct sockaddr*)destination->ai_addr, destination->ai_addrlen);
	if(rval != -1)
	  ptr->no_trans++;
	
	ptr = ptr->next;

	}while(ptr != NULL);

	clock_gettime(CLOCK_REALTIME, &last_time);
	printf("Re-transmitted packet, bytes in transit: %i\n", bytes_in_transit);
}

int process_incoming(char* buf, const int len, butp_packet* outgoing_packet){
	butp_wtheader* inbound = (butp_wtheader*)buf;

	// Process ack.
	if(has_ack(inbound)){
	  butp_packet* packet_in_transit = get_packet_in_transit(inbound->ack);
	  
	  if(packet_in_transit != NULL){
	    modify_congestion_window(ACK_RECEIVED);
	    if(PACKET_COUNTER == 0){
	      clock_gettime(CLOCK_REALTIME, &goodput_time_start);
	    }

	    PACKET_COUNTER++;
	    GOODPUT_COUNTER+=packet_in_transit->data_size;

	    ACKED_BYTE_COUNT += packet_in_transit->data_size;

	    printf("Acked bytes: %i\n", ACKED_BYTE_COUNT);

	    if(PACKET_COUNTER >= 100){
	      struct timespec goodput_final_time;
	      clock_gettime(CLOCK_REALTIME, &goodput_final_time);
	      
	      float goodput_run_time = goodput_final_time.tv_sec - goodput_time_start.tv_sec;
	      goodput_run_time += (float)((float)(goodput_final_time.tv_nsec - goodput_time_start.tv_nsec) / 1000000000);

	      uint32_t goodput = GOODPUT_COUNTER/goodput_run_time;
	
	      float timestamp = goodput_time_start.tv_sec - start_time.tv_sec;
	      timestamp += (float)((float)(goodput_time_start.tv_nsec - start_time.tv_nsec) / 1000000000);
	      fprintf(goodput_logfile, "%f\t%d\n", timestamp, goodput);

	      PACKET_COUNTER = 0;
	      GOODPUT_COUNTER = 0;
	    }

	    bytes_in_transit -= packet_in_transit->data_size;
	    if(CONTINUOUS_TRANSMISSION == 0){
	      unlink_packet_from_in_transit(packet_in_transit);
	    }
	  }
	}

	// Process sequence number.
	uint32_t data_length = get_data_length(inbound, len);
	uint16_t header_length = get_header_length(inbound);
	if(has_data(inbound) && checksum_ok(buf, len)){
	    butp_packet* new_data = malloc(sizeof(butp_packet));
	    //memset(new_data, 0, sizeof(butp_packet));
	    new_data->header.seq = inbound->seq;
	    new_data->data_size = data_length;
	    //new_data->datum = malloc(data_length);
	    //memcpy(new_data->datum, buf+header_length, data_length);
	    insert_packet_in_input_buffer(new_data);

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
	    receiver_window_max = inbound->window;

	// Process FL_SYN flag.
	if(has_syn(inbound)){
	  handle_intermittent_syn(inbound);
	  return -1;
	}

	// Process FL_ABO flag.
	if(has_abort(inbound)){
	  clear_lists();
	  state = KILL;
	  return -1;
	}

	return 0;
}

void handle_intermittent_syn(butp_wtheader* inbound){
	  clear_lists();
	  
	  your_seq = inbound->seq;

	  if(has_window(inbound) && !has_timestamp(inbound)){
	    butp_wheader* whdr = (butp_wheader*)inbound;
	    receiver_window_max = whdr->window;
	  }

	  butp_wtheader packet;
	  packet.seq = my_seq;
	  packet.ack = your_seq;
	  packet.opt = FL_SYN;
	  packet.opt |= FL_ACK;
	  int psize = sizeof(butp_header);
	  if(has_window(inbound) && (!has_timestamp(inbound))){
	    packet.opt |= FL_WIN;
	    packet.window = my_win;
	    psize+=0x4;
	  }
	  else if((!has_window(inbound)) && has_timestamp(inbound)){
	    packet.opt |= FL_TME;
	    packet.window = inbound->timestamp;
	    psize+=0x4;
	  }
	  else if(has_window(inbound) && has_timestamp(inbound)){
	    packet.opt |= FL_WIN;
	    packet.opt |= FL_TME;
	    packet.window = my_win;
	    packet.timestamp = inbound->timestamp;
	    psize+=0x8;
	  }

	  host_header_to_network(&packet);

	  int rval = sendto(sock, &packet, psize, 0, destination->ai_addr, destination->ai_addrlen);
	  if(rval == -1){
	    state = KILL;
	    return;
	  }
	  else{
	    if(syn_listen_wait() == -1)
	      state = KILL;
	    return;
	  }
	  state = ESTABLISHED;
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
	if( (first_in_buffer == NULL) && CONTINUOUS_TRANSMISSION && (TRANSMISSION_COUNT != 0) ){
	  if(bytes_in_transit+MTU-FULL_HEADER_SIZE < congestion_win){
	    butp_packet* packet = malloc(sizeof(butp_packet));
	    memset(packet, 0, sizeof(butp_packet));
	    first_in_buffer = packet;
	    packet->header.seq = my_seq+MTU-FULL_HEADER_SIZE;
	    packet->data_size = MTU-FULL_HEADER_SIZE;
	    packet->datum = malloc(packet->data_size);
	    my_seq += packet->data_size;
	  }
	}
	
	// Pull packet from output buffer.
	butp_packet* next_data_out = first_in_buffer;
	if((next_data_out == NULL) && (in_transit_first == NULL)){
	  outgoing_packet->header.opt|=FL_FIN;
	  return;
	}

	if((next_data_out == NULL)){
	  return;
	}

	// We must not ignore the receiver window size!	
	if(bytes_in_transit+next_data_out->data_size > congestion_win){
	  return;
	}

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

	const uint16_t* ptr = (uint16_t*)buf;
	const uint16_t* end = (uint16_t*)(buf+buflen);

	uint16_t checksum = 0;
	while( ptr < end )
	  checksum += ~(*ptr++);

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
if(CONTINUOUS_TRANSMISSION == 0){
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
	uint32_t proposed_timeout = 0;
	
	if(WIRELESS_CONNECTION)
	  proposed_timeout = wireless_timeout_increase_factor*RTT;
	else
	  proposed_timeout = round_trip_time_factor*RTT;

	if(proposed_timeout < packet_timeout_min)
	  new_timeout = packet_timeout_min;
	else
	  new_timeout = proposed_timeout;

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
	static int packets_in_buffer = 0;

	// This is just for test purposes, remove later.
	if(packets_in_buffer == 100){
	  butp_packet* ptr = received_ready_first;
	  do{
	    free(ptr->datum);
	    free(ptr);
	    ptr = ptr->next;
	  }while(ptr != NULL);
	  packets_in_buffer = 0;
	  received_ready_first = NULL;
	}
	// This was just for test purposes, remove later.

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

int has_fin_ack(const butp_wtheader* packet){
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

int has_wireless(const butp_wtheader* packet){
	return ((packet->opt)&FL_WIR);
}

void modify_congestion_window(uint8_t cause){
	static int cumulative_acks = 0;
	static int cumulative_timeouts = 0;

	float btproduct = (float)RTT/1000000000*receiver_window_max;

	if(WIRELESS_CONNECTION){
	  if(cause == ACK_RECEIVED){
	    if(TRANSMISSION_MODE == BOOST){
	      congestion_win = btproduct*(1-pow(boost_power, boost_factor*ACKED_BYTE_COUNT/btproduct));
	      if( (congestion_win / btproduct ) >= boost_transition_division_factor){
		printf("Switching mode to: GLIDE WIN: %i\n", congestion_win);
	        TRANSMISSION_MODE = GLIDE;
	      }
	    }
	    else if(TRANSMISSION_MODE == GLIDE){
	      congestion_win = btproduct*(1-glide_oscillation_amplitude_factor*sin(ACKED_BYTE_COUNT/glide_oscillation_frequency_factor));
	    }
	    else if(TRANSMISSION_MODE == SNEAK){
	      congestion_win += congestion_win/sneak_division_factor;
	      if(cumulative_acks >= 2){
	        cumulative_acks = 0;
	        TRANSMISSION_MODE = RESTORE;
		printf("Switching mode to: RESTORE WIN: %i\n", congestion_win);
	      }
	    }
	    else if(TRANSMISSION_MODE == RESTORE){
	      congestion_win *= restore_factor;
	      if(congestion_win >= btproduct ){
	        congestion_win = btproduct*boost_transition_division_factor;
	        TRANSMISSION_MODE = GLIDE;
		printf("Switching mode to: GLIDE WIN: %i\n", congestion_win);
	      }
	    }
	  }
	  else if(cause == RETRANSMISSION_TIMEOUT){
	    adjust_packet_timeout();

	    if(TRANSMISSION_MODE == BOOST){
	      congestion_win *=0.9;
	      if(cumulative_timeouts >= 4){
	        cumulative_timeouts = 0;
	        TRANSMISSION_MODE = GLIDE;
	        printf("Switching mode to: GLIDE WIN: %i\n", congestion_win);
	      }
	    }
	    else if(TRANSMISSION_MODE == GLIDE){
	      congestion_win *= glide_timeout_congestion_reduce_factor;
	      if(cumulative_timeouts >= 4){
	        cumulative_timeouts = 0;
	        TRANSMISSION_MODE = SNEAK;
	        printf("Switching mode to: SNEAK WIN: %i\n", congestion_win);
	      }
	    }
	    else if(TRANSMISSION_MODE == SNEAK){
	      congestion_win *= sneak_timeout_congestion_reduce_factor;
	    }
	    else if(TRANSMISSION_MODE == RESTORE){
	      congestion_win *= restore_timeout_congestion_reduce_factor;
	      if(cumulative_timeouts >= 3){
	        cumulative_timeouts = 0;
	        TRANSMISSION_MODE = GLIDE;
	        printf("Switching mode to: GLIDE WIN: %i\n", congestion_win);
	      }
	    }
	  }
	}
	else{
	  if(cause == ACK_RECEIVED){
	    printf("ACK cumulative_acks: %i\n", cumulative_acks);
	    cumulative_acks++;
	    cumulative_timeouts = 0;

	    if(TRANSMISSION_MODE == BOOST){
	      printf("BOOST\n");
	      congestion_win = btproduct*(1-pow(boost_power, boost_factor*ACKED_BYTE_COUNT/btproduct));
	      if( (congestion_win / btproduct ) >= boost_transition_division_factor){
		printf("Switching mode to: GLIDE WIN: %i\n", congestion_win);
	        TRANSMISSION_MODE = GLIDE;
	      }
	    }
	    else if(TRANSMISSION_MODE == GLIDE){
	      printf("GLIDE\n");
	      congestion_win = btproduct*(1-glide_oscillation_amplitude_factor*sin(ACKED_BYTE_COUNT/glide_oscillation_frequency_factor));
	    }
	    else if(TRANSMISSION_MODE == SNEAK){
	      printf("SNEAK cumulative_acks: %i\n", cumulative_acks);
	      if( cumulative_acks >= 2 ){
	        cumulative_acks = 0;
	        TRANSMISSION_MODE = RESTORE;
		printf("Switching mode to: RESTORE WIN: %i\n", congestion_win);
	      }
	    }
	    else if(TRANSMISSION_MODE == RESTORE){
	      printf("RESTORE\n");
	      congestion_win *= restore_factor;
	      if(congestion_win >= btproduct){
	        congestion_win = btproduct*boost_transition_division_factor;
	        TRANSMISSION_MODE = GLIDE;
		printf("Switching mode to: GLIDE WIN: %i\n", congestion_win);
	      }
	    }
	  }
	  else if(cause == RETRANSMISSION_TIMEOUT){
	    printf("RTO cumulative_timeouts: %i\n", cumulative_timeouts);
	    cumulative_timeouts++;
	    cumulative_acks = 0;

	    if(TRANSMISSION_MODE == BOOST){
	      if(cumulative_timeouts >= 5){
	        cumulative_timeouts = 0;
	      	congestion_win *=0.9;
	      	TRANSMISSION_MODE = GLIDE;
	      	printf("Switching mode to: GLIDE WIN: %i\n", congestion_win);
	      }
	    }
	    else if(TRANSMISSION_MODE == GLIDE){
	      if(cumulative_timeouts >= 5){
	        cumulative_timeouts = 0;
	        congestion_win *= glide_timeout_congestion_reduce_factor;
	        TRANSMISSION_MODE = SNEAK;
	        printf("Switching mode to: SNEAK WIN: %i\n", congestion_win);
	      }
	    }
	    else if(TRANSMISSION_MODE == SNEAK){
	      congestion_win *= sneak_timeout_congestion_reduce_factor;
	    }
	    else if(TRANSMISSION_MODE == RESTORE){
	      if(cumulative_timeouts >= 5){
	        cumulative_timeouts = 0;
	        congestion_win *= restore_timeout_congestion_reduce_factor;
	        TRANSMISSION_MODE = GLIDE;
	        printf("Switching mode to: GLIDE WIN: %i\n", congestion_win);
	      }
	    }
	  }
	}
	     
	if(congestion_win < receiver_window_min)
	  congestion_win = receiver_window_min;
	
	printf("Btproduct: %f WIN: %i\n", btproduct, congestion_win);
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
