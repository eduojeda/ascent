#!/bin/zsh
# PICT -> PNG for everything in Development Stuff (via netpbm), then build
# the runtime assets. Requires: brew install netpbm; tools/.venv with pillow.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/art-png
find "Development Stuff" -type f ! -name "*.obj" ! -name "Icon_" ! -name "*Read Me*" -print0 |
while IFS= read -r -d '' f; do
	rel="${f#Development Stuff/}"
	out="build/art-png/$(echo "$rel" | tr '/ ' '__').png"
	if picttoppm "$f" 2>/dev/null | pnmtopng > "$out" 2>/dev/null && [ -s "$out" ]; then
		:
	else
		rm -f "$out"
	fi
done
tools/.venv/bin/python tools/build_assets.py
