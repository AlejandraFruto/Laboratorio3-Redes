// Suscriptor TCP: se conecta al broker y puede suscribirse a varios temas.
// Envia una línea "SUB <tema>" por cada argumento recibido.
//
// Compilación: gcc -Wall -Wextra -O2 -o subscriber_tcp subscriber_tcp.c
// Uso:         ./subscriber_tcp <host> <puerto> <tema1> [<tema2> ...]
// Ejemplo:     ./subscriber_tcp 127.0.0.1 5555 "Partido_AvsB" "Partido_CvsD"

#include <arpa/inet.h>      // Provee funciones para manipular direcciones IP, como inet_ntop() que convierte IPs de binario a texto.
#include <errno.h>          // Permite el manejo de errores a través de la variable 'errno' y constantes como EINTR.
#include <netinet/in.h>     // Define la estructura 'sockaddr_in' y constantes necesarias para la programación de sockets de Internet.
#include <stdbool.h>        // Define el tipo de dato booleano 'bool' y los valores 'true' y 'false'.
#include <stdio.h>          // Librería estándar de Entrada/Salida para funciones como printf(), fprintf() y sscanf().
#include <stdlib.h>         // Librería estándar que provee funciones de gestión de memoria (calloc, free) y conversión de tipos (atoi).
#include <string.h>         // Provee funciones para la manipulación de cadenas de caracteres, como strcmp(), strncpy() y strlen().
#include <sys/socket.h>     // Contiene las definiciones y estructuras principales para la API de sockets (socket(), bind(), sendto(), recvfrom()).
#include <unistd.h>         // Provee acceso a la API del sistema operativo POSIX, incluyendo la función close() para cerrar descriptores de archivo.


#define MAX_LINE 4096

// Lee una línea terminada en '\n'; 1: ok, 0: peer cerró, -1: error.
static int read_line(int fd, char *out, size_t maxlen) {
    size_t i = 0;
    while (i + 1 < maxlen) {
        char c;
        ssize_t n = recv(fd, &c, 1, 0);
        if (n == 0) return 0;          // conexión cerrada
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (c == '\n') {
            out[i] = '\0';
            return 1;
        }
        out[i++] = c;
    }
    out[maxlen - 1] = '\0';
    return 1;
}

// Envío confiable de buffers.
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
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <host> <puerto> <tema1> [<tema2> ...]\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);

    // Crear socket y conectar al broker.
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

    // Enviar una línea SUB por cada tema (permite múltiples suscripciones).
    for (int i = 3; i < argc; i++) {
        char first[MAX_LINE];
        int n = snprintf(first, sizeof(first), "SUB %s\n", argv[i]);
        if (send_all(fd, first, (size_t)n) < 0) {
            perror("send");
            close(fd);
            return 1;
        }
        printf("[subscriber] Suscrito a '%s'\n", argv[i]);
    }

    printf("[subscriber] Esperando mensajes...\n");

    // Bucle de lectura de mensajes reenviados por el broker.
    char line[MAX_LINE];
    while (1) {
        int r = read_line(fd, line, sizeof(line));
        if (r <= 0) break;                 // desconexión o error
        printf("[mensaje] %s\n", line);    // formato "<tema>: <texto>"
        fflush(stdout);
    }

    printf("[subscriber] Conexión cerrada.\n");
    close(fd);
    return 0;
}

