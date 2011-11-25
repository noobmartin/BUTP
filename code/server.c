/* Author: Alexander Rajula
 * Contact: alexander@rajula.org
 */
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include "butp_functions.h"

int read_server_cat(char* buf, int buflen);

int main(int argc, char **argv){
	if(argc != 5){
	  printf("Usage: port packet_loss_ratio corruption_ratio continuous_transmission\n");
	  return 0;
	}

	struct addrinfo* servinfo = NULL;
        char port[6];
	memset(port, 0, 6);
	strcpy(port, argv[1]);

        int num = atoi(port);
        if(num > 65535){
         printf("Port range exceeded (65535) - exiting.\n");
         exit(1);
        }
        else if(num < 1024){
         printf("Please choose an ephemeral port number (min 1025).\n");
         exit(1);
        }

	uint8_t ploss = atoi(argv[2]);
	if(ploss > 100){
	  printf("Packet loss ratio cannot exceed 100 percent.\n");
	  return 0;
	}

	uint8_t corr = atoi(argv[3]);
	if(corr > 100){
	  printf("Corruption ratio cannot exceed 100 percent.\n");
	  return 0;
	}

	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE;

	int status;
        if((status = getaddrinfo(NULL,port, &hints, &servinfo)) != 0){
                fprintf(stderr,"Error in getaddrinfo: %s\n", gai_strerror(status));
                return -1;
        }

        struct addrinfo *p = servinfo;
        void* addr;
        char* ipver;

        printf("These are the addresses which were resolved:\n");
        int i = 0;
        while(p != NULL){
         if(p->ai_family == AF_INET) {
            struct sockaddr_in *ipv4 = (struct sockaddr_in *)p->ai_addr;
            addr = &(ipv4->sin_addr);
            ipver = "IPv4";
         }else{
            struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)p->ai_addr;
            addr = &(ipv6->sin6_addr);
            ipver = "IPv6";
         }
	 char ipstr[INET6_ADDRSTRLEN];
         inet_ntop(p->ai_family,addr,ipstr,sizeof(ipstr));
         printf("%i) %s: %s\n",i, ipver,ipstr);
         p = p->ai_next;
         i++;
        }

        printf("Choose one: ");
        char iface[2];
        iface[0] = getchar();
        iface[1] = '\0';
        int j = atoi(iface);
        if(!isdigit(iface[0]) || j >= i){
         printf("Not a destination - exiting.\n");
         exit(1);
        }

        int k;
        p = servinfo;
        for(k = 0; k < j && k < i; k++){
         if(p->ai_next != NULL)
          p = p->ai_next;
        }

	if(!set_parameters(p, ploss, corr, atoi(argv[4]))){
	  return -1;
	}
	
	char buf[100000];
	int read = read_server_cat(buf, 100000);
	if(read == -1){
	  printf("Could not open file servercat.jpg, please enter correct folder.\n");
	  return 0;
	}

	if(!syn_listen()){
	  return -1;
	}

	if(atoi(argv[4]) == 0)
	  push_data_to_output_buffer(buf, read);

	loop();

	return 0;
}

int read_server_cat(char* buf, int buflen){
        FILE *fp = fopen("servercat.jpg","rb");
	if(fp == NULL)
	  return -1;
        int read = fread(buf, 1, buflen, fp);
        fclose(fp);
        return read;
}
