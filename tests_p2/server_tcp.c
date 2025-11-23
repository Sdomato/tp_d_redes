#include "common.h"

// Prototipo de la función que maneja la conexión con el cliente
void handle_client(int connfd);

// Función para evitar la creación de procesos zombies (obligatorio al usar fork)
void sigchld_handler(int s) {
    // waitpid() puede fallar si no hay hijos que esperar.
    // Usamos el flag WNOHANG para que no se bloquee y maneje todos los zombies.
    while (waitpid(-1, NULL, WNOHANG) > 0);
}

int main(int argc, char *argv[]) {
    int listenfd, connfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t clilen;
    
    // Configurar el handler de señales para evitar procesos zombie
    struct sigaction sa;
    sa.sa_handler = sigchld_handler; 
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
    
    // 1. Crear el socket (TCP: SOCK_STREAM)
    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Opcional: Reutilizar la dirección rápidamente (útil durante el desarrollo)
    int optval = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

    // Configurar dirección del servidor
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    // Escuchar en todas las interfaces disponibles
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    servaddr.sin_port = htons(SERVER_PORT);

    // 2. Enlazar (bind) el socket a la dirección y puerto
    if (bind(listenfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) {
        perror("bind");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    // 3. Poner el socket en modo de escucha (listen)
    if (listen(listenfd, BACKLOG) < 0) {
        perror("listen");
        close(listenfd);
        exit(EXIT_FAILURE);
    }

    printf("Servidor TCP escuchando en puerto %d...\n", SERVER_PORT);

    // Bucle principal para aceptar conexiones
    for (;;) {
        clilen = sizeof(cliaddr);
        
        // 4. Aceptar la conexión entrante
        connfd = accept(listenfd, (struct sockaddr *)&cliaddr, &clilen);
        if (connfd < 0) {
            // Manejar interrupciones o errores
            if (errno == EINTR) continue; 
            perror("accept");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(cliaddr.sin_addr), client_ip, INET_ADDRSTRLEN);
        printf("Conexión aceptada de %s:%d\n", client_ip, ntohs(cliaddr.sin_port));

        // 5. Crear un proceso hijo para manejar la conexión (concurrencia)
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            close(connfd); // Si fork falla, cerramos la conexión y seguimos
        } else if (pid == 0) { 
            // Proceso HIJO: Maneja la conexión
            close(listenfd); // El hijo no necesita escuchar
            handle_client(connfd);
            close(connfd);  // Cerrar el socket de conexión
            printf("Proceso hijo finalizado (%s:%d).\n", client_ip, ntohs(cliaddr.sin_port));
            exit(EXIT_SUCCESS);
        } else { 
            // Proceso PADRE: Sigue escuchando
            close(connfd); // El padre no necesita el socket de conexión del cliente
        }
    }

    // Esta parte nunca se alcanza, pero se incluye por completitud
    close(listenfd);
    return EXIT_SUCCESS;
}

/**
 * Función que recibe el archivo de 1MB del cliente
 * @param connfd: Socket conectado al cliente.
 */
void handle_client(int connfd) {
    ssize_t n_read;
    long total_read = 0;
    char buffer[BUFFER_SIZE];

    // Bucle de recepción de datos hasta que el cliente cierre la conexión
    while (total_read < TOTAL_BYTES_1MB) {
        n_read = recv(connfd, buffer, BUFFER_SIZE, 0);

        if (n_read < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue; // Reintentar
            }
            perror("recv error");
            break;
        } else if (n_read == 0) {
            // Cliente cerró la conexión
            printf("Cliente cerró la conexión inesperadamente. Bytes recibidos: %ld\n", total_read);
            break;
        }
        
        // Simplemente contamos los bytes, no es necesario escribirlos en disco
        total_read += n_read;
    }

    if (total_read >= TOTAL_BYTES_1MB) {
        printf("Archivo (1MB) recibido completamente. Total: %ld bytes\n", total_read);
    }
}