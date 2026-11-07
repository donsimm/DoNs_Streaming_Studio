#!/bin/sh
# Bildschirmfoto P08 (Bericht): eigener Datenordner, eigener Kern, eigene Verbindung und eigenes
# XDG_CONFIG_HOME; die Daten des Nutzers werden nicht berührt.
#   sh docs/berichte/P08-daten/bildschirmfoto.sh BILD.png [--eigenschaften-art]
# Daten: Overlay «Test P08 Szene» im Ordner «Szenen» mit Bild, Video und Text (Text vorne),
# dazu «Chat Ecke» auf der obersten Ebene. Bild/Video aus prototypes/media, Schrift Be Vietnam Pro.
set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
WORK=$(mktemp -d)
RUN=$(mktemp -d "$XDG_RUNTIME_DIR/dss-foto-XXXXXX")
D="$WORK/daten"
CLI="$R/build/src/cli/dss-cli --datenordner $D"
$CLI overlay-neu "Test P08 Szene" 1920 1080 >/dev/null
$CLI overlay-neu "Chat Ecke" 800 600 >/dev/null
$CLI quelle-bild test-p08-szene "$R/prototypes/media/bild1.png" >/dev/null
$CLI quelle-video test-p08-szene "$R/prototypes/media/alpha.webm" --wiederholen >/dev/null
$CLI quelle-text test-p08-szene "Willkommen im Stream" "$R/src/common/schriften/be-vietnam-pro/BeVietnamPro-Regular.ttf" >/dev/null
$CLI quelle-geometrie test-p08-szene 3 120 860 900 140 2.5 >/dev/null
SZENE=$(basename "$(grep -l '"test-p08-szene"' "$D"/overlays/*.json)" .json)
CHAT=$(basename "$(grep -l '"chat-ecke"' "$D"/overlays/*.json)" .json)
# liste.json von Hand (dss-cli kennt keine Ordner): Ordner zuerst
cat > "$D/liste.json" <<J
{"entries":[{"expanded":true,"id":"5d2c8a0e-3f4b-4c6d-9e1f-2a3b4c5d6e7f","kind":"folder","name":"Szenen","overlays":["$SZENE"]},{"id":"$CHAT","kind":"overlay"}],"format_version":1}
J
$CLI overlay-liste
"$R/build/src/core/dss-core" --datenordner "$D" --verbindung "$RUN/kern.sock" 2> "$WORK/kern.log" &
CORE=$!
sleep 0.5
XDG_CONFIG_HOME="$WORK/cfg" "$R/build/src/app/dss-app" --verbindung "$RUN/kern.sock" --bildschirmfoto "$1" $2
wait $CORE
echo "Kern beendet mit $?"
cat "$WORK/kern.log"
test ! -e "$WORK/cfg" && echo "keine Einstellungen geschrieben"
rm -rf "$WORK" "$RUN"
