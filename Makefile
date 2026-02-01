CC=gcc
CFLAGS=-O0 -Ibuild/include -Wall -Wextra -Wpedantic -std=c99 -g -fsanitize=address

.PHONY=run clean

build: build/zzz build/zzz_get

run: build
	build/zzz

clean:
	rm -r build/*

build/zzz: main.c build/wlr-data-control-protocol.o build/zzz_list.o build/read_config.o build/pref_parse.o
	$(CC) $(CFLAGS) -lwayland-client -lpcre2-8 -o build/zzz main.c build/wlr-data-control-protocol.o build/zzz_list.o build/read_config.o build/pref_parse.o

build/zzz_get: zzz_get.c build/wlr-data-control-protocol.o
	$(CC) $(CFLAGS) -lwayland-client -o build/zzz_get zzz_get.c build/wlr-data-control-protocol.o build/zzz_list.o

build/read_config.o: read_config.c read_config.h
	$(CC) $(CFLAGS) -c -o build/read_config.o read_config.c

build/pref_parse.o: pref_parse.c pref_parse.h
	$(CC) $(CFLAGS) -c -o build/pref_parse.o pref_parse.c

build/zzz_list.o: zzz_list.c zzz_list.h
	$(CC) $(CFLAGS) -c -o build/zzz_list.o zzz_list.c

build/wlr-data-control-protocol.o: build/wlr-data-control-protocol.c
	$(CC) $(CFLAGS) -lwayland-client -c -o build/wlr-data-control-protocol.o build/wlr-data-control-protocol.c

build/wlr-data-control-protocol.c: build/include/wlr-data-control-protocol.h protocols/wlr-data-control-unstable-v1.xml
	wayland-scanner private-code < protocols/wlr-data-control-unstable-v1.xml > build/wlr-data-control-protocol.c

build/include/wlr-data-control-protocol.h: protocols/wlr-data-control-unstable-v1.xml
	mkdir -p build/include
	wayland-scanner client-header < protocols/wlr-data-control-unstable-v1.xml > build/include/wlr-data-control-protocol.h
