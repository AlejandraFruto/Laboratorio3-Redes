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
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <host> <puerto> <tema1> [<tema2> ...]\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);

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

    // Enviar solicitudes de suscripción para cada tópico recibido por línea de comandos
    for (int i = 3; i < argc; i++) {
        char sub_message[MAX_LINE];
        // snprintf() construye el comando SUB de forma segura
        int n = snprintf(sub_message, sizeof(sub_message), "SUB %s", argv[i]);

        // sendto() envía un datagrama UDP al broker
        // - sockfd: descriptor del socket UDP
        // - sub_message: buffer con el mensaje SUB
        // - n: longitud del mensaje
        // - 0: flags (no se usan)
        // - broker_addr: dirección del broker
        // - sizeof(): tamaño de la estructura sockaddr_in
        if (sendto(sockfd, sub_message, (size_t)n, 0,
                   (const struct sockaddr *)&broker_addr, sizeof(broker_addr)) < 0) {
            perror("sendto: no se pudo enviar la suscripción");
            close(sockfd);
            return 1;
        }

        // Notificamos al usuario que se envió la suscripción a cada tema
        printf("[subscriber] Solicitud de suscripción enviada para '%s'.\n", argv[i]);
    }

    printf("[subscriber] Esperando mensajes... 📡\n");

    // Bucle para recibir mensajes del broker
    char buffer[MAX_LINE];
    while (1) {
        // recvfrom() bloquea hasta que llega un datagrama UDP
        // - sockfd: socket por el que recibimos
        // - buffer: lugar donde se almacenará el mensaje
        // - sizeof(buffer)-1: tamaño máximo de recepción
        // - 0: flags
        // - NULL,NULL: no necesitamos saber quién lo envió (el broker siempre es el mismo)
        ssize_t n_bytes = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, NULL, NULL);
        if (n_bytes < 0) {
            perror("recvfrom");
            break; // Salir en caso de error
        }

        buffer[n_bytes] = '\0'; // Asegurar terminación null

        // Imprimir el mensaje recibido
        // El broker debe incluir el nombre del tema al principio del mensaje
        printf("🔔 [mensaje] %s\n", buffer);
        fflush(stdout); // Asegurar que el mensaje se imprima inmediatamente
    }

    printf("[subscriber] Terminando.\n");
    close(sockfd);
    return 0;
}
