#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

#define PORT "3131"
#define BACKLOG 10
#define MAX_BUFSIZE 1024
#define MAX_DATASIZE 1048576 // 100mb

int sendFile(int socket, char text[]);
int receiveFile(int socket, char text[]);
ssize_t read_line(int fd, char *buf, size_t maxlen);

int main() {
    struct addrinfo hints, *servinfo, *p;
    int sockfd, newfd, rv;
    const int yes=1;
    struct sockaddr_storage their_addr;
    socklen_t sinsize = sizeof(their_addr);
    char buffer[MAX_BUFSIZE];

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo");
        exit(1);
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            fprintf(stderr, "socket");
            exit(1);
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            fprintf(stderr, "setsockopt");
            exit(1);
        }

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            fprintf(stderr, "bind");
            exit(1);
        }

        break;
    }

    if (p == NULL) {
        fprintf(stderr, "failed to bind");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        fprintf(stderr, "listen");
        exit(1);
    }

    freeaddrinfo(servinfo);

    for (;;) {
        if ((newfd = accept(sockfd, (struct sockaddr*)&their_addr, &sinsize)) == -1) {
            fprintf(stderr, "accept");
            exit(1);
        }

        char options[] = "Options\nR - Receive a file from server\nS - Send a file to server\n";
        send(newfd, options, strlen(options), 0);
        char buffer[MAX_BUFSIZE];
        if (read_line(newfd, buffer, sizeof(buffer)) <= 0) {
            close(newfd);
            continue;
        }
        printf("Got option : %s", buffer);

        if (strcmp(buffer, "R") == 0) {
            send(newfd, "Please enter file path : ", 31, 0);
            if (read_line(newfd, buffer, sizeof(buffer)) > 0) sendFile(newfd, buffer);
        } else if (strcmp(buffer, "S") == 0) {
            send(newfd, "Please enter file path", 25, 0); 
            if (read_line(newfd, buffer, sizeof(buffer)) > 0) receiveFile(newfd, buffer);
        } else {
            printf("Please be more specific.");
        }
    }
    close(newfd);
}

int sendFile(int socket, char *filename) {
    FILE *fp = fopen(filename, "rb");
    if (!fp) {
        uint32_t zero = 0;
        send(socket, &zero, sizeof(zero), 0);
        return -1;
    }
    
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint32_t net_size = htonl((uint32_t)size);

    if (send(socket, &net_size, sizeof(net_size), 0) != sizeof(net_size)) {
        fclose(fp);
        return -1;
    }

    char buffer[4096];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        if (send(socket, buffer, bytes, 0) != bytes) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}

int receiveFile(int socket, char *filename) {
    uint32_t net_size;
    if (recv(socket, &net_size, sizeof(net_size), 0) != sizeof(net_size)) {
        return -1;
    }

    long size = ntohl(net_size);
    if (size <= 0 || size > 100*1024*1024) return -1;

    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        char err[] = "File can't be created.";
        send(socket, err, strlen(err), 0);
        return -1;
    }

    long remaining = size;
    char buffer[MAX_BUFSIZE * 4];
    while (remaining > 0) {
        int to_read = (remaining > sizeof(buffer)) ? sizeof(buffer) : remaining;
        int n = recv(socket, buffer, to_read, 0);
        if (n <= 0) {
            fclose(fp);
            return -1;
        }
        fwrite(buffer, 1, n, fp);
        remaining -= n;
    }
    fclose(fp);
    return 0;
}

ssize_t read_line(int fd, char *buf, size_t maxlen) {
    size_t i = 0;
    char c;
    ssize_t n;
    while (i < maxlen - 1) {
        n = recv(fd, &c, 1, 0);
        if (n <= 0) break;
        if (c == '\n') break;
        if (c != '\r') buf[i++] = c;
    }
    buf[i] = '\0';
    return i;
}
