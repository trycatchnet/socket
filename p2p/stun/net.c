#include "net.h"
#include "stun.h"
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>

int generate_tid(uint8_t *buf, size_t size) {
    if (size != 12) return -1;

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -2;

    ssize_t res = read(fd, buf, size);
    close(fd);

    return (res == (ssize_t)size) ? 0 : -3;
}

int stun_query(int sockfd, const struct sockaddr_in *server, uint32_t *ip_host, uint16_t *port_host)
{
    stun_msg_t msg;
    msg.msg_type = 0x01; /* i forgot to type this in main.c but this is binding req msg type. */
    msg.attribute_count = 0;
    msg.magic_cookie = STUN_MAGIC_COOKIE;

    if (generate_tid(msg.transaction_id, 12) != 0) {
        perror("generating pid");
        return -1;
    }

    uint8_t temp_tid[12];
    memcpy(temp_tid, msg.transaction_id, 12);
  
    uint8_t buf[STUN_MAX_MSG] = {0};
    size_t cap = sizeof(buf);
    socklen_t servlen = sizeof(*server);

    int encode_len = stun_encode(&msg, (uint8_t*)buf, cap);
    if (encode_len < 20) { perror("encoding"); return -2; }
    if (sendto(sockfd, buf, encode_len, 0, (struct sockaddr*)server, servlen) < 0) { perror("sendto"); return -3; }

    struct sockaddr_in temp;
    memcpy(&temp, server, sizeof(struct sockaddr_in));
    ssize_t recv_buf = recvfrom(sockfd, buf, cap, 0, (struct sockaddr*)&temp, &servlen);
    if (recv_buf < 0) { perror("recvfrom"); return -4;}

    int decode_len = stun_decode((uint8_t*)buf, recv_buf, &msg);
    if (decode_len != 0) { perror("decoding"); return -5; }

    if (memcmp(temp_tid, &msg.transaction_id, 12) != 0) return -6;

    if (stun_get_xor_mapped_addr(&msg, ip_host, port_host) != 0) { perror("get xor mapped addr"); return -7; }
    
    //uint32_t ip_nb = htonl(*ip_host);
    //printf("%s:%u\n", inet_ntop(AF_INET, &ip_nb, *char_buf, sizeof(*char_buf)), *port_host);

    return 0;
}
