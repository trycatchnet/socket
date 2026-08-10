#ifndef UTIL_H
#define UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>
#include <stddef.h>

#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
/*
* htobe64: Converts a 64-bit number in host byte order to big-endian byte order.
* be64toh: Converts a 64-bit number in big-endian format to the machine's native byte order.
*/
#include <endian.h>

/*
* In some libc/compilation combinations, this macro
* may be required to declare endian functions
*/
#ifndef __USE_MISC
#define __USE_MISC
#endif

extern char     DEFAULT_STUN_HOST[256];
extern uint16_t DEFAULT_STUN_PORT;
extern int      STUN_TIMEOUT_SEC;
extern int      STUN_MAX_ATTEMPTS;

#define MAX_BUFFER_SIZE   1500
#define PUNCH_INTERVAL_MS 300
#define PUNCH_TIMEOUT_MS  30000
#define PING_INTERVAL_MS  1000
#define PING_TIMEOUT_MS   2000
/* The maximum time to wait for a single poll() call. */
#define POLL_SLICE_MS     100
/*
*   P2P_MAGIC :
*   0x50 = P
*   0x32 = 2
 *  0x50 = P
 *  0x43 = C
 * */
#define P2P_MAGIC 0x50325043u
/* magic -> 4 bytes, type -> 1 byte, seq -> 4 bytes, ts_us -> 8 bytes
 * if you get it all together it's 17 bytes */
#define P2P_PACKET_SIZE 17

/* Global stop flag for CTRL+C
 *
 * volatile :
 * Tells the compiler, this variable can be changed by a signal handler out of the program flow.
 *
 * sig_atomic_t :
 * Compatible POSIX/C type for atomic access to signal handler.*/
extern volatile sig_atomic_t g_stop;

/*
* Resolve a hostname to IPv4
*/
int resolve_hostname(const char *hostname, struct sockaddr_in *addr);

/* CLOCK_MONOTONIC based microsecond converter.
 * It converts the current time into microseconds
 *
 * CLOCK_MONOTONIC:
 * - While the program is running, goes back and forth.
 * - If the system or the user changes system clock, it wouldn't get borked.
 * - It is more safe than CLOCK_REALTIME for calculating RTT, timeout and intervals. 
 *
 * ts_us :
 * ts -> timestamp
 * us -> microseconds (the u comes from mu in greek)
 */
uint64_t now_us(void);

/* Returns the same time as milliseconds
 *
 * now_us returns microseconds so we can divide the result to 1000 for milliseconds.
 * 1 microseconds = 1000 milliseconds */
uint64_t now_ms(void);

/* Executed when SIGINT signal cames.
 * Pressing CTRL+C on a terminal generates a SIGINT signal. */
void on_sigint(int sig);

#endif // !UTIL_H
