CC	:=	gcc
CFLAGS	:=	-Wall -Wextra
LDFLAGS	:= -lm -lpthread
EXT	:=	.out

ifeq ($(OS),Windows_NT)
	LDFLAGS += -lws2_32
	EXT	:=	.exe
endif

.PHONY: all listener clean

all: server$(EXT) listener$(EXT)

server$(EXT): main.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

listener$(EXT): listener.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f server$(EXT) listener$(EXT)