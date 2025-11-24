#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define PORT 20252
#define MAX_PDU      1009      // Máximo tamaño esperado de una PDU
#define BUFFER_SIZE  3000      // Buffer acumulador

int main(void) {
    int sockfd, connfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t clilen = sizeof(cliaddr);

    uint8_t buffer[BUFFER_SIZE];
    size_t buffer_len = 0;

    FILE *csv = fopen("delays.csv", "w");
    if (!csv) {
        perror("fopen CSV");
        return 1;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        fclose(csv);
        exit(1);
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family      = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port        = htons(PORT);

    if (bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        close(sockfd);
        fclose(csv);
        exit(1);
    }

    if (listen(sockfd, 1) < 0) {
        perror("listen");
        close(sockfd);
        fclose(csv);
        exit(1);
    }

    printf("Servidor TCP escuchando en puerto %d...\n", PORT);
    connfd = accept(sockfd, (struct sockaddr *)&cliaddr, &clilen);
    if (connfd < 0) {
        perror("accept");
        close(sockfd);
        fclose(csv);
        exit(1);
    }

    printf("Conexión aceptada de %s:%d\n",
           inet_ntoa(cliaddr.sin_addr), ntohs(cliaddr.sin_port));

    int count = 1;

    while (1) {
        uint8_t recvbuf[1500];
        ssize_t n = read(connfd, recvbuf, sizeof(recvbuf));

        if (n < 0) {
            perror("read");
            break;
        }
        if (n == 0) {
            printf("Cliente cerró la conexión.\n");
            break;
        }

        // Copiar lo leído al buffer acumulador
        if (buffer_len + n > BUFFER_SIZE) {
            // overflow del buffer → limpiamos y seguimos
            buffer_len = 0;
            continue;
        }

        memcpy(buffer + buffer_len, recvbuf, n);
        buffer_len += (size_t)n;

        // Procesar todas las PDUs completas que tengamos en el buffer
        // Formato esperado: [8 bytes timestamp][payload...]['|']
        while (buffer_len >= 9) {  // al menos timestamp (8) + '|'
            // Buscamos el delimitador '|' *después* de los 8 bytes de timestamp
            void *delim_ptr = memchr(buffer + 8, '|', buffer_len - 8);
            if (!delim_ptr) {
                // Todavía no llegó el delimitador: esperamos más datos
                break;
            }

            size_t pos     = (uint8_t *)delim_ptr - buffer;
            size_t pdu_len = pos + 1;  // incluye el '|'

            // Extraer timestamp de origen desde el comienzo de la PDU
            uint64_t origin_ts;
            memcpy(&origin_ts, buffer, sizeof(origin_ts));

            // Timestamp de llegada
            struct timeval tv;
            gettimeofday(&tv, NULL);
            uint64_t dst_ts = (uint64_t)tv.tv_sec * 1000000ULL
                            + (uint64_t)tv.tv_usec;

            // Usar tipo con signo para evitar overflow de uint64_t
            int64_t diff_us = (int64_t)dst_ts - (int64_t)origin_ts;
            double delay_sec = diff_us / 1000000.0;

            fprintf(csv, "%d,%.6f\n", count++, delay_sec);
            fflush(csv);

            // Desplazar el buffer, removiendo la PDU ya procesada
            size_t remaining = buffer_len - pdu_len;
            memmove(buffer, buffer + pdu_len, remaining);
            buffer_len = remaining;
        }
    }

    fclose(csv);
    close(connfd);
    close(sockfd);
    return 0;
}

