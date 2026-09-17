CC      = cc
WARN    = -Wall -Wno-unused-variable -Wno-unused-but-set-variable
CFLAGS  = -std=c11 -O2 $(WARN) $(shell pkg-config --cflags sdl3 sdl3-image)
LDFLAGS = $(shell pkg-config --libs sdl3 sdl3-image) -lm

GAME_SRCS = $(wildcard src/s*.c) src/misc.c src/gamma.c src/main.c
COMPAT_SRCS = src/compat/sat.c src/compat/sat_sound.c
SRCS = $(GAME_SRCS) $(COMPAT_SRCS)
OBJS = $(SRCS:.c=.o)

# ./ascent is the quick local build: host architecture, Homebrew SDL.
ascent: $(OBJS)
	$(CC) -o $@ $(OBJS) $(LDFLAGS)

%.o: %.c src/mySAT.h src/ascent.h src/gamma.h src/compat/mac_types.h
	$(CC) $(CFLAGS) -c $< -o $@

# Ascent.app is the build to hand to other people: universal, self-contained,
# and back-deployed. Homebrew's SDL3 is arm64-only and stamped with the build
# machine's macOS version, which makes an app that other Macs refuse to open.
FRAMEWORKS = third_party/frameworks
MIN_MACOS  = 11.0
ARCHS      = -arch arm64 -arch x86_64
DIST_FLAGS = -std=c11 -O2 $(WARN) $(ARCHS) -mmacosx-version-min=$(MIN_MACOS) \
             -F$(FRAMEWORKS)
DIST_LIBS  = -framework SDL3 -framework SDL3_image -lm \
             -Wl,-rpath,@executable_path/../Frameworks

.PHONY: clean assets bundle frameworks
clean:
	rm -f $(OBJS) ascent
	rm -rf Ascent.app

frameworks:
	./tools/fetch-frameworks.sh

assets:
	./tools/convert-art.sh
	./tools/gen-sounds.sh
	mkdir -p recovered
	ditto -xk "original/Juego PPC.zip" recovered/
	python3 tools/extract_rsrc.py \
	  "recovered/Juego PPC/Juego PPC/Ascent v1.0.1 ƒ/Ascent v1.0.1" \
	  recovered/resources
	tools/.venv/bin/python tools/install_original_assets.py
	tools/.venv/bin/python tools/build_hires_sprites.py
	tools/.venv/bin/python tools/make_icon.py

APP = Ascent.app/Contents
bundle: frameworks
	rm -rf Ascent.app
	mkdir -p $(APP)/MacOS $(APP)/Resources $(APP)/Frameworks
	$(CC) $(DIST_FLAGS) $(SRCS) -o $(APP)/MacOS/Ascent $(DIST_LIBS)
	cp -R $(FRAMEWORKS)/SDL3.framework $(APP)/Frameworks/
	cp -R $(FRAMEWORKS)/SDL3_image.framework $(APP)/Frameworks/
	rm -rf $(APP)/Frameworks/*.framework/Versions/A/Headers
	rm -rf $(APP)/Frameworks/*.framework/Headers
	cp -R assets $(APP)/Resources/assets
	cp assets/Ascent.icns $(APP)/Resources/Ascent.icns
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
	  '<key>CFBundleIconFile</key><string>Ascent</string>' \
	  '<key>LSMinimumSystemVersion</key><string>$(MIN_MACOS)</string>' \
	  '<key>NSHighResolutionCapable</key><true/>' \
	  '</dict></plist>' > $(APP)/Info.plist
	codesign --force --sign - $(APP)/Frameworks/SDL3.framework
	codesign --force --sign - $(APP)/Frameworks/SDL3_image.framework
	codesign --force --sign - Ascent.app
