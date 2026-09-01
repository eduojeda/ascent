CC      = cc
CFLAGS  = -std=c11 -O2 -Wall -Wno-unused-variable -Wno-unused-but-set-variable \
          $(shell pkg-config --cflags sdl3 sdl3-image)
LDFLAGS = $(shell pkg-config --libs sdl3 sdl3-image) -lm

GAME_SRCS = $(wildcard src/s*.c) src/misc.c src/gamma.c src/main.c
COMPAT_SRCS = src/compat/sat.c src/compat/sat_sound.c
SRCS = $(GAME_SRCS) $(COMPAT_SRCS)
OBJS = $(SRCS:.c=.o)

ascent: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c src/mySAT.h src/ascent.h src/gamma.h src/compat/mac_types.h
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: clean assets
clean:
	rm -f $(OBJS) ascent

assets:
	./tools/convert-art.sh
	./tools/gen-sounds.sh
