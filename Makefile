all: server.c
	gcc server.c -o server

rm:
	rm server
