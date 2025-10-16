// Publisher TCP: conecta al broker, envía "PUB <tema>" y luego publica líneas.
// Si el usuario no escribe "MSG ", el programa lo antepone automáticamente.
//
// Compilación: gcc -Wall -Wextra -O2 -o publisher_tcp publisher_tcp.c
// Uso:         ./publisher_tcp <host> <puerto> "<tema>"
// Ejemplo:     ./publisher_tcp 127.0.0.1 5555 "Partido_AvsB"

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

// Envía todo el buffer (maneja envíos parciales e interrupciones).
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

    // Crear socket y conectarse al broker.
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

    // Anunciar rol/tema al broker.
    char first[MAX_LINE];
    int n = snprintf(first, sizeof(first), "PUB %s\n", topic);
    if (send_all(fd, first, (size_t)n) < 0) { perror("send"); close(fd); return 1; }

    printf("[publisher] Conectado. Escribe mensajes.\n");

    // Leer desde stdin y enviar como "MSG <texto>\n".
    char buf[MAX_LINE];
    while (fgets(buf, sizeof(buf), stdin)) {
        // Quitar '\n' final si viene
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

