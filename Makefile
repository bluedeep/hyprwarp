PREFIX ?= /usr/local

CC = gcc
CFLAGS = -g -Wall -Wextra -pedantic -std=c99 -O2
CFLAGS += -D_DEFAULT_SOURCE -D_FORTIFY_SOURCE=2
CFLAGS += $(shell pkg-config --cflags wayland-client xkbcommon cairo)
CFLAGS += -I/usr/include/freetype2/
LDFLAGS = -lwayland-client -lwayland-cursor -lxkbcommon -lrt -lcairo -lpthread $(shell pkg-config --libs cairo)

MAIN_SRC = src/main.c
MAIN_OBJ = src/main.o
WL_OBJS = src/layer-shell.o src/virtual-pointer.o src/xdg-output.o src/xdg-shell.o

all: $(MAIN_OBJ) $(WL_OBJS)
	@mkdir -p bin
	$(CC) -o bin/hyprwarp $(MAIN_OBJ) $(WL_OBJS) $(LDFLAGS)

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

clean:
	rm -f $(MAIN_OBJ) $(WL_OBJS)
	rm -rf bin

install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m755 bin/hyprwarp $(DESTDIR)$(PREFIX)/bin/

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/hyprwarp

.PHONY: all clean install uninstall
