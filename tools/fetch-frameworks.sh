#!/bin/sh
# Downloads the official SDL3 macOS frameworks used by the shareable build.
#
# Homebrew's SDL3 is arm64-only and built for the host's macOS version, so an
# app linked against it refuses to launch on an older Mac ("You can't use this
# version of the application with this version of macOS"). The upstream
# frameworks are universal and target macOS 11.0, so they run anywhere.
#
# Idempotent: skips work once third_party/frameworks holds both frameworks.

set -eu

SDL3_VER=3.4.16
SDL3_IMAGE_VER=3.4.6

root=$(cd "$(dirname "$0")/.." && pwd)
vendor="$root/third_party"
out="$vendor/frameworks"

if [ -d "$out/SDL3.framework" ] && [ -d "$out/SDL3_image.framework" ]; then
	exit 0
fi

mkdir -p "$out"

fetch() {
	name=$1 url=$2 dmg="$vendor/$1.dmg"
	if [ ! -f "$dmg" ]; then
		echo "fetching $name"
		curl -fsSL --retry 3 -o "$dmg.part" "$url"
		mv "$dmg.part" "$dmg"
	fi
	mount="$vendor/mnt-$name"
	rm -rf "$mount"
	mkdir -p "$mount"
	hdiutil attach -nobrowse -readonly -quiet "$dmg" -mountpoint "$mount"
	# only the macOS slice; the dSYMs and the iOS/tvOS slices stay behind
	cp -R "$mount/$name.xcframework/macos-arm64_x86_64/$name.framework" "$out/"
	hdiutil detach -quiet "$mount"
	rmdir "$mount"
}

base=https://github.com/libsdl-org
fetch SDL3 "$base/SDL/releases/download/release-$SDL3_VER/SDL3-$SDL3_VER.dmg"
fetch SDL3_image \
	"$base/SDL_image/releases/download/release-$SDL3_IMAGE_VER/SDL3_image-$SDL3_IMAGE_VER.dmg"

echo "frameworks ready in $out"
