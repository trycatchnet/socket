#ifndef NET_H
#define NET_H

#include <netinet/in.h>
#include <stdint.h>

#include "stun.h"

int stun_query(int sockfd, const struct sockaddr_in *server, uint32_t *ip_host, uint16_t *port_host);

int generate_tid(uint8_t *buf, size_t size);

#endif /* NET_H */
