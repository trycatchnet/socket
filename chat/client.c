#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PORT "3131"
#define MAXSIZE 256

void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) {
    return &(((struct sockaddr_in*)sa)->sin_addr);
  }

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    printf("Not acceptible usage!\n");
    exit(1);
  }
  
  int sockfd, rv, numbytes;
  struct addrinfo hints, *servinfo, *p;
  socklen_t sinsize;
  char buf[MAXSIZE], s[INET6_ADDRSTRLEN];

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  
  if ((rv = getaddrinfo(argv[1], PORT, &hints, &servinfo)) != 0) {
    perror("getaddrinfo : ");
    exit(1);
  }

  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket : ");
      continue;
    }

    inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof(s));

    if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      perror("connect : ");
      close(sockfd);
      continue;
    }

    break;
  }

  if (p == NULL) {
    fprintf(stderr, "p == NULL");
    return 2;
  }

  printf("connected to %s\n", s);

  inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), s, sizeof(s));

  freeaddrinfo(servinfo);

  if ((numbytes = recv(sockfd, buf, MAXSIZE-1, 0)) == -1) {
    perror("recv : ");
    exit(1);
  }

  buf[numbytes] = '\0';
  
  printf("RECV: %s", buf);

  close(sockfd);

  return 0;
}
