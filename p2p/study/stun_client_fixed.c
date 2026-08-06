/*
* RFC 5389 Compliant STUN Client Implementation
* This code implements a complete STUN Binding Request/Response loop
*/

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>

#define STUN_SERVER_DEFAULT_IP   "132.145.97.194" // pub.stun.google.de
#define STUN_SERVER_DEFAULT_PORT 3478
#define SOCKET_TIMEOUT_SEC       5
#define MAX_BUFFER_SIZE          1500
#define ALIGN_TO_4(x)            (((x) + 3) & ~3)

struct addrinfo hinst = {0};

/*
* STUN Header (RFC 5389 Section 6)
* Total 20 byte - the first 20 bytes of each STUN message
*/
typedef struct __attribute__((packed)) {
    /* 2 byte (network byte order / big-endian)
       High nibble: Message class (0x0 = Request, 0x1 = Response)
       Low nibble: Method (0x1 = Binding)
       Binding Request: 0x0001
       Binding Success: 0x0101 */
    uint16_t msg_type;
    /* 2 byte - attributes total length
       Not included header (only attributes) */
    uint16_t msg_length;
    /* 4 byte constant value
       Required in RFC 5389: 0x2112A442
       It also using in Transaction ID for XOR */
    uint32_t magic_cookie;
    /* 12 byte random/variable
       Must be unique in all requests (for match response) */
    uint8_t  transaction_id[12];
} stun_header_t;

/*
* XOR-MAPPED-ADDRESS Attribute Structure (RFC 5389 Section 15.2)
* It carries the public IP and port
*/
typedef struct __attribute__((packed)) {
    /* 0x0020 = XOR-MAPPED-ADDRESS */
    uint16_t attr_type;
    /* 8 byte (for IPv4) */
    uint16_t attr_length;
    /* Must be zero (always 0x00) */
    uint8_t reserved;
    /* 0x01 = IPv4, 0x02 = IPv6 */
    uint8_t family;
    /* Port XOR'd with upper 16 bits of magic cookie
       XOR = port ^ 0x2112 */
    uint16_t xor_port; // (network order)
    /* IP XOR'd with full magic cookie
       XOR = address ^ 0x2112A442 */
    uint32_t xor_address;
} stun_xmapped_attr_t;

/*
* SOFTWARE Attribute (Optional - RFC 5389 Section 15.7)
* Gives information about client software (usefull for debug)
*/
typedef struct __attribute__((packed)) {
    /* 0x8022 = SOFTWARE (optional attr) */
    uint16_t attr_type;
    /* software name length */
    uint16_t attr_length;
    /* Value: UTF-8 encoded string (variable length)
       Padding may required (4-byte aligned) */
} stun_software_attr_t;

/*
* Random Transaction ID Generator (12 byte)
*/
void generate_transaction_id(uint8_t tid[12]) { // tid[] - buffer (min 12 byte)
    time_t now = time(NULL);
    uint64_t timestamp = (uint64_t)now;

    // divide the timestamp into 4 bytes + 4 bytes (be careful of endianness)
    tid[0] = (timestamp >> 24) & 0xFF;
    tid[1] = (timestamp >> 16) & 0xFF;
    tid[2] = (timestamp >> 8) & 0xFF;
    tid[3] = timestamp & 0xFF;
    tid[4] = (timestamp >> 24) & 0xFF;
    tid[5] = (timestamp >> 16) & 0xFF;
    tid[6] = (timestamp >> 8) & 0xFF;
    tid[7] = timestamp & 0xFF;

    // process ID - 2 byte
    uint16_t pid = getpid();
    tid[8] = (pid >> 8) & 0xFF;
    tid[9] = pid & 0xFF;

    // random - 2 byte
    tid[10] = rand() & 0xFF;
    tid[11] = (rand() >> 8) & 0xFF;
}

/*
* Print the byte array in hex format (for debugging purposes)
*/
void print_hex(const uint8_t data[], size_t len, const char *prefix) {
    printf("%s", prefix);
    for (size_t i = 0; i < len; i++) {
        printf("%02X", data[i]);
        if ((i + 1) % 16 == 0 && i + 1 < len) {
            printf("\n     ");
        } else if (i + 1 < len) {
            printf(":");
        }
    }
    printf("\n");
}

/*
* Convert STUN Header to binary format (host -> network byte order)
*/
int stun_header_to_binary(const stun_header_t *header, uint8_t *buffer, size_t buf_len) {
    if (buf_len < sizeof(stun_header_t)) {
        fprintf(stderr, "[ERR] Buffer too small for STUN header\n");
        return -1;
    }

    // convert to network byte order (big-endian)
    uint16_t mt = htons(header->msg_type);
    uint16_t ml = htons(header->msg_length);
    uint32_t mc = htonl(header->magic_cookie);

    // copy to buffer
    memcpy(buffer, &mt, sizeof(mt));
    memcpy(buffer + 2, &ml, sizeof(ml));
    memcpy(buffer + 4, &mc, sizeof(mc));
    memcpy(buffer + 8, header->transaction_id, 12);

    return sizeof(stun_header_t);
}

/*
* Parse binary STUN message (network -> host byte order)
*/
int stun_header_from_binary(const uint8_t *buffer, size_t buf_len, stun_header_t *header) {
    if (buf_len < sizeof(stun_header_t)) {
        fprintf(stderr, "[ERR] Packet too small for STUN header (%zu < 20)", buf_len);
        return -1;
    }

    // change to host byte order
    header->msg_type = ntohs(*(const uint16_t *)(buffer + 0));
    header->msg_length = ntohs(*(const uint16_t *)(buffer + 2));
    header->magic_cookie = ntohl(*(const uint32_t *)(buffer + 4));
    memcpy(header->transaction_id, buffer + 8, 12);

    // verify magic cookie
    if (header->magic_cookie != 0x2112A442) {
        fprintf(stderr, "[ERR] Invalid magic cookie: 0x%08X\n", header->magic_cookie);
        return -1;
    }

    return 0;
}

/*
* Create a XOR-MAPPED-ADDRESS Attribute
*/
int create_mapped_address_attribute(uint32_t ip, uint16_t port, uint8_t *buffer, size_t buf_len) {
    if (buf_len < 12) {
        fprintf(stderr, "[ERR] Buffer too small for XOR-MAPPED-ADDRESS\n");
        return -1;
    }
    
    stun_xmapped_attr_t attr = {0};
    attr.attr_type = htons(0x0020); // XOR-MAPPED-ADDRESS
    attr.attr_length = htons(8); // 8 byte
    attr.reserved = 0x00; // = 0
    attr.family = 0x01; // (IPv4)
    
    uint16_t xor_port = htons(port) ^ 0x2112;
    attr.xor_port = xor_port;

    uint32_t xor_addr = ip ^ 0x2112A442;
    attr.xor_address = htonl(xor_addr);

    memcpy(buffer, &attr, sizeof(attr)); // copy buffer
    
    return sizeof(attr);
}

/*
* Parse the XOR-MAPPED-ADDRESS Attribute (read at answer)
*/
int parse_mapped_address_attribute(const uint8_t *buffer, size_t payload_len, 
                                   uint32_t *mapped_ip, uint16_t *mapped_port) {
    if (payload_len < 12) {
        fprintf(stderr, "[ERR] Payload too small for mapped address\n");
        return -1;
    }

    // read header
    uint16_t attr_type = ntohs(*(const uint16_t *)(buffer + 0));
    //uint16_t attr_length = ntohs(*(const uint16_t *)(buffer + 2));

    // is XOR-MAPPED-ADDRESS?
    if (attr_type != 0x0020) {
        fprintf(stderr, "[WAR] Not XOR-MAPPED-ADDRESS (type=0x%04X)\n", attr_type);
        return -1;
    }

    // go to family & port/ip fields
    const uint8_t *value = buffer + 4;

    // is IPv4?
    uint8_t family = *(value + 1);
    if (family != 0x01) {
        fprintf(stderr, "[ERR] Unsupported address family: 0x%02X\n", family);
        return -1;
    }

    // solve XOR Port
    uint16_t xor_port = *(const uint16_t *)(value + 2);
    uint16_t port_host = ntohs(xor_port) ^ 0x2112;

    // solve XOR IP
    uint32_t xor_ip = ntohl(*(const uint32_t *)(value + 4));
    uint32_t ip_net = xor_ip ^ 0x2112A442;
    
    // write results in response
    *mapped_port = port_host;
    *mapped_ip = ip_net;

    return 0;
}

/*
* Hostname -> IP resolution (getaddrinfo)
*/
int resolve_hostname(const char *hostname, struct sockaddr_in *addr) {
    struct addrinfo hinst = {0}, *result, *rp;

    hinst.ai_family = AF_INET;
    hinst.ai_socktype = SOCK_DGRAM;

    int ret = getaddrinfo(hostname, NULL, &hinst, &result);
    if (ret != 0) {
        fprintf(stderr, "[ERR] getaddrinfo failed: %s\n", gai_strerror(ret));
        return -1;
    }

    // get started ip address
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            memcpy(addr, rp->ai_addr, sizeof(*addr));
            freeaddrinfo(result);
            return 0;
        }
    }

    fprintf(stderr, "[ERR] No IPv4 address found for %s\n", hostname);
    freeaddrinfo(result);
    return -1;
}

int main(int argc, char** argv) {
    int sock_fd;
    struct sockaddr_in server_addr, local_addr;
    socklen_t addr_len;

    uint8_t request_buf[MAX_BUFFER_SIZE], response_buf[MAX_BUFFER_SIZE];
    ssize_t sent_bytes, received_bytes; // I/O count

    stun_header_t request_hdr, response_hdr;
    uint32_t mapped_ip;
    uint16_t mapped_port;

    const char *stun_ip = STUN_SERVER_DEFAULT_IP;
    uint16_t stun_port = STUN_SERVER_DEFAULT_PORT;

    if (argc >= 2) stun_ip = argv[1];
    if (argc >= 3) stun_port = (uint16_t)atoi(argv[2]);

    printf("RFC 5389 STUN Client - P2P NAT Traversal\n");
    printf("Target STUN Server: %s:%d\n", stun_ip, stun_port);

    srand(time(NULL) ^ getpid()); // random seed initialize
    
    printf("\nUDP socket is creating...\n");
    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("[FATAL] socket() failed");
        return EXIT_FAILURE;
    }
    printf("Socket descriptor: %d\n\n", sock_fd);

    memset(&local_addr, 0, sizeof(local_addr));
    local_addr.sin_family = AF_INET;
    local_addr.sin_port = 0; // OS selecting port
    local_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0)
        perror("[WAR] bind() failed (continue anyway)");
    else {
        // bind success, now learn the local address
        socklen_t local_len = sizeof(local_addr);
        if (getsockname(sock_fd, (struct sockaddr *)&local_addr, &local_len) == 0)
            printf("\n[DEBUG] Local address assingned by OS:"
                   "\n\tPrivate IP: %s"
                   "\n\tPrivate Port: %d\n\n",
                   inet_ntoa(local_addr.sin_addr), ntohs(local_addr.sin_port));  
    }

    // adjust STUN address
    printf("Resolving the STUN server address...\n");
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    //server_addr.sin_port = htons(stun_port); --> not now

    if (inet_pton(AF_INET, stun_ip, &server_addr.sin_addr) <= 0) {
        printf(" -> Not a numeric UP, resolving hostname '%s'...\n", stun_ip);
        if (resolve_hostname(stun_ip, &server_addr) < 0) {
            perror("[FATAL] Invalid STUN server IP");
            close(sock_fd);
            return EXIT_FAILURE;
        }
    }
    server_addr.sin_port = htons(stun_port);
    printf("Server: %s:%d (binary: %s)\n\n",
           stun_ip, stun_port, inet_ntoa(server_addr.sin_addr));
    
    // socket timeout setting (SO_RCVTIMEO)
    struct timeval tv = { .tv_sec = SOCKET_TIMEOUT_SEC, .tv_usec = 0 };
    if (setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0)
        perror("[WAR] Cannot set receive timeout");
    printf("Socket timeout: %d sec.\n\n", SOCKET_TIMEOUT_SEC);

    // Prepare STUN Bind Request
    printf("Preparing STUN Bind Request...\n");
    memset(&request_hdr, 0, sizeof(request_hdr));
    request_hdr.msg_type = 0x0001;      // Binding request (host byte order;
                                         // stun_header_to_binary() converts to network order)
    request_hdr.msg_length = 0;         // no attributes for now
    request_hdr.magic_cookie = 0x2112A442;

    // generate Transaction ID
    generate_transaction_id(request_hdr.transaction_id);

    print_hex(request_hdr.transaction_id, 12, "  Transaction ID: ");

    // convert header to binary
    memset(request_buf, 0, sizeof(request_buf));
    int header_size = stun_header_to_binary(&request_hdr, request_buf, sizeof(request_buf));
    
    if (header_size < 0) {
        fprintf(stderr, "[FATAL] Failed to serialize STUN header\n");
        close(sock_fd);
        return EXIT_FAILURE;
    }
    printf("Request size: %d bytes (header only)\n\n", header_size);

    // Send STUN Binding Request
    printf("Sending STUN Binding Request...\n");
    sent_bytes = sendto(sock_fd, request_buf, header_size, 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

    if (sent_bytes < 0) {
        perror("[FATAL] sendto() failed");
        close(sock_fd);
        return EXIT_FAILURE;
    }
    printf("Send %zd bytes to %s:%d\n\n", sent_bytes, stun_ip, stun_port);

    // Receive STUN Response
    printf("Waiting Response (timeout: %ds)...\n", SOCKET_TIMEOUT_SEC);

    memset(response_buf, 0, sizeof(response_buf));
    addr_len = sizeof(server_addr);

    received_bytes = recvfrom(sock_fd, response_buf, sizeof(response_buf),
                              0, (struct sockaddr *)&server_addr, &addr_len);
    
    if (received_bytes < 0) {
        perror("[FATAL] recvfrom() failed (timeout?)");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Received %zd bytes\n\n", received_bytes);

    // Parse Response
    printf("Parsing STUN Response...\n");
    if (stun_header_from_binary(response_buf, received_bytes, &response_hdr) < 0) {
        fprintf(stderr, "[FATAL] Invalid STUN response header\n");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Response type: 0x%04X ", response_hdr.msg_type);
    if (response_hdr.msg_type == 0x0101)
        printf("(Binding Success)\n");
    else if (response_hdr.msg_type == 0x0111) {
        printf("(Binding Error)\n");
        fprintf(stderr, "STUN server error response\n");
        close(sock_fd);
        return EXIT_FAILURE;
    } else printf("(Unknown, expected 0x0101)\n");

    // Transaction ID Check (match verify)
    if (memcmp(request_hdr.transaction_id, response_hdr.transaction_id, 12) != 0) {
        fprintf(stderr, "Transaction ID mismatch! Ignoring old/spurious response.\n");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    printf("Transaction ID matched\n\n");

    // XOR-MAPPED-ADDRESS attribute extraction
    printf("Searching XOR-MAPPED-ADDRESS attribute...\n");

    // Payload start (after the header)
    const uint8_t *payload = response_buf + sizeof(stun_header_t);
    size_t payload_len = received_bytes - sizeof(stun_header_t);

    if (parse_mapped_address_attribute(payload, payload_len, 
                                       &mapped_ip, &mapped_port) < 0) {
        fprintf(stderr, "[FATAL] Could not find XOR-MAPPED-ADDRESS attribute\n");
        close(sock_fd);
        return EXIT_FAILURE;
    }
    printf("Found XOR-MAPPED-ADDRESS attribute\n\n");

    // show results
    printf("STUN Test Results\n\n");
    
    printf("LOCAL (Private) Address\nIP Address: %s\n", inet_ntoa(local_addr.sin_addr));
    printf("Port: %d\n\n", ntohs(local_addr.sin_port));

    struct in_addr mapped_addr_struct = { .s_addr = htonl(mapped_ip) };
    
    printf("MAPPED (Public) Address\nIP Address: %s\n", inet_ntoa(mapped_addr_struct));
    printf("Port: %d\n\n", mapped_port);

    printf("NAT ANALYSIS\n");
    // basic NAT type analysis
    uint16_t local_port = ntohs(local_addr.sin_port);
    if (local_port == mapped_port) {
        printf("-> Port preserved! Likely FULL CONE NAT\n");
        printf("-> UDP Hole Punching should work perfectly!\n");
    } else {
        printf("-> Port changed! Possible SYMMETRIC NAT\n");
        printf("-> UDP Hole Punching may FAIL. Turn relay required.\n");
    }
    printf("\n");

    close(sock_fd);
    printf("STUN test completed. Socket closed.\n");

    return EXIT_SUCCESS;
}
