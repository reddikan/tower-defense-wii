#!/bin/sh
# Builds and runs the native test harness (no Wii toolchain needed). Frame dumps go to tools/out/.
set -e
cd "$(dirname "$0")/.."
mkdir -p tools/out
cc -O2 -std=c99 -Wall -Wextra -DGAME_DEBUG source/game.c source/audio.c tools/test.c -lm -o tools/out/ptest
tools/out/ptest tools/out
# optional: turn the dumped frames into PNGs (needs python3 + Pillow)
python3 tools/topng.py tools/out 3 2>/dev/null || echo "(skipping PNG export: python3/Pillow not available)"
# optional: export the sound effects as WAV files you can listen to
cc -O2 -std=c99 source/audio.c tools/audiotest.c -lm -o tools/out/audiotest
mkdir -p tools/out/sounds && tools/out/audiotest tools/out/sounds > /dev/null
echo "sound previews: tools/out/sounds/*.wav"
