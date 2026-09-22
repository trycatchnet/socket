#ifndef STUN_H
#define STUN_H

#include <stdint.h>
#include <stddef.h>

#define STUN_MAGIC_COOKIE   0x2112A442u
#define STUN_HEADER_LEN     20
#define STUN_MAX_MSG        1500
#define STUN_MAX_ATTR_VALUE 255
#define STUN_MAX_ATTRS      16

typedef enum {
    STUN_ATTR_MAPPED_ADDR = 0x0001,
    STUN_ATTR_XOR_MAPPED_ADDR = 0x0020 } stun_attr_type;

typedef struct {
    uint16_t attr_type;
    uint16_t length;
    uint8_t value[STUN_MAX_ATTR_VALUE];
} stun_attr_t;

typedef struct {
    uint16_t msg_type;
    uint16_t msg_length;
    uint32_t magic_cookie;
    uint8_t  transaction_id[12];
    stun_attr_t attributes[STUN_MAX_ATTRS];
    uint16_t attribute_count;
} stun_msg_t;

int stun_decode(const uint8_t *buf, size_t len, stun_msg_t *msg);

int stun_encode(const stun_msg_t *msg, uint8_t *buf, size_t cap);

int stun_get_xor_mapped_addr(const stun_msg_t *msg, uint32_t *ip, uint16_t *port);

#endif /* STUN_H */
