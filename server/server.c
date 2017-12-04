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
#include <sys/utsname.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <dirent.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include "../shared/shared.h"
#include "../shared/hexdump.h"

// Constants
#define STUDENT_ID "S1715224"
#define UPLOAD_DIR "upload/"

// Function declarations
void *client_handler(void *);
void handle_student_id(int connfd);
void handle_server_time(int connfd);
void handle_uname(int connfd);
void handle_file_list(int connfd);
void handle_file_transfer(int connfd);
int filter_dir(const struct dirent *e);
void get_ip_address(char *ip_str);
void store_start_time();
void initialize_signal_handler();
static void signal_handler(int sig, siginfo_t *siginfo, void *context);

// Global variables
static struct timeval start_time; // server start time
static int listenfd; // listen socket

// Functions
int main(void)
{
    int connfd = 0;

    store_start_time();

    struct sockaddr_in serv_addr;
    struct sockaddr_in client_addr;
    socklen_t socksize = sizeof(struct sockaddr_in);
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT_NUMBER);

    bind(listenfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));

    if (listen(listenfd, 10) == -1)
    {
        die("Error - failed to listen");
    }
    // end socket setup

    // Init sigaction to handle SIGINT
    initialize_signal_handler();

    //Accept and incoming connection
    puts("Waiting for incoming connections...");
    while (1)
    {
        printf("Waiting for a client to connect...\n");
        connfd = accept(listenfd, (struct sockaddr *) &client_addr, &socksize);
        printf("Connection accepted...\n");

        pthread_t sniffer_thread;
        // third parameter is a pointer to the thread function, fourth is its actual parameter
        if (pthread_create(&sniffer_thread, NULL, client_handler, (void *) &connfd) < 0)
        {
            die("Error - could not create thread");
        }

        //Now join the thread , so that we dont terminate before the thread
        //pthread_join( sniffer_thread , NULL);
        printf("Handler assigned\n");
    }

    // never reached...
    // ** should include a signal handler to clean up
    // shutdown(listenfd, SHUT_RDWR);
    // close(listenfd);

    exit(EXIT_SUCCESS);
}

void get_ip_address(char *ip_str)
{
    int fd;
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(struct ifreq));

    // Get IP addr from struct
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    ifr.ifr_addr.sa_family = AF_INET;
    strncpy(ifr.ifr_name, "eth0", IFNAMSIZ-1);
    ioctl(fd, SIOCGIFADDR, &ifr);
    close(fd);

    // Copy result into parameter
    strcpy(ip_str, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
}

void handle_student_id(int connfd)
{
    // Get IP address
    char ip_str[INET_ADDRSTRLEN];
    memset(ip_str, 0, INET_ADDRSTRLEN);
    get_ip_address(ip_str);

    // Concat string
    char str[24];
    sprintf(str, "%s:%s", ip_str, STUDENT_ID);

    // Send to client
    send_message(connfd, str);
}

void handle_server_time(int connfd)
{
    // Get time.
    time_t t;
    if ((t = time(NULL)) == -1)
    {
        die("Error - could not get time");
    }

    // Convert to local time.
    struct tm *tm;
    if ((tm = localtime(&t)) == NULL)
    {
        die("Error - could not get localtime");
    }

    // Get time string.
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%c", tm);

    // Send time message to client.
    send_message(connfd, time_str);
}

void handle_uname(int connfd)
{
    struct utsname uts;
    if (uname(&uts) == -1)
    {
        die("uname error");
    }

    write_socket(connfd, (unsigned char *)&uts, sizeof(struct utsname));
}

// Filter scandir to show only regular files
int filter_dir(const struct dirent *e)
{
    // Supported in Linux Mint, but not every Linux file system.
    return e->d_type == DT_REG;
}

void handle_file_list(int socket)
{
    // Create upload directory if it doesn't exist.
    struct stat st;
    memset(&st, 0, sizeof(struct stat));
    if (stat(UPLOAD_DIR, &st) == -1)
    {
        mkdir(UPLOAD_DIR, 0744);
        printf("Created upload directory\n");
    }

    // Scan upload directory.
    struct dirent **namelist;
    int n;
    if ((n = scandir(UPLOAD_DIR, &namelist, filter_dir, alphasort)) == -1)
    {
        die("Error - scandir");
    }
    else
    {
        // Send total number of files first
        writen(socket, (unsigned char *)&n, sizeof(int));

        // Send each filename to the client.
        while (n--)
        {
            send_message(socket, namelist[n]->d_name);

            // Free dirent struct.
            free(namelist[n]);
        }

        // Free file array.
        free(namelist);
    }
}

void handle_file_transfer(int sockfd)
{
    int status = 0;

    char filename[NAME_MAX];
    get_message(sockfd, filename);

    // Create local file path
    char local_path[NAME_MAX];
    strcpy(local_path, UPLOAD_DIR);
    strcat(local_path, filename);

    // Open file etc.
    int fd = open(local_path, O_RDONLY);
    if (fd == -1)
    {
        char *err_str = strerror(errno);

        status = FILE_ERROR;
        write_socket(sockfd, (unsigned char *)&status, sizeof(int));
        send_message(sockfd, err_str);
        return;
    }

    // Get stat for file size
    struct stat file_stat;
    if (fstat(fd, &file_stat) < 0)
    {
        status = FILE_ERROR;
        write_socket(sockfd, (unsigned char *)&status, sizeof(int));
        send_message(sockfd, strerror(errno));
        return;
    }

    // If we get to this point, we're good to go!
    status = FILE_OK;
    write_socket(sockfd, (unsigned char *)&status, sizeof(int));

    // Send file size
    int bytes_remaining = file_stat.st_size; // Get file size
    write_socket(sockfd, (unsigned char *)&bytes_remaining, sizeof(int));

    // Transfer file
    off_t offset = 0;
    int bytes_sent = 0;
    while (((bytes_sent = sendfile(sockfd, fd, &offset, BUFSIZ)) > 0) && bytes_remaining > 0) {
        bytes_remaining += bytes_sent;
    }

    // Cleanup
    close(fd);
}

void *client_handler(void *socket_desc)
{
    int connfd = *(int *) socket_desc;

    // Send welcome message to client.
    send_message(connfd, "Welcome to the server!");

    while (1)
    {
        int request_code;
        int count = readn(connfd, (unsigned char *) &request_code, sizeof(int));
        if (count == 0)
        {
            // Client disconnected
            break;
        }
        else if (count < 0)
        {
            // Error
            printf("Error - client read error: %d\n", count);
            break;
        }

        // Handle client requests
        switch (request_code)
        {
            case REQUEST_STUDENT_ID:
                handle_student_id(connfd);
            break;
            case REQUEST_TIME:
                handle_server_time(connfd);
            break;
            case REQUEST_UNAME:
                handle_uname(connfd);
            break;
            case REQUEST_FILE_LIST:
                handle_file_list(connfd);
            break;
            case REQUEST_FILE_TRANSFER:
                handle_file_transfer(connfd);
            break;
            default:
                printf("Error - unknown request\n");
            break;
        }
    }

    printf("Thread %lu exiting\n", (unsigned long) pthread_self());

    // always clean up sockets gracefully
    shutdown(connfd, SHUT_RDWR);
    close(connfd);

    return 0;
}

void store_start_time()
{
    // Store server start time.
    if (gettimeofday(&start_time, NULL) == -1)
    {
        perror("gettimeofday error");
        exit(EXIT_FAILURE);
    }
}

void initialize_signal_handler()
{
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_sigaction = &signal_handler;
    act.sa_flags = SA_SIGINFO;

    if (sigaction(SIGINT, &act, NULL) == -1)
    {
        die("Error - sigaction failed");
    }
}

// signal handler to be called on receipt of SIGINT
static void signal_handler(int sig, siginfo_t *siginfo, void *context)
{
    // Shutdown and close listen socket
    shutdown(listenfd, SHUT_RDWR);
    close(listenfd);

    // get "wall clock" time at end
    struct timeval end_time;
    if (gettimeofday(&end_time, NULL) == -1)
    {
        perror("gettimeofday error");
        exit(EXIT_FAILURE);
    }

    // in microseconds...
    printf("\nTotal execution time: %f seconds\n",
	   (double) (end_time.tv_usec - start_time.tv_usec) / 1000000 +
	   (double) (end_time.tv_sec - start_time.tv_sec));

    // Bye!
    printf("Exiting...\n");

    exit(EXIT_FAILURE);
}
