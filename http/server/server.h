#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define PORT "8080"
#define BACKLOG 10
#define MAXREAD 1024

void http400(int fd) {
  const char *http400 = "HTTP/1.0 400 Bad Request\r\nConnection: close\r\nContent-Length: 15\r\n\r\n400 Bad Request";
  send(fd, http400, strlen(http400), 0);
  close(fd);
}

void http404(int fd) {
  const char *http404 = "HTTP/1.0 404 Not Found\r\nConnection: close\r\nContent-Length: 13\r\n\r\n404 Not Found";
  send(fd, http404, strlen(http404), 0);
  close(fd);
}

void http418(int fd) {
  const char *http418 = "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 25\r\n\r\n500 Internal Server Error";
  send(fd, http418, strlen(http418), 0);
  close(fd);
}

void http500(int fd) {
  const char *http418 = "HTTP/1.0 500 Internal Server Error\r\nConnection: close\r\nContent-Length: 25\r\n\r\n500 Internal Server Error";
  send(fd, http418, strlen(http418), 0);
  close(fd);
}

void *http200(int fd, size_t contentSize, char *contentType) {
  char response[1024];
  sprintf(response, "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: %u\r\nContent-Type: %s\r\n\r\n", contentSize, contentType);
  send(fd, response, strlen(response), 0);
}

void sendContent(int fd, FILE *fp) {
  char content[MAXREAD];
  int readSize = fread(content, 1, MAXREAD, fp);
  while (readSize > 0) {
    send(fd, content, readSize, 0);
    readSize = fread(content, 1, MAXREAD, fp);
  }
}

char * getContentType(char *path){
  char *dotType = strrchr(path, '.');
  if (dotType) {
    if (strcmp(dotType, ".css") == 0) return "text/css";
    if (strcmp(dotType, ".csv") == 0) return "text/csv";
    if (strcmp(dotType, ".gif") == 0) return "image/gif";
    if (strcmp(dotType, ".htm") == 0) return "text/html";
    if (strcmp(dotType, ".html") == 0) return "text/html";
    if (strcmp(dotType, ".ico") == 0) return "image/x-icon";
    if (strcmp(dotType, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(dotType, ".jpg") == 0) return "image/jpeg";
    if (strcmp(dotType, ".js") == 0) return "application/javascript";
    if (strcmp(dotType, ".json") == 0) return "application/json";
    if (strcmp(dotType, ".png") == 0) return "image/png";
    if (strcmp(dotType, ".pdf") == 0) return "application/pdf";
    if (strcmp(dotType, ".svg") == 0) return "image/svg+xml";
    if (strcmp(dotType, ".txt") == 0) return "text/plain";
  }

  return "application/octet-stream";
}

#endif // SERVER_H
