
struct parsed_request;

#ifndef UTIL_H
#define UTIL_H

#include <errno.h>
#include <pthread.h>
#include <semaphore.h> // for file descriptor
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <stdarg.h>

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_BYTES 4096
#define MAX_CLIENTS 400
#define MAX_SIZE 200 * (1 << 200)      // size of cache
#define MAX_ELEMET_SIZE 10 * (1 << 20) // max size of an element in cache

typedef struct cache_element cache_element;
struct cache_element {
    char *data;            // data stores response
    int len;               // length of data (sizeof(data))
    char *url;             // url stores the request
    time_t lru_time_track; // lru_time_track stores the lastest time the element
                           // is accesed
    cache_element *next;
};

int addCacheElement(char *data, int size, char *url);
void removeCacheElement();
int sendErrorMessage(int socket, int status_code);
int connectRemoteServer(char *host_addr, int port_num);
int handleRequest(int client_socket, struct parsed_request *request, char *buf, char *temp_req);
int checkHTTPVersion(char *msg);
void *thread_fn(void *socket_new);
void *get_in_addr(struct sockaddr *sa);

#endif // !UTIL_H
