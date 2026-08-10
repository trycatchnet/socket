#define _GNU_SOURCE

#include "./network.h"

#include "../parse/parse.h"
#include "../crypto/crypto.h"

int stun_discover(int sock_fd, const char *stun_host, uint16_t stun_port, char *out_ip, size_t out_ip_len, uint16_t *out_port) {
    // STUN server network address
    struct sockaddr_in stun_addr = {0};
    stun_addr.sin_family = AF_INET;

    if (inet_pton(AF_INET, stun_host, &stun_addr.sin_addr) <= 0) {
        if (resolve_hostname(stun_host, &stun_addr) < 0) {
            fprintf(stderr, "[ERR] STUN server not resolved: %s\n", stun_host);
            return -1;
        }
    }
    stun_addr.sin_port = htons(stun_port);

    // 5 sec. sock-level timeout for recvfrom()
    struct timeval tv = {
        .tv_sec = STUN_TIMEOUT_SEC,
        .tv_usec = 0
    };

    setsockopt(sock_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    for (int attempt = 1; attempt <= STUN_MAX_ATTEMPTS; attempt++) {
        // that's will starts Request Header with zero 
        stun_header_t req = {0};

        /*
        * 0x0001 = Binding request.
        * It is a question asked of the STUN server: “From which IP:port are
        * you seeing me?”
        */
        req.msg_type = 0x0001;
        // No additional payload/attribute after header in this request
        req.msg_length = 0;
        // RFC 5389 magic cookie
        req.magic_cookie = 0x2112A442;
        generate_transaction_id(req.transaction_id);
        uint8_t req_buf[20];
        int req_len = stun_header_to_binary(&req, req_buf, sizeof(req_buf));

        if (sendto(sock_fd, req_buf, req_len, 0, (struct sockaddr *)&stun_addr, sizeof(stun_addr)) < 0) {
            fprintf(stderr, "[ERR] STUN sendto (test %d/%d): %s\n", attempt, STUN_MAX_ATTEMPTS, strerror(errno));
            continue;
        }

        // general UDP buffer to receive the STUN response
        uint8_t resp_buf[MAX_BUFFER_SIZE];
        // the return address is entered here
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);

        ssize_t n = recvfrom(sock_fd, resp_buf, sizeof(resp_buf), 0, (struct sockaddr *)&from_addr, &from_len);
        if (n < 0) {
            fprintf(stderr, "[WAR] Can't received STUN response (test %d/%d): %s\n", attempt, STUN_MAX_ATTEMPTS, strerror(errno));
            continue;
        }

        // parse the STUN header of the incoming packet
        stun_header_t resp;
        
        if (stun_header_from_binary(resp_buf, (size_t)n, &resp) < 0)
            continue;

        // 0x0101 = Binding Success Response
        if (resp.msg_type != 0x0101)
            continue;

        // make sure the response is for the request we just sent
        if (memcmp(req.transaction_id, resp.transaction_id, 12) != 0)
            continue;

        // STUN payload: data of after the header
        const uint8_t *payload = resp_buf + sizeof(stun_header_t);
        // to ensure security, the header size is subtracted from the total number of bytes
        size_t payload_len = (size_t)n - sizeof(stun_header_t);

        // parsed public IP & port
        uint32_t ip_be;
        uint16_t port_host;

        if (parse_xor_mapped_address(payload, payload_len, &ip_be, &port_host) < 0)
            continue;

        // put binary IPv4 address in to struct in_addr
        struct in_addr ia = {
            .s_addr = ip_be
        };

        strncpy(out_ip, inet_ntoa(ia), out_ip_len - 1);

        out_ip[out_ip_len - 1] = '\0';
        *out_port = port_host;

        return 0;
    }

    fprintf(stderr, "[ERR] STUN discovery %d, test is failed.\n", STUN_MAX_ATTEMPTS);
    return -1;
}

int p2p_pack(const p2p_packet_t *pkt, uint8_t *buf, size_t buf_len) {
    if (buf_len < P2P_PACKET_SIZE) return -1;

    /* convert 32 bit values into network byte order */ 
    uint32_t magic_be = htonl(pkt->magic);
    uint32_t seq_be = htonl(pkt->seq);
    
    /* htobe64
     * Convert 64 bit timestamp onto big-endian format */
    uint64_t ts_be = htobe64(pkt->ts_us);

    /* wire byte layout */
    memcpy(buf, &magic_be, 4); // byte 0-3 
    buf[4] = pkt->type; // byte 4 only
    memcpy(buf + 5, &seq_be, 4); // byte 5-8
    memcpy(buf + 9, &ts_be, 8); // byte 9-16

    return P2P_PACKET_SIZE;
}

int p2p_unpack(const uint8_t *buf, size_t len, p2p_packet_t *pkt) {
    if (len < P2P_PACKET_SIZE) return -1;

    /* temporary variables for network byte order */
    uint32_t magic_be;
    uint32_t seq_be;
    uint64_t ts_be;

    /* reads wire format fields */
    memcpy(&magic_be, buf, 4);
    pkt->type = buf[4];
    memcpy(&seq_be, buf + 5, 4);
    memcpy(&ts_be, buf + 9, 8);

    /* convert to host byte order */
    pkt->magic = ntohl(magic_be);
    pkt->seq = ntohl(seq_be);
    pkt->ts_us = be64toh(ts_be);

    /* If magic isn't the correct value, it's not belong to this program. */
    if (pkt->magic != P2P_MAGIC) return -1;

    return 0;
}

int p2p_send(int sock_fd, const struct sockaddr_in *dst, uint8_t type, uint32_t seq, uint64_t ts_us) {
    p2p_packet_t pkt = {
        P2P_MAGIC,
        type,
        seq,
        ts_us
    };

    uint8_t buf[P2P_PACKET_SIZE];

    int len = p2p_pack(&pkt, buf, sizeof(buf));
    if (len < 0) return -1;

    return (int)sendto(sock_fd, buf, (size_t)len, 0, (const struct sockaddr*)dst, sizeof(*dst));
}

int addr_equal(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

int hole_punch(int sock_fd, const struct sockaddr_in *peer_addr) {
    printf("\n[HOLE-PUNCH] Opening a hole in %s:%d address (max %d sec)...\n", inet_ntoa(peer_addr->sin_addr), ntohs(peer_addr->sin_port), PUNCH_TIMEOUT_MS / 1000);

    /* Hole punch start time */
    uint64_t start = now_ms();

    /* Holds the last time we sent PUNCH packet. 
     * It's 0 at default, we can send packet immediately. */
    uint64_t last_sent = 0;

    /* Sequence number for punch packets */
    uint32_t seq = 0;

    /* pollfd:
     * Indicates which file descriptor and which scenario we are waiting to poll function.
     *
     * POLLIN = Theres info to be read.
     * */

    struct pollfd pfd = {
        .fd = sock_fd,
        .events = POLLIN
    };

    while (!g_stop) {
        uint64_t t = now_ms();

        /* If timeout assume it's failure */
        if (t - start > PUNCH_TIMEOUT_MS) {
            fprintf(stderr, "[HOLE-PUNCH] Timeout - Other peer didn't send any packets.\n" 
                    "Possible Reason : Either one of the peer is behind SYMETRIC NAT.\n"
                    "This case can't be resolved without any relay/TURN server.\n");
            return -1;
        }

        /* Send package if PUNCH_INTERVAL_MS passed */
        if (t - last_sent >= PUNCH_INTERVAL_MS) {
            p2p_send(sock_fd, peer_addr, MSG_PUNCH, seq++, now_us());

            last_sent = t;
        }

        /*
         * Wait for packets to arrive at most POLL_SLICE_MS 
         *
         * poll return : 
         * > 0 : Case happened
         * = 0 : Timeout
         * < 0 : Error
         * */

        int pr = poll(&pfd, 1, POLL_SLICE_MS);

        /* If there's a data to read, take the packet */
        if (pr > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[MAX_BUFFER_SIZE];

            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);

            ssize_t n = recvfrom(sock_fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);

            /* n > 0:
             * Non-empty UDP packet came. */

            if (n > 0 && addr_equal(&from, peer_addr)) {
                p2p_packet_t pkt;

                if (p2p_unpack(buf, (size_t)n, &pkt) == 0) {
                    printf("[HOLE-PUNCH] Got package from other peer -> opened hole!\n");

                    /* To increase the chance for the target peer, receive the package we send 3 extra PUNCH packet. 
                     *
                     * This reduceses situations where both sides don't see success at the exact time.*/
                    for (int i = 0; i < 3; i++) {
                        p2p_send(sock_fd, peer_addr, MSG_PUNCH, seq++, now_us());

                        /* 50 ms = 50000 microseconds */
                        usleep(50 * 1000);
                    }

                    return 0;
                }
            }
        }
    }

    /* Failed return for CTRL+C */
    return -1;
}

void ping_loop(int sock_fd, const struct sockaddr_in *peer_addr) {
    /* Peer string to print */
    const char *peer_ip_str = inet_ntoa(peer_addr->sin_addr);

    /* Host byte order port to print */
    uint16_t peer_port = ntohs(peer_addr->sin_port);

    printf("\n[PING] Mutual ping started with %s:%d (Stop with CTRL+C)\n\n", peer_ip_str, peer_port);

    /* Sequence number for the next ping */
    uint32_t seq = 0;

    /* Waiting
     * 0 = No waiting pong
     * 1 = Waiting a pong
     */
    int waiting = 0;

    /* Sequence number for waiting pong */
    uint32_t waiting_seq = 0;

    /* Ping sent time in monotic. */
    uint64_t sent_at_us = 0;

    /* Start time for sending the first ping immediately */
    uint64_t next_send_ms = now_ms();

    /* Stats variables
     *
     * sent : Targets total ping count 
     * recvd : Successfull pong count to pings 
     * rtt : Round-Trip-Time; Time for packets go and come
     */
    long sent = 0;
    long recvd = 0;

    double rtt_min = -1;
    double rtt_max = -1;
    double rtt_sum = 0;

    struct pollfd pfd = {
        .fd = sock_fd,
        .events = POLLIN
    };

    while (!g_stop) {
        uint64_t t = now_ms();

        /* New ping rule:
         * - Not waiting for upcoming pong 
         * - Interval has came 
         */

        if (!waiting && t >= next_send_ms) {
            seq++;

            sent_at_us = now_us();

            p2p_send(sock_fd, peer_addr, MSG_PING, seq, sent_at_us);

            waiting = 1;
            waiting_seq = seq;
            sent++;
        }

        /* Pong is awaiting however if timeout passed out, we assume it's loss or timeout.
         *
         * sent_at_us / 1000 
         * Microseconds to milliseconds, for making it same as t to compare*/
        else if (waiting && (t - sent_at_us / 1000) > PING_TIMEOUT_MS) {
            printf("Request got timeout : seq=%u\n", waiting_seq);

            waiting = 0;

            /* Next ping will send after timeout by PING_INTERVAL_MS */
            next_send_ms = t + PING_INTERVAL_MS;
        }

        int pr = poll(&pfd, 1, POLL_SLICE_MS);

        if (pr > 0 && (pfd.revents & POLLIN)) {
            uint8_t buf[MAX_BUFFER_SIZE];

            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);

            ssize_t n = recvfrom(sock_fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);

            /* Only process;
             * - Not empty
             * - Came from the waiting peer
             * - Valid P2P formated packet */
            if (n > 0 && addr_equal(&from, peer_addr)) {
                p2p_packet_t pkt;

                if (p2p_unpack(buf, (size_t)n, &pkt) == 0) {
                    if (pkt.type == MSG_PING) {
                        /*
                         * Other peer sent PING
                         * 
                         * We sent pong back without changing their;
                         * - sequence number,
                         * - transmission timestamp 
                         *
                         * With this other peer can calculate their RTT with pkt.ts_us through now_us() 
                         *
                         * Both machines doesn't need a synchronized sync. 
                         * The creator and calculator of the timestamp are the same peer.*/
                        
                        p2p_send(sock_fd, peer_addr, MSG_PONG, pkt.seq, pkt.ts_us);

                        printf("[<-PING] Received %s:%d seq=%u, Sent PONG\n", peer_ip_str, peer_port, pkt.seq);
                    }
                    /* If incoming packet is PONG and it's our waiting sequence number we can calculate RTT */
                    else if (pkt.type == MSG_PONG && waiting && pkt.seq == waiting_seq) {
                        /* RTT Calculating:
                         * Current time - Ping transmission time.
                         *
                         * The result is in microseconds.
                         */
                        double rtt_ms = (double)(now_us() - pkt.ts_us) / 1000.0;

                        printf("[PONG<-] %s:%d: seq=%u time=%.2f ms\n", peer_ip_str, peer_port, pkt.seq, rtt_ms);

                        recvd++;

                        /* If it's first successfull RTT start minimum*/
                        if (rtt_min < 0 || rtt_ms < rtt_min) rtt_min = rtt_ms;

                        if (rtt_max < 0 || rtt_ms > rtt_max) rtt_max = rtt_ms;

                        rtt_sum += rtt_ms;

                        /* Got waiting pong; New ping can be sent */
                        waiting = 0;

                        next_send_ms = t + PING_INTERVAL_MS;
                    }

                    /* If the other peer exited, they send BYE */
                    else if (pkt.type == MSG_BYE) {
                        printf("\n[INFO] Other peer closed connection. (Got BYE)\n");

                        g_stop = 1;
                    }
                }
            }
        }
    }

    printf("\n --- %s:%d p2p ping stats --- \n", peer_ip_str, peer_port);

    /* Packet loss percentage 
     *
     * (sent - received) / sent * 100 
     * 
     * We are using a ternary opeartor to prevent dividing to 0*/
    double loss_pct = sent > 0 ? 100.0 * (double)(sent - recvd) / (double)sent : 0.0;

    /* %%%.1f inside printf :
     * - %% means print percentage symbol as normal.
     * - %.1f means write as floating point 
     *
     * Example result : "%0.0 lose" */
    printf("%ld packet sent, %ld packet got, %%%.1f lose\n", sent, recvd, loss_pct);

    if (recvd > 0) {
        printf("rtt min/avg/max = %.2f/%.2f/%.2f ms\n", rtt_min, rtt_sum / recvd, rtt_max);
    }

    /*
     * If program closed with CTRL+C inform other peer.
     * NOTE : Because udp isn't safe, there's no guarantee for the packet to arrive.
     * */
    p2p_send(sock_fd, peer_addr, MSG_BYE, seq, now_us());
}
