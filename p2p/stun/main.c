#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <unistd.h>

#include "stun.h"

int main() {
    int sockfd;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    uint8_t buf[STUN_MAX_MSG] = {0};
    char char_buf[INET_ADDRSTRLEN] = {0};
    size_t cap = sizeof(buf);

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation.");
        return -1;
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(19302);
    if (inet_pton(AF_INET, "74.125.250.129", (void*)&addr.sin_addr) != 1) {
        perror("Inet_pton");
        return -6;
    }

    struct timeval tv;
    tv.tv_sec = 3;
    tv.tv_usec = 0;

    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (struct timeval*)&tv, sizeof(struct timeval)) != 0) {
        perror("Set socket opt");
        return -2;
    }

    stun_msg_t msg;
    msg.msg_type = 0x01;
    msg.attribute_count = 0;
    msg.magic_cookie = STUN_MAGIC_COOKIE;

    srand((unsigned)time(NULL));

    for (size_t i = 0; i < sizeof(msg.transaction_id); i++) {
        msg.transaction_id[i] = (uint8_t)(rand() % 256); 
    }

    uint8_t temp_tid[12];
    memcpy(temp_tid, msg.transaction_id, 12);

    int len = stun_encode(&msg, (uint8_t*)buf, cap);
    sendto(sockfd, buf, len, 0, (struct sockaddr*)&addr, sizeof(addr));

    const ssize_t recv_buf = recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr*)&addr, &addrlen);
    stun_decode((uint8_t*)buf, recv_buf, &msg);
    
    if (memcmp(temp_tid, &msg.transaction_id, 12) != 0) {
        return -3;
    }
 
    uint32_t ip_hb;
    uint16_t port_hb;
    stun_get_xor_mapped_addr(&msg, &ip_hb, &port_hb);

    uint32_t ip_nb = htonl(ip_hb);

    printf("%s:%u\n", inet_ntop(AF_INET, &ip_nb, (char*)char_buf, sizeof(char_buf)), port_hb);
    close(sockfd);
}
