PREFIX ?= /usr/local

VERSION = 1.0.1

CC = gcc
CFLAGS = -g -Wall -Wextra -pedantic -std=c99 -O2
CFLAGS += -D_DEFAULT_SOURCE -D_FORTIFY_SOURCE=2
CFLAGS += -DVERSION=\"$(VERSION)\"
CFLAGS += $(shell pkg-config --cflags wayland-client xkbcommon cairo)
CFLAGS += -I/usr/include/freetype2/
LDFLAGS = -lwayland-client -lwayland-cursor -lxkbcommon -lrt -lcairo -lpthread $(shell pkg-config --libs cairo)

MAIN_SRC = src/main.c
MAIN_OBJ = src/main.o
CONFIG_OBJ = src/config.o
KEY_LISTENER_OBJ = src/key_listener.o
WL_OBJS = src/layer-shell.o src/virtual-pointer.o src/xdg-output.o src/xdg-shell.o

all: $(MAIN_OBJ) $(CONFIG_OBJ) $(KEY_LISTENER_OBJ) $(WL_OBJS)
	@mkdir -p bin
	$(CC) -o bin/hyprwarp $(MAIN_OBJ) $(CONFIG_OBJ) $(KEY_LISTENER_OBJ) $(WL_OBJS) $(LDFLAGS)

clean:
	rm -f $(MAIN_OBJ) $(CONFIG_OBJ) $(KEY_LISTENER_OBJ) $(WL_OBJS)
	rm -rf bin

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m755 bin/hyprwarp $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/hyprwarp

.PHONY: all clean install uninstall
