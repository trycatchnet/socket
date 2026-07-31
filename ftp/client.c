#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

// tbh this file is AI-generated (not server.c i wrote it myself with p0unter)
// the reason i got this file from AI is that i didn't had time and i was tired.

#define PORT 3131
#define BUFFER_SIZE 4096

void send_file_to_server(int sock, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        perror("Dosya acilamadi");
        return;
    }

    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    uint32_t net_size = htonl((uint32_t)size);
    if (send(sock, &net_size, sizeof(net_size), 0) != sizeof(net_size)) {
        perror("Can't send size");
        fclose(fp);
        return;
    }

    char buffer[BUFFER_SIZE];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(sock, buffer, bytes, 0);
    }
    fclose(fp);
    printf("Dosya sunucuya gonderildi.\n");
}

void receive_file_from_server(int sock, const char *path) {
    uint32_t net_size;
    if (recv(sock, &net_size, sizeof(net_size), 0) != sizeof(net_size)) {
        perror("Boyut okunamadi");
        return;
    }
    long size = ntohl(net_size);
    printf("File size: %ld byte", size);

    if (size <= 0) { printf("Invalid file size.\n"); return; }

    FILE *fp = fopen(path, "wb");
    if (!fp) {
        perror("Dosya olusturulamadi");
        return;
    }

    char buffer[BUFFER_SIZE];
    long remaining = size;
    while (remaining > 0) {
        int to_read = (remaining > BUFFER_SIZE) ? BUFFER_SIZE : remaining;
        int n = recv(sock, buffer, to_read, 0);
        if (n <= 0) { printf("Failed to get data"); break; }
        fwrite(buffer, 1, n, fp);
        remaining -= n;
    }
    fclose(fp);
    printf("Dosya alindi (ancak boyut hatali oldugu icin dosya bozuk olabilir).\n");
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Kullanim: %s <sunucu_ip>\n", argv[0]);
        return 1;
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket olusturulamadi");
        return 1;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    if (inet_pton(AF_INET, argv[1], &addr.sin_addr) <= 0) {
        perror("Gecersiz IP adresi");
        close(sock);
        return 1;
    }

    if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Baglanti basarisiz");
        close(sock);
        return 1;
    }

    char buffer[1024];
    int n;

    n = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
    }

    char option[10];
    printf("Secim (R/S): ");
    fgets(option, sizeof(option), stdin);
    option[strcspn(option, "\r\n")] = '\0';  
    send(sock, option, strlen(option), 0);
    send(sock, "\n", 1, 0);  

    n = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (n > 0) {
        buffer[n] = '\0';
        printf("%s", buffer);
    }

    char path[256];
    fgets(path, sizeof(path), stdin);
    path[strcspn(path, "\r\n")] = '\0';
    send(sock, path, strlen(path), 0);
    send(sock, "\n", 1, 0);

    if (strcmp(option, "S") == 0) {
        send_file_to_server(sock, path);
    } else if (strcmp(option, "R") == 0) {
        receive_file_from_server(sock, path);
    } else {
        printf("Gecersiz secenek.\n");
    }

    close(sock);
    return 0;
}
