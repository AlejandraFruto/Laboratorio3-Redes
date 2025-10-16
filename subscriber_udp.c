#include <arpa/inet.h>      // Provee funciones para manipular direcciones IP, como inet_pton() que convierte IPs de texto a binario.
#include <errno.h>          // Permite el manejo de errores a través de la variable 'errno'.
#include <netinet/in.h>     // Define la estructura 'sockaddr_in' y constantes para sockets de Internet (ej. AF_INET).
#include <stdbool.h>        // Define el tipo de dato booleano 'bool' y los valores 'true' y 'false'.
#include <stdio.h>          // Librería estándar de Entrada/Salida para funciones como printf() y fprintf().
#include <stdlib.h>         // Librería estándar que provee funciones para conversión de tipos (atoi) y salida del programa (exit).
#include <string.h>         // Provee funciones para la manipulación de cadenas de caracteres, como strlen() y snprintf().
#include <sys/socket.h>     // Contiene las definiciones principales para la API de sockets, como socket(), sendto() y recvfrom().
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

    // Enviar el mensaje de suscripción
    char sub_message[MAX_LINE];
    int n = snprintf(sub_message, sizeof(sub_message), "SUB %s", topic);
    if (sendto(sockfd, sub_message, (size_t)n, 0,
               (const struct sockaddr *)&broker_addr, sizeof(broker_addr)) < 0) {
        perror("sendto: no se pudo enviar la suscripción");
        close(sockfd);
        return 1;
    }

    printf("[subscriber] Solicitud de suscripción enviada para '%s'. Esperando mensajes... 📡\n", topic);

    // Bucle para recibir mensajes del broker
    char buffer[MAX_LINE];
    while (1) {
        ssize_t n_bytes = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
        if (n_bytes < 0) {
            perror("recvfrom");
            break; // Salir en caso de error
        }
        buffer[n_bytes] = '\0'; // Asegurar terminación null

        // Imprimir el mensaje recibido
        printf("🔔 [mensaje] %s\n", buffer);
        fflush(stdout); // Asegurar que el mensaje se imprima inmediatamente
    }

    printf("[subscriber] Terminando.\n");
    close(sockfd);
    return 0;
}