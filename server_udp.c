#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include "protocol.h"

// ---- Configurar credencial esperada (ejemplo) ----
static const char *VALID_CREDENTIAL = "grupoXX";   // TODO: reemplazar por la real

// ---- Array global de estados de clientes ----
static client_state_t clients[MAX_CLIENTS];

// ---- Helpers de manejo de clientes ----
static client_state_t *find_client(const struct sockaddr_in *addr) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i].in_use) continue;
        if (clients[i].addr.sin_addr.s_addr == addr->sin_addr.s_addr &&
            clients[i].addr.sin_port == addr->sin_port) {
            return &clients[i];
        }
    }
    return NULL;
}

static client_state_t *create_client(const struct sockaddr_in *addr,
                                     socklen_t addrlen) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i].in_use) {
            clients[i].in_use        = true;
            clients[i].addr          = *addr;
            clients[i].addrlen       = addrlen;
            clients[i].phase         = PHASE_EXPECT_WRQ;
            clients[i].expected_seq  = 0;
            clients[i].authenticated = true;
            clients[i].fp            = NULL;
            clients[i].filename[0]   = '\0';
            return &clients[i];
        }
    }
    return NULL;
}

// Enviar ACK helper
static void send_ack(int sockfd,
                     const struct sockaddr_in *addr,
                     socklen_t addrlen,
                     uint8_t seq,
                     const char *msg) {
    uint8_t buffer[2 + MAX_DATA];
    size_t msg_len = 0;
    buffer[0] = TYPE_ACK;
    buffer[1] = seq;

    if (msg != NULL) {
        msg_len = strlen(msg);
        if (msg_len > MAX_DATA) msg_len = MAX_DATA;
        memcpy(&buffer[2], msg, msg_len);
    }

    ssize_t n = sendto(sockfd, buffer, 2 + msg_len, 0,
                       (const struct sockaddr *)addr, addrlen);
    if (n < 0) {
        perror("sendto ACK");
    }
}

// ---------------------- HELLO ----------------------
static void handle_hello(int sockfd,
                         const struct sockaddr_in *addr,
                         socklen_t addrlen,
                         const uint8_t *data,
                         size_t data_len) {
    // data = credencial (sin '\0')
    char cred[MAX_CRED_LEN];
    if (data_len >= MAX_CRED_LEN) {
        // credencial demasiado larga
        send_ack(sockfd, addr, addrlen, 0, "Credencial muy larga");
        fprintf(stderr, "HELLO: credencial muy larga desde %s:%d\n",
                inet_ntoa(addr->sin_addr),
                ntohs(addr->sin_port));
        return;
    }

    memcpy(cred, data, data_len);
    cred[data_len] = '\0';

    printf("HELLO recibido de %s:%d con credencial '%s'\n",
           inet_ntoa(addr->sin_addr),
           ntohs(addr->sin_port),
           cred);

    if (strcmp(cred, VALID_CREDENTIAL) != 0) {
        send_ack(sockfd, addr, addrlen, 0, "Credencial invalida");
        fprintf(stderr, "Credencial inválida para %s:%d\n",
                inet_ntoa(addr->sin_addr),
                ntohs(addr->sin_port));
        return;
    }

    // Credencial válida → crear/actualizar cliente
    client_state_t *cli = find_client(addr);
    if (!cli) {
        cli = create_client(addr, addrlen);
        if (!cli) {
            send_ack(sockfd, addr, addrlen, 0, "Servidor ocupado");
            fprintf(stderr, "No hay slots libres para nuevo cliente.\n");
            return;
        }
    }

    // marcamos fase siguiente
    cli->phase         = PHASE_EXPECT_WRQ;
    cli->authenticated = true;

    // ACK sin mensaje de error
    send_ack(sockfd, addr, addrlen, 0, NULL);
    printf("HELLO OK → ACK enviado a %s:%d, fase=WRQ\n",
           inet_ntoa(addr->sin_addr),
           ntohs(addr->sin_port));
}

// ---------------------- WRQ ----------------------
static void handle_wrq(int sockfd,
                       const struct sockaddr_in *addr,
                       socklen_t addrlen,
                       const uint8_t *data,
                       size_t data_len,
                       uint8_t seq)
{
    // WRQ solo es válido si ya hubo HELLO correcto
    client_state_t *cli = find_client(addr);
    if (!cli || !cli->authenticated || cli->phase != PHASE_EXPECT_WRQ) {
        fprintf(stderr,
                "WRQ fuera de fase o sin HELLO desde %s:%d → descartado\n",
                inet_ntoa(addr->sin_addr),
                ntohs(addr->sin_port));
        return;
    }

    // data debe contener filename NULL-terminated
    if (data_len == 0 || data_len > MAX_FILENAME) {
        send_ack(sockfd, addr, addrlen, seq, "Filename muy largo/corto");
        fprintf(stderr, "WRQ: data_len inválido (%zu)\n", data_len);
        return;
    }

    // último byte debe ser '\0'
    if (data[data_len - 1] != '\0') {
        send_ack(sockfd, addr, addrlen, seq, "Filename sin terminador");
        fprintf(stderr, "WRQ: filename sin terminador NULL\n");
        return;
    }

    const char *fname = (const char *)data;
    size_t fname_len = strlen(fname);

    if (fname_len < 4 || fname_len > 10) {
        send_ack(sockfd, addr, addrlen, seq, "Filename debe tener 4-10 chars");
        fprintf(stderr, "WRQ: filename '%s' con longitud inválida (%zu)\n",
                fname, fname_len);
        return;
    }

    // Solo ASCII
    for (size_t i = 0; i < fname_len; ++i) {
        unsigned char c = (unsigned char)fname[i];
        if (!isascii(c)) {
            send_ack(sockfd, addr, addrlen, seq, "Filename no ASCII");
            fprintf(stderr, "WRQ: filename '%s' contiene no-ASCII\n", fname);
            return;
        }
    }

    // Abrir archivo para escritura (modo binario)
    if (cli->fp != NULL) {
        fclose(cli->fp);
        cli->fp = NULL;
    }

    strncpy(cli->filename, fname, MAX_FILENAME);
    cli->filename[MAX_FILENAME - 1] = '\0';

    cli->fp = fopen(cli->filename, "wb");
    if (!cli->fp) {
        perror("fopen WRQ");
        send_ack(sockfd, addr, addrlen, seq, "No se pudo abrir archivo");
        return;
    }

    // WRQ aceptado → pasamos a fase DATA
    cli->phase        = PHASE_EXPECT_DATA;
    cli->expected_seq = 0;  // primer DATA va con seq 0

    send_ack(sockfd, addr, addrlen, seq, NULL);

    printf("WRQ OK de %s:%d → archivo '%s' abierto, fase=DATA\n",
           inet_ntoa(addr->sin_addr),
           ntohs(addr->sin_port),
           cli->filename);
}

// ---------------------- DATA ----------------------
static void handle_data(int sockfd,
                        const struct sockaddr_in *addr,
                        socklen_t addrlen,
                        const uint8_t *data,
                        size_t data_len,
                        uint8_t seq)
{
    client_state_t *cli = find_client(addr);
    if (!cli || !cli->authenticated || cli->phase != PHASE_EXPECT_DATA) {
        fprintf(stderr,
                "DATA fuera de fase o sin WRQ desde %s:%d → descartado\n",
                inet_ntoa(addr->sin_addr),
                ntohs(addr->sin_port));
        return;
    }

    if (!cli->fp) {
        fprintf(stderr,
                "DATA recibido pero archivo no abierto para %s:%d\n",
                inet_ntoa(addr->sin_addr),
                ntohs(addr->sin_port));
        send_ack(sockfd, addr, addrlen, seq, "Archivo no abierto");
        return;
    }

    // Verificar seq
    if (seq != cli->expected_seq) {
        fprintf(stderr,
                "DATA con seq inesperado %u (esperado %u) de %s:%d\n",
                seq, cli->expected_seq,
                inet_ntoa(addr->sin_addr),
                ntohs(addr->sin_port));

        // Reenviar ACK del último bloque correcto (opcional)
        uint8_t last_seq = cli->expected_seq ^ 1;
        send_ack(sockfd, addr, addrlen, last_seq, NULL);
        return;
    }

    // Escribir datos en archivo
    if (data_len > 0) {
        size_t written = fwrite(data, 1, data_len, cli->fp);
        if (written != data_len) {
            perror("fwrite DATA");
            send_ack(sockfd, addr, addrlen, seq, "Error escribiendo archivo");
            return;
        }
    }

    // ACK correcto
    send_ack(sockfd, addr, addrlen, seq, NULL);

    // Alternar expected_seq para el próximo bloque
    cli->expected_seq ^= 1;
}

// ---------------------- FIN ----------------------
static void handle_fin(int sockfd,
                       const struct sockaddr_in *addr,
                       socklen_t addrlen,
                       const uint8_t *data,
                       size_t data_len,
                       uint8_t seq)
{
    client_state_t *cli = find_client(addr);
    if (!cli || !cli->authenticated) {
        fprintf(stderr,
                "FIN de cliente desconocido %s:%d → descartado\n",
                inet_ntoa(addr->sin_addr),
                ntohs(addr->sin_port));
        return;
    }

    // data = filename '\0' (podemos chequear que coincida con cli->filename)
    if (data_len > 0 && data[data_len - 1] == '\0') {
        const char *fname = (const char *)data;
        if (strcmp(fname, cli->filename) != 0) {
            fprintf(stderr,
                    "FIN con filename '%s' que no coincide con '%s' para %s:%d\n",
                    fname, cli->filename,
                    inet_ntoa(addr->sin_addr),
                    ntohs(addr->sin_port));
            // igual cerramos, no lo tratamos como error fatal de protocolo
        }
    }

    if (cli->fp) {
        fclose(cli->fp);
        cli->fp = NULL;
    }

    cli->phase = PHASE_DONE;

    // ACK del FIN
    send_ack(sockfd, addr, addrlen, seq, NULL);

    printf("FIN recibido de %s:%d. Archivo '%s' cerrado.\n",
           inet_ntoa(addr->sin_addr),
           ntohs(addr->sin_port),
           cli->filename);

    // Podrías liberar el slot si querés:
    // cli->in_use = false;
}

// ---------------------- MAIN SERVER ----------------------
int main(void) {
    // Inicializar array de clientes
    memset(clients, 0, sizeof(clients));

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port        = htons(SERVER_PORT);

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor UDP escuchando en puerto %d...\n", SERVER_PORT);

    for (;;) {
        uint8_t buf[2 + MAX_DATA];
        struct sockaddr_in cliaddr;
        socklen_t cliaddrlen = sizeof(cliaddr);
        ssize_t n = recvfrom(sockfd, buf, sizeof(buf), 0,
                             (struct sockaddr *)&cliaddr, &cliaddrlen);
        if (n < 0) {
            perror("recvfrom");
            continue;
        }
        if (n < 2) {
            fprintf(stderr, "PDU demasiado corta (n=%zd)\n", n);
            continue;
        }

        uint8_t type     = buf[0];
        uint8_t seq      = buf[1];
        size_t  data_len = (size_t)(n - 2);
        uint8_t *data    = &buf[2];

        if (type == TYPE_HELLO && seq == 0) {
            handle_hello(sockfd, &cliaddr, cliaddrlen, data, data_len);
        } else if (type == TYPE_WRQ && seq == 1) {
            handle_wrq(sockfd, &cliaddr, cliaddrlen, data, data_len, seq);
        } else if (type == TYPE_DATA) {
            handle_data(sockfd, &cliaddr, cliaddrlen, data, data_len, seq);
        } else if (type == TYPE_FIN) {
            handle_fin(sockfd, &cliaddr, cliaddrlen, data, data_len, seq);
        } else {
            fprintf(stderr,
                    "PDU no soportada (type=%u, seq=%u) desde %s:%d\n",
                    type, seq,
                    inet_ntoa(cliaddr.sin_addr),
                    ntohs(cliaddr.sin_port));
        }
    }

    close(sockfd);
    return 0;
}
