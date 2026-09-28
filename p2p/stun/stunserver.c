#include <stdio.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include "stun.h"

int main(void) {

    int sockfd;
    struct sockaddr_in addr;
    uint8_t buf[STUN_MAX_MSG] = {0};
    socklen_t addrlen = sizeof(addr);
    ssize_t rlen;
    stun_msg_t msg;
    uint8_t temp_tid[12];
    uint32_t x_ip;
    uint16_t x_port;

    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { perror("socket()"); return -1; }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(3478);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind()"); return -2; }
    
    while (1) {
        addrlen = sizeof(addr);
        if ((rlen = (recvfrom(sockfd, buf, STUN_MAX_MSG, 0, (struct sockaddr*)&addr, &addrlen))) < 0) { perror("recvfrom()"); if (errno == EINTR) continue; else return -3; }
        if (rlen < 20) { fprintf(stderr, "the header length not fits.\n"); continue; }
        if (stun_decode(buf, rlen, &msg) < 0) { fprintf(stderr, "stun_decode()\n"); continue; }
        if (msg.magic_cookie != STUN_MAGIC_COOKIE) { fprintf(stderr, "magic cookie isn't true.\n"); continue;}
        
        uint16_t msg_class = (((msg.msg_type >> 7) & 0x02) | ((msg.msg_type >> 4) & 0x01));
        uint16_t msg_method = (msg.msg_type & 0x000F) | ((msg.msg_type & 0x00E0) >> 1) | ((msg.msg_type & 0x3E00) >> 2);
        if (msg_class != 0x0000 || msg_method != 0x01) { fprintf(stderr, "class or method isn't correct\n"); continue; }
       
        memcpy(temp_tid, msg.transaction_id, sizeof(msg.transaction_id));

        uint32_t t_ip = ntohl(addr.sin_addr.s_addr);
        uint16_t t_port = ntohs(addr.sin_port);

        x_ip = t_ip ^ STUN_MAGIC_COOKIE;
        x_port = t_port ^ (STUN_MAGIC_COOKIE >> 16);

        memset(&msg, 0, sizeof(msg)); 
        msg.msg_type = 0x0101;
        msg.magic_cookie = STUN_MAGIC_COOKIE;
        memcpy(msg.transaction_id, temp_tid, sizeof(temp_tid));
        msg.attribute_count = 1;
        msg.attributes[0].attr_type = STUN_ATTR_XOR_MAPPED_ADDR;
        msg.attributes[0].length = 8;
        msg.attributes[0].value[0] = (uint8_t)0x00; /*reserved*/
        msg.attributes[0].value[1] = (uint8_t)0x01; /*ipv4*/
        msg.attributes[0].value[2] = (uint8_t)(x_port >> 8);
        msg.attributes[0].value[3] = (uint8_t)(x_port & 0xFF);
        msg.attributes[0].value[4] = (uint8_t)((x_ip >> 24) & 0xFF);
        msg.attributes[0].value[5] = (uint8_t)((x_ip >> 16) & 0xFF);
        msg.attributes[0].value[6] = (uint8_t)((x_ip >> 8) & 0xFF);
        msg.attributes[0].value[7] = (uint8_t)((x_ip) & 0xFF);

        if ((rlen = stun_encode(&msg, buf, sizeof(buf))) < 0) { fprintf(stderr, "stun_encode() | %zd\n", rlen); continue; }
        if (sendto(sockfd, buf, rlen, 0, (struct sockaddr*)&addr, addrlen) < 0) { perror("sendto"); continue; }
    }

    close(sockfd);
    return 0;
}
