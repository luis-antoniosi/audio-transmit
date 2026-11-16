.PHONY: all listener

all:
	gcc main.c -lm -lpthread -o server.out

listener:
	gcc listener.c -lm -lpthread -o listener.c
