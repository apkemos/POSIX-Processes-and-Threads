all: player gameserver

player : client.c
	cc client.c -o player

gameserver : pserver.c
	cc pserver.c -o gameserver -lrt -pthread

clean:
	rm player gameserver
