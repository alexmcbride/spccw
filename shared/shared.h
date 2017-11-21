#ifndef _SHARED_H
#define _SHARED_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include "rdwrn.h"

#define PORT_NUMBER 50031
#define ERR_MSG_SIZE 64

#define REQUEST_STUDENT_ID 1
#define REQUEST_TIME 2
#define REQUEST_UNAME 3
#define REQUEST_QUIT 4

void die(char *message);
size_t read_socket(int sockfd, unsigned char *buffer, int length);
size_t write_socket(int sockfd, unsigned char *buffer, int length);

#endif