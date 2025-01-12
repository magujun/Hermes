/*
 * =====================================================================================
 *
 *       Filename:  server.c
 *
 *    Description:  listens for incoming requests, establishes SSH port-forwarding for a particular service 
 *
 *        Version:  1.0
 *        Created:  06/14/2022 05:07:23 PM
 *       Revision:  none
 *       Compiler:  gcc
 *
 *         Author:  maroctamorg (mg), marcelogn.dev@gmail.com
 *   Organization:  
 *
 * =====================================================================================
 */

#include "../networking/network-include.h"
#include "../networking/portable-macros.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/time.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>
#include <errno.h>

typedef struct {
	int start;
	int state;
} stimer_t;

typedef struct {
	int * cpid;
	int * tunnel;
	stimer_t* timer;
} state_t;

int gettime() {
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec;
}

int updtimer(stimer_t* timer) {
	timer->state = gettime() - timer->start;

	return timer->state;
}

void resettimer(stimer_t* timer) {
	timer->start = gettime();
	timer->state = 0;
}

void * timer_f(void * stt) {
	state_t * state = (state_t *) stt;
	
	resettimer(state->timer);
	while(1) {
		
		//*(state->tunnel) = kill( *(state->cpid), 0 ) == -1 ? 0 : 1;
		if( !*(state->tunnel) ) { 
			sleep(60);
			continue;
		}

		printf("timer: %d\n", updtimer(state->timer));
		if(state->timer->state > 1800) {
			printf( "killing ssh tunnel, pid %d\n", *(state->cpid) );
			kill( *(state->cpid), SIGKILL );
			*(state->tunnel) = 0;
			resettimer(state->timer);
		}

		sleep(60);
	}
}

int main() {

#if defined(_WIN32)
    WSADATA d;
    if (WSAStartup(MAKEWORD(2, 2), &d)) {
        fprintf(stderr, "Failed to initialize.\n");
        return 1;
    }
#endif

    printf("Configuring local address...\n");
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo *bind_address;
    getaddrinfo(0, "3650", &hints, &bind_address);


    printf("Creating socket...\n");
    SOCKET socket_listen;
    socket_listen = socket(bind_address->ai_family,
            bind_address->ai_socktype, bind_address->ai_protocol);
    if (!ISVALIDSOCKET(socket_listen)) {
        fprintf(stderr, "socket() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }

	printf("Binding socket to local address...\n");
    if (bind(socket_listen,
                bind_address->ai_addr, bind_address->ai_addrlen)) {
        fprintf(stderr, "bind() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }
    freeaddrinfo(bind_address);


    printf("Listening on PORT 3650...\n");
    if (listen(socket_listen, 10) < 0) {
        fprintf(stderr, "listen() failed. (%d)\n", GETSOCKETERRNO());
        return 1;
    }

    printf("Waiting for connections...\n");

	// GATEWAY WILL RECEIVE ONLY FROM THIS ADDRESS
	printf("Configuring remote address...\n");
	struct addrinfo cl_hints;
	memset(&cl_hints, 0, sizeof(cl_hints));
	hints.ai_socktype = SOCK_STREAM;
	struct addrinfo *peer_address;
	if (getaddrinfo("18.125.5.100", 0, &cl_hints, &peer_address)) {
	    fprintf(stderr, "getaddrinfo() failed. (%d)\n", GETSOCKETERRNO());
	    return 1;
	}
	
	printf("Remote address is: ");
	char gateway_address[100];
	getnameinfo(peer_address->ai_addr, peer_address->ai_addrlen,
	        gateway_address, sizeof(gateway_address),
	        0, 0,
	        NI_NUMERICHOST);
	printf("%s\n", gateway_address);
    
	// SET UP CLIENT SOCKET
	SOCKET socket_client;
	
		// SET TIMEOUT
	#if defined(_WIN32)
	DWORD timeout = timeout_in_seconds * 1000;
	setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof timeout);
	#else 
	int timeout_in_seconds = 10;
	struct timeval tv;
	tv.tv_sec = timeout_in_seconds;
	tv.tv_usec = 0;
	setsockopt(socket_client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);
	#endif
   
	char* ssh_args[6] = {"/usr/bin/ssh", "-o ServerAliveInterval=300", "-NR", "5000:localhost:5000", "oncoto@oncoto.app", NULL};

	stimer_t timer;
	resettimer(&timer);
	int cpid = -1;
	int tunnel = 0;
	
	state_t state;
	state.cpid = &cpid;
	state.tunnel = &tunnel;
	state.timer = &timer;

	pthread_t timer_thr;
	int timer_thrid = pthread_create(&timer_thr, NULL, timer_f, (void *) &state);

	while(1) {

		// ACCEPT INCOMING CONNECTION ATTEMPTS
		struct sockaddr_storage client_address;
	    socklen_t client_len = sizeof(client_address);
	    socket_client = accept(socket_listen,
	            (struct sockaddr*) &client_address,
	            &client_len);
	    if (!ISVALIDSOCKET(socket_client)) {
	        fprintf(stderr, "accept() failed. (%d)\n",
	                GETSOCKETERRNO());
	        break;
	    }

        char address_buffer[100];
        getnameinfo((struct sockaddr*)&client_address,
                client_len,
                address_buffer, sizeof(address_buffer), 0, 0,
                NI_NUMERICHOST);
        printf("New connection from %s\n", address_buffer);

		if(!strcmp(gateway_address, address_buffer)) {
			printf("Reading incoming byte\n");
			char read;
        	char bytes_received = recv(socket_client, &read, 1, 0);
        	
			if (bytes_received > 0) {
				if(tunnel) {
					resettimer(&timer);
				} else {
					tunnel = 1;
					resettimer(&timer);
					printf("establishing tunnel...\n");
					cpid = fork();
					if(cpid == 0) {
						printf("/usr/bin/ssh -o \"ServerAliveInterval=300\" -fNR 5000:localhost:5000 oncoto@oncoto.app\n");
						if( execv("/usr/bin/ssh", ssh_args) == -1) {
							printf("failed to establish ssh tunnel in child process... [%d]\n", errno);
							return 1;
						}
						return 0;
					}
				}
				send(socket_client, "s", 1, 0);
			}
		}
		
		printf("Closing client socket...\n");
		CLOSESOCKET(socket_client);
	}

	if(tunnel) {
		kill(cpid, SIGKILL);
		tunnel = 0;
	}

    printf("Closing listening socket...\n");
    CLOSESOCKET(socket_listen);

#if defined(_WIN32)
    WSACleanup();
#endif


    printf("Finished.\n");

    return 0;
}

