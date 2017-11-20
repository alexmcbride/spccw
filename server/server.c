// Cwk2: server.c - multi-threaded server using readn() and writen()

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include "rdwrn.h"

#define PORT_NUMBER 50001
#define INPUTSIZ 256
#define STUDENT_ID "S1715224"

// thread function
void *client_handler(void *);
void handle_student_id(int connfd);
void send_message(int socket, char *msg);
void get_ip_address(char *ip_str);

// you shouldn't need to change main() in the server except the port number
int main(void)
{
    int listenfd = 0, connfd = 0;

    struct sockaddr_in serv_addr;
    struct sockaddr_in client_addr;
    socklen_t socksize = sizeof(struct sockaddr_in);
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT_NUMBER);

    bind(listenfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));

    if (listen(listenfd, 10) == -1) {
	    perror("Failed to listen");
	    exit(EXIT_FAILURE);
    }
    // end socket setup

    //Accept and incoming connection
    puts("Waiting for incoming connections...");
    while (1) {
	    printf("Waiting for a client to connect...\n");
	    connfd = accept(listenfd, (struct sockaddr *) &client_addr, &socksize);
	    printf("Connection accepted...\n");

	    pthread_t sniffer_thread;
            // third parameter is a pointer to the thread function, fourth is its actual parameter
	    if (pthread_create(&sniffer_thread, NULL, client_handler, (void *) &connfd) < 0) {
	        perror("could not create thread");
	        exit(EXIT_FAILURE);
	    }

	    //Now join the thread , so that we dont terminate before the thread
	    //pthread_join( sniffer_thread , NULL);
	    printf("Handler assigned\n");
    }

    // never reached...
    // ** should include a signal handler to clean up
    exit(EXIT_SUCCESS);
} 

void get_ip_address(char *ip_str) 
{
    int fd;
    struct ifreq ifr;

    fd = socket(AF_INET, SOCK_DGRAM, 0);

    /* I want to get an IPv4 IP address */
    ifr.ifr_addr.sa_family = AF_INET;

    /* I want an IP address attached to "eth0" */
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);

    ioctl(fd, SIOCGIFADDR, &ifr);

    close(fd);

    /* Copy result into parameter */
    strcpy(ip_str, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
}

void handle_student_id(int connfd)
{
    char ip_str[INET_ADDRSTRLEN];
    memset(ip_str, 0, INET_ADDRSTRLEN);
    get_ip_address(ip_str);

    char str[24];
    memset(str, 0, sizeof(str));
    strcat(str, ip_str);
    strcat(str, ":");
    strcat(str, STUDENT_ID);
    send_message(connfd, str);
}

void *client_handler(void *socket_desc)
{
    //Get the socket descriptor
    int connfd = *(int *) socket_desc;

    // Send welcome message to client.
    send_message(connfd, "Welcome to the server!");

    while (1) 
    {
        int request_code;
        int count = readn(connfd, (unsigned char *) &request_code, sizeof(int)); 

        // Check if client disconnected.
        if (count == 0) 
        {
            printf("Error - lost connection");
            break;
        }
        else if (count < 0)
        {
           printf("Error - read socket error");
           break;
        }

        printf("Handling request code: %d\n", request_code);

        switch (request_code) 
        {
            case 1:
                handle_student_id(connfd);
            break;
        }
    }

    // Cleanup...
    shutdown(connfd, SHUT_RDWR);
    close(connfd);

    printf("Thread %lu exiting\n", (unsigned long) pthread_self());

    // always clean up sockets gracefully
    shutdown(connfd, SHUT_RDWR);
    close(connfd);

    return 0;
}  

void send_message(int socket, char *msg)
{
    size_t length = strlen(msg) + 1;
    writen(socket, (unsigned char *) &length, sizeof(size_t));   
    writen(socket, (unsigned char *) msg, length);    
} 
