server : server.c
	gcc server.c -lrt -o server
clean: 
	-rm -f server
