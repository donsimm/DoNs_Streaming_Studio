#!/bin/sh
# Erzeugt die Tondateien ohne Bild für die Medienart «Ton» des Chat-Bots (P14 t). Nur erzeugte
# Dateien, eingefroren im Repo (Regel 7). Beschreibung in LIESMICH.md, «Töne ohne Bild».
#
# 1 s lang: 0,1 s Stille, 0,2 s Sinus 1 kHz mit halber Vollaussteuerung («Piep»), danach Stille.
# Je eine Datei für die vier Endungen der Medienart «Ton» (mp3, wav, ogg, m4a), mit
# verschiedenen Raten und Kanälen, damit der Decoder auch umwandeln muss.
#
# Aufruf: sh tests/daten/ton/erzeugen-toene.sh   (schreibt in den Ordner dieses Skripts)
# Quellen: https://ffmpeg.org/ffmpeg-filters.html (aevalsrc, between)
#          https://ffmpeg.org/ffmpeg-codecs.html (pcm_s16le, libmp3lame, libvorbis, aac)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
FF="ffmpeg -loglevel error -y -nostdin"
PIEP="0.5*sin(2*PI*1000*t)*between(t,0.1,0.3)"

ton() { # Datei, Rate, Kanalbelegung, Codec und Optionen
    file=$1 rate=$2 layout=$3
    shift 3
    if [ "$layout" = mono ]; then exprs="$PIEP"; else exprs="$PIEP|$PIEP"; fi
    $FF -f lavfi -i "aevalsrc=exprs='$exprs':s=$rate:c=$layout:d=1" "$@" -bitexact "$DIR/$file"
}
ton ton-piep.wav 48000 stereo -c:a pcm_s16le
ton ton-piep.mp3 44100 stereo -c:a libmp3lame -b:a 128k
ton ton-piep.ogg 48000 stereo -c:a libvorbis -q:a 4
ton ton-piep.m4a 48000 mono -c:a aac -b:a 96k -movflags +faststart
