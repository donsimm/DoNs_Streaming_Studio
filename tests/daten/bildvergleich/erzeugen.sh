#!/bin/sh
# Erzeugt die Testdateien des Bildvergleichs (P11, K-08). Nur erzeugte Dateien (Entscheid des
# Nutzers, 2026-11-08), eingefroren im Repo (Regel 7). Beschreibung in LIESMICH.md.
#
# Farbfelder: 8 x 4 deckende Felder (Grundfarben, 75-%-Balken, Graustufen, Hauttöne und
# Mitteltöne), Kanten auf geraden Pixeln (4:2:0). Jedes Farbvideo hat 10 gleiche Bilder mit 30
# Bildern/s. «ohne» heisst ohne Farbangabe im Bitstrom und im Container; kodiert ist es nach der
# üblichen Regel (unter 720 Pixel Höhe BT.601, sonst BT.709), eingeschränkter Bereich.
# 1280x719 aus dem Plan geht mit 4:2:0 nicht (Höhe muss gerade sein), deshalb 1280x718.
#
# Aufruf: sh tests/daten/bildvergleich/erzeugen.sh   (schreibt in den Ordner dieses Skripts)
# Quellen: https://ffmpeg.org/ffmpeg-filters.html (drawbox, scale: out_color_matrix, out_range;
#            setparams, geq, transpose, hflip, vflip, format)
#          https://ffmpeg.org/ffmpeg.html (-display_rotation)
#          https://trac.ffmpeg.org/wiki/Encode/VP9 (-lossless, Alpha mit yuva420p)
set -e
DIR=$(cd "$(dirname "$0")" && pwd)
EXIF="$DIR/../exif"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
FF="ffmpeg -loglevel error -y -nostdin"

# Farben der Felder, Zeile für Zeile (RRGGBB)
ROWS="FF0000 00FF00 0000FF 00FFFF FF00FF FFFF00 FFFFFF 000000
BFBFBF BFBF00 00BFBF 00BF00 BF00BF BF0000 0000BF 202020
000000 242424 494949 6D6D6D 929292 B6B6B6 DBDBDB FFFFFF
FFDBAC F1C27D E0AC69 C68642 8D5524 5C3836 3F7F3F 3F3F7F"

# Vorlage W x H als PNG (rgb24): Feldbreite und -höhe gerade, Rest unten und rechts schwarz.
fields() { # Breite, Höhe, Datei
    fw=$(( $1 / 16 * 2 )); fh=$(( $2 / 8 * 2 ))
    filter="format=rgb24"; row=0
    echo "$ROWS" | while read -r line; do
        col=0
        for c in $line; do
            printf ',drawbox=x=%d:y=%d:w=%d:h=%d:color=0x%s:t=fill' $(( col * fw )) $(( row * fh )) "$fw" "$fh" "$c"
            col=$(( col + 1 ))
        done
        row=$(( row + 1 ))
    done > "$TMP/boxes"
    $FF -f lavfi -i "color=c=black:s=$1x$2" -vf "$filter$(cat "$TMP/boxes")" -frames:v 1 "$3"
}

# Farbangaben als Eigenschaften der Bilder (setparams): ffmpeg 7.1 übernimmt sie von dort in den
# Encoder; -color_primaries und -color_trc auf der Befehlszeile kamen nicht an (mit ffprobe geprüft).
OHNE="setparams=colorspace=unknown:color_primaries=unknown:color_trc=unknown:range=unknown"
BT709="setparams=colorspace=bt709:color_primaries=bt709:color_trc=bt709:range=tv"
BT601="setparams=colorspace=smpte170m:color_primaries=smpte170m:color_trc=smpte170m:range=tv"
VOLL="setparams=colorspace=bt709:color_primaries=bt709:color_trc=bt709:range=pc"

# H.264 aus der Vorlage. Matrix (bt601/bt709), Bereich (tv/pc), Angaben (OHNE, BT709 …)
h264() { # Datei, Breite, Höhe, Matrix, Bereich, Angaben
    fields "$2" "$3" "$TMP/vorlage.png"
    $FF -loop 1 -i "$TMP/vorlage.png" -vf "scale=out_color_matrix=$4:out_range=$5,format=yuv420p,$6" \
        -frames:v 10 -r 30 -c:v libx264 -crf 10 -preset veryslow -bitexact -movflags +faststart "$DIR/$1"
}

# VP9 verlustfrei (im YUV-Raum) aus der Vorlage
vp9() { # Datei, Breite, Höhe, Matrix, Angaben
    fields "$2" "$3" "$TMP/vorlage.png"
    $FF -loop 1 -i "$TMP/vorlage.png" -vf "scale=out_color_matrix=$4:out_range=tv,format=yuv420p,$5" \
        -frames:v 10 -r 30 -c:v libvpx-vp9 -lossless 1 -row-mt 1 -bitexact "$DIR/$1"
}

# Vorlage in voller Grösse (Vergleich mit den gewünschten Farben)
fields 1920 1080 "$DIR/farbfelder.png"

h264 h264-640x360-ohne.mp4 640 360 bt601 tv "$OHNE"
h264 h264-1280x718-ohne.mp4 1280 718 bt601 tv "$OHNE"
h264 h264-1280x720-ohne.mp4 1280 720 bt709 tv "$OHNE"
h264 h264-1920x1080-ohne.mp4 1920 1080 bt709 tv "$OHNE"
h264 h264-1920x1080-bt709.mp4 1920 1080 bt709 tv "$BT709"
h264 h264-1920x1080-bt601.mp4 1920 1080 bt601 tv "$BT601"
h264 h264-1920x1080-voll.mp4 1920 1080 bt709 pc "$VOLL"
vp9 vp9-640x360-ohne.webm 640 360 bt601 "$OHNE"
vp9 vp9-1920x1080-ohne.webm 1920 1080 bt709 "$OHNE"
vp9 vp9-1920x1080-bt709.webm 1920 1080 bt709 "$BT709"

# Festes Einzelbild (K-05): 10 verschiedene Graubilder (0 und 255 im Wechsel), das letzte 128.
# Mit «Am Ende anhalten» müssen beide Zeichner das letzte Bild zeigen.
$FF -f lavfi -i "color=c=black:s=320x180:r=30:d=1" \
    -vf "format=gray,geq=lum='if(eq(N\,9)\,128\,mod(N\,2)*255)',scale=out_color_matrix=bt709:out_range=tv,format=yuv420p,$BT709" \
    -frames:v 10 -c:v libx264 -crf 10 -preset veryslow -bitexact -movflags +faststart "$DIR/bildwechsel.mp4"

# WebM mit Drehangabe (Punkt 80), Bild wie ../video-drehung/: erst die Drehung zeigt es richtig.
# -display_rotation zählt gegen den Uhrzeigersinn (siehe ../video-drehung/erzeugen.sh).
webm_turn() { # Datei, Filter für das gespeicherte Bild, Winkel für -display_rotation
    $FF -loop 1 -i "$EXIF/orientierung-1.png" -vf "$2,format=yuv420p" -frames:v 10 -r 30 \
        -c:v libvpx-vp9 -lossless 1 -bitexact "$TMP/$1"
    $FF -display_rotation "$3" -i "$TMP/$1" -c copy -bitexact "$DIR/$1"
}
webm_turn drehung-90.webm transpose=cclock 270
webm_turn drehung-180.webm hflip,vflip 180
webm_turn drehung-270.webm transpose=clock 90

# WebM mit Alpha: 128x64, Viertel rot/grün/blau/gelb, Alpha steigt von links (0) nach rechts (255)
$FF -f lavfi -i "color=c=black:s=128x64:r=30:d=1" \
    -vf "format=rgba,geq=r='if(lt(Y\,H/2)\,if(lt(X\,W/2)\,255\,0)\,if(lt(X\,W/2)\,0\,255))':g='if(lt(Y\,H/2)\,if(lt(X\,W/2)\,0\,255)\,if(lt(X\,W/2)\,0\,255))':b='if(lt(Y\,H/2)\,0\,if(lt(X\,W/2)\,255\,0))':a='X*255/(W-1)',format=yuva420p" \
    -frames:v 10 -c:v libvpx-vp9 -lossless 1 -auto-alt-ref 0 -bitexact "$DIR/alpha.webm"

# Bilder: PNG mit Alpha wie oben (rgba), Bild mit feinen Einzelheiten zum Verkleinern
$FF -f lavfi -i "color=c=black:s=128x64" \
    -vf "format=rgba,geq=r='if(lt(Y\,H/2)\,if(lt(X\,W/2)\,255\,0)\,if(lt(X\,W/2)\,0\,255))':g='if(lt(Y\,H/2)\,if(lt(X\,W/2)\,0\,255)\,if(lt(X\,W/2)\,0\,255))':b='if(lt(Y\,H/2)\,0\,if(lt(X\,W/2)\,255\,0))':a='X*255/(W-1)'" \
    -frames:v 1 "$DIR/alpha.png"
$FF -f lavfi -i "testsrc=s=320x240:r=1" -frames:v 1 "$DIR/einzelheiten.png"
