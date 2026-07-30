#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>

#define PORT "80"
#define MAXSIZE 999999

int main(int argc, char** argv) {
	if (argc != 2) {
		printf("./app [address]\n");
		exit(EXIT_SUCCESS);
	}
	
	struct addrinfo hints, *p;
  int sockfd, byte_count, rv;
  char buf[MAXSIZE];
  
  memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC; // expect IPv4, IPv6
	hints.ai_socktype = SOCK_STREAM;

	if ((rv = getaddrinfo(argv[1], PORT, &hints, &p)) != 0) {
		perror("getaddrinfo");
		exit(EXIT_FAILURE);
	}

	if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	printf("\033[33m'%s' Connecting...\033[0m\n", argv[1]);

	if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
		perror("connect");
		exit(EXIT_FAILURE);
	}

	printf("\033[32m'%s' Connected!\033[0m\n", argv[1]);

	char header[256];
	snprintf(header, sizeof(header), "GET /index.html HTTP/1.1\r\nHost: %s\r\n\r\n", argv[1]);
	
  /* OTHER OPTION TO MIX STRINGS(but snprintf() is easier) 
  strcat(header, argv[1]);
  size_t lenght = strlen(header);
	header[lenght + 1] = '\n';
	*/
  printf("\n%s\n\n", header);

  // not working sizeof(header) i'm gonna change to strlen(header)
	if (send(sockfd, header, strlen(header), 0) == -1) {
		perror("send");
		exit(EXIT_FAILURE);
	}

	printf("GET Sent...\n");

  memset(buf, 0, sizeof(buf));

	if ((byte_count = recv(sockfd, buf, sizeof(buf)-1, 0)) == -1) {
		perror("recv");
		exit(EXIT_FAILURE);
	}

	printf("\n------------------------------\n"
	        "sockfd: %d\nrv: %d\nrecv()'d %d bytes of data in buf\n"
	        "------------------------------\n\n",
	        sockfd, rv, byte_count);
	printf("\n\033[35m%s\033[0m\n", buf);

  close(sockfd);

	return 0;
}
