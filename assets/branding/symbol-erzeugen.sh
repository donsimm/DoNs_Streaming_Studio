#!/usr/bin/env bash
# Erzeugt das Programm-Symbol (P10a, Teil B) aus logo-fernseher-2400x1800.png: Quadrat um den
# sichtbaren Fernseher, Ränder durchsichtig, in den Grössen der Ordner «apps» des Themas hicolor
# auf dem Studio-PC (/usr/share/icons/hicolor/index.theme, geprüft 2026-11-03).
#
# Sichtbarer Kasten (Alpha ab 1): x 388 bis 2031, y 312 bis 1562 (1644 x 1251), gemessen mit
#   ffmpeg -i logo-fernseher-2400x1800.png -vf "alphaextract,bbox=min_val=1" -f null -
# Quadrat 1644 x 1644 ab (388, 115): oben 197, unten 196 Pixel durchsichtig.
# Skalieren mit vormultipliziertem Alpha, damit an den Rändern keine dunklen Säume entstehen
# (Filter premultiply/unpremultiply, scale mit Lanczos):
#   https://ffmpeg.org/ffmpeg-filters.html#premultiply
#   https://ffmpeg.org/ffmpeg-filters.html#scale
set -eu
cd "$(dirname "$(readlink -f "$0")")"
mkdir -p symbol
for n in 16 22 24 32 48 64 128 256 512; do
    ffmpeg -hide_banner -loglevel error -y -i logo-fernseher-2400x1800.png \
        -vf "crop=1644:1644:388:115,premultiply=inplace=1,scale=$n:$n:flags=lanczos,unpremultiply=inplace=1,format=rgba" \
        -frames:v 1 "symbol/dons-streaming-studio-$n.png"
done
