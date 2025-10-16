// Broker TCP para pub/sub simple por temas con múltiples SUB por conexión.
// Compilación: gcc -Wall -Wextra -O2 -pthread -o broker_tcp broker_tcp.c
// Ejecución:   ./broker_tcp <puerto>

#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define BACKLOG 128
#define MAX_LINE 4096
#define TOPIC_MAX 128

typedef struct SubNode {
    int fd;
    struct SubNode *next;
} SubNode;

typedef struct Topic {
    char name[TOPIC_MAX];
    SubNode *subs;
    struct Topic *next;
} Topic;

static Topic *topics = NULL;
static pthread_mutex_t topics_mtx = PTHREAD_MUTEX_INITIALIZER;

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

static Topic *find_or_create_topic(const char *name) {
    Topic *t = topics;
    while (t) {
        if (strcmp(t->name, name) == 0) return t;
        t = t->next;
    }
    // crear
    Topic *nt = (Topic *)calloc(1, sizeof(Topic));
    if (!nt) return NULL;
    strncpy(nt->name, name, TOPIC_MAX - 1);
    nt->subs = NULL;
    nt->next = topics;
    topics = nt;
    return nt;
}

static void add_subscriber(const char *topic, int fd) {
    pthread_mutex_lock(&topics_mtx);
    Topic *t = find_or_create_topic(topic);
    if (t) {
        // evitar duplicados
        for (SubNode *n = t->subs; n; n = n->next) {
            if (n->fd == fd) {
                pthread_mutex_unlock(&topics_mtx);
                return; // ya estaba suscrito
            }
        }
        SubNode *node = (SubNode *)calloc(1, sizeof(SubNode));
        node->fd = fd;
        node->next = t->subs;
        t->subs = node;
    }
    pthread_mutex_unlock(&topics_mtx);
}

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

typedef struct {
    int fd;
    struct sockaddr_in addr;
} ClientInfo;

static void *client_thread(void *arg) {
    ClientInfo *ci = (ClientInfo *)arg;
    int fd = ci->fd;
    free(ci);

    char line[MAX_LINE];
    char role[8] = {0};
    char topic[TOPIC_MAX] = {0};

    // 1) Leer la primera línea para determinar el rol
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
        // 🔸 Primer SUB
        add_subscriber(topic, fd);
        printf("[broker] Cliente %d suscrito a '%s'\n", fd, topic);

        // 🔸 Permitir múltiples SUB adicionales en la misma conexión
        while (true) {
            int r = read_line(fd, line, sizeof(line));
            if (r <= 0) break;

            // Si recibe otra orden SUB <tema_nuevo>
            char cmd[8] = {0};
            char new_topic[TOPIC_MAX] = {0};
            if (sscanf(line, "%7s %127s", cmd, new_topic) == 2 && strcmp(cmd, "SUB") == 0) {
                add_subscriber(new_topic, fd);
                printf("[broker] Cliente %d suscrito a '%s'\n", fd, new_topic);
                continue;
            }

            // Si no es SUB, ignoramos (suscriptores no deberían mandar otra cosa)
        }

        remove_subscriber_fd(fd);
        close(fd);
        return NULL;

    } else if (strcmp(role, "PUB") == 0) {
        // Publisher: bucle de mensajes
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
    signal(SIGPIPE, SIG_IGN);

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
        pthread_detach(th);
    }

    close(srv);
    return 0;
}
