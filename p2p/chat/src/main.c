#include "./sup.h"

int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
            print_help_menu(argv[0]);
    }
    if (argc < 2)
        print_help_menu(argv[0]);

    int local_port = atoi(argv[1]);
    const char *stun_host = DEFAULT_STUN_HOST;
    int stun_port = DEFAULT_STUN_PORT, use_stun = 1;

    char *positional[8];
    int npos = 0;

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--no-stun") == 0)
            use_stun = 0;
        else if (strcmp(argv[i], "--stun-host") == 0 && i + 1 < argc) stun_host = argv[++i];
        else if (strcmp(argv[i], "--stun-port") == 0 && i + 1 < argc) stun_port = atoi(argv[++i]);
        else if (npos < 8) positional[npos++] = argv[i];
    }

    const char *peer_ip = NULL;
    int peer_port = -1;

    if (npos >= 2) {
        peer_ip = positional[0];
        peer_port = atoi(positional[1]);
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    signal(SIGINT, on_sigint);

    int sock_fd = 0;
    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
        perror("[FATAL] socket()");
        return EXIT_SUCCESS;
    }

    int reuse = 1;

    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in local_addr = {0};

    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons((uint16_t)local_port);

    if (bind(sock_fd, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("[FATAL] bind()");
        close(sock_fd);
        return EXIT_FAILURE;
    }

    socklen_t la_len = sizeof(local_addr);

    getsockname(sock_fd, (struct sockaddr *)&local_addr, &la_len);

    printf("Local UDP socket port : %d\n", ntohs(local_addr.sin_port));

    char public_ip[INET_ADDRSTRLEN] = {0};

    uint16_t public_port = 0;

    if (use_stun) {
        printf("Discovering public address through STUN (%s:%d)...\n", stun_host, stun_port);
        if (stun_discover(sock_fd, stun_host, (uint16_t)stun_port, public_ip, sizeof(public_ip), &public_port) < 0) {
            fprintf(stderr, "[FATAL] Stun discovery failed; UDP output to internet is maybe blocking. You can do LAN test with --no-stun\n");
            close(sock_fd);
            return EXIT_SUCCESS;
        }
    } else {
        strncpy(public_ip, "0.0.0.0", sizeof(public_ip) - 1);
        public_port = ntohs(local_addr.sin_port);
    }

    if (use_stun) {
        printf("\n\n");
        printf(" Your public address : %s:%d\n", public_ip, public_port);
        printf(" Send this address to opposite side");
    } else printf("Stun skipped. Send your local IP:PORT to opposite side\nExample (127.0.0.1:%d)\n", public_port);

    struct sockaddr_in peer_addr = {0};
    peer_addr.sin_family = AF_INET;

    if (!peer_ip) {
        char line[128];
        printf("\n\n");
        printf("Enter opposite sides public address (IP:PORT), or just leave it empty and press ENTER to exit: ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin) || line[0] == '\n') {
            printf("No peer address entered, exiting.\n");
            close(sock_fd);
            return EXIT_SUCCESS;
        }

        char ipbuf[64];
        int pport;

        if (sscanf(line, "%63s", ipbuf, &pport) != 2) {
            fprintf(stderr, "[ERR] Invalid format. Expected: IP PORT\n");
            close(sock_fd);
            return EXIT_FAILURE;
        }

        peer_ip = ipbuf;
        peer_port = pport;

    } else {
        if (inet_pton(AF_INET, peer_ip, &peer_addr.sin_addr) <= 0) {
            fprintf(stderr, "[ERR] Invalid IP address: %s\n", peer_ip);
            close(sock_fd);
            return EXIT_FAILURE;
        }
    }

    peer_addr.sin_port = htons((uint16_t)peer_port);

    int fl = fcntl(sock_fd, F_GETFL, 0);

    fcntl(sock_fd, F_SETFL, fl | O_NONBLOCK);

    if (hole_punch(sock_fd, &peer_addr) < 0) {
        close(sock_fd);
        return EXIT_FAILURE;
    }

    chat_loop(sock_fd, &peer_addr);

    close(sock_fd);

    printf("\nEnded.\n");
    return EXIT_SUCCESS;
}
