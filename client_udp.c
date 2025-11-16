#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "protocol.h"

#define DATA_BLOCK_SIZE 1024   // tamaño de bloque de datos (<= MAX_DATA)

// ---- Prototipos ----
int do_hello(int sockfd,
             const struct sockaddr_in *servaddr,
             socklen_t servlen,
             const char *credential);

int do_wrq(int sockfd,
           const struct sockaddr_in *servaddr,
           socklen_t servlen,
           const char *remote_filename);

int do_data_and_fin(int sockfd,
                    const struct sockaddr_in *servaddr,
                    socklen_t servlen,
                    const char *local_file,
                    const char *remote_filename);

// ---------------------- MAIN ----------------------
int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr,
                "Uso: %s <server_ip> <credencial> <local_file> <remote_filename>\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip    = argv[1];
    const char *credential   = argv[2];
    const char *local_file   = argv[3];
    const char *remote_name  = argv[4];

    // Crear socket UDP
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    // Configurar dirección del servidor
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port   = htons(SERVER_PORT);

    if (inet_pton(AF_INET, server_ip, &servaddr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return EXIT_FAILURE;
    }

    socklen_t servlen = sizeof(servaddr);

    // Configurar timeout de recepción (para retransmisión)
    struct timeval tv;
    tv.tv_sec  = RECV_TIMEOUT_S;
    tv.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("setsockopt SO_RCVTIMEO");
        // no abortamos, pero avisamos
    }

    // ---- Fase 1: HELLO ----
    if (do_hello(sockfd, &servaddr, servlen, credential) != 0) {
        fprintf(stderr, "HELLO falló. Abortando.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf(">>> HELLO OK: credenciales aceptadas por el servidor.\n");

    // ---- Fase 2: WRQ ----
    if (do_wrq(sockfd, &servaddr, servlen, remote_name) != 0) {
        fprintf(stderr, "WRQ falló. Abortando.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf(">>> WRQ OK: filename aceptado por el servidor.\n");

    // ---- Fase 3 + 4: DATA + FIN ----
    if (do_data_and_fin(sockfd, &servaddr, servlen, local_file, remote_name) != 0) {
        fprintf(stderr, "DATA/FIN falló. Abortando.\n");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf(">>> DATA + FIN OK: archivo transferido correctamente.\n");

    close(sockfd);
    return EXIT_SUCCESS;
}

// ---------------------- Fase HELLO ----------------------
int do_hello(int sockfd,
             const struct sockaddr_in *servaddr,
             socklen_t servlen,
             const char *credential)
{
    size_t cred_len = strlen(credential);
    if (cred_len == 0 || cred_len >= MAX_CRED_LEN) {
        fprintf(stderr,
                "Credencial inválida (longitud 1..%d)\n",
                MAX_CRED_LEN - 1);
        return -1;
    }

    uint8_t buffer[2 + MAX_CRED_LEN];
    ssize_t n;
    struct sockaddr_in reply_addr;
    socklen_t reply_len;

    // Construir PDU HELLO
    buffer[0] = TYPE_HELLO; // Type = 1
    buffer[1] = 0;          // Seq = 0
    memcpy(&buffer[2], credential, cred_len);  // sin '\0'

    for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
        printf("Enviando HELLO (intento %d)...\n", attempt);

        // Enviar HELLO
        n = sendto(sockfd, buffer, 2 + cred_len, 0,
                   (const struct sockaddr *)servaddr, servlen);
        if (n < 0) {
            perror("sendto HELLO");
            return -1;
        }

        // Esperar ACK
        uint8_t rbuf[2 + MAX_DATA];
        memset(&reply_addr, 0, sizeof(reply_addr));
        reply_len = sizeof(reply_addr);

        n = recvfrom(sockfd, rbuf, sizeof(rbuf), 0,
                     (struct sockaddr *)&reply_addr, &reply_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr,
                        "Timeout esperando ACK de HELLO. Reintentando...\n");
                continue;
            } else {
                perror("recvfrom HELLO");
                return -1;
            }
        }

        if (n < 2) {
            fprintf(stderr,
                    "PDU demasiado corta recibida en HELLO (n=%zd)\n", n);
            continue;  // volver a intentar
        }

        uint8_t rtype    = rbuf[0];
        uint8_t rseq     = rbuf[1];
        size_t  data_len = (size_t)(n - 2);

        if (rtype != TYPE_ACK || rseq != 0) {
            fprintf(stderr,
                    "PDU inesperada en HELLO (type=%u, seq=%u)\n",
                    rtype, rseq);
            continue;
        }

        if (data_len > 0) {
            // Payload con mensaje de error
            char msg[256];
            size_t copy_len = data_len < sizeof(msg) - 1 ? data_len : sizeof(msg) - 1;
            memcpy(msg, &rbuf[2], copy_len);
            msg[copy_len] = '\0';
            fprintf(stderr, "Servidor rechazó credenciales: %s\n", msg);
            return -1;
        }

        // Si llegamos acá: ACK correcto, sin error → HELLO OK
        return 0;
    }

    fprintf(stderr,
            "No se recibió ACK válido de HELLO tras %d intentos.\n",
            MAX_RETRIES);
    return -1;
}

// ---------------------- Fase WRQ ----------------------
int do_wrq(int sockfd,
           const struct sockaddr_in *servaddr,
           socklen_t servlen,
           const char *remote_filename)
{
    size_t name_len = strlen(remote_filename);

    // Validación lado cliente según protocolo
    if (name_len < 4 || name_len > 10) {
        fprintf(stderr,
                "WRQ: Filename debe tener entre 4 y 10 caracteres.\n");
        return -1;
    }

    for (size_t i = 0; i < name_len; ++i) {
        unsigned char c = (unsigned char)remote_filename[i];
        if (!isascii(c)) {
            fprintf(stderr,
                    "WRQ: Filename contiene caracteres no ASCII.\n");
            return -1;
        }
    }

    // Preparamos PDU WRQ(type=2, seq=1, data = filename + '\0')
    uint8_t buffer[2 + MAX_FILENAME];
    buffer[0] = TYPE_WRQ;   // 2
    buffer[1] = 1;          // Seq Num = 1
    memcpy(&buffer[2], remote_filename, name_len + 1); // incluimos '\0'

    for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
        printf("Enviando WRQ (intento %d)...\n", attempt);

        ssize_t n = sendto(sockfd, buffer, 2 + name_len + 1, 0,
                           (const struct sockaddr *)servaddr, servlen);
        if (n < 0) {
            perror("sendto WRQ");
            return -1;
        }

        // Esperar ACK
        uint8_t rbuf[2 + MAX_DATA];
        struct sockaddr_in reply_addr;
        socklen_t reply_len = sizeof(reply_addr);
        n = recvfrom(sockfd, rbuf, sizeof(rbuf), 0,
                     (struct sockaddr *)&reply_addr, &reply_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr,
                        "Timeout esperando ACK de WRQ. Reintentando...\n");
                continue;
            } else {
                perror("recvfrom WRQ");
                return -1;
            }
        }

        if (n < 2) {
            fprintf(stderr,
                    "PDU demasiado corta recibida en WRQ (n=%zd)\n", n);
            continue;
        }

        uint8_t rtype    = rbuf[0];
        uint8_t rseq     = rbuf[1];
        size_t  data_len = (size_t)(n - 2);

        if (rtype != TYPE_ACK || rseq != 1) {
            fprintf(stderr,
                    "PDU inesperada en WRQ (type=%u, seq=%u)\n",
                    rtype, rseq);
            continue;
        }

        if (data_len > 0) {
            // ACK con mensaje de error
            char msg[256];
            size_t copy_len = data_len < sizeof(msg) - 1 ? data_len : sizeof(msg) - 1;
            memcpy(msg, &rbuf[2], copy_len);
            msg[copy_len] = '\0';
            fprintf(stderr, "Servidor rechazó filename: %s\n", msg);
            return -1;
        }

        // ACK correcto, sin error
        return 0;
    }

    fprintf(stderr,
            "No se recibió ACK válido de WRQ tras %d intentos.\n",
            MAX_RETRIES);
    return -1;
}

// ---------------------- Fase DATA + FIN ----------------------
int do_data_and_fin(int sockfd,
                    const struct sockaddr_in *servaddr,
                    socklen_t servlen,
                    const char *local_file,
                    const char *remote_filename)
{
    FILE *fp = fopen(local_file, "rb");
    if (!fp) {
        perror("fopen local_file");
        return -1;
    }

    uint8_t seq = 0;  // primer bloque DATA arranca en 0
    uint8_t sendbuf[2 + DATA_BLOCK_SIZE];
    uint8_t rbuf[2 + MAX_DATA];

    // ---------- Fase DATA ----------
    for (;;) {
        size_t nread = fread(&sendbuf[2], 1, DATA_BLOCK_SIZE, fp);
        if (nread == 0) {
            if (feof(fp)) {
                // fin de archivo → pasamos a FIN
                break;
            } else {
                perror("fread");
                fclose(fp);
                return -1;
            }
        }

        sendbuf[0] = TYPE_DATA;
        sendbuf[1] = seq;

        int success = 0;

        for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
            printf("Enviando DATA seq=%u (intento %d, %zu bytes)...\n",
                   seq, attempt, nread);

            ssize_t n = sendto(sockfd, sendbuf, 2 + nread, 0,
                               (const struct sockaddr *)servaddr, servlen);
            if (n < 0) {
                perror("sendto DATA");
                fclose(fp);
                return -1;
            }

            struct sockaddr_in reply_addr;
            socklen_t reply_len = sizeof(reply_addr);
            n = recvfrom(sockfd, rbuf, sizeof(rbuf), 0,
                         (struct sockaddr *)&reply_addr, &reply_len);
            if (n < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    fprintf(stderr,
                            "Timeout esperando ACK de DATA seq=%u. Reintentando...\n",
                            seq);
                    continue;
                } else {
                    perror("recvfrom DATA");
                    fclose(fp);
                    return -1;
                }
            }

            if (n < 2) {
                fprintf(stderr,
                        "PDU demasiado corta recibida en DATA (n=%zd)\n", n);
                continue;
            }

            uint8_t rtype    = rbuf[0];
            uint8_t rseq     = rbuf[1];
            size_t  data_len = (size_t)(n - 2);

            if (rtype != TYPE_ACK || rseq != seq) {
                fprintf(stderr,
                        "PDU inesperada en DATA (type=%u, seq=%u; esperado ACK seq=%u)\n",
                        rtype, rseq, seq);
                continue;
            }

            if (data_len > 0) {
                char msg[256];
                size_t copy_len = data_len < sizeof(msg) - 1 ? data_len : sizeof(msg) - 1;
                memcpy(msg, &rbuf[2], copy_len);
                msg[copy_len] = '\0';
                fprintf(stderr, "Servidor devolvió error en ACK DATA: %s\n", msg);
                fclose(fp);
                return -1;
            }

            // ACK correcto
            success = 1;
            break;
        }

        if (!success) {
            fprintf(stderr,
                    "No se recibió ACK válido de DATA seq=%u tras %d intentos.\n",
                    seq, MAX_RETRIES);
            fclose(fp);
            return -1;
        }

        // Alternar seq (stop & wait con 0/1)
        seq ^= 1;
    }

    fclose(fp);

    // ---------- Fase FIN ----------
    size_t fname_len = strlen(remote_filename);
    if (fname_len + 1 > MAX_FILENAME) {
        fprintf(stderr,
                "FIN: remote_filename demasiado largo (>%d)\n",
                MAX_FILENAME - 1);
        return -1;
    }

    uint8_t finbuf[2 + MAX_FILENAME];
    finbuf[0] = TYPE_FIN;
    finbuf[1] = seq;  // usamos el "siguiente" seq luego del último DATA
    memcpy(&finbuf[2], remote_filename, fname_len + 1); // incluye '\0'

    for (int attempt = 1; attempt <= MAX_RETRIES; ++attempt) {
        printf("Enviando FIN seq=%u (intento %d)...\n", seq, attempt);

        ssize_t n = sendto(sockfd, finbuf, 2 + fname_len + 1, 0,
                           (const struct sockaddr *)servaddr, servlen);
        if (n < 0) {
            perror("sendto FIN");
            return -1;
        }

        struct sockaddr_in reply_addr;
        socklen_t reply_len = sizeof(reply_addr);
        n = recvfrom(sockfd, rbuf, sizeof(rbuf), 0,
                     (struct sockaddr *)&reply_addr, &reply_len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                fprintf(stderr,
                        "Timeout esperando ACK de FIN seq=%u. Reintentando...\n",
                        seq);
                continue;
            } else {
                perror("recvfrom FIN");
                return -1;
            }
        }

        if (n < 2) {
            fprintf(stderr,
                    "PDU demasiado corta recibida en FIN (n=%zd)\n", n);
            continue;
        }

        uint8_t rtype    = rbuf[0];
        uint8_t rseq     = rbuf[1];
        size_t  data_len = (size_t)(n - 2);

        if (rtype != TYPE_ACK || rseq != seq) {
            fprintf(stderr,
                    "PDU inesperada en FIN (type=%u, seq=%u; esperado ACK seq=%u)\n",
                    rtype, rseq, seq);
            continue;
        }

        if (data_len > 0) {
            char msg[256];
            size_t copy_len = data_len < sizeof(msg) - 1 ? data_len : sizeof(msg) - 1;
            memcpy(msg, &rbuf[2], copy_len);
            msg[copy_len] = '\0';
            fprintf(stderr, "Servidor devolvió error en ACK FIN: %s\n", msg);
            return -1;
        }

        // ACK FIN correcto
        return 0;
    }

    fprintf(stderr,
            "No se recibió ACK válido de FIN tras %d intentos.\n",
            MAX_RETRIES);
    return -1;
}
