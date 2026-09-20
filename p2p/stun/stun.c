#include "stun.h"
#include <string.h>
#include <stdbool.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))

int stun_decode(const uint8_t *buf, size_t len, stun_msg_t *msg)
{
    if (len < STUN_HEADER_LEN) return -1;

    msg->msg_type = (uint16_t)(buf[0] << 8 | buf[1]);
    msg->msg_length = (uint16_t)(buf[2] << 8 | buf[3]);
    msg->magic_cookie = (uint32_t)buf[4] << 24 | (uint32_t)buf[5] << 16 | (uint32_t)buf[6] << 8 | (uint32_t)buf[7] << 0; /* We didn't used any 0xFF in here, because we don't need to cut anything, we just need to combine shi*/
    memcpy(msg->transaction_id, buf + 8,12);

    int pos = 20;
    int limit = MIN(len, 20 + msg->msg_length);

    msg->attribute_count = 0;

    while (pos < limit) {
        if (limit - pos < 4) return -2;
        //memcpy(msg->attributes->attr_type, buf + pos + 2, 2);
        if (msg->attribute_count >= STUN_MAX_ATTRS) return -5;
        msg->attributes[msg->attribute_count].attr_type = (uint16_t)(buf[pos + 0] << 8 | buf[pos + 1]);
        msg->attributes[msg->attribute_count].length = (uint16_t)(buf[pos + 2] << 8 | buf[pos + 3]);
        if (msg->attributes[msg->attribute_count].length > limit - pos - 4) return -3; /* it may be limit tho...*/
        if (msg->attributes[msg->attribute_count].length > STUN_MAX_ATTR_VALUE) return -4;
        memcpy(msg->attributes[msg->attribute_count].value, buf + pos + 4, msg->attributes[msg->attribute_count].length);
        pos += 4 + ((msg->attributes[msg->attribute_count].length +3) & ~3);
        msg->attribute_count++;
    }

    return 0;
}

int stun_encode(const stun_msg_t *msg, uint8_t *buf, size_t cap)
{
    if (cap < STUN_HEADER_LEN) return -1;
    /* if cap is smaller then 20, then we ain fitting that shi */
    
    buf[0] = (uint8_t)(msg->msg_type >> 8); /* we can get the upper byte by moving this n**** 8 bytes to right */
    buf[1] = (uint8_t)(msg->msg_type & 0xFF); /* and can get the rest(bottom dawg) with cutting the upper */
    buf[2] = (uint8_t)(msg->msg_length >> 8);
    buf[3] = (uint8_t)(msg->msg_length & 0xFF);

    /* if we'll do the below code like the upper one, it would repeat the magic cookie idk why tho */

    /* okay i learned why is that. If we use 0xFF without shifting it'll always give the bottom bytes, that will repeat the code and shi. */

    buf[4] = (uint8_t)((msg->magic_cookie >> 24) & 0xFF);
    buf[5] = (uint8_t)((msg->magic_cookie >> 16) & 0xFF);
    buf[6] = (uint8_t)((msg->magic_cookie >> 8) & 0xFF);
    buf[7] = (uint8_t)((msg->magic_cookie >> 0) & 0xFF);

    memcpy(buf + 8, msg->transaction_id, 12);

    int total_length = 0;

    for (int j = 0; j < msg->attribute_count; j++) {
        total_length += 4 + ((msg->attributes[j].length + 3) & ~3);
    }
    
    if (cap < STUN_HEADER_LEN + total_length) return -2;

    buf[2] = (uint8_t)(total_length >> 8);
    /* i thought why would i'll need to rewrite the resetted bottom bytes anyways. */
    buf[3] = (uint8_t)(total_length & 0xFF);

    int pos = 20;
    for (int i = 0; i < msg->attribute_count; i++) {
        buf[pos + 0] = (uint8_t)(msg->attributes[i].attr_type >> 8);
        buf[pos + 1] = (uint8_t)(msg->attributes[i].attr_type & 0xFF);        
        buf[pos + 2] = (uint8_t)(msg->attributes[i].length >> 8);        
        buf[pos + 3] = (uint8_t)(msg->attributes[i].length & 0xFF);

        memcpy(buf + pos + 4, msg->attributes[i].value, msg->attributes[i].length);
       
        int pad_len = (msg->attributes[i].length + 3) & ~3;
        int pad = pad_len - msg->attributes[i].length;

        memset(buf + pos + 4 + msg->attributes[i].length , 0, pad);

        pos += 4 + (((msg->attributes[i].length) + 3) & ~3);
    }

    return 20 + total_length; 
}

int stun_get_xor_mapped_addr(const stun_msg_t *msg, uint32_t *ip, uint16_t *port) {
    int i;
    bool found = false;
    for (i = 0; i < msg->attribute_count; i++) {
        if (msg->attributes[i].attr_type == STUN_ATTR_XOR_MAPPED_ADDR) { found = true; break; }
    }
    if (!found) return -1;

    if (msg->attributes[i].length < 8) return -2;
    if (msg->attributes[i].value[1] != 0x01) return -3; /* TODO : Add ipv6 support. */
    
    uint16_t xport = (uint16_t)(msg->attributes[i].value[2] << 8 | msg->attributes[i].value[3]);
    *port = xport ^ (uint16_t)(STUN_MAGIC_COOKIE >> 16);

    uint32_t xip = (uint32_t)msg->attributes[i].value[4] << 24 | (uint32_t)msg->attributes[i].value[5] << 16 | (uint32_t)msg->attributes[i].value[6] << 8 | (uint32_t)msg->attributes[i].value[7] << 0;
    *ip = xip ^ (uint32_t)(STUN_MAGIC_COOKIE);

    return 0;
}
