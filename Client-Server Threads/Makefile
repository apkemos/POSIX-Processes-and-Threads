all: player gameserver

player : client.c
	cc client.c -o player

gameserver : tserver.c
	cc tserver.c -o gameserver -lrt -pthread

clean:
	rm player gameserver
