#define _POSIX_C_SOURCE 200112L
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <sys/time.h>

#include "net.h"

int main(void) {
    int sockfd;
    struct sockaddr_in google, cloudflare;
    uint32_t ip_hb, ip_hb2, ip_nb, ip_nb2;
    uint16_t port_hb, port_hb2;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { perror("Socket creation"); return -1; }

    google.sin_family = AF_INET;
    google.sin_port = htons(19302);
    if (inet_pton(AF_INET, "74.125.250.129", (void*)&google.sin_addr) != 1) { perror("Inet_pton"); return -2; }

    cloudflare.sin_family = AF_INET;
    cloudflare.sin_port = htons(3478);

    int stat;
    struct addrinfo hints, *servinfo;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ((stat = getaddrinfo("stun.cloudflare.com", "3478", &hints, &servinfo)) != 0) { perror("getaddrinfo"); return -3; }

    cloudflare.sin_addr = ((struct sockaddr_in *)servinfo->ai_addr)->sin_addr;
    freeaddrinfo(servinfo);

    if (stun_query(sockfd, &google, &ip_hb, &port_hb) < 0) { perror("google stun"); return -5; }
    
    ip_nb = htonl(ip_hb);
    char char_buf[INET_ADDRSTRLEN] = {0};
    /*
     * char *x[N] = {0}; -> N pointers, allocated n addresses in memory. sizeof(x) -> N*8, sizeof(*x) -> 8
     * char x[N] = {0}; -> N characters allocated in memory, x is the addr in memory. sizeof(x) = N, sizeof(*x) = 1
     * */
    printf("%s:%u\n", inet_ntop(AF_INET, &ip_nb, char_buf, sizeof(char_buf)), port_hb);
    
    if (stun_query(sockfd, &cloudflare, &ip_hb2, &port_hb2) < 0) { perror("cloudflare stun"); return -6; }
    ip_nb2 = htonl(ip_hb2);
    printf("%s:%u\n", inet_ntop(AF_INET, &ip_nb2, char_buf, sizeof(char_buf)), port_hb2);

    if ((port_hb != port_hb2) || (ip_nb != ip_nb2)) {
        printf("info : There's, symmetric NAT, TURN is needed.\n");
    } else {
        printf("info : There's cone NAT, hole punching is possible.\n");
    }

    return 0;
}
