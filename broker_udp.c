#define _GNU_SOURCE         // Habilita extensiones no estándar de GNU en las librerías, a veces necesario para funciones avanzadas.
#include <arpa/inet.h>      // Provee funciones para manipular direcciones IP, como inet_ntop() que convierte IPs de binario a texto.
#include <errno.h>          // Permite el manejo de errores a través de la variable 'errno' y constantes como EINTR.
#include <netinet/in.h>     // Define la estructura 'sockaddr_in' y constantes necesarias para la programación de sockets de Internet.
#include <stdbool.h>        // Define el tipo de dato booleano 'bool' y los valores 'true' y 'false'.
#include <stdio.h>          // Librería estándar de Entrada/Salida para funciones como printf(), fprintf() y sscanf().
#include <stdlib.h>         // Librería estándar que provee funciones de gestión de memoria (calloc, free) y conversión de tipos (atoi).
#include <string.h>         // Provee funciones para la manipulación de cadenas de caracteres, como strcmp(), strncpy() y strlen().
#include <sys/socket.h>     // Contiene las definiciones y estructuras principales para la API de sockets (socket(), bind(), sendto(), recvfrom()).
#include <sys/types.h>      // Define tipos de datos primitivos usados en llamadas al sistema, como ssize_t y socklen_t.
#include <unistd.h>         // Provee acceso a la API del sistema operativo POSIX, incluyendo la función close() para cerrar descriptores de archivo.

#define MAX_BUFFER 4096
#define TOPIC_MAX 128
#define MAX_SUBSCRIBERS 512 // Límite por tema

// Estructura para almacenar la dirección de un suscriptor
typedef struct SubNode {
    struct sockaddr_in addr;
    struct SubNode *next;
} SubNode;

// Estructura para un tema (partido) y sus suscriptores
typedef struct Topic {
    char name[TOPIC_MAX];
    SubNode *subs;
    struct Topic *next;
} Topic;

static Topic *topics = NULL;

// Busca un tema por su nombre. Si no existe, lo crea.
static Topic *find_or_create_topic(const char *name) {
    Topic *t = topics;
    while (t) {
        if (strcmp(t->name, name) == 0) return t;
        t = t->next;
    }
    // No encontrado, crear nuevo tema
    Topic *nt = (Topic *)calloc(1, sizeof(Topic));
    if (!nt) {
        perror("calloc para Topic");
        return NULL;
    }
    strncpy(nt->name, name, TOPIC_MAX - 1);
    nt->subs = NULL;
    nt->next = topics;
    topics = nt; // Agregar a la lista global
    printf("[broker] Tema nuevo creado: '%s'\n", name);
    return nt;
}

// Agrega un suscriptor a la lista de un tema.
static void add_subscriber(const char *topic_name, const struct sockaddr_in *sub_addr) {
    Topic *t = find_or_create_topic(topic_name);
    if (!t) return;

    // Verificar si el suscriptor ya existe para evitar duplicados
    for (SubNode *current = t->subs; current != NULL; current = current->next) {
        if (current->addr.sin_addr.s_addr == sub_addr->sin_addr.s_addr &&
            current->addr.sin_port == sub_addr->sin_port) {
            return;
        }
    }

    // Agregar nuevo suscriptor
    SubNode *node = (SubNode *)calloc(1, sizeof(SubNode));
    if (!node) {
        perror("calloc para SubNode");
        return;
    }
    node->addr = *sub_addr;
    node->next = t->subs;
    t->subs = node;
    char sub_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(sub_addr->sin_addr), sub_ip, INET_ADDRSTRLEN);
    printf("[broker] Nuevo suscriptor %s:%d para el tema '%s'\n", sub_ip, ntohs(sub_addr->sin_port), topic_name);
}

// Reenvía un mensaje a todos los suscriptores de un tema.
static void broadcast_to_topic(int sockfd, const char *topic_name, const char *msg) {
    Topic *t = topics;
    while (t) {
        if (strcmp(t->name, topic_name) == 0) {
            for (SubNode *sub = t->subs; sub; sub = sub->next) {
                // sendto(): Envía un datagrama UDP al suscriptor.
                // - sockfd: descriptor de socket UDP del broker.
                // - msg: buffer con el mensaje a reenviar.
                // - strlen(msg): tamaño del mensaje.
                // - 0: sin flags adicionales.
                // - sub->addr: dirección IP y puerto del suscriptor.
                // - sizeof(sub->addr): tamaño de la estructura sockaddr_in.
                sendto(sockfd, msg, strlen(msg), 0, (const struct sockaddr *)&sub->addr, sizeof(sub->addr));
            }
            break;
        }
        t = t->next;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);

    // socket(): crea un socket UDP
    // - AF_INET: IPv4
    // - SOCK_DGRAM: socket de datagramas (UDP)
    // - 0: protocolo por defecto (UDP)
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // Configurar la dirección local del broker (puerto + IP)
    struct sockaddr_in srv_addr = {0};
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Escuchar en todas las interfaces
    srv_addr.sin_port = htons((uint16_t)port);

    // bind(): asocia el socket UDP a una dirección IP/puerto
    // - sockfd: descriptor de socket
    // - (sockaddr *)&srv_addr: estructura con IP y puerto local
    // - sizeof(srv_addr): tamaño de la estructura
    if (bind(sockfd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    printf("[broker] Escuchando en puerto UDP %d ...\n", port);

    while (1) {
        char buffer[MAX_BUFFER];
        struct sockaddr_in cli_addr = {0};
        socklen_t clilen = sizeof(cli_addr);

        // recvfrom(): recibe un datagrama UDP de cualquier cliente (publisher o subscriber)
        // - sockfd: socket UDP del broker
        // - buffer: donde se almacena el mensaje recibido
        // - sizeof(buffer)-1: tamaño máximo permitido
        // - 0: flags (ninguno)
        // - (sockaddr *)&cli_addr: almacena la IP/puerto de quien lo envió
        // - &clilen: tamaño de la estructura anterior
        ssize_t n = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&cli_addr, &clilen);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("recvfrom");
            continue;
        }
        buffer[n] = '\0'; // Asegurar terminación null

        // Parsear mensaje entrante
        char role[8] = {0};
        char topic[TOPIC_MAX] = {0};
        sscanf(buffer, "%7s %127s", role, topic);

        if (strcmp(role, "SUB") == 0 && topic[0] != '\0') {
            add_subscriber(topic, &cli_addr);

        } else if (strcmp(role, "PUB") == 0 && topic[0] != '\0') {
            const char *msg = buffer + 4 + strlen(topic) + 1;
            if (strlen(msg) > 0) {
                 char pub_ip[INET_ADDRSTRLEN];
                 inet_ntop(AF_INET, &(cli_addr.sin_addr), pub_ip, INET_ADDRSTRLEN);
                 printf("[broker] Publicación de %s:%d para tema '%s': %s\n",
                        pub_ip, ntohs(cli_addr.sin_port), topic, msg);
                 broadcast_to_topic(sockfd, topic, msg);
            }

        } else {
             char cli_ip[INET_ADDRSTRLEN];
             inet_ntop(AF_INET, &(cli_addr.sin_addr), cli_ip, INET_ADDRSTRLEN);
             fprintf(stderr, "[broker] Mensaje inválido de %s:%d: %s\n",
                     cli_ip, ntohs(cli_addr.sin_port), buffer);
        }
    }

    // Cerrar el socket cuando se termine la ejecución
    close(sockfd);
    return 0;
}
