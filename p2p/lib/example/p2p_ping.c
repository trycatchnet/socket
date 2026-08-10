#include "../src/p2p.h"

static void print_usage(const char *prog) {
    fprintf(stderr, 
            "Usage: %s <local_port> [options] [peer_ip peer_port]\n\n"
            "Options:\n"
            "--no-stun      Skip stun, only for local tests.\n"
            "--stun-host HOST       Stun server (default: %s)\n"
            "--stun-port PORT       Stun port (default: %d)\n"
            "-h, --help     Print this message\n\n"
            "Examples:\n"
            "# 1) First learn your public IP:\n"
            "%s 55000\n\n"
            "# 2) Connect to public IP:PORT you got from other peer:\n"
            "%s 55000 203.0.113.9 41234\n\n"
            "# localhost test (WITHOUT STUN):\n"
            "%s 6000 --no-stun 127.0.0.1 6601\n",
            prog, DEFAULT_STUN_HOST, DEFAULT_STUN_PORT, prog, prog, prog);
}

/* Main program flow
 *
 * 1. Parse command line args.
 * 2. Create UDP socket.
 * 3. Bind socket with local_port that came from user.
 * 4. Learn public address with STUN.
 * 5. Get peer address from arg or terminal.
 * 6. Set socket as non-blocking.
 * 7. Do hole punching.
 * 8. Start ping/pong loop.
 * 9. Close socket and leave. 
 */
int main(int argc, char **argv) {
    /* -h / --help can be given at any position.
     * For that reason we scan all arguments */
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { 
            print_usage(argv[0]); return EXIT_SUCCESS; 
        }
    }

    /* at least one argument */
    if (argc < 2) {
        print_usage(argv[0]); return EXIT_FAILURE;
    }

    /* atoi: Converts decimal data written as string to integer. 
     *
     * NOTE : Atoi can return invalid strings to 0. For that to not happen in production, strtol() must be prefered.*/
    int local_port = atoi(argv[1]);

    const char *stun_host = DEFAULT_STUN_HOST;
    int stun_port = DEFAULT_STUN_PORT;

    /* If 1 use stun 
     * If 0 --no-stun is selected*/
    int use_stun = 1;

    /* Little array to store positional arguments that aren't options.
     *
     * Waited positional arguments:
     * positional[0] = peer IP
     * poistional[1] = peer port
     */

    char *positional[8];
    int npos = 0;

    /* argv[2] and after options can be peer info */
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--no-stun") == 0) {
            use_stun = 0;
        }
        /* After --stun-host hostname/IP will be excepted. 
         *
         * ++i : First increments i, then uses argv[i] value.
         * With this the option value will not get processed again. */
        else if (strcmp(argv[i], "--stun-host") == 0 && i + 1 < argc) stun_host = argv[++i];
        else if (strcmp(argv[i], "--stun-port") == 0 && i + 1 < argc) stun_port = atoi(argv[++i]);
        else if (npos < 8) positional[npos++] = argv[i];
    }

    /* Peer information will be assumed as non-given default. */
    const char *peer_ip = NULL;
    int peer_port = -1;

    /*
     * If theres at least two positional arguments;
     * - First is IP
     * - Second accepted as port
     */

    if (npos >= 2) {
        peer_ip = positional[0];
        peer_port = atoi(positional[1]);
    }

    /* Sets stdout to line-buffered mode.
     *
     * Normally if stdout will be redirected to pipe/file instead of terminal, output can wait in the buffer a long time.
     * _IOLBF flag targets flush after every line */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /* Start seed for rand()
     *
     * Time and PID gets XORed: 
     * Decreasing the possibilities of processes that started at the same time generate the same rand() seed.*/
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    /* will be called when CTRL+C pressed */
    signal(SIGINT, on_sigint);
    
    /* Creates UDP IPv4 socket
     *
     * AF_INET:
     * IPv4 address family
     *
     * SOCK_DGRAM:
     * Datagram, UDP socket.
     *
     * 0:
     * Select default protocol for this socket type and family: UDP */

    int sock_fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock_fd < 0) {
        perror("[FATAL] socket()");
        return EXIT_FAILURE;
    }

    /* SO_REUSEADDR:
     * Lets you use local address over and over again.
     *
     * Sometimes if you close the program and reopen it with a small time gap bind can have problems.
     * This reduceses the possibility of bind having problems.
     */
    int reuse = 1;
    
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    /* Getting local bind address ready, we are cleaning all fields. */
    struct sockaddr_in local_addr = {0};

    local_addr.sin_family = AF_INET;
    /* INADDR_ANY = 0.0.0.0
     *
     * Accept all incoming, valid IPV4 packets.*/
    local_addr.sin_addr.s_addr = INADDR_ANY;
    /* Converts user-given local port to network byte order. */
    local_addr.sin_port = htons((uint16_t)local_port);

    if (bind(sock_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("[FATAL] bind()");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    /* getsockname:
     * Gets local address and local port that socket is connected.
     *
     * As an example if port was 0 the operating systems port was be learned.
     */
    socklen_t la_len = sizeof(local_addr);

    getsockname(sock_fd, (struct sockaddr*)&local_addr, &la_len);

    printf("Local UDP socket port : %d\n", ntohs(local_addr.sin_port));

    /* INET_ADDRSTRLEN:
     * Maximum buffer size for IPv4 text.
     *
     * Longest IPv4 Text : "255.255.255.255" + '\0' 
     */
    char public_ip[INET_ADDRSTRLEN] = {0};

    /* Public port that we gonna to learn from stun */
    uint16_t public_port = 0;

    if (use_stun) {
        printf("Discovering public address through STUN (%s:%d)...\n", stun_host, stun_port);

        if (stun_discover(sock_fd, stun_host, (uint16_t)stun_port, public_ip, sizeof(public_ip), &public_port) < 0) {
            fprintf(stderr, "[FATAL] Stun discovery failed; UDP output to internet is maybe blocking. You can do lan test with --no-stun\n");
            close(sock_fd);
            return EXIT_FAILURE;
        } 
    } else {
        /* If there's no STUN used we can't know real public IP.
         * This is just a placeholder for output. 
         */
        strncpy(public_ip, "0.0.0.0", sizeof(public_ip) - 1);
        /* Local socket port is going to be used. */
        public_port = ntohs(local_addr.sin_port);
    }

    printf("\n====\n");

    if (use_stun) {
        printf(" Your public address : %s:%d\n", public_ip, public_port);
        printf(" Send this address to opposite side");
    } else {
        printf("Stun skipped. Send your local IP:PORT to opposite side\nExample (127.0.0.1:%d)\n", public_port);
    }

    printf("====\n");

    /* Other peers IPv4 socket address */
    struct sockaddr_in peer_addr = {0};

    peer_addr.sin_family = AF_INET;

    /* If peer didn't set with IP/port argument, get it from terminal interactively. */
    if (!peer_ip) {
        char line[128];

        printf("\nEnter opposite sides public address (IP PORT), or just leave it empty and press ENTER to exit.");
        fflush(stdout);
        
        /* fgets:
         * Reads user input safely.
         * If user only presses ENTER line[0] will become '\n'
         */
        if (!fgets(line, sizeof(line), stdin) || line[0] == '\n') {
            printf("No peer address entered, exiting.\n");
            close(sock_fd);
            return EXIT_SUCCESS;
        }

        /* Temporary buffer for IP text */
        char ipbuf[64];

        /* The input of users port */
        int pport;

        /* sscanf:
         * Tries to read through the line in turn targets to get an text and integer. 
         *
         * %63s:
         * Maximum of 63 characters to block overflowing for ipbuf.
         */
        if (sscanf(line, "%63s %d", ipbuf, &pport) != 2) {
            fprintf(stderr, "[ERR] Invalid format. Excepted: IP PORT\n");
            close(sock_fd);
            return EXIT_FAILURE;
        }

        /* ipbuf only will stay in memory through this if block.
         * However peer_ip points to it, in this particular usage peer_ip gets used with inet_pton so it wouldn't occur any problems.
         */
        
        peer_ip = ipbuf;
        peer_port = pport;

        /* Convert peer ip string to binary IPv4 format. */
        if (inet_pton(AF_INET, peer_ip, &peer_addr.sin_addr) <= 0) {
            fprintf(stderr, "[ERR] Invalid IP address: %s\n", peer_ip);
            close(sock_fd);
            return EXIT_FAILURE;
        }
    } else {
        /* If peer ip cames from command line arguments do the same IPv4 verification. */
        if (inet_pton(AF_INET, peer_ip, &peer_addr.sin_addr) <= 0) {
            fprintf(stderr, "[ERR] Invalid IP address: %s\n", peer_ip);
            close(sock_fd);
            return EXIT_FAILURE;
        }
    }

    /* Convert peer port to network byte order. */
    peer_addr.sin_port = htons((uint16_t)peer_port);

    /* Change socket to non-blocking mode.
     *
     * F_GETFL: 
     * Reads current file status flags. 
     *
     * O_NONBLOCK:
     * Let's calls like recvfrom() don't get blocked infinitely.
     *
     * This program doesn't waits package with directly blocking recvfrom it uses poll first.
     */
    int fl = fcntl(sock_fd, F_GETFL, 0);

    fcntl(sock_fd, F_SETFL, fl | O_NONBLOCK);

    /* First go through NAT hole punching.
     * If it's failed, there's no point for passing to ping.
     */

    if (hole_punch(sock_fd, &peer_addr) < 0) {
        close(sock_fd);
        return EXIT_FAILURE;
    }

    /* If UDP transmission got directly set, start ping/pong loop */
    ping_loop(sock_fd, &peer_addr);

    close(sock_fd);

    printf("\np2p_ping ended.\n");
    return EXIT_SUCCESS;
}
