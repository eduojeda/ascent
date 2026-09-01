#!/bin/bash
# Generate placeholder sounds for the Ascent SDL3 port.
# Synth SFX come from gen_sfx.py; voice lines from macOS `say`.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/assets/sounds"
mkdir -p "$OUT"

echo "== Synthesized SFX =="
python3 "$ROOT/tools/gen_sfx.py"

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

VOICE_EXCITED="Rocko (English (US))"
VOICE_ANNOUNCER="Reed (English (US))"

have_voice() { say -v '?' | grep -qF "$1"; }
have_voice "$VOICE_EXCITED" || VOICE_EXCITED="Samantha"
have_voice "$VOICE_ANNOUNCER" || VOICE_ANNOUNCER="Fred"

speak() { # speak <voice> <text> <out.wav>
    say -v "$1" -o "$TMP/v.aiff" "$2"
    afconvert -f WAVE -d LEI16@22050 -c 1 "$TMP/v.aiff" "$3"
}

echo "== Voice (announcer: $VOICE_ANNOUNCER) =="
speak "$VOICE_ANNOUNCER" "three!" "$OUT/three.wav"
speak "$VOICE_ANNOUNCER" "two!" "$OUT/two.wav"
speak "$VOICE_ANNOUNCER" "one!" "$OUT/one.wav"
speak "$VOICE_ANNOUNCER" "go!" "$OUT/go.wav"

echo "== Voice (excited: $VOICE_EXCITED) =="
speak "$VOICE_EXCITED" "woo hoo!" "$OUT/woohoo.wav"
speak "$VOICE_EXCITED" "wow!" "$OUT/wow.wav"
speak "$VOICE_EXCITED" "joy!" "$OUT/joy.wav"
speak "$VOICE_EXCITED" "not bad!" "$OUT/not-bad.wav"
speak "$VOICE_EXCITED" "cool!" "$OUT/cool1.wav"
speak "$VOICE_EXCITED" "so cool!" "$OUT/cool2.wav"
speak "$VOICE_EXCITED" "whoa!" "$OUT/whoa.wav"
speak "$VOICE_EXCITED" "shove it!" "$OUT/shove.wav"
speak "$VOICE_EXCITED" "I'm gonna smack you!" "$OUT/smack-you.wav"
speak "$VOICE_EXCITED" "not cool, man!" "$OUT/not-cool.wav"
speak "$VOICE_EXCITED" "you fucker!" "$OUT/you-fucker.wav"
speak "$VOICE_EXCITED" "this sucks!" "$OUT/this-sux.wav"
speak "$VOICE_EXCITED" "oh!" "$OUT/oh.wav"
speak "$VOICE_EXCITED" "I love you, you love me" "$OUT/barney.wav"

echo "== clickclick-oh-shit =="
python3 "$ROOT/tools/gen_sfx.py" clicks "$TMP/clicks.wav"
speak "$VOICE_EXCITED" "oh shit!" "$TMP/ohshit.wav"
python3 - "$TMP/clicks.wav" "$TMP/ohshit.wav" "$OUT/clickclick-oh-shit.wav" <<'EOF'
import sys, wave
data = b""
for p in sys.argv[1:3]:
    with wave.open(p, "rb") as w:
        assert (w.getnchannels(), w.getsampwidth(), w.getframerate()) == (1, 2, 22050), p
        data += w.readframes(w.getnframes())
with wave.open(sys.argv[3], "wb") as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(22050)
    w.writeframes(data)
EOF

echo "Done: $(ls "$OUT"/*.wav | wc -l | tr -d ' ') WAV files in $OUT"
