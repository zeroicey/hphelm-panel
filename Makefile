# hphelm-panel —— 运维面板 + 自研锁屏（X11 + cairo + pango，无 GUI 工具包）
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
PKGS     = x11 x11-xcb xscrnsaver cairo cairo-xlib pangocairo
CFLAGS  += $(shell pkg-config --cflags $(PKGS))
LDLIBS  += $(shell pkg-config --libs $(PKGS)) -lm

BIN  = hphelm-panel
AUTH = hphelm-auth
OBJ  = src/panel.o src/metrics.o src/view.o src/view_panel.o src/view_login.o src/auth.o

all: $(BIN) $(AUTH)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

$(AUTH): src/hphelm-auth.c
	$(CC) -O2 -Wall -Wextra -o $@ $< -lpam

src/%.o: src/%.c src/metrics.h src/theme.h src/view.h
	$(CC) $(CFLAGS) -c -o $@ $<

install: all
	install -m 755 $(BIN) /usr/local/bin/$(BIN)
	install -d -m 755 /usr/local/libexec
	install -m 4755 -o root -g root $(AUTH) /usr/local/libexec/$(AUTH)

clean:
	rm -f $(OBJ) $(BIN) $(AUTH)

.PHONY: all clean install
