#!/bin/sh
# Builds the round UI on this PC and renders every screen to out/*.png.
# Needs g++ and python3 with Pillow. Fetches TFT_eSPI's fonts the first time.
set -e
cd "$(dirname "$0")"
[ -d .tft ] || git clone -q --depth 1 --branch V2.5.43 https://github.com/Bodmer/TFT_eSPI.git .tft
mkdir -p out
g++ -std=gnu++17 -O1 -w -Istub -I.tft -I../../src -I../../src/round \
  preview.cpp tft_stub.cpp host_hw.cpp ../../src/monster.cpp ../../src/render.cpp ../../src/battle.cpp \
  -o out/preview
./out/preview
python3 make_pngs.py
