#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdbool.h>
#include <inttypes.h>
#include <openssl/rand.h>
#include <quiche.h>

#define MAX_DATAGRAM_SIZE 1350
#define MAX_CLIENTS 32

typedef struct {
    quiche_conn *conn;
    struct sockaddr_in addr;
    socklen_t addr_len;
    bool in_use;
} Client;

static void pump_send(int sock, quiche_conn *c,
                      struct sockaddr_in *to, socklen_t to_len,
                      struct sockaddr_in *from, socklen_t from_len)
{
    if (!c) return;
    uint8_t out[MAX_DATAGRAM_SIZE];
    quiche_send_info s_info = {
        .to = (struct sockaddr *)to,
        .to_len = to_len,
        .from = (struct sockaddr *)from,
        .from_len = from_len
    };

    for (;;) {
        ssize_t n = quiche_conn_send(c, out, sizeof(out), &s_info);
        if (n == QUICHE_ERR_DONE) break;
        if (n < 0) break;
        sendto(sock, out, n, 0, (struct sockaddr *)to, to_len);
    }
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <puerto> <cert.pem> <key.pem>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    const char *cert_file = argv[2];
    const char *key_file = argv[3];

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind"); return 1;
    }

    quiche_config *config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (!config) {
        fprintf(stderr, "[broker] ❌ quiche_config_new falló\n");
        return 1;
    }
    if (quiche_config_load_cert_chain_from_pem_file(config, cert_file) < 0 ||
        quiche_config_load_priv_key_from_pem_file(config, key_file) < 0) {
        fprintf(stderr, "[broker] ❌ Error cargando certificados.\n");
        return 1;
    }
    quiche_config_verify_peer(config, false);
    if (quiche_config_set_application_protos(config,
        (uint8_t*)"\x05hq-29\x08http/0.9", 14) < 0) {
        fprintf(stderr, "[broker] ❌ ALPN inválido\n");
        return 1;
    }

    Client clients[MAX_CLIENTS] = {0};

    printf("[broker] 🟢 Escuchando en %d\n", port);

    for (;;) {
        uint8_t in[MAX_DATAGRAM_SIZE];
        struct sockaddr_in peer = {0};
        socklen_t peer_len = sizeof(peer);

        ssize_t n = recvfrom(sock, in, sizeof(in), 0, (struct sockaddr *)&peer, &peer_len);
        if (n < 0) continue;

        // buscar cliente
        int idx = -1;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (clients[i].in_use &&
                clients[i].addr.sin_addr.s_addr == peer.sin_addr.s_addr &&
                clients[i].addr.sin_port == peer.sin_port) {
                idx = i;
                break;
            }
        }

        if (idx < 0) {
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (!clients[i].in_use) { idx = i; break; }
            }
            if (idx < 0) continue;

            uint8_t scid[16];
            RAND_bytes(scid, sizeof(scid));
            quiche_conn *c = quiche_accept(
                scid, sizeof(scid),
                NULL, 0,
                (const struct sockaddr *)&server_addr, sizeof(server_addr),
                (const struct sockaddr *)&peer, peer_len,
                config
            );
            if (!c) {
                fprintf(stderr, "[broker] ❌ quiche_accept falló\n");
                continue;
            }

            clients[idx].conn = c;
            clients[idx].addr = peer;
            clients[idx].addr_len = peer_len;
            clients[idx].in_use = true;
            printf("[broker] nuevo cliente %s:%d (slot %d)\n",
                   inet_ntoa(peer.sin_addr), ntohs(peer.sin_port), idx);
        }

        Client *cl = &clients[idx];
        if (!cl->conn) continue;

        quiche_recv_info r_info = {
            .from = (struct sockaddr *)&peer,
            .from_len = peer_len,
            .to = (struct sockaddr *)&server_addr,
            .to_len = sizeof(server_addr),
        };
        quiche_conn_recv(cl->conn, in, n, &r_info);

        // leer streams legibles
        quiche_stream_iter *it = quiche_conn_readable(cl->conn);
        if (!it) continue;
        uint64_t sid;
        while (quiche_stream_iter_next(it, &sid)) {
            for (;;) {
                uint8_t sbuf[4096];
                bool fin = false;
                uint64_t err = 0;
                ssize_t got = quiche_conn_stream_recv(cl->conn, sid, sbuf, sizeof(sbuf), &fin, &err);
                if (got == QUICHE_ERR_DONE) break;
                if (got < 0) break;
                printf("[broker] msg sid=%" PRIu64 " -> %.*s\n", sid, (int)got, sbuf);

                for (int j = 0; j < MAX_CLIENTS; j++) {
                    if (!clients[j].in_use || !clients[j].conn) continue;
                    quiche_conn_stream_send(clients[j].conn, sid, sbuf, got, false, &err);
                    pump_send(sock, clients[j].conn,
                              &clients[j].addr, clients[j].addr_len,
                              &server_addr, sizeof(server_addr));
                }
            }
        }
        quiche_stream_iter_free(it);

        pump_send(sock, cl->conn, &cl->addr, cl->addr_len,
                  &server_addr, sizeof(server_addr));
    }
}
