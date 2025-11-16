#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>        // FILE*
#include <netinet/in.h>   // struct sockaddr_in

// Puerto del servidor según enunciado
#define SERVER_PORT 20252

// Tamaños máximos
#define MAX_DATA       1478    // recomendado en el enunciado
#define MAX_FILENAME   11      // 10 chars + '\0'
#define MAX_CRED_LEN   64      // credenciales cortas
#define MAX_CLIENTS    32      // cantidad máxima de clientes concurrentes
#define MAX_RETRIES    5       // reintentos para HELLO / WRQ / DATA / FIN
#define RECV_TIMEOUT_S 5       // timeout de recepción en segundos

// Tipos de PDU según enunciado
typedef enum {
    TYPE_HELLO = 1,
    TYPE_WRQ   = 2,
    TYPE_DATA  = 3,
    TYPE_ACK   = 4,
    TYPE_FIN   = 5
} pdu_type_t;

// Fases lógicas del protocolo en el servidor
typedef enum {
    PHASE_EXPECT_HELLO = 0,
    PHASE_EXPECT_WRQ,
    PHASE_EXPECT_DATA,
    PHASE_EXPECT_FIN,
    PHASE_DONE
} phase_t;

// Estructura de PDU en memoria (si quisieras usar struct en vez de buffers)
typedef struct {
    uint8_t type;
    uint8_t seq;
    uint8_t data[MAX_DATA];
} pdu_t;

// Estado por cliente en el servidor (para concurrencia)
typedef struct {
    struct sockaddr_in addr;  // IP + puerto del cliente
    socklen_t addrlen;
    phase_t phase;
    uint8_t expected_seq;
    bool in_use;
    bool authenticated;
    FILE *fp;                 // archivo donde escribimos los datos
    char filename[MAX_FILENAME];
} client_state_t;

#endif // PROTOCOL_H
