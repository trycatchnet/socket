#include "server.h"

void *get_in_addr(struct sockaddr *sa);

int main() {
  int sockfd, newfd, rv;
  struct addrinfo hints, *servinfo, *p;
  struct sockaddr_storage their_addr;
  socklen_t sinsize;
  char s[INET6_ADDRSTRLEN];

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {
    perror("getaddrinfo");
    exit(EXIT_FAILURE);
  }

  for (p = servinfo; p != NULL; p = p->ai_next) {
    if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
      perror("socket");
      exit(EXIT_FAILURE);
    }

    if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
      perror("bind");
      exit(EXIT_FAILURE);
    }

    if (listen(sockfd, BACKLOG) == -1) {
      perror("listen");
      exit(EXIT_FAILURE);
    }
    break;
  }

  freeaddrinfo(servinfo);

  fd_set master;
  FD_ZERO(&master);
  FD_SET(sockfd, &master);
  int maxSocket = sockfd;

  printf("Waiting for connections...\n");

  while (1) {
    sinsize = sizeof(their_addr);
    
    if ((newfd = accept(sockfd, (struct sockaddr *)&their_addr, &sinsize)) == -1) {
      perror("accept");
      exit(EXIT_FAILURE);
    }

    char addrstr[100];
    if ((getnameinfo((struct sockaddr *)&their_addr, sinsize, addrstr, sizeof(addrstr), 0, 0, NI_NUMERICHOST)) != 0) {
      perror("getnameinfo");
      exit(EXIT_FAILURE);
    }
    
    printf("Connection from: %s\n", addrstr);

    if (!fork()) {
      while (1) {
        close(sockfd);

        char httpRequest[2048];
        int bytes_recieved;

        if ((bytes_recieved = recv(newfd, httpRequest, sizeof(httpRequest), 0)) == -1) {
          perror("recv");
          exit(EXIT_FAILURE);
        }

        if (bytes_recieved < 1) {
          close(sockfd);
          exit(EXIT_FAILURE);
        }

        char* request = strtok(httpRequest, "\n");

        if (strncmp("GET /", httpRequest, 5) == 0) {
          char* unParsedPath = httpRequest + 4;

          char *path = strtok(unParsedPath, " ");
          printf("%s", path);

          if (strstr(path, "..")) {
            http400(newfd);
            exit(EXIT_FAILURE);
          }

          if (strlen(path) > 100) {
            http400(newfd);
            exit(EXIT_FAILURE);
          }

          if (strcmp(path, "/") == 0) {
            path = "./index.html";
          }

          //if (strcmp(path, "/test")) {
          //  http418(newfd);
          //}

          FILE* fp = fopen(path, "rb");

          if (!fp) {
            http404(newfd);
            exit(EXIT_FAILURE);
          }

          if (fseek(fp, 0L, SEEK_END)) {
            http500(newfd);
            exit(EXIT_FAILURE);
          }

          size_t fileLenght = ftell(fp);
          char *contentType = getContentType(path);
          rewind(fp);

          http200(newfd, fileLenght, contentType);
          sendContent(newfd, fp);

          close(newfd);
          exit(EXIT_SUCCESS);
        } else {
          http404(newfd);
          exit(EXIT_SUCCESS);
        }
      }
    }
  }  

  close(sockfd);
  return 0;  
}

void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET) return &(((struct sockaddr_in *)sa)->sin_addr);
  return &(((struct sockaddr_in6 *)sa)->sin6_addr);
}
