.POSIX:

CC=cc
CFLAGS_EXTRA=
CFLAGS=-O2 -Ibuild/include -Wall -Wextra -Wpedantic -std=c11 -g $(CFLAGS_EXTRA)
# set these in command line for debug mode
DESTDIR=
PREFIX=/usr

.PHONY=clean check uninstall

build: build/zzzclip

check: build
	test/test-all.sh

install: build
	install -Dm755 build/zzzclip $(DESTDIR)$(PREFIX)/bin

uninstall:
	rm $(DESTDIR)$(PREFIX)/bin/zzzclip

clean:
	rm -r build/*

build/zzzclip: main.c \
		build/ext-data-control-protocol.o \
		build/wlr-data-control-protocol.o \
		build/util.o \
		build/xmalloc.o \
		build/zzz_list.o \
		build/read_config.o \
		build/parse_config.o \
		build/store.o \
		build/symlink_manager.o \
		build/data_control_wrapper.o \
		build/daemon.o \
		build/getter.o \
		build/display.o \
		build/registry.o
	@# cflags at the end... awful.... do it for linker flags
	$(CC) -o build/zzzclip main.c build/*.o -lwayland-client $(CFLAGS)

build/daemon.o: daemon.c daemon.h
	$(CC) $(CFLAGS) -c -o build/daemon.o daemon.c

build/data_control_wrapper.o: data_control_wrapper.c data_control_wrapper.h
	$(CC) $(CFLAGS) -c -o build/data_control_wrapper.o data_control_wrapper.c

build/getter.o: getter.c getter.h
	$(CC) $(CFLAGS) -c -o build/getter.o getter.c

build/display.o: display.c display.h
	$(CC) $(CFLAGS) -c -o build/display.o display.c

build/registry.o: registry.c registry.h
	$(CC) $(CFLAGS) -c -o build/registry.o registry.c

build/read_config.o: read_config.c read_config.h
	$(CC) $(CFLAGS) -c -o build/read_config.o read_config.c

build/parse_config.o: parse_config.c parse_config.h
	$(CC) $(CFLAGS) -c -o build/parse_config.o parse_config.c

build/store.o: store.c store.h
	$(CC) $(CFLAGS) -c -o build/store.o store.c

build/symlink_manager.o: symlink_manager.c symlink_manager.h
	$(CC) $(CFLAGS) -c -o build/symlink_manager.o symlink_manager.c

build/util.o: util.c util.h
	$(CC) $(CFLAGS) -c -o build/util.o util.c

build/xmalloc.o: xmalloc.c xmalloc.h
	$(CC) $(CFLAGS) -c -o build/xmalloc.o xmalloc.c

build/zzz_list.o: zzz_list.c zzz_list.h
	$(CC) $(CFLAGS) -c -o build/zzz_list.o zzz_list.c

build/wlr-data-control-protocol.o: build/wlr-data-control-protocol.c
	$(CC) $(CFLAGS) -c -o build/wlr-data-control-protocol.o build/wlr-data-control-protocol.c

build/ext-data-control-protocol.o: build/ext-data-control-protocol.c
	$(CC) $(CFLAGS) -c -o build/ext-data-control-protocol.o build/ext-data-control-protocol.c

build/wlr-data-control-protocol.c: build/include/wlr-data-control-protocol.h protocols/wlr-data-control-unstable-v1.xml
	wayland-scanner private-code < protocols/wlr-data-control-unstable-v1.xml > build/wlr-data-control-protocol.c

build/ext-data-control-protocol.c: build/include/ext-data-control-protocol.h protocols/ext-data-control-v1.xml
	wayland-scanner private-code < protocols/ext-data-control-v1.xml > build/ext-data-control-protocol.c

build/include/wlr-data-control-protocol.h: protocols/wlr-data-control-unstable-v1.xml
	mkdir -p build/include
	wayland-scanner client-header < protocols/wlr-data-control-unstable-v1.xml > build/include/wlr-data-control-protocol.h

build/include/ext-data-control-protocol.h: protocols/ext-data-control-v1.xml
	mkdir -p build/include
	wayland-scanner client-header < protocols/ext-data-control-v1.xml > build/include/ext-data-control-protocol.h
