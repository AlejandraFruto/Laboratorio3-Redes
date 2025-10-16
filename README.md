#Requisitos
GCC (o clang) con soporte POSIX.
pthread en tiempo de compilación.
Linux

# Instrucciones para ejecutar los archivos
- Broker UDP:
Compilación: gcc -Wall -Wextra -O2 -o broker_udp broker_udp.c
Ejecución:   ./broker_udp <puerto>
Ejemplo:     ./broker_udp 5555
- Publisher UDP:
Compilación: gcc -Wall -Wextra -O2 -o publisher_udp publisher_udp.c
Uso:         ./publisher_udp <host> <puerto> "<tema>"
Ejemplo:     ./publisher_udp 127.0.0.1 8080 "Partido_AvsB"
- Subscriber UDP:
Compilación: gcc -Wall -Wextra -O2 -o subscriber_udp subscriber_udp.c
Uso:         ./subscriber_udp <host> <puerto> "<tema>"
Ejemplo:     ./subscriber_udp 127.0.0.1 8080 "Partido_AvsB"

Broker TCP:
Compilación: gcc -Wall -Wextra -O2 -pthread -o broker_tcp broker_tcp.c
Ejecución: ./broker_tcp <puerto>
Ejemplo: ./broker_tcp 5555

Publisher TCP:
Compilación: gcc -Wall -Wextra -O2 -o publisher_tcp publisher_tcp.c
Uso: ./publisher_tcp <host> <puerto> "<tema>"
Ejemplo: ./publisher_tcp 127.0.0.1 5555 "Partido_AvsB"

Subscriber TCP (múltiples temas opcional):
Compilación: gcc -Wall -Wextra -O2 -o subscriber_tcp subscriber_tcp.c
Uso: ./subscriber_tcp <host> <puerto> "<tema1>" [<tema2> ...]
Ejemplo: ./subscriber_tcp 127.0.0.1 5555 "Partido_AvsB" "Partido_CvsD"
