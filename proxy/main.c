#include "./parse.h"
#include "./util.h"

#include <arpa/inet.h>
#include <asm-generic/socket.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>

pthread_mutex_t lock;
cache_element *find(char *url);

int port_number = 8080;     // default
int proxy_sock_id;         // socket descriptor of proxy server
pthread_t tid[MAX_CLIENTS]; // array to store the thread ids of clients

/* if client request exceeds the MAX_CLIENTS this seamaphore
   puts the waiting threads to sleep and wakes them when traffic on queue
   decreases.

   sem_t cache_lock;
*/
sem_t seamaphore;

cache_element *head; // pointer to the cache
int cache_size = 0;  // denotes the current size of the cache

int sendErrorMessage(int socket, int status_code) {
    char str[1024], current_time[50];
    time_t now = time(0);
    struct tm data = *gmtime(&now);
    strftime(current_time, sizeof(current_time), "%a, %d %b %Y %H:%M:%S %Z",
             &data);

    switch (status_code) {
    case 400:
        snprintf(
            str, sizeof(str),
            "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: "
            "keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: "
            "VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad "
            "Request</TITLE></HEAD>\n<BODY><H1>400 Bad "
            "Request</H1>\n</BODY></HTML>",
            current_time);
        printf("400 Bad Request\n");

        send(socket, str, strlen(str), 0);
        break;
    case 403:
        snprintf(
            str, sizeof(str),
            "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: "
            "text/html\r\nConnection: keep-alive\r\nDate %s\r\n Server: "
            "VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 "
            "Forbidden</TITLE></HEAD>\n<BODY><H1>403 "
            "Forbidden</H1><br>Permission Denied\n</BODY></HTML>",
            current_time);
        printf("403 Forbidden\n");

        send(socket, str, strlen(str), 0);
        break;
    case 404:
        snprintf(
            str, sizeof(str),
            "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: "
            "text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: "
            "VaibhavN/1485\r\n\r\n<HTML><HEAD><TITLE>404 Not "
            "Found</TITLE></HEAD>\n<BODY><H1>404 Not "
            "Found</H1>\n</BODY></HTML>",
            current_time);
        printf("404 Not Found\n");

        send(socket, str, strlen(str), 0);
        break;
    case 500:
        snprintf(
            str, sizeof(str),
            "HTTP/1.1 500 Internal Server Error\r\nContent-Length: "
            "115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: "
            "%s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 "
            "Internal "
            "Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server "
            "Error</H1>\n</BODY></HTML>",
            current_time);
        printf("500 Internal Server Error\n");

        send(socket, str, strlen(str), 0);
        break;
    case 501:
        snprintf(str, sizeof(str),
                 "HTTP/1.1 501 Not Implemented\r\nContent-Length: "
                 "103\r\nConnection: "
                 "keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: "
                 "VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not "
                 "Implemented</TITLE></HEAD>\n<BODY><H1>501 Not "
                 "Implemented</H1>\n</BODY></HTML>",
                 current_time);
        printf("501 Not Implemented\n");

        send(socket, str, strlen(str), 0);
        break;
    case 505:
        snprintf(
            str, sizeof(str),
            "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: "
            "125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: "
            "%s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP "
            "Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version "
            "Not "
            "Supported</H1>\n</BODY></HTML>",
            current_time);
        printf("505 HTTP Version Not Supported\n");

        send(socket, str, strlen(str), 0);
        break;
    default:
        return -1;
    }

    return 1;
}

int connectRemoteServer(char *host_addr, int port_num) {
    // Creating Socket
    int remote_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (remote_socket < 0) {
        fprintf(stderr, "Error in Creating Socket.\n");
        return -1;
    }

    // Resolve host (handles both names and dotted IPs)
    struct addrinfo hints;
    struct addrinfo *res;
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port_num);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; // IPv4 for now
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host_addr, port_str, &hints, &res) != 0) {
        fprintf(stderr, "No such host exists.\n");
        close(remote_socket);
        return -1;
    }

    // Non-blocking connect with a timeout so that hosts blackholed by the
    // ISP/DNS don't leave the thread stuck for minutes
    int flags = fcntl(remote_socket, F_GETFL, 0);
    fcntl(remote_socket, F_SETFL, flags | O_NONBLOCK);

    if (connect(remote_socket, res->ai_addr, res->ai_addrlen) < 0) {
        if (errno != EINPROGRESS) {
            fprintf(stderr, "Error in connecting!\n");
            freeaddrinfo(res);
            close(remote_socket);
            return -1;
        }
        fd_set wfds;
        struct timeval tv = {10, 0};
        FD_ZERO(&wfds);
        FD_SET(remote_socket, &wfds);

        if (select(remote_socket + 1, NULL, &wfds, NULL, &tv) <= 0) {
            fprintf(stderr, "Connection timed out!\n");
            freeaddrinfo(res);
            close(remote_socket);
            return -1;
        }
        int err = 0;
        socklen_t elen = sizeof(err);
        if (getsockopt(remote_socket, SOL_SOCKET, SO_ERROR, &err, &elen) < 0 ||
            err != 0) {
            fprintf(stderr, "Error in connecting!\n");
            freeaddrinfo(res);
            close(remote_socket);
            return -1;
        }
    }

    fcntl(remote_socket, F_SETFL, flags); // restore blocking mode

    // Read/write timeout so an unresponsive remote can't hold a thread
    // (and therefore a semaphore slot) forever
    struct timeval tv = {15, 0};
    setsockopt(remote_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(remote_socket, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    freeaddrinfo(res);
    return remote_socket;
}

int handleRequest(int client_socket, struct parsed_request *request, char *buf, char *temp_req) {
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");

    size_t len = strlen(buf);

    if (ParsedHeader_set(request, "Connection", "close") < 0) {
        printf("Set header key not working\n");
    }
    // Always rewrite the Host header to the target server (with port).
    // If we forwarded the client's "Host: localhost:8080", the origin server
    // would reject or ignore the request.
    {
        char host_hdr[512];
        if (request->port != NULL)
            snprintf(host_hdr, sizeof(host_hdr), "%s:%s", request->host, request->port);
        else
            snprintf(host_hdr, sizeof(host_hdr), "%s", request->host);
        ParsedHeader_set(request, "Host", host_hdr);
    }
    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
        printf("unparse is failed\n");
    }

    int server_port = 80; // Default Remote Server Port
    if (request->port != NULL)
        server_port = atoi(request->port);

    int remote_socket_id = connectRemoteServer(request->host, server_port);

    if (remote_socket_id < 0) {
        free(temp_req);
        return -1;
    }

    int bytes_send = send(remote_socket_id, buf, strlen(buf), 0);

    if (bytes_send < 0) {
        fprintf(stderr, "Error in sending to remote.\n");
        close(remote_socket_id);
        free(temp_req);
        return -1;
    }

    bzero(buf, MAX_BYTES);
    bytes_send = recv(remote_socket_id, buf, MAX_BYTES - 1, 0);

    if (bytes_send <= 0) {
        // remote closed the connection or timed out without a response
        fprintf(stderr, "No response from remote.\n");
        close(remote_socket_id);
        free(temp_req);
        return -1;
    }

    while (bytes_send > 0) {
        if (send(client_socket, buf, bytes_send, 0) < 0) {
            perror("Error in sending data to client socket.\n");
            break;
        }

        bzero(buf, MAX_BYTES);
        bytes_send = recv(remote_socket_id, buf, MAX_BYTES - 1, 0);

        if (bytes_send < 0) {
            // remote timed out or the connection dropped mid-response
            fprintf(stderr, "Remote read timed out.\n");
            break;
        }
    }

    close(remote_socket_id);
    free(temp_req);

    printf("Done\n");

    return 0;
}

int checkHTTPVersion(char *msg) {
    int version = -1;
    if (strncmp(msg, "HTTP/1.1", 8) == 0) {
        version = 1;
    } else if (strncmp(msg, "HTTP/1.0", 8) == 0) {
        version = 1;
    } else
        version = -1;
    return version;
}

void *thread_fn(void *socket_new) {
    int p;
    int *t = (int *)(socket_new);
    int socket = *t;            // socket descriptor of the connected client
    int bytes_send_client, len; // bytes transferred

    // creating buffer of 4kb for a client
    char *buf = (char *)calloc(MAX_BYTES, sizeof(char)); 
    
    bzero(buf, MAX_BYTES);

    // receiving the request of the client by proxy server
    bytes_send_client = recv(socket, buf, MAX_BYTES, 0);

    while (bytes_send_client > 0) {
        len = strlen(buf);
        // loop until you find "\r\n\r\n" in the buffer
        if (strstr(buf, "\r\n\r\n") == NULL) {
            bytes_send_client = recv(socket, buf + len, MAX_BYTES - len, 0);
        } else break;
    }

    // buffer both store the http request sent by client
    char *temp_req = (char *)malloc(strlen(buf) * sizeof(char) + 10);
    
    for (int i = 0; i < strlen(buf); i++) temp_req[i] = buf[i];
    temp_req[strlen(buf)] = '\0';

    struct cache_element *temp = find(temp_req);

    if (temp != NULL) {
        // request found in cache, so sending the response to client from proxy's cache

        int size = temp->len / sizeof(char);
        int pos = 0;
        char res[MAX_BYTES];

        while (pos < size) {
            bzero(res, MAX_BYTES);

            for (int i = 0; i < MAX_BYTES; i++) {
                res[i] = temp->data[pos];
                pos++;
            }
            send(socket, res, MAX_BYTES, 0);
        }
        printf("Data retrived from the cache\n\n");
        printf("%s\n\n", res);
    } else if (bytes_send_client > 0) {
        len = strlen(buf);

        // Parsing the request
        struct parsed_request *req = ParsedRequest_create();

        /* ParsedRequest_parse returns 0 on success and -1 on failure.
           On success it stores parsed request in the request 
        */
        if (ParsedRequest_parse(req, buf, len) < 0) {
            fprintf(stderr, "Parsing failed\n");
            sendErrorMessage(socket, 400);
            free(temp_req);
        } else {
            bzero(buf, MAX_BYTES);
            if (!strcmp(req->method, "GET")) {
                if (req->host && req->path && (checkHTTPVersion(req->version) == 1)) {
                    // Never connect to the proxy itself, otherwise each
                    // self-connection spawns another thread until the
                    // semaphore pool is exhausted
                    int is_self = !strcmp(req->host, "localhost") ||
                                  !strcmp(req->host, "127.0.0.1") ||
                                  !strcmp(req->host, "0.0.0.0");
                    int req_port = (req->port != NULL) ? atoi(req->port) : 80;
                    if (is_self && req_port == port_number) {
                        sendErrorMessage(socket, 400);
                    } else {
                        // Handle GET Request
                        bytes_send_client = handleRequest(socket, req, buf, temp_req);
                        
                        if (bytes_send_client == -1) sendErrorMessage(socket, 500);
                    }
                } else {
                    sendErrorMessage(socket, 500);
                }
            } else printf("This code doesn't support any method other that GET\n");
        }
        ParsedRequest_destroy(req);
    }
    else if (bytes_send_client < 0) 
        perror("Error in receiving from client.\n");
    else if (bytes_send_client == 0)
        perror("Client disconnected!\n");

    shutdown(socket, SHUT_RDWR);
    close(socket);
    free(buf);
    sem_post(&seamaphore);
    sem_getvalue(&seamaphore, &p);

    printf("Seamaphore post value: %d\n", p);

    return NULL;
}

void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) return &(((struct sockaddr_in *)sa)->sin_addr);
    return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}

cache_element *find(char *url) {
    /* Checks for url in the cache if found returns pointer
       to the respective cache element or else returns NULL
    */
    cache_element *site = NULL;
    int temp_lock_val = pthread_mutex_lock(&lock);

    printf("Find cache lock acquired %d\n", temp_lock_val);
    if (head != NULL) {
        site = head;
        while (head != NULL) {
            if (!strcmp(site->url, url)) {
                printf("\nurl found\n");

                // update the time_track
                site->lru_time_track = time(NULL);
                break;
            }
            site = site->next;
        }
    } else printf("\nUrl not found\n");

    // sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Find cache lock unlocked %d\n", temp_lock_val);
    
    return site;
}

void removeCacheElement() {
    // If cache not empty searches for the node which has the least
    // lru_time_track and deletes it
    cache_element *p; // Prev pointer
    cache_element *q; // Next pointer
    cache_element *temp;

    // sem_wait(&cache_lock);
    
    int temp_lock_val = pthread_mutex_lock(&lock);
    if (head != NULL) {
        for (q = head, p = head, temp = head; q->next != NULL; q = q->next) {
            // iterate through entire cache and search for oldest time track
            if (((q->next)->lru_time_track) < (temp->lru_time_track)) {
                temp = q->next;
                p = q;
            }
        }

        if (temp == head) head = head->next; // handle the base case
        else p->next = temp->next;

        cache_size = cache_size - (temp->len) - sizeof(cache_element) - strlen(temp->url) - 1;

        free(temp->data);
        free(temp->url);
        free(temp);
    }
    temp_lock_val = pthread_mutex_unlock(&lock);

    printf("Remove cache lock unlocked %d\n", temp_lock_val);
}

int addCacheElement(char *data, int size, char *url) {
    // sem_wait(&cache_lock);
    printf("\nurl: %s\n", url);
    printf("\ndata: %s\n", data);

    int ret = 0;
    int temp_lock_val = pthread_mutex_lock(&lock);

    printf("Add cache lock acquired %d\n", temp_lock_val);
    
    // size of the new element which will be added to the cache
    int element_size = size + 1 + strlen(url) + sizeof(cache_element);

    if (element_size <= MAX_ELEMET_SIZE) {
        while (cache_size + element_size > MAX_SIZE) {
            // We keep removing elements from cache until we get enough space to add the element
            removeCacheElement();
        }

        // allocating memory for the response to be stored
        cache_element *element = (cache_element *)malloc(sizeof(cache_element));

        // allocating memory for the request to be stored in the cache elemnt (as a key) 
        element->data = (char *)malloc(10 + (strlen(url) * sizeof(char)));

        strcpy(element->url, url);
        element->lru_time_track = time(NULL);
        element->next = head;
        element->len = size;

        head = element;
        cache_size += element_size;
        
        // sem_post(&cache_lock)
        ret = 1;
    }

    temp_lock_val = pthread_mutex_unlock(&lock);

    printf("Add cache lock unlocked %d\n", temp_lock_val);
    
    free(url);
    free(data);
    
    printf("\ncache size: %d\n", cache_size);

    return ret;
}

int main(int argc, char *argv[]) {
    int client_sock_id, client_len;
    struct sockaddr_in server_addr, client_addr;
    
    sem_init(&seamaphore, 0, MAX_CLIENTS);
    pthread_mutex_init(&lock, NULL);

    if (argc == 2) {
        port_number = atoi(argv[1]);
        printf("Setting proxy server port: %d\n", port_number);
    } else if (argc > 2) {
        printf("Usage: %s [port]", argv[0]);
        exit(1);
    } else {
        printf("Setting proxy server port: %d[default]\n", port_number);
    }

    proxy_sock_id = socket(AF_INET, SOCK_STREAM, 0);
    if (proxy_sock_id < 0) {
        perror("Failed to create proxy socket.\n");
        exit(1);
    }

    int reuse = 1;

    if (setsockopt(proxy_sock_id, SOL_SOCKET, SO_REUSEADDR,
                   (const char *)&reuse, sizeof(reuse)) < 0)
        perror("setsockopt(SO_REUSEADDR) failed\n");
    
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_number);
    server_addr.sin_addr.s_addr = INADDR_ANY; // any avaible address assigned

    // Binding the socket
    if (bind(proxy_sock_id, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Port is not free\n");
        exit(1);
    }

    printf("Binding on port: %d\n", port_number);

    if (listen(proxy_sock_id, MAX_CLIENTS) < 0) {
        perror("Error while listening\n");
        exit(1);
    }

    int connected_sock_id[MAX_CLIENTS];
    int i = 0; // Iterator for thread_id(tid) and accepted client_socket for each thread

    for (;;) {
        // Limit the number of concurrent client threads to MAX_CLIENTS.
        // Each accepted connection consumes one slot; the worker thread
        // releases it with sem_post() when it is done.
        sem_wait(&seamaphore);

        int p;
        sem_getvalue(&seamaphore, &p);
        printf("seamaphore value: %d\n", p);

        bzero((char *)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);

        if ((client_sock_id = accept(proxy_sock_id, (struct sockaddr *)&client_addr,
                                     (socklen_t *)&client_len)) < 0) {
            fprintf(stderr, "Error in accepting connection.\n");
            sem_post(&seamaphore); // no thread was created, so release the slot
            continue;
        }

        connected_sock_id[i] = client_sock_id; // storing accepted client into array

        // Give up on a client that connects but never sends a request
        struct timeval ctv = {30, 0};
        setsockopt(client_sock_id, SOL_SOCKET, SO_RCVTIMEO, &ctv, sizeof(ctv));

        // getting IP address and port number of client
        struct sockaddr_in *client_pt = (struct sockaddr_in *)&client_addr;
        struct in_addr ip_addr = client_pt->sin_addr;
        char str[INET6_ADDRSTRLEN]; // containing IPv6 (soon)
        
        //inet_ntop(client_addr.sin_family, get_in_addr((struct sockaddr *)&client_pt), str, INET6_ADDRSTRLEN);
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN); // using just IPv4

        pthread_create(&tid[i], NULL, thread_fn, (void *)&connected_sock_id[i]);
        pthread_detach(tid[i]); // reclaim thread resources when it finishes

        // wrap around so we never write past the ends of tid[]/connected_sock_id[]
        i = (i + 1) % MAX_CLIENTS;
    }

    close(proxy_sock_id);

    return 0;
}
