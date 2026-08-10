#ifndef TYPE_H
#define TYPE_H

#include <stdint.h>

/* STUN HEADER (RFC 5389)
* STUN header total size is 20 byte:
* 0..1  : Message Type   (2 byte)
* 2..3  : Message Length (2 byte)
* 4..7  : Magic Cookie   (4 byte)
* 8..19 : Transaction ID (12 byte)
*
* __attribute__((packed)): Prevents the compiler from adding extra bytes
* for alignment between struct fields.
*/
typedef struct __attribute__((packed)) {
    uint16_t msg_type;
    uint16_t msg_length;         // payload length after header
    uint32_t magic_cookie;       // RFC 5389 constant: 0x2112A442
    uint8_t  transaction_id[12]; // Request-response pairing ID
} stun_header_t;

/* Custom packet types for this program.
 * 
 * MSG_PUNCH : Packet is currently on NAT punching.
 * MSG_PING : Start ping.
 * MSG_PONG : Response for the coming ping, sends the exact seq and timestamp.
 * MSG_BYE : Tells us that the peer we connected is exiting.
 * */

enum { MSG_PUNCH = 1, MSG_PING = 2, MSG_PONG = 3, MSG_BYE = 4};

/* P2P Packet structure
 *
 * Note :
 * You can't directly send this structure through network.
 * Because the compiler can add padding or the numbres can be hold in different byte orders.
 *
 * To prevent that, we are using custom functions, p2p_pack() and p2p_unpack() */

typedef struct {
    uint32_t magic; /* P2P_MAGIC verification value */
    uint8_t type; /* MSG_PUNCH, MSG_PING etc. */
    uint32_t seq; /* Sequence number, obviously */
    uint64_t ts_us; /* Transmission/send time, in microseconds */
} p2p_packet_t;

#endif // !TYPE_H
