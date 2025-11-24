#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>

#define PORT 20252

uint64_t get_timestamp_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000000ULL + (uint64_t)tv.tv_usec;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Uso: %s <server_ip> -d <ms> -N <segundos>\n", argv[0]);
        printf("Ejemplo: %s 192.168.0.10 50 10\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int d_ms = atoi(argv[2]);
    int N_seconds = atoi(argv[3]);

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) { perror("socket"); exit(1); }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));

    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, server_ip, &servaddr.sin_addr) <= 0) {
        perror("inet_pton");
        exit(1);
    }

    if (connect(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect");
        exit(1);
    }

    printf("Conectado al servidor %s:%d\n", server_ip, PORT);

    uint64_t start_ts = get_timestamp_us();

    while (1) {
        uint64_t now = get_timestamp_us();
        if ((now - start_ts) / 1000000 >= N_seconds)
            break;

        uint64_t origin = get_timestamp_us();
        send(sockfd, &origin, sizeof(origin), 0);

        int payload_len = 500 + rand() % 501;
        uint8_t *payload = malloc(payload_len);
        memset(payload, 0x20, payload_len);

        send(sockfd, payload, payload_len, 0);
        free(payload);

        uint8_t delim = '|';
        send(sockfd, &delim, 1, 0);

        usleep(d_ms * 1000);
    }

    close(sockfd);
    return 0;
}
