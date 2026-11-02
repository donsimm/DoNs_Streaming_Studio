#!/bin/sh
# Erzeugt die Testdateien für T-02 mit ffmpeg (Debian-Paket ffmpeg).
# Aufruf: sh prototypes/messung/media_erzeugen.sh [Zielordner]
# Standard-Zielordner: prototypes/media (nicht im Repo, siehe .gitignore)
#
# Quellen:
#   VP9 mit Alpha (yuva420p, libvpx-vp9): https://trac.ffmpeg.org/wiki/Encode/VP9
#   Filter geq / testsrc2:               https://ffmpeg.org/ffmpeg-filters.html
set -eu

DIR="${1:-$(dirname "$0")/../media}"
mkdir -p "$DIR"
FF="ffmpeg -hide_banner -loglevel error -y"

# Fünf Bilder 256x256 mit Alpha: weiche Scheiben in verschiedenen Farben
i=1
for col in "255:80:80" "80:200:120" "80:140:255" "250:200:60" "200:90:220"; do
  r=${col%%:*}; rest=${col#*:}; g=${rest%%:*}; b=${rest#*:}
  $FF -f lavfi -i "nullsrc=s=256x256,format=rgba,geq=r=$r:g=$g:b=$b:a='255*clip(1.25-hypot(X-128,Y-128)/100,0,1)'" \
      -frames:v 1 "$DIR/bild$i.png"
  i=$((i + 1))
done

# Video ohne Alpha: 960x540, 30 Bilder/s, 10 s, VP9 in WebM
$FF -f lavfi -i "testsrc2=s=960x540:r=30:d=10" \
    -c:v libvpx-vp9 -b:v 2M -pix_fmt yuv420p -deadline good -cpu-used 4 "$DIR/video.webm"

# WebM mit Alpha: 512x512, 30 Bilder/s, 10 s, VP9 yuva420p.
# Alpha: kreisende, voll deckende Scheibe; oben ein Streifen mit Verlauf 0..255
# (Teiltransparenz); Rest voll transparent.
$FF -f lavfi -i "testsrc2=s=512x512:r=30:d=10,format=rgba,geq=r='r(X,Y)':g='g(X,Y)':b='b(X,Y)':a='if(lt(hypot(X-256-140*cos(2*PI*T/5),Y-256-140*sin(2*PI*T/5)),110),255,if(lt(Y,64),X/2,0))'" \
    -c:v libvpx-vp9 -b:v 2M -pix_fmt yuva420p -auto-alt-ref 0 -deadline good -cpu-used 4 "$DIR/alpha.webm"

ls -l "$DIR"
