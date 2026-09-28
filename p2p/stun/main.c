#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

#include "net.h"

int main(void) {
    int sockfd;
    struct sockaddr_in addr;
    uint32_t ip_hb;
    uint16_t port_hb;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation.");
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(3478);
    if (inet_pton(AF_INET, "127.0.0.1", (void*)&addr.sin_addr) != 1) {
        perror("Inet_pton");
        return -2;
    }

    if (stun_query(sockfd, &addr, &ip_hb, &port_hb) < 0) { printf("err stun failed."); return -4; }
    uint32_t ip_nb = htonl(ip_hb);
    char char_buf[INET_ADDRSTRLEN] = {0};
    printf("%s:%u\n", inet_ntop(AF_INET, &ip_nb, char_buf, sizeof(char_buf)), port_hb);

    close(sockfd);
}
