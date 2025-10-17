// ------------------------------------------------------------
// publisher_quic.c (QUIC + TLS con quiche) - con control de flujo
// ------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <openssl/rand.h>
#include <quiche.h>

#define MAX_DATAGRAM_SIZE 1350

static void pump_send(int sock, quiche_conn *conn,
                      struct sockaddr_in *peer_addr,
                      struct sockaddr_in *local_addr) {
    uint8_t out[MAX_DATAGRAM_SIZE];
    quiche_send_info s_info = {
        .to = (struct sockaddr *)peer_addr,
        .to_len = sizeof(*peer_addr),
        .from = (struct sockaddr *)local_addr,
        .from_len = sizeof(*local_addr)
    };

    for (;;) {
        ssize_t len = quiche_conn_send(conn, out, sizeof(out), &s_info);
        if (len == QUICHE_ERR_DONE) break;
        if (len < 0) {
            fprintf(stderr, "[publisher] quiche_conn_send error: %zd\n", len);
            break;
        }
        sendto(sock, out, len, 0, (struct sockaddr *)peer_addr, sizeof(*peer_addr));
    }
}

static void pump_recv(int sock, quiche_conn *conn,
                      struct sockaddr_in *peer_addr,
                      struct sockaddr_in *local_addr) {
    uint8_t in[MAX_DATAGRAM_SIZE];
    struct sockaddr_in from = {0};
    socklen_t from_len = sizeof(from);

    ssize_t n = recvfrom(sock, in, sizeof(in), MSG_DONTWAIT,
                         (struct sockaddr *)&from, &from_len);
    if (n > 0) {
        quiche_recv_info r_info = {
            .from = (struct sockaddr *)&from,
            .from_len = from_len,
            .to = (struct sockaddr *)local_addr,
            .to_len = sizeof(*local_addr)
        };
        quiche_conn_recv(conn, in, n, &r_info);
    }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Uso: %s <host> <puerto> <topic>\n", argv[0]);
        return 1;
    }

    const char *host = argv[1];
    int port = atoi(argv[2]);
    const char *topic = argv[3];

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    struct sockaddr_in local_addr = {0};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = 0;

    if (bind(sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind");
        return 1;
    }
    socklen_t l = sizeof(local_addr);
    getsockname(sock, (struct sockaddr *)&local_addr, &l);

    struct sockaddr_in peer_addr = {0};
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &peer_addr.sin_addr);

    quiche_config *config = quiche_config_new(QUICHE_PROTOCOL_VERSION);
    quiche_config_verify_peer(config, false);

    // 🚀 aumentar límites de flujo para evitar bloqueos por falta de crédito
    quiche_config_set_initial_max_data(config, 10 * 1024 * 1024);
    quiche_config_set_initial_max_stream_data_bidi_local(config, 5 * 1024 * 1024);
    quiche_config_set_initial_max_stream_data_bidi_remote(config, 5 * 1024 * 1024);
    quiche_config_set_initial_max_streams_bidi(config, 100);

    static const uint8_t ALPN[] = "\x05hq-29\x08http/0.9";
    quiche_config_set_application_protos(config, ALPN, sizeof(ALPN)-1);

    uint8_t scid[16];
    RAND_bytes(scid, sizeof(scid));

    quiche_conn *conn = quiche_connect(
        host,
        scid, sizeof(scid),
        (const struct sockaddr *)&local_addr, sizeof(local_addr),
        (const struct sockaddr *)&peer_addr, sizeof(peer_addr),
        config
    );
    if (!conn) {
        fprintf(stderr, "Error creando conexión QUIC.\n");
        return 1;
    }

    // --- handshake inicial ---
    while (!quiche_conn_is_established(conn)) {
        pump_send(sock, conn, &peer_addr, &local_addr);
        pump_recv(sock, conn, &peer_addr, &local_addr);
    }

    printf("[publisher] Handshake QUIC completado.\n");

    uint64_t stream_id = 0;
    uint64_t err_code = 0;

    // --- enviar tópico ---
    size_t topic_len = strlen(topic);
    int wr = quiche_conn_stream_writable(conn, stream_id, topic_len);
    if (wr <= 0) {
        pump_send(sock, conn, &peer_addr, &local_addr);
        pump_recv(sock, conn, &peer_addr, &local_addr);
    }
    ssize_t sret = quiche_conn_stream_send(conn, stream_id,
                                           (const uint8_t *)topic, topic_len,
                                           false, &err_code);
    if (sret < 0)
        fprintf(stderr, "stream_send(topic) err=%zd code=%llu\n",
                sret, (unsigned long long)err_code);
    pump_send(sock, conn, &peer_addr, &local_addr);

    // --- loop de mensajes ---
    for (;;) {
        char msg[1024];
        printf("Mensaje a enviar ('exit' para salir): ");
        if (!fgets(msg, sizeof(msg), stdin)) break;
        msg[strcspn(msg, "\n")] = 0;
        if (strcmp(msg, "exit") == 0) break;

        size_t want = strlen(msg);
        int writable = quiche_conn_stream_writable(conn, stream_id, want);

        while (writable <= 0) {
            fprintf(stderr, "[publisher] Stream bloqueado, bombeando...\n");
            pump_send(sock, conn, &peer_addr, &local_addr);
            pump_recv(sock, conn, &peer_addr, &local_addr);
            writable = quiche_conn_stream_writable(conn, stream_id, want);
        }

        ssize_t s = quiche_conn_stream_send(conn, stream_id,
                                            (const uint8_t *)msg, want,
                                            false, &err_code);
        if (s == -12) {
            fprintf(stderr, "[publisher] Bloqueado (-12), reintentando...\n");
            continue;
        } else if (s < 0) {
            fprintf(stderr, "[publisher] stream_send err=%zd code=%llu\n",
                    s, (unsigned long long)err_code);
        }

        pump_send(sock, conn, &peer_addr, &local_addr);
    }

    quiche_conn_free(conn);
    close(sock);
    return 0;
}
