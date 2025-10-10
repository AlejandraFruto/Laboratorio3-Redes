// publisher_tcp.c
// Publisher TCP: conecta al broker, envía "PUB <tema>"
// y luego lee líneas desde stdin. Cada línea debe empezar con "MSG ".
// Para ayudar, si escribes sin "MSG ", el programa lo añadirá automáticamente.
//
// Compilación: gcc -Wall -Wextra -O2 -o publisher_tcp publisher_tcp.c
// Uso:         ./publisher_tcp <host> <puerto> "<tema>"
// Ejemplo:     ./publisher_tcp 127.0.0.1 5555 "Partido_AvsB"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define MAX_LINE 4096

static ssize_t send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) return 0;
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <host> <puerto> <tema>\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *topic = argv[3];

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) <= 0) {
        perror("inet_pton");
        return 1;
    }
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        return 1;
    }

    char first[MAX_LINE];
    int n = snprintf(first, sizeof(first), "PUB %s\n", topic);
    if (send_all(fd, first, (size_t)n) < 0) { perror("send"); close(fd); return 1; }

    printf("[publisher] Conectado. Escribe mensajes.\n");
   

    char buf[MAX_LINE];
    while (fgets(buf, sizeof(buf), stdin)) {
        // Quitar '\n'
        size_t len = strlen(buf);
        if (len && buf[len-1] == '\n') buf[len-1] = '\0';

        char line[MAX_LINE];
        if (strncmp(buf, "MSG ", 4) == 0) {
            n = snprintf(line, sizeof(line), "%s\n", buf);
        } else {
            n = snprintf(line, sizeof(line), "MSG %s\n", buf);
        }
        if (send_all(fd, line, (size_t)n) < 0) { perror("send"); break; }
    }

    close(fd);
    return 0;
}

