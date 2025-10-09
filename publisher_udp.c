// con el formato "PUB <tema> <mensaje>".
//
// Compilación: gcc -Wall -Wextra -O2 -o publisher_udp publisher_udp.c
// Uso:         ./publisher_udp <host> <puerto> "<tema>"
// Ejemplo:     ./publisher_udp 127.0.0.1 8080 "Partido_AvsB"

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

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <host> <puerto> <tema>\n", argv[0]);
        return 1;
    }
    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *topic = argv[3];

    // Crear socket UDP
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Configurar la dirección del broker
    struct sockaddr_in broker_addr = {0};
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &broker_addr.sin_addr) <= 0) {
        perror("inet_pton: Dirección de host inválida");
        close(sockfd);
        return 1;
    }

    printf("[publisher] Publicando en el tema '%s'. Escribe mensajes y presiona Enter.\n", topic);
    printf("            Presiona Ctrl+D para salir.\n");

    char user_input[MAX_LINE];
    // Bucle para leer mensajes del usuario desde la entrada estándar
    while (fgets(user_input, sizeof(user_input), stdin)) {
        // Quitar el salto de línea final de fgets
        size_t len = strlen(user_input);
        if (len > 0 && user_input[len - 1] == '\n') {
            user_input[len - 1] = '\0';
        }
        
        // Si el usuario no escribe nada, no enviar
        if(strlen(user_input) == 0) continue;

        // Construir el mensaje final en el formato "PUB <tema> <mensaje>"
        char final_message[MAX_LINE];
        int n = snprintf(final_message, sizeof(final_message), "PUB %s %s", topic, user_input);
        if (n < 0 || (size_t)n >= sizeof(final_message)) {
            fprintf(stderr, "Error: el mensaje es demasiado largo.\n");
            continue;
        }

        // Enviar el datagrama al broker
        if (sendto(sockfd, final_message, strlen(final_message), 0,
                   (const struct sockaddr *)&broker_addr, sizeof(broker_addr)) < 0) {
            perror("sendto");
            break; // Salir del bucle si hay un error de envío
        }
    }

    printf("\n[publisher] Terminando.\n");
    close(sockfd);
    return 0;
}