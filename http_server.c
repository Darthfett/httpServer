#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

#define TRUE 1
#define FALSE 0

#define PORT 8000
#define HOST_NAME "127.0.0.1"
#define MAX_CONNECTIONS 5
#define MAX_TIMESTAMP_LENGTH 64

#define MAX_RETRIES 5

int sockfd; // Listening socket
int client_sockfd; // Connected socket
int child;

struct sockaddr_in serverhost;
struct hostent *host_entity;
int addr_len;
struct sockaddr_in clienthost;
struct hostent *client_entity;

time_t seconds;
struct tm *timestamp;
char timestamp_str[MAX_TIMESTAMP_LENGTH];

int read_socket(int fd, char *buffer, int size) {
    int bytes_recvd = 0;
    int retries = 0;
    int total_recvd = 0;

    while (retries < MAX_RETRIES && size > 0 && strstr(buffer, ">") == NULL) {
        bytes_recvd = read(fd, buffer, size);

        if (bytes_recvd > 0) {
            buffer += bytes_recvd;
            size -= bytes_recvd;
            total_recvd += bytes_recvd;
        } else {
            retries++;
        }
    }

    if (bytes_recvd >= 0) {
        // Last read was not an error, return how many bytes were recvd
        return total_recvd;
    }
    // Last read was an error, return error code
    return -1;
}

int write_socket(int fd, char *msg, int size) {
    int bytes_sent = 0;
    int retries = 0;
    int total_sent = 0;

    while (retries < MAX_RETRIES && size > 0) {
        bytes_sent = write(fd, msg, size);

        if (bytes_sent > 0) {
            msg += bytes_sent;
            size -= bytes_sent;
            total_sent += bytes_sent;
        } else {
            retries++;
        }
    }

    if (bytes_sent >= 0) {
        // Last write was not an error, return how many bytes were sent
        return total_sent;
    }
    // Last write was an error, return error code
    return -1;
}

int handle_client_connection() {

}

void terminate(int err_code) {
    shutdown(client_sockfd, SHUT_RDWR);
    close(client_sockfd);
    close(sockfd);

    exit(err_code);
}

int main(int argc, char *argv[]) {

    // Clear all invalid data
    memset((char*) &serverhost, 0, sizeof(serverhost));

    // Get host information for 127.0.0.1
    host_entity = gethostbyname(HOST_NAME);
    if (host_entity == NULL) {
        printf("Unable to resolve hostname %s\n", HOST_NAME);
        terminate(1);
        return 1;
    }

    // Copy host information to serverhost
    memcpy((char*) &serverhost.sin_addr, host_entity->h_addr, host_entity->h_length);
    serverhost.sin_port = htons((short) PORT);
    serverhost.sin_family = host_entity->h_addrtype;

    // Open socket for listening
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    if (sockfd < 0) {
        printf("Unable to open socket\n");
        terminate(1);
        return 1;
    }

    // Try to bind to socket
    if (bind(sockfd, (struct sockaddr*) &serverhost, sizeof(serverhost)) < 0) {
        printf("Unable to bind to socket\n");
        terminate(1);
        return 1;
    }

    // Try to listen to socket
    if (listen(sockfd, MAX_CONNECTIONS) < 0) {
        printf("Unable to listen on socket\n");
        terminate(1);
        return 1;
    }

    while(TRUE) {
        // Clear all invalid data
        memset(&clienthost, 0, sizeof(clienthost));

        // Try to accept connection with client
        client_sockfd = accept(sockfd, (struct sockaddr*) &clienthost, (socklen_t*) &addr_len);

        if (client_sockfd < 0) {
            printf("Unable to accept connection\n");
            terminate(1);
            return 1;
        }
        // Timestamp the connection accept
        time(&seconds);
        timestamp = localtime(&seconds);
        memset(timestamp_str, 0, MAX_TIMESTAMP_LENGTH);
        strftime(timestamp_str, MAX_TIMESTAMP_LENGTH, "%r %A %d %B, %Y", timestamp);

        // Get client host
        client_entity = gethostbyaddr((char*) &(clienthost.sin_addr), sizeof(clienthost.sin_addr), AF_INET);
        if (client_entity > 0) {
            printf("[%s] Connection accepted from %s (%s)\n", timestamp_str, client_entity->h_name, client_entity->h_addr);
        } else {
            printf("[%s] Connection accepted from unresolvable host\n", timestamp_str);
        }

        child = fork();
        if (child == 0) {
            handle_client_connection();
            exit(0);
        }
    }
    return 0;
}
