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
