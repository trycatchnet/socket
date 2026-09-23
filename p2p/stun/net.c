#define _POSIX_C_SOURCE 200112L

#include "net.h"
#include "stun.h"
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <time.h>
#include <errno.h>
#include <inttypes.h>

int generate_tid(uint8_t *buf, size_t size) {
    if (size != 12) return -1;

    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return -2;

    ssize_t res = read(fd, buf, size);
    close(fd);

    return (res == (ssize_t)size) ? 0 : -3;
}

uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
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

    int timeout = STUN_RTO_DEFAULT_MS;
    int encode_len = stun_encode(&msg, (uint8_t*)buf, cap);
    if (encode_len < 20) { printf("err encoding"); return -2; }

    struct pollfd r[1];
    r[0].fd = sockfd;
    r[0].events = POLLIN; 

    int budget = 0;
    uint64_t first = now_ms();
    for (int i = 0; i < STUN_RC_DEFAULT; i++) {
        fprintf(stderr, "send %d at\t %" PRIu64 "\n", i, now_ms() - first);
        if (sendto(sockfd, buf, encode_len, 0, (struct sockaddr*)server, servlen) < 0) { perror("sendto"); }
    
        if (i == STUN_RC_DEFAULT - 1) budget = STUN_RM_DEFAULT * STUN_RTO_DEFAULT_MS;
        else budget = timeout;

        uint64_t deadline = now_ms() + budget;
        
        while (1) {
            int64_t remaining = (int64_t)(deadline - now_ms());
            if (remaining <= 0) break;

            int pollr = poll(r, 1, (int)remaining);
            if (pollr > 0) { if (!(r[0].revents & POLLIN)) return -4; } /* .events -> is read-only for poll(), but .revents get's set from the kernel. */
            if (pollr == 0) break;
            if (pollr < 0) {
                if (errno != EINTR) return -3;
                else continue;
            }
            struct sockaddr_in temp;
            memcpy(&temp, server, sizeof(struct sockaddr_in));

            servlen = sizeof(*server);
            
            ssize_t recv_buf = recvfrom(sockfd, buf, cap, 0, (struct sockaddr*)&temp, &servlen);
            if (recv_buf < 0) { perror("recvfrom"); continue;}

            int decode_len = stun_decode((uint8_t*)buf, recv_buf, &msg);
            if (decode_len != 0) { printf("err decoding"); continue; }

            if (memcmp(temp_tid, &msg.transaction_id, 12) != 0) continue;
            if (stun_get_xor_mapped_addr(&msg, ip_host, port_host) != 0) { printf("err get xor mapped addr"); continue; } else return 0;
        }

        timeout *= 2;
    }

    return -5;
}
