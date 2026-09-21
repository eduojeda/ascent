#!/bin/sh
# Downloads the official SDL3 MinGW development packages used by the Windows
# build, which is cross-compiled from macOS with Homebrew's mingw-w64.
#
# Only the x86_64 slice is kept: headers and import libraries to build
# against, and the two DLLs that ship next to Ascent.exe.
#
# Idempotent: skips work once third_party/windows holds both DLLs.

set -eu

SDL3_VER=3.4.16
SDL3_IMAGE_VER=3.4.6

root=$(cd "$(dirname "$0")/.." && pwd)
vendor="$root/third_party"
out="$vendor/windows"

if [ -f "$out/bin/SDL3.dll" ] && [ -f "$out/bin/SDL3_image.dll" ]; then
	exit 0
fi

mkdir -p "$out"

fetch() {
	name=$1 ver=$2 url=$3 tar="$vendor/$name-mingw.tar.gz"
	if [ ! -f "$tar" ]; then
		echo "fetching $name"
		curl -fsSL --retry 3 -o "$tar.part" "$url"
		mv "$tar.part" "$tar"
	fi
	tmp="$vendor/tmp-$name"
	rm -rf "$tmp"
	mkdir -p "$tmp"
	tar -xzf "$tar" -C "$tmp"
	# the package holds i686 and x86_64 trees; merge the x86_64 one into $out
	cp -R "$tmp/$name-$ver/x86_64-w64-mingw32/." "$out/"
	rm -rf "$tmp"
}

base=https://github.com/libsdl-org
fetch SDL3 "$SDL3_VER" \
	"$base/SDL/releases/download/release-$SDL3_VER/SDL3-devel-$SDL3_VER-mingw.tar.gz"
fetch SDL3_image "$SDL3_IMAGE_VER" \
	"$base/SDL_image/releases/download/release-$SDL3_IMAGE_VER/SDL3_image-devel-$SDL3_IMAGE_VER-mingw.tar.gz"

echo "windows sdl ready in $out"
