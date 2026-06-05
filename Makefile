CC=gcc
CFLAGS=-O0 -Ibuild/include -Wall -Wextra -Wpedantic -std=c11 -g -fsanitize=address

.PHONY=run clean debug

build: build/zzz

debug: CFLAGS += -O0 -g -fsanitize=address
debug: build build/event-viewer

run: build
	build/zzz

clean:
	rm -r build/*

build/event-viewer: build/zzz event_viewer.c
	$(CC) $(CFLAGS) -lwayland-client -lpcre2-8 -o build/event-viewer event_viewer.c build/*.o

build/zzz: main.c build/wlr-data-control-protocol.o build/zzz_list.o build/read_config.o build/parse_config.o build/storer.o build/daemon.o build/getter.o build/lister.o build/registry.o
	$(CC) $(CFLAGS) -lwayland-client -lpcre2-8 -o build/zzz main.c build/*.o

build/daemon.o: daemon.c daemon.h
	$(CC) $(CFLAGS) -c -o build/daemon.o daemon.c

build/getter.o: getter.c getter.h
	$(CC) $(CFLAGS) -c -o build/getter.o getter.c

build/lister.o: lister.c lister.h
	$(CC) $(CFLAGS) -c -o build/lister.o lister.c

build/registry.o: registry.c registry.h
	$(CC) $(CFLAGS) -c -o build/registry.o registry.c

build/read_config.o: read_config.c read_config.h
	$(CC) $(CFLAGS) -c -o build/read_config.o read_config.c

build/parse_config.o: parse_config.c parse_config.h
	$(CC) $(CFLAGS) -c -o build/parse_config.o parse_config.c

build/storer.o: storer.c storer.h
	$(CC) $(CFLAGS) -c -o build/storer.o storer.c

build/zzz_list.o: zzz_list.c zzz_list.h
	$(CC) $(CFLAGS) -c -o build/zzz_list.o zzz_list.c

build/wlr-data-control-protocol.o: build/wlr-data-control-protocol.c
	$(CC) $(CFLAGS) -lwayland-client -c -o build/wlr-data-control-protocol.o build/wlr-data-control-protocol.c

build/wlr-data-control-protocol.c: build/include/wlr-data-control-protocol.h protocols/wlr-data-control-unstable-v1.xml
	wayland-scanner private-code < protocols/wlr-data-control-unstable-v1.xml > build/wlr-data-control-protocol.c

build/include/wlr-data-control-protocol.h: protocols/wlr-data-control-unstable-v1.xml
	mkdir -p build/include
	wayland-scanner client-header < protocols/wlr-data-control-unstable-v1.xml > build/include/wlr-data-control-protocol.h
