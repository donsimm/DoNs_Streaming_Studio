#!/bin/sh
# Erzeugt die Testvideos mit Drehung in den Metadaten (P10a, Teil A 5, Punkt 65).
#
# Jedes Video zeigt nach der Drehung dasselbe Bild wie die EXIF-Testbilder (../exif/): 64x32
# Pixel in vier Vierteln, links oben rot, rechts oben grün, links unten blau, rechts unten gelb,
# 10 Bilder mit 30 Bildern/s. Gespeichert ist es so, dass erst die Drehung es richtig zeigt
# (bei 90° und 270° als 32x64). Die Drehung steht als Display-Matrix im MP4 (Box tkhd); ffmpeg
# schreibt sie aus der Option -display_rotation, deren Winkel gegen den Uhrzeigersinn zählt:
#   drehung-90.mp4   90° im Uhrzeigersinn  = -display_rotation 270, gespeichert transpose=cclock
#   drehung-180.mp4  180°                  = -display_rotation 180, gespeichert hflip,vflip
#   drehung-270.mp4  270° im Uhrzeigersinn = -display_rotation 90,  gespeichert transpose=clock
#   drehung-0.mp4    ohne Drehung (Vergleich)
#
# Aufruf: sh tests/daten/video-drehung/erzeugen.sh   (schreibt in den Ordner dieses Skripts)
# Quellen: https://ffmpeg.org/ffmpeg.html (Option -display_rotation),
#          https://ffmpeg.org/ffmpeg-filters.html (transpose, hflip, vflip)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
SRC="$DIR/../exif/orientierung-1.png"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
make() { # Name, Filter für das gespeicherte Bild, Winkel für -display_rotation (leer = keiner)
    ffmpeg -loglevel error -y -loop 1 -i "$SRC" -vf "$2,format=yuv420p" -frames:v 10 -r 30 \
        -c:v libx264 -crf 10 -preset veryslow -bitexact -movflags +faststart "$TMP/$1"
    if [ -n "$3" ]; then
        ffmpeg -loglevel error -y -display_rotation "$3" -i "$TMP/$1" -c copy -bitexact "$DIR/$1"
    else
        cp "$TMP/$1" "$DIR/$1"
    fi
}
make drehung-0.mp4 null ""
make drehung-90.mp4 transpose=cclock 270
make drehung-180.mp4 hflip,vflip 180
make drehung-270.mp4 transpose=clock 90
