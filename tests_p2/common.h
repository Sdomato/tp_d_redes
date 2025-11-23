#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/wait.h> // Necesario para el servidor (manejo de procesos hijos)
#include <signal.h>
#define SERVER_PORT 20252       
#define BACKLOG 10              // Cola máxima de conexiones pendientes
#define BUFFER_SIZE 8192        // Tamaño del buffer para send/recv (8KB)
#define TOTAL_BYTES_1MB 1048576 

#endif // COMMON_H