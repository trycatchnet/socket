#ifndef NETWORK_H
#define NETWORK_H

#include "../util.h"
#include "../type.h"

/*
* It sends a STUN Binding Request over the open UDP socket and
* returns the public IP:port value as seen by the STUN server.
*
* This function does not open a separate socket; it uses the
* socket (sock_fd) opened by main(). The reason for this is
* that a NAT device typically establishes mappings based on
* the local IP address and port and the destination. It is important
* that the socket used in the STUN query be the same as the one used
* in P2P hole punching in order to preserve the external port mapping.
*
* Params:
* - sock_fd : binded socket file descriptor
* - stun_host : IP or Hostname
* - stun_port : STUN UDP port
* - out_ip : the buffer where the public IPv4 text will be written
* - out_ip_len : out_ip capacity
* - out_port : where to enter the public UDP port
*/
int stun_discover(int sock_fd, const char *stun_host, uint16_t stun_port, char *out_ip, size_t out_ip_len, uint16_t *out_port);

/* Convert the p2p_packet_t struct to exact 17 byte wire format. */
int p2p_pack(const p2p_packet_t *pkt, uint8_t *buf, size_t buf_len);

/* Unconvert the p2p_packet_t struct. */
int p2p_unpack(const uint8_t *buf, size_t len, p2p_packet_t *pkt);

/* Creates p2p packet fields, converts onto binary wire format then sends to target with udp */
int p2p_send(int sock_fd, const struct sockaddr_in *dst, uint8_t type, uint32_t seq, uint64_t ts_us);

/* Maximum chat text length that fits in one UDP datagram after the header.
 * MAX_BUFFER_SIZE (1500) - P2P_PACKET_SIZE (17) */
#define CHAT_MAX_TEXT (MAX_BUFFER_SIZE - P2P_PACKET_SIZE)

/* Sends a MSG_CHAT packet: the normal 17 byte p2p header, immediately
 * followed by the raw chat text bytes (no null terminator on the wire).
 *
 * text_len larger than CHAT_MAX_TEXT is silently truncated, since a single
 * UDP datagram can't carry more than MAX_BUFFER_SIZE bytes here anyway. */
int p2p_send_chat(int sock_fd, const struct sockaddr_in *dst, uint32_t seq, const char *text, size_t text_len);

/* It cheks if a sockaddr_in address is the same in both ipv4 and port.
 *
 * Why ?
 * - UDP socket can have different packages from the internet.
 * - We need to process only the packages from waiting peer.
 *
 * sin_port is in network byte order.
 * sin_addr.s_addr is in network byte order too.
 *
 * Because of that reason we can check directly without conversion.
 * */
int addr_equal(const struct sockaddr_in *a, const struct sockaddr_in *b);

/* Step 1 : UDP Hole Punching
 * 
 * hole_punch() 
 *
 * Sends MSG_PUNCH to target peer while waiting for a interval also waits for an incoming P2P packet. 
 *
 * Rules:
 * - Target peer must send UDP packet.
 * - Packet needs valid P2P magic value.
 * 
 * Why we want both peers to send PUNCH at the same time ?
 * NAT, generally creates a peer when the first UDP packet is sent inside-out.
 * If other peers packet catches this temporary packet NAT can let peer in.
 * */
int hole_punch(int sock_fd, const struct sockaddr_in *peer_addr);

/* Step 2 : Mutual Ping + Chat
 *
 * chat_loop()
 *
 * Runs after hole punching is success.
 *
 * This function does 3 things at the same time, all from one poll() loop :
 * 1. Sends ping regularly to other peer, in the background, to keep
 *    checking the connection is still alive (unchanged from before).
 * 2. If a ping came it returns PONG immediately, and RTT gets printed.
 * 3. Watches stdin (keyboard) for typed lines. Whenever the user presses
 *    ENTER, the typed line is sent to the peer as a MSG_CHAT packet, and
 *    any MSG_CHAT packet coming from the peer is printed to the screen.
 *
 * Typing "/quit" and pressing ENTER exits the loop the same way CTRL+C
 * (SIGINT) does: it sends a final MSG_BYE to the peer before closing. */
void chat_loop(int sock_fd, const struct sockaddr_in *peer_addr);

#endif // !NETWORK_H
