/* Author: Alexander Rajula
 * Contact: alexander@rajula.org
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include "butp_functions.h"

int main(int argc, char **argv){
	if(argc != 7){
	  printf("Usage: hostname port packet_loss_ratio corruption_ratio continuous_transmission simulation_runtime\n");
	  return 0;
	}
	char address[256];
	char port[6];
	memset(address, 0, sizeof(address));
	memset(port, 0, sizeof(port));
	strcpy(address, argv[1]);
	strcpy(port, argv[2]);
	if(atoi(port) > 65535){
	  printf("Port range exceeded (65535).\n");
	  return 0;
	}

	uint8_t ploss = atoi(argv[3]);
	if(ploss > 100){
	  printf("Packet loss ratio cannot exceed 100 percent.\n");
	  return 0;
	}

	uint8_t corr = atoi(argv[4]);
	if(corr > 100){
	  printf("Corruption ratio cannot exceed 100 percent.\n");
	  return 0;
	}

	struct addrinfo hints;
	memset(&hints, 0, sizeof(struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;

	int status;
	struct addrinfo* servinfo = NULL;
        /* Get information about all IP addresses to choose from. */
        if((status = getaddrinfo(address,port, &hints, &servinfo)) != 0){
                fprintf(stderr,"Error in getaddrinfo: %s\n", gai_strerror(status));
                exit(1);
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
        if((iface[0]<48)||(iface[0]>57) || j >= i){
         printf("Not a destination - exiting.\n");
         exit(1);
        }

        int k;
        p = servinfo;
        for(k = 0; k < j && k < i; k++){
         if(p->ai_next != NULL)
          p = p->ai_next;
        }

	if(!set_parameters(p, ploss, corr, atoi(argv[5]), atoi(argv[6]))){
	  printf("Could not set network parameters.\n");
	  return -1;
	}

	if(!syn_init()){
	  printf("Could not establish connection to sleepy server.\n");
	  return -1;
	}

	loop();

	return 0;
}
