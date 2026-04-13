CC     = gcc
CFLAGS = -Wall -Wextra -Iinclude
LIBS   = -lpthread -lrt

all: main engine_ecu gateway dashboard ecu_client

main: main.c
	$(CC) $(CFLAGS) -o main main.c $(LIBS)

engine_ecu: engine_ecu.c
	$(CC) $(CFLAGS) -o engine_ecu engine_ecu.c $(LIBS)

gateway: gateway.c
	$(CC) $(CFLAGS) -o gateway gateway.c $(LIBS)

dashboard: dashboard.c
	$(CC) $(CFLAGS) -o dashboard dashboard.c $(LIBS)

ecu_client: ecu_client.c
	$(CC) $(CFLAGS) -o ecu_client ecu_client.c $(LIBS)

# force-clear IPC resources after a crash (octal keys: 011=9, 022=18)
ipcclean:
	-ipcrm -M 9
	-ipcrm -Q 18
	-sem_unlink /ecu_sem 2>/dev/null || true
	-rm -f /tmp/ecu_cmd

clean:
	rm -f main engine_ecu gateway dashboard ecu_client
	rm -f can_bus.log fault_log.txt snapshot.txt
