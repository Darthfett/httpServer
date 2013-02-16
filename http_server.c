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

int main(int argc, char *argv[]) {

    // Clear all invalid data
    memset((char*) &serverhost, 0, sizeof(serverhost));

    // Get host information for 127.0.0.1
    host_entity = gethostbyname(HOST_NAME);
    if (host_entity == NULL) {
        printf("Unable to resolve hostname %s\n", HOST_NAME);
        exit(1);
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
        exit(1);
        return 1;
    }

    // Try to bind to socket
    if (bind(sockfd, (struct sockaddr*) &serverhost, sizeof(serverhost)) < 0) {
        printf("Unable to bind to socket\n");
        exit(1);
        return 1;
    }

    // Try to listen to socket
    if (listen(sockfd, MAX_CONNECTIONS) < 0) {
        printf("Unable to listen on socket\n");
        exit(1);
        return 1;
    }

    while(TRUE) {
        // Clear all invalid data
        memset(&clienthost, 0, sizeof(clienthost));

        // Try to accept connection with client
        client_sockfd = accept(sockfd, (struct sockaddr*) &clienthost, (socklen_t*) &addr_len);

        if (client_sockfd < 0) {
            printf("Unable to accept connection\n");
            exit(1);
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
           // do_something();
           exit(0);
        }
    }
    return 0;
}
