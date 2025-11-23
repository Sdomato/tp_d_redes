#include "common.h"

// Convierte un struct timeval a segundos (double).

double timeval_to_double(struct timeval *tv) {
    return (double)tv->tv_sec + (double)tv->tv_usec / 1000000.0;
}

// Envía el archivo de 1MB al servidor, midiendo el tiempo.

int send_file_and_measure(int sockfd, int rwin_size) {
    struct timeval start, end;
    
    // --- Configuración de la Ventana de Recepción (RWIN) ---
    // Ajustamos tanto el buffer de recepción como el de envío
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, &rwin_size, sizeof(rwin_size)) < 0) {
        perror("setsockopt SO_RCVBUF");
        return -1;
    }
    if (setsockopt(sockfd, SOL_SOCKET, SO_SNDBUF, &rwin_size, sizeof(rwin_size)) < 0) {
        perror("setsockopt SO_SNDBUF");
        return -1;
    }
    
    printf("Configurando RWIN/SWIN a %d bytes.\n", rwin_size);

    // --- Preparación de datos (1MB) ---
    // Usaremos el buffer para simular el archivo (no es necesario leerlo de disco)
    char buffer[BUFFER_SIZE];
    memset(buffer, 'A', BUFFER_SIZE); // Rellenar con cualquier dato para enviar
    
    long bytes_sent = 0;
    ssize_t n;
    
    // --- INICIO de Medición ---
    gettimeofday(&start, NULL);
    
    // --- Bucle de Envío de Datos ---
    while (bytes_sent < TOTAL_BYTES_1MB) {
        long remaining = TOTAL_BYTES_1MB - bytes_sent;
        size_t send_len = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
        
        n = send(sockfd, buffer, send_len, 0);
        
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                // El socket está lleno (buffer de envío lleno), reintentar
                continue; 
            }
            perror("send error");
            return -1;
        } else if (n == 0) {
            fprintf(stderr, "Conexión cerrada por el servidor.\n");
            return -1;
        }
        
        bytes_sent += n;
    }
    
    close(sockfd);
    
    // --- FIN de Medición ---
    gettimeofday(&end, NULL);
    
    // --- Cálculo de Resultados ---
    double elapsed_time = timeval_to_double(&end) - timeval_to_double(&start);
    double throughput_mbps = (double)TOTAL_BYTES_1MB / elapsed_time / 1024.0 / 1024.0 * 8.0;

    printf("\n--- Resultados de Transferencia ---\n");
    printf("Total enviado: %ld bytes (1MB)\n", bytes_sent);
    printf("Tiempo total: %.6f segundos\n", elapsed_time);
    printf("Throughput: %.2f Mbps\n", throughput_mbps);
    printf("RWIN/SWIN usado: %d bytes\n", rwin_size);
    printf("----------------------------------\n");
    
    return 0;
}


int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <server_ip> <rwin_size_bytes>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip = argv[1];
    int rwin_size = atoi(argv[2]); // Tamaño de RWIN pasado como argumento

    int sockfd;
    struct sockaddr_in servaddr;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, server_ip, &servaddr.sin_addr) <= 0) {
        perror("inet_pton");
        close(sockfd);
        return EXIT_FAILURE;
    }

    // 3-way handshake
    printf("Intentando conectar con %s:%d...\n", server_ip, SERVER_PORT);
    if (connect(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    printf("Conexión TCP establecida. Iniciando transferencia...\n");
    
    // 3. Enviar archivo y medir
    int result = send_file_and_measure(sockfd, rwin_size);
    
    // El socket ya fue cerrado dentro de send_file_and_measure
    return (result == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}