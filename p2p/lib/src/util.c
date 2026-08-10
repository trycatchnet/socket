#define _GNU_SOURCE

#include "./util.h"

char DEFAULT_STUN_HOST[256] = "stun.l.google.com";
uint16_t DEFAULT_STUN_PORT = 19302;
int STUN_TIMEOUT_SEC = 5;
int STUN_MAX_ATTEMPTS = 3;

volatile sig_atomic_t g_stop = 0;

int resolve_hostname(const char *hostname, struct sockaddr_in *addr) {
    /* I WON'T WRITE ANY EXPLANATION FOR THIS SECTION, BECAUSE THIS SECTION IS REALLY
    MUCH EASIER THAN THE OTHER SECTIONS */ 
    struct addrinfo hints = {0}, *result, *rp;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    int ret;
    if ((ret = getaddrinfo(hostname, NULL, &hints, &result)) != 0) {
        fprintf(stderr, "[ERR] getaddrinfo: %s\n", gai_strerror(ret));
        return -1;
    }
    for (rp = result; rp != NULL; rp = rp->ai_next) {
        if (rp->ai_family == AF_INET) {
            memcpy(addr, rp->ai_addr, sizeof(*addr));
            freeaddrinfo(result);
            return 0;
        }
    }
    freeaddrinfo(result);
    return -1;
}

uint64_t now_us(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

uint64_t now_ms(void) {
    return now_us() / 1000ULL;
}

void on_sigint(int sig) {
    (void)sig; /* not using any parameters */
    g_stop = 1;
}
