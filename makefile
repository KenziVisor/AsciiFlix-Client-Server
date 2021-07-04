make all:
	gcc Server.c -o film_server -lpthread
	gcc Client.c -o flix_control -lpthread
clean:
	rm film_server
	rm flix_control
