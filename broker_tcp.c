// Broker TCP para pub/sub simple por temas con múltiples SUB por conexión.
// Compilación: gcc -Wall -Wextra -O2 -pthread -o broker_tcp broker_tcp.c
// Ejecución:   ./broker_tcp <puerto>
//
// Protocolo (línea inicial por cliente):
//   SUB <tema>            -> registra el socket como suscriptor del <tema>.
//   PUB <tema>            -> registra el socket como publicador de <tema>.
// Publicación (lado publisher):
//   MSG <texto>           -> el broker reenvía "<tema>: <texto>\n" a todos los SUB del tema.
//
// Concurrencia:
//   - Un hilo por cliente (pthread). Acceso a la lista de temas/suscriptores protegido por mutex.
//   - Se eliminan suscriptores “muertos” al fallar send().
//
// Notas de robustez:
//   - read_line() lee de a 1 byte hasta '\n' (suficiente para práctica).
//   - send_all() asegura enviar el buffer completo o reportar error.
//   - SIGPIPE ignorado para evitar terminar el proceso si un peer cierra.

#define _GNU_SOURCE         // Habilita extensiones no estándar de GNU en las librerías, a veces necesario para funciones avanzadas.
#include <arpa/inet.h>      // Provee funciones para manipular direcciones IP, como inet_ntop() que convierte IPs de binario a texto.
#include <errno.h>          // Permite el manejo de errores a través de la variable 'errno' y constantes como EINTR.
#include <netinet/in.h>     // Define la estructura 'sockaddr_in' y constantes necesarias para la programación de sockets de Internet.
#include <pthread.h>        // Proporciona la API POSIX para manejo de hilos, incluyendo funciones como pthread_create() y pthread_join().
#include <signal.h>         // Permite manejar señales del sistema como SIGINT o SIGTERM, útil para cerrar procesos de forma controlada.
#include <stdbool.h>        // Define el tipo de dato booleano 'bool' y los valores 'true' y 'false'.
#include <stdio.h>          // Librería estándar de Entrada/Salida para funciones como printf(), fprintf() y sscanf().
#include <stdlib.h>         // Librería estándar que provee funciones de gestión de memoria (calloc, free) y conversión de tipos (atoi).
#include <string.h>         // Provee funciones para la manipulación de cadenas de caracteres, como strcmp(), strncpy() y strlen().
#include <sys/socket.h>     // Contiene las definiciones y estructuras principales para la API de sockets (socket(), bind(), sendto(), recvfrom()).
#include <sys/types.h>      // Define tipos de datos primitivos usados en llamadas al sistema, como ssize_t y socklen_t.
#include <unistd.h>         // Provee acceso a la API del sistema operativo POSIX, incluyendo la función close() para cerrar descriptores de archivo.


#define BACKLOG 128
#define MAX_LINE 4096
#define TOPIC_MAX 128

// Lista enlazada de suscriptores por tema.
typedef struct SubNode {
    int fd;                 // descriptor del cliente suscriptor
    struct SubNode *next;
} SubNode;

// Nodo de tema con su lista de suscriptores.
typedef struct Topic {
    char name[TOPIC_MAX];   // nombre del tema
    SubNode *subs;          // cabeza de la lista de suscriptores
    struct Topic *next;
} Topic;

static Topic *topics = NULL;                               // lista global de temas
static pthread_mutex_t topics_mtx = PTHREAD_MUTEX_INITIALIZER; // protege 'topics'

// Envío confiable de 'len' bytes (maneja señales y envíos parciales).
static ssize_t send_all(int fd, const void *buf, size_t len) {
    const char *p = (const char *)buf;
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, p + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue; // reintentar si fue interrumpido
            return -1;                    // error duro
        }
        if (n == 0) return 0;             // peer cerró
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

// Lee una línea terminada en '\n' (estilo simple). Devuelve:
//  1 si obtuvo línea, 0 si conexión cerrada, -1 en error.
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

// Busca un tema por nombre o lo crea si no existe.
// PRE: se llama con el mutex tomado.
static Topic *find_or_create_topic(const char *name) {
    Topic *t = topics;
    while (t) {
        if (strcmp(t->name, name) == 0) return t;
        t = t->next;
    }
    // crear nuevo tema al vuelo
    Topic *nt = (Topic *)calloc(1, sizeof(Topic));
    if (!nt) return NULL;
    strncpy(nt->name, name, TOPIC_MAX - 1);
    nt->subs = NULL;
    nt->next = topics;
    topics = nt;
    return nt;
}

// Agrega un suscriptor (evita duplicados por fd).
static void add_subscriber(const char *topic, int fd) {
    pthread_mutex_lock(&topics_mtx);
    Topic *t = find_or_create_topic(topic);
    if (t) {
        for (SubNode *n = t->subs; n; n = n->next) {
            if (n->fd == fd) {
                pthread_mutex_unlock(&topics_mtx);
                return; // ya estaba suscrito a ese tema
            }
        }
        SubNode *node = (SubNode *)calloc(1, sizeof(SubNode));
        node->fd = fd;
        node->next = t->subs;
        t->subs = node;
    }
    pthread_mutex_unlock(&topics_mtx);
}

// Elimina un fd de todas las listas de suscriptores (cuando un cliente se va).
static void remove_subscriber_fd(int fd) {
    pthread_mutex_lock(&topics_mtx);
    for (Topic *t = topics; t; t = t->next) {
        SubNode **pp = &t->subs;
        while (*pp) {
            if ((*pp)->fd == fd) {
                SubNode *dead = *pp;
                *pp = (*pp)->next;
                free(dead);
            } else {
                pp = &(*pp)->next;
            }
        }
    }
    pthread_mutex_unlock(&topics_mtx);
}

// Reenvía 'msg' a todos los suscriptores del 'topic'.
// Si un envío falla, se asume desconexión y se remueve el suscriptor.
static void broadcast_to_topic(const char *topic, const char *msg) {
    pthread_mutex_lock(&topics_mtx);
    for (Topic *t = topics; t; t = t->next) {
        if (strcmp(t->name, topic) == 0) {
            SubNode **pp = &t->subs;
            while (*pp) {
                int fd = (*pp)->fd;
                char line[MAX_LINE];
                int n = snprintf(line, sizeof(line), "%s: %s\n", topic, msg);
                if (n < 0) { pp = &(*pp)->next; continue; }

                if (send_all(fd, line, (size_t)n) < 0) {
                    // desconexión: limpiar nodo
                    SubNode *dead = *pp;
                    *pp = (*pp)->next;
                    close(fd);
                    free(dead);
                } else {
                    pp = &(*pp)->next;
                }
            }
            break;
        }
    }
    pthread_mutex_unlock(&topics_mtx);
}

// Info por cliente para el hilo.
typedef struct {
    int fd;
    struct sockaddr_in addr;
} ClientInfo;

// Hilo por cliente: decide rol (SUB|PUB) y ejecuta bucle correspondiente.
static void *client_thread(void *arg) {
    ClientInfo *ci = (ClientInfo *)arg;
    int fd = ci->fd;
    free(ci);

    char line[MAX_LINE];
    char role[8] = {0};
    char topic[TOPIC_MAX] = {0};

    // 1) Leer la primera línea para determinar el rol y el tema.
    if (read_line(fd, line, sizeof(line)) <= 0) {
        close(fd);
        return NULL;
    }
    if (sscanf(line, "%7s %127s", role, topic) != 2) {
        const char *err = "ERR protocolo: use 'SUB <tema>' o 'PUB <tema>'\n";
        send_all(fd, err, strlen(err));
        close(fd);
        return NULL;
    }

    if (strcmp(role, "SUB") == 0) {
        // Suscripción inicial
        add_subscriber(topic, fd);
        printf("[broker] Cliente %d suscrito a '%s'\n", fd, topic);

        // Acepta múltiples SUB en la misma conexión.
        while (true) {
            int r = read_line(fd, line, sizeof(line));
            if (r <= 0) break;

            char cmd[8] = {0};
            char new_topic[TOPIC_MAX] = {0};
            if (sscanf(line, "%7s %127s", cmd, new_topic) == 2 && strcmp(cmd, "SUB") == 0) {
                add_subscriber(new_topic, fd);
                printf("[broker] Cliente %d suscrito a '%s'\n", fd, new_topic);
                continue;
            }
            // Otras líneas de un SUB se ignoran.
        }

        // Limpieza al salir.
        remove_subscriber_fd(fd);
        close(fd);
        return NULL;

    } else if (strcmp(role, "PUB") == 0) {
        // Bucle de publicación: solo acepta "MSG <texto>"
        while (true) {
            int r = read_line(fd, line, sizeof(line));
            if (r <= 0) break;
            if (strncmp(line, "MSG ", 4) == 0) {
                const char *payload = line + 4;
                broadcast_to_topic(topic, payload);
            } else {
                const char *warn = "WARN: use 'MSG <texto>'\n";
                if (send_all(fd, warn, strlen(warn)) < 0) break;
            }
        }
        close(fd);
        return NULL;

    } else {
        const char *err = "ERR rol desconocido\n";
        send_all(fd, err, strlen(err));
        close(fd);
        return NULL;
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return 1;
    }
    signal(SIGPIPE, SIG_IGN); // evitar terminación por escritura a socket cerrado

    int port = atoi(argv[1]);
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return 1; }

    int yes = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port);

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }
    if (listen(srv, BACKLOG) < 0) {
        perror("listen");
        return 1;
    }

    printf("[broker] Escuchando en puerto %d ...\n", port);

    // Bucle principal: aceptar clientes y lanzar hilo.
    while (1) {
        struct sockaddr_in cli = {0};
        socklen_t clilen = sizeof(cli);
        int fd = accept(srv, (struct sockaddr *)&cli, &clilen);
        if (fd < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            continue;
        }
        ClientInfo *ci = (ClientInfo *)malloc(sizeof(ClientInfo));
        ci->fd = fd; ci->addr = cli;

        pthread_t th;
        pthread_create(&th, NULL, client_thread, ci);
        pthread_detach(th); // no join; limpiará el SO al terminar
    }

    close(srv);
    return 0;
}

