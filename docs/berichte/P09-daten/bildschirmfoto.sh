#!/bin/sh
# Bildschirmfoto P09 (Bericht): eigener Datenordner, eigener Kern, eigene Verbindung und eigenes
# XDG_CONFIG_HOME; die Daten des Nutzers werden nicht berührt.
#   sh docs/berichte/P09-daten/bildschirmfoto.sh BILD.png
# Daten: Overlay «Test P09» 1920x1080 mit (von hinten nach vorne) bild1.png gestreckt, bild3.png
# gedreht, bild2.png ausgeblendet, alpha.webm (Wiederholen) über bild1.png, Text in zwei Zeilen
# (Be Vietnam Pro, 96 px) gedreht. Das Fenster wählt die vorderste Quelle (Rahmen mit Griffen).
set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
WORK=$(mktemp -d)
RUN=$(mktemp -d "$XDG_RUNTIME_DIR/dss-foto-XXXXXX")
D="$WORK/daten"
CLI="$R/build/src/cli/dss-cli --datenordner $D"
M="$R/prototypes/media"
$CLI overlay-neu "Test P09" 1920 1080 >/dev/null
$CLI quelle-bild test-p09 "$M/bild1.png" >/dev/null
$CLI quelle-bild test-p09 "$M/bild3.png" >/dev/null
$CLI quelle-bild test-p09 "$M/bild2.png" >/dev/null
$CLI quelle-video test-p09 "$M/alpha.webm" --wiederholen >/dev/null
$CLI quelle-text test-p09 "Willkommen im Stream
Zweite Zeile" "$R/src/common/schriften/be-vietnam-pro/BeVietnamPro-Regular.ttf" >/dev/null
$CLI quelle-geometrie test-p09 1 100 100 768 512 0 >/dev/null
$CLI quelle-geometrie test-p09 2 1200 150 512 256 15 >/dev/null
$CLI quelle-geometrie test-p09 3 1300 600 256 256 0 >/dev/null
$CLI quelle-geometrie test-p09 4 300 100 512 512 0 >/dev/null
$CLI quelle-geometrie test-p09 5 120 760 1100 280 2.5 >/dev/null
# bild2.png ausblenden und Text 96 px: dss-cli kann beides nicht, also in der Overlay-Datei
# (Format 2, Felder visible und font_size)
F=$(grep -l '"test-p09"' "$D"/overlays/*.json)
python3 - "$F" <<'P'
import json, sys
p = sys.argv[1]
o = json.load(open(p))
o["sources"][2]["visible"] = False
o["sources"][4]["text"]["font_size"] = 96
json.dump(o, open(p, "w"), indent=4)
P
$CLI quelle-liste test-p09
"$R/build/src/core/dss-core" --datenordner "$D" --verbindung "$RUN/kern.sock" 2> "$WORK/kern.log" &
CORE=$!
sleep 0.5
XDG_CONFIG_HOME="$WORK/cfg" "$R/build/src/app/dss-app" --verbindung "$RUN/kern.sock" --bildschirmfoto "$1"
wait $CORE
echo "Kern beendet mit $?"
cat "$WORK/kern.log"
test ! -e "$WORK/cfg" && echo "keine Einstellungen geschrieben"
rm -rf "$WORK" "$RUN"
