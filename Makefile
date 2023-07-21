CC=gcc
CFLAGS=-O0 -Wall -Wextra -std=c99 -g

.PHONY=run

build: build/zzz build/zzz_get

build/zzz: main.c wlr-data-control-protocol.o
	$(CC) $(CFLAGS) -lwayland-client -lpcre2-8 -o build/zzz main.c wlr-data-control-protocol.o

build/zzz_get: zzz_get.c wlr-data-control-protocol.o
	$(CC) $(CFLAGS) -lwayland-client -o build/zzz_get zzz_get.c wlr-data-control-protocol.o

run: build
	build/zzz

wlr-data-control-protocol.o: wlr-data-control-protocol.c
	$(CC) $(CFLAGS) -lwayland-client -c -o wlr-data-control-protocol.o wlr-data-control-protocol.c

wlr-data-control-protocol.c: wlr-data-control-protocol.h protocols/wlr-data-control-unstable-v1.xml
	wayland-scanner private-code < protocols/wlr-data-control-unstable-v1.xml > wlr-data-control-protocol.c

wlr-data-control-protocol.h: protocols/wlr-data-control-unstable-v1.xml
	wayland-scanner client-header < protocols/wlr-data-control-unstable-v1.xml > wlr-data-control-protocol.h
