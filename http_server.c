#define _XOPEN_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

#define TRUE 1
#define FALSE 0

#define PORT 80 
#define HOST_NAME "127.0.0.1"
#define MAX_CONNECTIONS 5
#define MAX_TIMESTAMP_LENGTH 64
#define SERVER_STRING "Server: httpServer/0.1.1\r\n"

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

// Value specific to client connections
int keep_alive = TRUE; // Default in HTTP/1.1
int content_length = -1;
int cookie = FALSE;
int header_err_flag = FALSE;
struct tm *if_modified_since;
int time_is_valid = TRUE;
char *content = NULL;
int not_eng = FALSE;
int acceptable_text = TRUE;
int acceptable_charset = TRUE;
int acceptable_encoding = TRUE;
char from_email[512];
char user_agent[512];

int read_line(int fd, char *buffer, int size) {
    char next = '\0';
    char err;
    int i = 0;
    while ((i < (size - 1)) && (next != '\n')) {
        err = read(fd, &next, 1);

        if (err <= 0) break;

        if (next == '\r') {
            err = recv(fd, &next, 1, MSG_PEEK);
            if (err > 0 && next == '\n') {
                read(fd, &next, 1);
            } else {
                next = '\n';
            }
        }
        buffer[i] = next;
        i++;
    }
    buffer[i] = '\0';
    return i;
}

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

void ok(char *body) {
    // 200 OK
    char buffer[8096];
    sprintf(buffer, "HTTP/1.1 200 OK\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, SERVER_STRING);
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, "Content-Type: text/plain\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, "Content-Length: %d\r\n", strlen(body));
    write_socket(client_sockfd, buffer, strlen(buffer));
    write_socket(client_sockfd, "\r\n", strlen("\r\n"));

    write_socket(client_sockfd, body, strlen(body));
}

void not_modified() {
    // 304
    char buffer[8096];
    char body[8096];
    sprintf(buffer, "HTTP/1.1 304 Not Modified\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, SERVER_STRING);
    write_socket(client_sockfd, buffer, strlen(buffer));
    
    // TODO: Add Date header field
            // strptime(modified_since_buffer, "%a, %d %b %Y %T %Z", if_modified_since);

    // Body isn't sent for this type of error
    write_socket(client_sockfd, "\r\n", strlen("\r\n"));

}

void bad_request() {
    // 400 Error
    char buffer[8096];
    char body[8096];
    sprintf(buffer, "HTTP/1.1 400 Bad Request\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, SERVER_STRING);
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, "Content-Type: text/html\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));

    sprintf(body, "<HTML><HEAD><TITLE>Bad Request</TITLE></HEAD>\r\n");
    sprintf(body + strlen(body), "<BODY><P>The request cannot be fulfilled due to bad syntax.</P></BODY></HTML>\r\n");

    sprintf(buffer, "Content-Length: %d\r\n", strlen(body));
    write_socket(client_sockfd, buffer, strlen(buffer));
    write_socket(client_sockfd, "\r\n", strlen("\r\n"));

    write_socket(client_sockfd, body, strlen(body));
}

void forbidden() {
    // 403 Error
    char buffer[8096];
    char body[8096];
    sprintf(buffer, "HTTP/1.1 403 Forbidden\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, SERVER_STRING);
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, "Content-Type: text/html\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));

    sprintf(body, "<HTML><HEAD><TITLE>Forbidden</TITLE></HEAD>\r\n");
    sprintf(body + strlen(body), "<BODY><P>The server understood the request, but is refusing to fulfill it.</P></BODY></HTML>\r\n");

    sprintf(buffer, "Content-Length: %d\r\n", strlen(body));
    write_socket(client_sockfd, buffer, strlen(buffer));
    write_socket(client_sockfd, "\r\n", strlen("\r\n"));

    write_socket(client_sockfd, body, strlen(body));
}

void not_found() {
    // 404 Error
    char buffer[8096];
    char body[8096];
    sprintf(buffer, "HTTP/1.1 404 File Not Found\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, SERVER_STRING);
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, "Content-Type: text/html\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));

    sprintf(body, "<HTML><HEAD><TITLE>File Not Found</TITLE></HEAD>\r\n");
    sprintf(body + strlen(body), "<BODY><P>File not found.</P></BODY></HTML>\r\n");

    sprintf(buffer, "Content-Length: %d\r\n", strlen(body));
    write_socket(client_sockfd, buffer, strlen(buffer));
    write_socket(client_sockfd, "\r\n", strlen("\r\n"));

    write_socket(client_sockfd, body, strlen(body));
}

void method_not_allowed() {
    // 405 Error
    char buffer[8096];
    char body[8096];
    sprintf(buffer, "HTTP/1.1 501 Method Not Allowed\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, SERVER_STRING);
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, "Allow: GET\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, "Content-Type: text/html\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));

    sprintf(body, "<HTML><HEAD><TITLE>Method Not Implemented</TITLE></HEAD>\r\n");
    sprintf(body + strlen(body), "<BODY><P>HTTP request method not supported.</P></BODY></HTML>\r\n");

    sprintf(buffer, "Content-Length: %d\r\n", strlen(body));
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, "\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));

    write_socket(client_sockfd, body, strlen(body));
}

void server_error() {
    // 500 Error
    char buffer[8096];
    char body[8096];
    sprintf(buffer, "HTTP/1.1 500 Internal Server Error\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, SERVER_STRING);
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, "Content-Type: text/html\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));

    sprintf(body, "<HTML><HEAD><TITLE>Internal Server Error</TITLE></HEAD>\r\n");
    sprintf(body + strlen(body), "<BODY><P>The server encountered an unexpected condition which prevented it from fulfilling the request. </P></BODY></HTML>\r\n");

    sprintf(buffer, "Content-Length: %d\r\n", strlen(body));
    write_socket(client_sockfd, buffer, strlen(buffer));
    write_socket(client_sockfd, "\r\n", strlen("\r\n"));

    write_socket(client_sockfd, body, strlen(body));
}

void not_implemented() {
    // 501 Error
    char buffer[8096];
    char body[8096];
    sprintf(buffer, "HTTP/1.1 501 Not Implemented\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, SERVER_STRING);
    write_socket(client_sockfd, buffer, strlen(buffer));
    sprintf(buffer, "Content-Type: text/html\r\n");
    write_socket(client_sockfd, buffer, strlen(buffer));

    sprintf(body, "<HTML><HEAD><TITLE>Not Implemented</TITLE></HEAD>\r\n");
    sprintf(body + strlen(body), "<BODY><P>The server does not support the functionality required to fulfill the request. </P></BODY></HTML>\r\n");

    sprintf(buffer, "Content-Length: %d\r\n", strlen(body));
    write_socket(client_sockfd, buffer, strlen(buffer));
    write_socket(client_sockfd, "\r\n", strlen("\r\n"));

    write_socket(client_sockfd, body, strlen(body));
}

void read_headers() {
    fprintf(stderr, "\n--READ HEADERS--\n\n");
    while(1) {
        char header[8096];
        int len;
        char next;
        int err;
        char *header_value_start;
        len = read_line(client_sockfd, header, sizeof(header));

        if (len <= 0) {
            // Error in reading from socket
            header_err_flag = TRUE;
            continue;
        }

        fprintf(stderr, "%s", header);

        if (strcmp(header, "\n") == 0) {
            // Empty line signals end of HTTP Headers
            return;
        }

        // If the next line begins with a space or tab, it is a continuation of the previous line.
        err = recv(client_sockfd, &next, 1, MSG_PEEK);
        while (isspace(next) && next != '\n' && next != '\r') {
            if (err) {
                fprintf(stderr, "header space/tab continuation check err\n");
                // Not sure what to do in this scenario
            }
            // Read the space/tab and get rid of it 
            read(client_sockfd, &next, 1);
            
            // Concatenate the next line to the current running header line
            len = len + read_line(client_sockfd, header + len, sizeof(header) - len);
            err = recv(client_sockfd, &next, 1, MSG_PEEK);
        }

        // Find first occurence of colon, to split by header type and value
        header_value_start = strchr(header, ':');
        if (header_value_start == NULL) {
            // Invalid header, not sure what to do in this scenario
            fprintf(stderr, "invalid header\n");
            header_err_flag = TRUE;
            continue;
        }
        int header_type_len = header_value_start - header;

        // Increment header value start past colon
        header_value_start++;
        // Increment header value start to first non-space character
        while (isspace(*header_value_start) && (*header_value_start != '\n') && (*header_value_start != '\r')) {
            header_value_start++;
        }
        int header_value_len = len - (header_value_start - header);


        if (strncasecmp(header, "Connection", header_type_len) == 0) {
            // We care about the connection type "keep-alive"
            if (strncasecmp(header_value_start, "keep-alive", strlen("keep-alive")) == 0) {
                keep_alive = TRUE;
            }
            else if (strncasecmp(header_value_start, "close", strlen("close")) == 0) {
                keep_alive = FALSE;

            }
        } else if (strncasecmp(header, "Content-Length", header_type_len) == 0) {
            content_length = atoi(header_value_start);
        } else if (strncasecmp(header, "Cookie", header_type_len) == 0) {
            cookie = TRUE;
        } else if (strncasecmp(header, "If-Modified-Since", header_type_len) == 0) {
            // Copy everything but trailing newline to buffer
            char *modified_since_buffer = (char*) malloc(sizeof(char) * strlen(header_value_start));
            strncpy(modified_since_buffer, header_value_start, strlen(header_value_start) - 1);

            // Get actual time structure for received time
            strptime(modified_since_buffer, "%a, %d %b %Y %T %Z", if_modified_since);
            free(modified_since_buffer);

            if (if_modified_since == NULL) {
                time_is_valid = FALSE;    
            }
        } else if (strncasecmp(header, "Accept-Language", header_type_len) == 0) {
            if (!(strncasecmp(header_value_start, "en-US", strlen("en-US")) == 0)) {
                not_eng = TRUE;
            }
        } else if (strncasecmp(header, "Accept", header_type_len) == 0) {
            char *traverse = header_value_start;
            char *temporary;
            acceptable_text = FALSE;
            while (1){
                if (strncasecmp(traverse, "text/plain", strlen("text/plain")) == 0) {
                    acceptable_text = TRUE;
                    break;
                }
                temporary = strchr(traverse, ',');
                if (temporary == NULL)
                    break;
                // Skip past comma
                temporary++;
                while(isspace(*temporary))
                    temporary++;
                traverse = temporary;
            }
        } else if (strncasecmp(header, "Accept-Charset", header_type_len) == 0) {
            char *traverse = header_value_start;
            char *temporary;
            acceptable_charset = FALSE;
            while (1){
                if (strncasecmp(traverse, "ISO-8859-1", strlen("ISO-8859-1")) == 0) {
                    acceptable_charset = TRUE;
                    break;
                }
                temporary = strchr(traverse, ',');
                if(temporary == NULL)
                    break;
                // Skip past comma
                temporary++;
                while(isspace(*temporary))
                    temporary++;
                traverse = temporary;
            }
        } else if (strncasecmp(header, "Accept-Encoding", header_type_len) == 0) {
            acceptable_encoding = FALSE;
        } else if (strncasecmp(header, "FROM", header_type_len) == 0) {
            strcpy(from_email, header_value_start);
        } else if (strncasecmp(header, "User-Agent", header_type_len) == 0) {
            strcpy(user_agent, header_value_start);
        }
    }
}

int is_valid_fname(char *fname) {
    char *it = fname;
    /*
    while (
    */
    return TRUE;
}

int handle_client_connection() {
    char buffer[8096];
    int buffer_len; // Length of buffer

    char method[256];
    char url[256];
    char version[256];

    int i = 0, // Used to iterate over the first line to get method, url, version
        j = 0;

    // Read first line
    buffer_len = read_line(client_sockfd, buffer, sizeof(buffer));

    // Unable to read from socket, not sure what to do in this case
    if (buffer_len <= 0) {
        return -1;
    }

    fprintf(stderr, "==== Read Next Request ====\n");

    // Get Method (e.g. GET, POST, etc)
    while ((i < (sizeof(method) - 1)) && (!isspace(buffer[i]))) {
        method[i] = buffer[i];
        i++;
    }
    method[i] = '\0';

    fprintf(stderr, "method: %s\n", method);

    // Skip over spaces
    while (i < buffer_len && isspace(buffer[i])) {
        i++;
    }

    // Get URL
    j = 0;
    while (i < buffer_len && (j < (sizeof(url) - 1)) && !isspace(buffer[i])) {
        url[j] = buffer[i];
        i++;
        j++;
    }
    url[j] = '\0';

    fprintf(stderr, "url: %s\n", url);

    // Skip over spaces
    while (i < buffer_len && isspace(buffer[i])) {
        i++;
    }

    j = 0;
    while (j < sizeof(version) - 1 && !isspace(buffer[i])) {
        version[j] = buffer[i];
        i++;
        j++;
    }
    version[j] = '\0';

    fprintf(stderr, "version: %s\n", version);

    read_headers();

    if (header_err_flag) {
        keep_alive = FALSE;
        bad_request();
        return -1;
    }

    if (content_length > 0) {
        content = (char*) malloc(content_length + 1);
        read_socket(client_sockfd, content, content_length);
    }

    fprintf(stderr, "Content-Length: %d\n", content_length);
    fprintf(stderr, "Connection (keep_alive): %d\n", keep_alive);
    fprintf(stderr, "Cookie: %d\n", cookie);
    fprintf(stderr, "If-Modified-Since Valid Time: %d\n", time_is_valid);
    fprintf(stderr, "If-Modified-Since Time: %p\n", if_modified_since);
    if (content != NULL) {
        fprintf(stderr, "Content: %s\n", content);
    }

    /***********************************************************/
    /*       Full message has been read, respond to client     */
    /***********************************************************/

    if (strcmp(method, "GET") != 0) {
        // Inform client we don't support method
        fprintf(stderr, "Method Not Allowed:\n");
        fprintf(stderr, "%s\n", method);
        method_not_allowed();    
        return 0;
    }

    if (cookie) {
        // Inform client we don't support cookies
        not_implemented();
        return 0;
    }

    if (not_eng) {
        // Inform client we only support English
        not_implemented();
        return 0;
    }

    if (!acceptable_text) {
        // Inform client we only support plain text
        not_implemented();
        return 0;
    }

    if (!acceptable_charset) {
        // Inform client we only support ASCII
        not_implemented();
        return 0;
    }

    // Fix filename
    char file_path[512];
    sprintf(file_path, "htdocs%s", url);
    if (file_path[strlen(file_path)-1] == '/') {
        file_path[strlen(file_path)-1] = '\0';
    }

    fprintf(stderr, "%s\n", file_path);

    // int fname_valid = is_valid_fname(file_path);
    int fname_valid = TRUE;

    struct stat file_info;

    if (!fname_valid) {
        // stat failed, or invalid filename
        fprintf(stderr, "Invalid file name\n");
        forbidden();
        return 0;
    }

    if (stat(file_path, &file_info)) {
        fprintf(stderr, "Stat failed\n");
        // Stat failed
        not_found();
        return 1;
    }

    if (!S_ISREG(file_info.st_mode)) {
        // Not a file
        forbidden();
        fprintf(stderr, "Not a file\n");
        return 0;
    }


    if (!(file_info.st_mode & S_IRUSR)) {
        // No read permissions
        forbidden();
        fprintf(stderr, "No permissions\n");
        return 0;
    }

    FILE *f = fopen(file_path, "r");
    if (f == NULL) {
        // No file
        not_found();
        fprintf(stderr, "Unable to open file\n");
        return 0;
    }

    if (if_modified_since != NULL) {
        struct tm *last_modified = gmtime(&file_info.st_mtime);

        time_t last = mktime(last_modifed);
        time_t since = mktime(if_modified_since);

        double diff = difftime(last, since);
        if (diff <= 0) {
            not_modified();
            return 0;
        }
    }

    fprintf(stderr, "Serving up Content\n");

    char *file_contents = NULL;
    int contents_length = 0;
    char line[512];

    while (fgets(line, sizeof(line), f) != NULL) {
        if (file_contents != NULL) {
            char *new_contents = (char*) malloc(contents_length + strlen(line) + 1);
            strcpy(new_contents, file_contents);
            strcpy(new_contents + strlen(new_contents), line);
            contents_length += strlen(line);

            free(file_contents);
            file_contents = new_contents;
        } else {
            file_contents = (char*) malloc(strlen(line) + 1);
            strcpy(file_contents, line);
            contents_length += strlen(line);
        }
    }
    fclose(f);

    fprintf(stderr, "File Contents:\n");

    fprintf(stderr, "%s\n", file_contents);

    ok(file_contents);

    

    return 0;
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
    memcpy((char*) &serverhost.sin_addr, host_entity->h_addr_list[0], host_entity->h_length);
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
            printf("[%s] Connection accepted from %s (%s)\n", timestamp_str, client_entity->h_name, client_entity->h_addr_list[0]);
        } else {
            printf("[%s] Connection accepted from unresolvable host\n", timestamp_str);
        }

        // Handle the new connection with a child process
        child = fork();
        if (child == 0) {
            // Continuously perform GET responses until we no longer wish to keep alive
            while (keep_alive) {
                content_length = -1;
                cookie = FALSE;
                header_err_flag = FALSE;
                if_modified_since = NULL;
                time_is_valid = TRUE;
                content = NULL;
                not_eng = FALSE;
                acceptable_text = TRUE;
                acceptable_charset = TRUE;
                acceptable_encoding = TRUE;

                handle_client_connection();
                if (content != NULL) free(content);
            }
            exit(0);
        }
    }
    return 0;
}
