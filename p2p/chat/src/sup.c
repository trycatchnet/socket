#include "sup.h"

void print_help_menu(const char *prog) {
    fprintf(stderr, 
            "Usage: %s <local_port> [options] [peer_ip_port]\n\n"
            "Options:\n"
            " --no-stun\t\tSkip stun, only for local tests.\n"
            " --stun-host HOST\tStun server (default: %s)\n"
            " --stun-port PORT\tStun port (default: %d)\n"
            " -h, --help\t\tPrint this message\n\n"
            "Once connected, just type a message and press ENTER to chat.\n"
            "Ping keeps checking the connection in the background.\n"
            "Type /quit or press CTRL+C to leave.\n\n", prog, DEFAULT_STUN_HOST, DEFAULT_STUN_PORT);

    exit(EXIT_SUCCESS);
}
