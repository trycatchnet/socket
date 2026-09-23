#ifndef NET_H
#define NET_H

#include <netinet/in.h>
#include <stdint.h>
#include <poll.h>

#define STUN_RTO_DEFAULT_MS 500
#define STUN_RC_DEFAULT     7
#define STUN_RM_DEFAULT     16

int stun_query(int sockfd, const struct sockaddr_in *server, uint32_t *ip_host, uint16_t *port_host);

int generate_tid(uint8_t *buf, size_t size);

uint64_t now_ms(void);

#endif /* NET_H */
