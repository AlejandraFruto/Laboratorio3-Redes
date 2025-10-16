#include <arpa/inet.h>      // Provee funciones para manipular direcciones IP, como inet_pton() que convierte IPs de texto a binario.
#include <errno.h>          // Permite el manejo de errores a través de la variable 'errno'.
#include <netinet/in.h>     // Define la estructura 'sockaddr_in' y constantes para sockets de Internet (ej. AF_INET).
#include <stdbool.h>        // Define el tipo de dato booleano 'bool' y los valores 'true' y 'false'.
#include <stdio.h>          // Librería estándar de Entrada/Salida para funciones como printf(), fprintf() y fgets().
#include <stdlib.h>         // Librería estándar que provee funciones para conversión de tipos (atoi) y salida del programa (exit).
#include <string.h>         // Provee funciones para la manipulación de cadenas de caracteres, como strlen() y snprintf().
#include <sys/socket.h>     // Contiene las definiciones principales para la API de sockets, como la función socket() y sendto().
#include <unistd.h>         // Provee acceso a la API del sistema operativo POSIX, incluyendo la función close() para cerrar el socket.

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