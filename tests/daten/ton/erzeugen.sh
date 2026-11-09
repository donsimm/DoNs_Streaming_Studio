#!/bin/sh
# Erzeugt die Testdateien «Blitz und Piep» für den Ton von Videos (P12a, P12b). Nur erzeugte
# Dateien, eingefroren im Repo (Regel 7). Beschreibung in LIESMICH.md.
#
# 60 s, 1280x720. Alle 2 s ist ein Bild ganz weiss («Blitz»), gleichzeitig läuft ein Ton von
# 1 kHz genau ein Bild lang («Piep»); sonst schwarzes Bild mit Zähler und Stille. Vorbild ist das
# Messsignal «flash» und «blip» aus EBU R37 (weisses Bild ein Bild lang, 1 kHz ebenso lang).
# Blitz ist das erste Bild, das in einem neuen 2-s-Abschnitt beginnt (bei ganzzahliger Bildrate
# genau bei 0, 2, 4 … s). Der Piep beginnt mit dem Blitzbild und endet mit ihm, auf die Probe
# genau (Rechnung über die Bildnummer der Probe).
#
# Aufruf: sh tests/daten/ton/erzeugen.sh [Bildrate]   (schreibt in den Ordner dieses Skripts)
#   Bildrate als Zahl oder Bruch wie bei ffmpeg (30, 60, 30000/1001), Standard 30. Die Bildrate
#   von OBS liest Lotse mit GetVideoSettings (fpsNumerator/fpsDenominator); weicht sie ab, werden
#   die Dateien mit ihr neu erzeugt und LIESMICH.md nachgeführt.
# Quellen: https://ffmpeg.org/ffmpeg-filters.html (aevalsrc, color, drawbox, drawtext mit
#            textfile und Textausdehnung %{frame_num} %{pts} %{eif}, Zeitachse mit enable)
#          https://ffmpeg.org/ffmpeg-utils.html (Ausdrücke: floor, eq, gt, st, ld)
#          https://ffmpeg.org/ffmpeg-codecs.html (aac, libopus, libx264, libvpx-vp9)
#          https://tech.ebu.ch/publications/r037 (EBU R37-2007, «flash» und «blip»)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
FPS=${1:-30}
FONT="$DIR/../../../src/common/schriften/be-vietnam-pro/BeVietnamPro-Regular.ttf"
DUR=60
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FF="ffmpeg -loglevel error -y -nostdin"

# Bild n ist ein Blitz, wenn es als erstes in einem neuen 2-s-Abschnitt beginnt (n = 0 immer).
# 1e-6 fängt Rundungsfehler ab, wenn n/(2*FPS) genau ganzzahlig ist.
BLITZ_N="eq(n,0)+gt(floor(n/(2*($FPS))+1e-6),floor((n-1)/(2*($FPS))+1e-6))"
# Dasselbe für die Bildnummer der Probe zur Zeit t (in ld(0)), für den Ton
BLITZ_T="st(0,floor(t*($FPS)+1e-6));eq(ld(0),0)+gt(floor(ld(0)/(2*($FPS))+1e-6),floor((ld(0)-1)/(2*($FPS))+1e-6))"
PIEP="if($BLITZ_T,0.5*sin(2*PI*1000*t),0)"

# Bild: schwarz mit Zähler (Bildnummer, Zeit, Nummer des Blitzes), beim Blitz ganz weiss.
# Text grau, damit er auf Schwarz und Weiss lesbar bleibt.
printf 'Bild %%{frame_num}\nZeit %%{pts:hms}\nBlitz %%{eif:floor(n/(2*(%s))+1e-6):d}\n%s Bilder/s' \
    "$FPS" "$FPS" > "$TMP/zaehler.txt"
VF="drawbox=x=0:y=0:w=iw:h=ih:color=white:t=fill:enable='$BLITZ_N',drawtext=fontfile=$FONT:textfile=$TMP/zaehler.txt:fontsize=56:fontcolor=0x808080:line_spacing=12:x=(w-tw)/2:y=(h-th)/2,format=yuv420p"
# Schlüsselbild alle 2 s (auf dem Blitz), damit Springen an den Anfang billig bleibt
KEY="expr:gte(t,n_forced*2)"
$FF -f lavfi -i "color=c=black:s=1280x720:r=$FPS:d=$DUR" -vf "$VF" -force_key_frames "$KEY" \
    -c:v libx264 -preset veryslow -crf 32 -bitexact "$TMP/bild.mp4"
$FF -f lavfi -i "color=c=black:s=1280x720:r=$FPS:d=$DUR" -vf "$VF" -force_key_frames "$KEY" \
    -c:v libvpx-vp9 -crf 50 -b:v 0 -row-mt 1 -bitexact "$TMP/bild.webm"

# Ton: Piep zu den Blitzen. Rate, Kanalbelegung, Ausdruck je Kanal (mit | getrennt)
ton() { # Datei, Rate, Kanalbelegung, Ausdrücke
    $FF -f lavfi -i "aevalsrc=exprs='$4':s=$2:c=$3:d=$DUR" -c:a pcm_f32le "$TMP/$1"
}
ton stereo-48000.wav 48000 stereo "$PIEP|$PIEP"
ton mono-48000.wav 48000 mono "$PIEP"
ton stereo-44100.wav 44100 stereo "$PIEP|$PIEP"
# 5.1 (FL FR FC LFE BL BR): Piep in allen Kanälen ausser LFE (AAC beschneidet LFE oben)
ton 5.1-48000.wav 48000 5.1 "$PIEP|$PIEP|$PIEP|0|$PIEP|$PIEP"

mp4() { # Datei, Ton (leer = ohne Ton)
    if [ -n "$2" ]; then
        $FF -i "$TMP/bild.mp4" -i "$TMP/$2" -map 0:v -map 1:a -c:v copy -c:a aac -b:a 128k \
            -bitexact -movflags +faststart "$DIR/$1"
    else
        $FF -i "$TMP/bild.mp4" -map 0:v -c:v copy -bitexact -movflags +faststart "$DIR/$1"
    fi
}
mp4 blitz-piep.mp4 stereo-48000.wav
mp4 blitz-piep-mono.mp4 mono-48000.wav
mp4 blitz-piep-44100.mp4 stereo-44100.wav
mp4 blitz-piep-5.1.mp4 5.1-48000.wav
mp4 blitz-ohne-ton.mp4 ""
$FF -i "$TMP/bild.webm" -i "$TMP/stereo-48000.wav" -map 0:v -map 1:a -c:v copy -c:a libopus \
    -b:a 128k -bitexact "$DIR/blitz-piep.webm"
