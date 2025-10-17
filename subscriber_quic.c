// ------------------------------------------------------------
// subscriber_quic.c  (Cliente SUBSCRIBER con QUIC + TLS)
// Compilar:
//   gcc subscriber_quic.c -o subscriber_quic \
//     -I./quiche/quiche/include ./quiche/target/release/libquiche.a \
//     -lssl -lcrypto -lpthread -ldl -lm -lrt
// Ejecutar:
//   ./subscriber_quic 127.0.0.1 4444 topic
// ------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdbool.h>
#include <inttypes.h>
#include <quiche.h>

#define MAX_DATAGRAM_SIZE 1350

static void pump_send(int sock, quiche_conn *c,
                      struct sockaddr_in *to, socklen_t to_len,
                      struct sockaddr_in *from, socklen_t from_len)
{
    uint8_t out[MAX_DATAGRAM_SIZE];

    quiche_send_info s_info = {
        .to      = (struct sockaddr *)to,
        .to_len  = to_len,
        .from    = (struct sockaddr *)from,
        .from_len= from_len,
    };

    for (;;) {
        ssize_t n = quiche_conn_send(c, out, sizeof(out), &s_info);
        if (n == QUICHE_ERR_DONE) break;
        if (n < 0) {
            fprintf(stderr, "[subscriber] quiche_conn_send err=%zd\n", n);
            break;
        }
        sendto(sock, out, n, 0, (struct sockaddr *)to, to_len);
    }
}

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <host> <puerto> <topic>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *topic = argv[3];

    // Crear socket UDP
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in local_addr = {0};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr.sin_port = 0; // puerto efímero
    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        return 1;
    }

    struct sockaddr_in peer_addr = {0};
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons((uint16_t)port);
    inet_pton(AF_INET, server_ip, &peer_addr.sin_addr);

    // Configuración QUIC
    quiche_config *config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    if (!config) { fprintf(stderr, "quiche_config_new failed\n"); return 1; }

    quiche_config_verify_peer(config, false);

    static const uint8_t ALPN[] = "\x05hq-29\x08http/0.9";
    if (quiche_config_set_application_protos(config, ALPN, sizeof(ALPN) - 1) < 0) {
        fprintf(stderr, "set_application_protos failed\n");
        return 1;
    }

    // 💡 Aumentar créditos iniciales
    quiche_config_set_initial_max_data(config, 10 * 1024 * 1024);
    quiche_config_set_initial_max_stream_data_bidi_local(config, 5 * 1024 * 1024);
    quiche_config_set_initial_max_stream_data_bidi_remote(config, 5 * 1024 * 1024);
    quiche_config_set_initial_max_streams_bidi(config, 100);

    // ID de conexión local (random)
    uint8_t scid[16];
    for (int i = 0; i < 16; i++) scid[i] = (uint8_t) rand();

    quiche_conn *conn = quiche_connect(
        server_ip,
        scid, sizeof(scid),
        (const struct sockaddr *)&local_addr, sizeof(local_addr),
        (const struct sockaddr *)&peer_addr, sizeof(peer_addr),
        config
    );


    if (!conn) {
        fprintf(stderr, "[subscriber] quiche_connect failed\n");
        return 1;
    }

    // Inicializar handshake
    pump_send(sock, conn, &peer_addr, sizeof(peer_addr),
              &local_addr, sizeof(local_addr));

    printf("[subscriber] Conectando a %s:%d...\n", server_ip, port);

    // Bucle principal
    for (;;) {
        // Recibir datagrama
        uint8_t buf[MAX_DATAGRAM_SIZE];
        struct sockaddr_in recv_addr;
        socklen_t recv_len = sizeof(recv_addr);

        ssize_t n = recvfrom(sock, buf, sizeof(buf), 0,
                             (struct sockaddr *)&recv_addr, &recv_len);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("[subscriber] recvfrom");
            continue;
        }

        quiche_recv_info r_info = {
            .from    = (struct sockaddr *)&recv_addr,
            .from_len= recv_len,
            .to      = (struct sockaddr *)&local_addr,
            .to_len  = sizeof(local_addr),
        };

        ssize_t rv = quiche_conn_recv(conn, buf, n, &r_info);
        if (rv < 0 && rv != QUICHE_ERR_DONE) {
            fprintf(stderr, "[subscriber] quiche_conn_recv err=%zd\n", rv);
            continue;
        }

        // Verificar si handshake completado
        if (quiche_conn_is_established(conn)) {
            // ✅ Leer streams legibles
            quiche_stream_iter *it = quiche_conn_readable(conn);
            uint64_t sid;
            while (quiche_stream_iter_next(it, &sid)) {
                for (;;) {
                    uint8_t sbuf[4096];
                    bool fin = false;
                    uint64_t err = 0;

                    ssize_t got = quiche_conn_stream_recv(conn, sid, sbuf,
                                                          sizeof(sbuf), &fin, &err);
                    if (got == QUICHE_ERR_DONE) break;
                    if (got < 0) {
                        fprintf(stderr, "[subscriber] stream_recv sid=%" PRIu64 " err=%zd code=%" PRIu64 "\n",
                                sid, got, err);
                        break;
                    }

                    printf("[subscriber] Mensaje recibido (sid=%" PRIu64 "): %.*s\n",
                           sid, (int)got, (char *)sbuf);
                }
            }
            quiche_stream_iter_free(it);
        }

        // Bombear ACKs y ventana de flujo
        pump_send(sock, conn, &peer_addr, sizeof(peer_addr),
                  &local_addr, sizeof(local_addr));

        // Si se cerró la conexión
        if (quiche_conn_is_closed(conn)) {
            fprintf(stderr, "[subscriber] Conexión cerrada.\n");
            break;
        }
    }

    quiche_conn_free(conn);
    close(sock);
    return 0;
}
