para la compilacion del punto 1

cd tests_p1   # o la carpeta raíz que contenga src/
gcc -Wall -Wextra -std=c11 -Isrc src/server.c -o server_udp
gcc -Wall -Wextra -std=c11 -Isrc src/client.c -o client_udp

Esto genera:
server_udp → servidor UDP del TP
client_udp → cliente UDP del TP

Instrucciones de uso
1. Ejecución del servidor propio

El servidor no recibe parámetros por línea de comandos.
Escucha en el puerto definido en protocol.h (por defecto SERVER_PORT = 20252).

Desde la carpeta donde se encuentra el binario:
./server_udp
Salida esperada:
Servidor UDP escuchando en puerto 20252...

El servidor se queda bloqueado esperando PDUs HELLO/WRQ/DATA/FIN de los clientes.

Ejecución del cliente propio
Formato de ejecución:
./client_udp <server_ip> <credencial> <archivo_local> <nombre_remoto>
<server_ip>

IP del servidor (ej: 127.0.0.1 para pruebas locales, o la IP dada por la cátedra 167.114.129.206).

<credencial>

Cadena enviada en la PDU HELLO.
Para las pruebas con los tests se usó "TEST".
Para validar con el servidor de la cátedra se utilizó la credencial provista (g06-e945).

<archivo_local>
Ruta del archivo que se desea enviar (se abre en modo binario).
Nosotros usamos prueba1.txt

<nombre_remoto>
Nombre con el que el servidor guardará el archivo.
Debe cumplir con las restricciones de protocolo:
Longitud mínima: 4 caracteres.
Si no tiene extensión (no contiene '.'), longitud máxima: 10 caracteres.
Todos los caracteres deben ser ASCII (código < 128).

Entonces:
En una terminal:
./server_udp

En otra terminal, en la misma carpeta:
./client_udp 127.0.0.1 TEST prueba1 prueba1

validación contra el servidor de la cátedra

Credenciales e IP provistas:
IP servidor cátedra: 167.114.129.206

Credencial: g06-e945
Archivo: g06.data (MD5: 1e093f7fe8d787b8af4ebc292dc00833)

# Ejecutar cliente contra el servidor de la cátedra
./client_udp 167.114.129.206 g06-e945 g06.data g06data