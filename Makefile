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

.PHONY: clean assets bundle
clean:
	rm -f $(OBJS) ascent
	rm -rf Ascent.app

assets:
	./tools/convert-art.sh
	./tools/gen-sounds.sh
	mkdir -p recovered
	ditto -xk "Juego PPC.zip" recovered/
	python3 tools/extract_rsrc.py \
	  "recovered/Juego PPC/Juego PPC/Ascent v1.0.1 ƒ/Ascent v1.0.1" \
	  recovered/resources
	tools/.venv/bin/python tools/install_original_assets.py

APP = Ascent.app/Contents
bundle: ascent
	rm -rf Ascent.app
	mkdir -p $(APP)/MacOS $(APP)/Resources
	cp ascent $(APP)/MacOS/Ascent
	cp -R assets $(APP)/Resources/assets
	printf '%s\n' \
	  '<?xml version="1.0" encoding="UTF-8"?>' \
	  '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' \
	  '<plist version="1.0"><dict>' \
	  '<key>CFBundleExecutable</key><string>Ascent</string>' \
	  '<key>CFBundleIdentifier</key><string>com.eduardoojeda.ascent</string>' \
	  '<key>CFBundleName</key><string>Ascent</string>' \
	  '<key>CFBundleVersion</key><string>1.0.1</string>' \
	  '<key>CFBundleShortVersionString</key><string>1.0.1</string>' \
	  '<key>CFBundlePackageType</key><string>APPL</string>' \
	  '<key>NSHighResolutionCapable</key><true/>' \
	  '</dict></plist>' > $(APP)/Info.plist
	codesign --force --sign - Ascent.app
