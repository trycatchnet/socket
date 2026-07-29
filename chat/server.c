#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

#define PORT "3131"
#define BACKLOG 10

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) return &(((struct sockaddr_in*)sa)->sin_addr);
    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main() {
    struct addrinfo hints, *servinfo, *p;
    int sockfd, newfd, rv;
    char buf[512];
    struct sockaddr_storage their_addr;
    socklen_t sinsize;
    char s[INET6_ADDRSTRLEN] = {0};
    int yes = 1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
        perror("getaddrinfo : ");
        exit(1);
    }

    for (p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("socket : ");
            exit(1);
        }

        if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes))) {
            perror("setsockopt : ");
            exit(1);
        } 

        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(sockfd);
            perror("bind : ");
            exit(1);
        }
        break;
    }

    if (p == NULL) {
        perror("failed to bind");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen : ");
        exit(1);
    }

    freeaddrinfo(servinfo);

    printf("waiting for connections...\n");

    while (1) {
        newfd = accept(sockfd, (struct sockaddr*)&their_addr, &sinsize);
        inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr*)&their_addr), s, sizeof(s));
        printf("got connection from : %s\n", s);
        char msg[] = "popomda\n";
        if (read(newfd, buf, sizeof(buf)) == -1) {
            perror("read");
        }
        if (send(newfd, msg, sizeof(msg) + 1, 0) == -1) {
            perror("send");
        }
        close(newfd);
    }
    close(sockfd);
}
