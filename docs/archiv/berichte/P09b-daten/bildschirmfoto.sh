#!/bin/sh
# Bildschirmfotos P09b (Bericht): eigener Datenordner, eigener Kern, eigene Verbindung und eigenes
# XDG_CONFIG_HOME; die Daten des Nutzers werden nicht berührt.
#   sh docs/berichte/P09b-daten/bildschirmfoto.sh BILD.png VORNE [BREITExHOEHE] [art]
# VORNE: bild, video oder text – diese Quelle liegt zuvorderst und ist gewählt (Dock
# «Eigenschaften» zeigt ihre Bereiche). Ohne BREITExHOEHE 1280x800. Mit «art» ist das Dock
# «Eigenschaften» zum Bereich der Art gerollt (--eigenschaften-art).
# Daten: Overlay «Test P09b» 1920x1080 mit dem Testbild aus tests/daten/exif (Viertel rot, grün,
# blau, gelb): ungespiegelt, waagrecht gespiegelt, als JPEG mit EXIF-Ausrichtung 6 senkrecht
# gespiegelt und 10° gedreht; alpha.webm (Wiederholen); Text in zwei Zeilen.
set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
WORK=$(mktemp -d)
RUN=$(mktemp -d "$XDG_RUNTIME_DIR/dss-foto-XXXXXX")
D="$WORK/daten"
CLI="$R/build/src/cli/dss-cli --datenordner $D"
M="$R/prototypes/media"
E="$R/tests/daten/exif"
$CLI overlay-neu "Test P09b" 1920 1080 >/dev/null
$CLI quelle-bild test-p09b "$E/orientierung-1.png" >/dev/null
$CLI quelle-bild test-p09b "$E/orientierung-2.png" >/dev/null
$CLI quelle-bild test-p09b "$E/orientierung-6.jpg" >/dev/null
$CLI quelle-video test-p09b "$M/alpha.webm" --wiederholen >/dev/null
$CLI quelle-text test-p09b "Gespiegelt
und gedreht" "$R/src/common/schriften/be-vietnam-pro/BeVietnamPro-Regular.ttf" >/dev/null
$CLI quelle-geometrie test-p09b 1 100 100 640 320 0 >/dev/null
$CLI quelle-geometrie test-p09b 2 900 100 640 320 0 --waagrecht-gespiegelt >/dev/null
$CLI quelle-geometrie test-p09b 3 100 560 640 320 10 --senkrecht-gespiegelt >/dev/null
$CLI quelle-geometrie test-p09b 4 1400 520 400 400 0 >/dev/null
$CLI quelle-geometrie test-p09b 5 820 600 520 260 0 >/dev/null
# Gewählte Quelle nach vorne: in der Overlay-Datei ans Ende der Liste (Format 3, sources)
F=$(grep -l '"test-p09b"' "$D"/overlays/*.json)
python3 - "$F" "$2" <<'P'
import json, sys
p, front = sys.argv[1], sys.argv[2]
o = json.load(open(p))
index = {"bild": 2, "video": 3, "text": 4}[front]
o["sources"].append(o["sources"].pop(index))
json.dump(o, open(p, "w"), indent=4)
P
$CLI quelle-liste test-p09b
"$R/build/src/core/dss-core" --datenordner "$D" --verbindung "$RUN/kern.sock" 2> "$WORK/kern.log" &
CORE=$!
sleep 0.5
XDG_CONFIG_HOME="$WORK/cfg" "$R/build/src/app/dss-app" --verbindung "$RUN/kern.sock" \
    --bildschirmfoto "$1" --fenstergroesse "${3:-1280x800}" \
    ${4:+--eigenschaften-art}
wait $CORE
echo "Kern beendet mit $?"
cat "$WORK/kern.log"
test ! -e "$WORK/cfg" && echo "keine Einstellungen geschrieben"
rm -rf "$WORK" "$RUN"
