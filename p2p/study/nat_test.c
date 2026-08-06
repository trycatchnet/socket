/*
 * Note: This is a simple test. For true NAT type determination, RFC 3489 (Classic STUN) Test 1/2/3 procedure should be applied.
 */

#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <sys/socket.h>

#define STUN_IP   "172.217.197.127" // example STUN server address --> google stun
#define STUN_PORT 19302             // standart STUN port --> google
#define BUF_SIZE  1024

typedef struct {
    uint16_t msg_type;      // 0x0001 - binding request
    uint16_t msg_length;    // payload length
    uint32_t magic_cookie;  // 0x2112A442
    uint8_t transaction_id[12]; // random 12 byte
} stun_header_t;
/*
 * NOTE: must be real magic_cookie & transaction_id in real implementation
 */

void create_stun_request(uint8_t *buffer, size_t *len) {
    stun_header_t *hdr = (stun_header_t *)buffer;
    hdr->msg_type      = htons(0x0001); // binding request
    hdr->msg_length    = htons(0);      // no attribute for now
    hdr->magic_cookie  = htonl(0x2112A442);

    /* random transaction ID */
    for (int i = 0; i < 12; i++) hdr->transaction_id[i] = rand() % 256;

    *len = sizeof(stun_header_t); // 20 byte
}


// resolve XOR-MAPPED-ADDRESS from STUN response
int parse_stun_response(uint8_t *buffer, ssize_t len, struct sockaddr_in *mapped_addr) {
    stun_header_t *hdr = (stun_header_t *)buffer;
    uint16_t msg_type = ntohs(hdr->msg_type);

    if (msg_type != 0x0101) { // binding request response
        fprintf(stderr, "STUN error response (type: 0x%04x)\n", msg_type);
        return -1;
    }

    uint16_t msg_length = ntohs(hdr->msg_length);
    uint8_t *attr_ptr = buffer + sizeof(stun_header_t);
    ssize_t remaining = len - sizeof(stun_header_t);

    // scan attributes
    while (remaining >= 4) {
        uint16_t attr_type = ntohs(*(uint16_t *)attr_ptr);
        uint16_t attr_len = ntohs(*(uint16_t *)(attr_ptr + 2));

        // XOR-MAPPED-ADDRESS (0x0020)
        if (attr_type == 0x0020 && attr_len >= 8) {
            uint8_t family = *(attr_ptr + 5);
            if (family != 0x01) continue; // only IPv4

            // resolve PORT with XOR
            uint16_t xor_port = ntohl(*(uint16_t *)(attr_ptr + 6));
            uint16_t mapped_port = xor_port ^ 0x2112; // magic_cookie top 16 byte

            // resolve IP with XOR
            uint32_t xor_ip = ntohl(*(uint32_t *)(attr_ptr + 8));
            uint32_t mapped_ip = xor_ip ^ 0x2112A442;

            mapped_addr->sin_family = AF_INET;
            mapped_addr->sin_port = htons(mapped_port);
            mapped_addr->sin_addr.s_addr = htonl(mapped_ip);

            return 0; // success
        }

        // MAPPED-ADDRESS (0x0001) - without XOR version
        if (attr_type == 0x0001 && attr_len >= 8) {
            uint8_t family = *(attr_ptr + 5);
            if (family != 0x01) continue;

            uint16_t mapped_port = ntohs(*(uint16_t *)(attr_ptr + 6));
            uint32_t mapped_ip = ntohl(*(uint32_t *)(attr_ptr + 8));

            mapped_addr->sin_family = AF_INET;
            mapped_addr->sin_port = htons(mapped_port);
            mapped_addr->sin_addr.s_addr = htonl(mapped_ip);

            return 0; // success
        }

        // move on the next attribute (4-byte aligned)
        uint16_t padded_len = (attr_len + 3) & ~3;
        attr_ptr += 4 + padded_len;
        remaining -= 4 + padded_len;
    }

    return -1; // not found
}

int main(void) {
    int sock;
    struct sockaddr_in stun_addr, mapped_addr, local_addr;
    socklen_t addr_len = sizeof(stun_addr);
    
    srand(getpid());
    
    // create UDP socket
    if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // STUN server address
    memset(&stun_addr, 0, sizeof(stun_addr));
    stun_addr.sin_family = AF_INET;
    stun_addr.sin_port = htons(STUN_PORT);
    inet_pton(AF_INET, STUN_IP, &stun_addr.sin_addr);
    
    // send STUN Binding Request
    uint8_t request[64];
    uint8_t response[BUF_SIZE];
    size_t req_len;
    
    create_stun_request(request, &req_len);
    
    printf("Sending STUN Binding Request → %s:%d\n", STUN_IP, STUN_PORT);
    
    ssize_t sent = sendto(sock, request, req_len, 0,
                          (struct sockaddr*)&stun_addr, sizeof(stun_addr));
    if (sent < 0) {
        perror("sendto failed");
        close(sock);
        exit(EXIT_FAILURE);
    }
    
    // Wait to response (5 sec. timeout)
    struct timeval tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    ssize_t received = recvfrom(sock, response, BUF_SIZE, 0,
                                (struct sockaddr*)&stun_addr, &addr_len);
    if (received < 0) {
        perror("recvfrom failed (timeout?)");
        close(sock);
        exit(EXIT_FAILURE);
    }
    
    printf("Received STUN Response (%zd bytes)\n", received);
    
    // resolve the response
    memset(&mapped_addr, 0, sizeof(mapped_addr));
    if (parse_stun_response(response, received, &mapped_addr) == 0) {
        // get Local (private) address 
        socklen_t local_len = sizeof(local_addr);
        getsockname(sock, (struct sockaddr*)&local_addr, &local_len);
        
        printf("\n");
        printf("═══════════════════════════════════════════\n");
        printf("  NAT Detection Result\n");
        printf("═══════════════════════════════════════════\n");
        printf("  Local  (Private) IP: %s:%d\n",
               inet_ntoa(local_addr.sin_addr),
               ntohs(local_addr.sin_port));
        printf("  Public (Mapped)  IP: %s:%d\n",
               inet_ntoa(mapped_addr.sin_addr),
               ntohs(mapped_addr.sin_port));
        
        // basic NAT type test
        if (ntohs(local_addr.sin_port) == ntohs(mapped_addr.sin_port)) {
            printf("  NAT Type: Likely FULL CONE (port preserved)\n");
        } else {
            printf("  NAT Type: Likely SYMMETRIC (port changed)\n");
            printf("  → UDP Hole Punching may not work!\n");
            printf("  → TURN relay required.\n");
        }
        printf("═══════════════════════════════════════════\n");
    } else {
        fprintf(stderr, "Can't parse STUN response\n");
    }
    
    close(sock);
    return 0;
}
