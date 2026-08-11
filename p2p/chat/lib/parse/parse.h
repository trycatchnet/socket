#ifndef PARSE_H
#define PARSE_H

#include "../util.h"
#include "../type.h"

/*
* Converts the STUN header structure into a raw byte sequence to be sent
* over the network.
* 
* Why isn't `memcpy(&header, ...)` used directly?:
* This is because the CPU's native byte order may be little-endian. On the network,
* however, multi-byte numbers must be transmitted in big-endian/network byte order.
*/
int stun_header_to_binary(const stun_header_t *header, uint8_t *buffer, size_t buf_len);

/*
* Parses the header of the raw STUN data retrieved from the network.
*/
int stun_header_from_binary(const uint8_t *buffer, size_t buf_len, stun_header_t *header);

/*
* It extracts the public IP address and port from
* the XOR-MAPPED-ADDRESS attribute in the STUN response.
* 
* What is XOR-MAPPED-ADDRESS?: The STUN server returns the external
* IP:port address it sees for the client. This information is sent
* in an XOR-encrypted format rather than in plain text.
*
* Why is XOR using?: To reduce the likelihood that legacy NAT ALG
* devices will inadvertently alter the IP/port information visible in the packet.
*
* XOR OPERATION:
* XOR = exclusive OR
*
* The operation A ^ B returns the initial value when the same B value
* is applied again:
* (A ^ B) ^ B = A
*
* That's why we can restore the port/IP value that the STUN server XORed by performing
* an XOR operation with the same constant.
*
* Params:
* - buffer: STUN payload start
* - payload_len: Payload length after the header
* - mapped_ip_be: Response public IP (in network byte order form)
* - mapped_port_host: Response public PORT (in host byte order form)
*/
int parse_xor_mapped_address(const uint8_t *buffer, size_t payload_len, uint32_t *mapped_ip_be, uint16_t *mapped_port_host);

#endif // !PARSE_H
