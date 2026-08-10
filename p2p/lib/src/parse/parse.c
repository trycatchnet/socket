#include "parse.h"

int stun_header_to_binary(const stun_header_t *header, uint8_t *buffer, size_t buf_len) {
    // min 20 byte acceptible
    if (buf_len < sizeof(stun_header_t))
        return -1;

    // host to network short
    uint16_t mt = htons(header->msg_type), ml = htons(header->msg_length);

    // host to network long
    uint32_t mc = htonl(header->magic_cookie);

    // write the header fields to the correct byte offsets specified in the RFC
    memcpy(buffer,     &mt, 2); /* 0..1 type */
    memcpy(buffer + 2, &ml, 2); /* 2..3: length */
    memcpy(buffer + 4, &mc, 4); /* 4..7: cookie */
    memcpy(buffer + 8, header->transaction_id, 12); /* 8..19: TID */
    
    return (int)sizeof(stun_header_t);
}

int stun_header_from_binary(const uint8_t *buffer, size_t buf_len, stun_header_t *header) {
    if (buf_len < sizeof(stun_header_t))
        return -1;

    // converts from network byte order to host byte order
    header->msg_type = ntohs(*(const uint16_t *)(buffer + 0));
    header->msg_length = ntohs(*(const uint16_t *)(buffer + 2));
    header->magic_cookie = ntohl(*(const uint32_t *)(buffer + 4));

    // since the transaction ID is a byte array, no byte order conversion is required
    memcpy(header->transaction_id, buffer + 8, 12);

    if (header->magic_cookie != 0x2112A442)
        return -1;

    return 0;
}

int parse_xor_mapped_address(const uint8_t *buffer, size_t payload_len, uint32_t *mapped_ip_be, uint16_t *mapped_port_host) {
    if (payload_len < 12)
        return -1;

    // first 2 byte STUN attribute type field
    uint16_t attr_type = ntohs(*(const uint16_t *)(buffer + 0));

    // 0x0020 = XOR-MAPPED-ADDRESS attribute type
    if (attr_type != 0x0020)
        return -1;

    /* attribute header is 4 byte
    * - 2 byte type
    * - 2 byte length
    * value field starts at buffer + 4
    */
    const uint8_t *value = buffer + 4;

    /*
    * value[0]: reserved, ussally 0
    * value[1]: address family
    * 
    * 0x01 = IPv4
    * 0x02 = IPv6
    */
    uint8_t family = *(value + 1);
    if (family != 0x01)
        return -1;

    /*
    * There is a 16-bit port XORed with value + 2.
    * The port has been XORed with 0x2112, which is the upper 16
    * bits of the magic cookie.
    */
    uint16_t xor_port = *(const uint16_t *)(value + 2);
    uint16_t port_host = ntohs(xor_port) ^ 0x2112;
    
    /*
    * There is a IPv4 address XORed with value + 4.
    * IPv4 address, XORed with 32-byte magic cookie.
    */
    uint32_t xor_ip_host = ntohl(*(const uint32_t *)(value + 4));
    uint32_t ip_host = xor_ip_host ^ 0x2112A442;

    // now port is ready in host byte order form
    *mapped_port_host = port_host;
    /*
    * Network functions such as `sockaddr_in` and `inet_ntoa` expect
    * the IP address to be in network byte order. Therefore, `htonl`
    * is applied again.
    */
    *mapped_ip_be = htonl(ip_host);

    return 0;
}
