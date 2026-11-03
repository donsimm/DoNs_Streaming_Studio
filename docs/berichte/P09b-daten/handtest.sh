#!/bin/sh
# Handtest P09b: eigener Datenordner, eigener Kern, eigene Verbindung und eigenes XDG_CONFIG_HOME
# unter /tmp/dss-handtest-p09b. Die Daten des Nutzers (~/.local/share/dons-streaming-studio),
# fenster.ini und der Dienst dss-core.service werden nicht berührt. Die eigenen Symbole des
# Nutzers (~/.config/dons-streaming-studio/symbole/) werden nur gelesen und in die eigenen
# Einstellungen kopiert; dort wird nichts geschrieben.
#   sh docs/berichte/P09b-daten/handtest.sh start       voriges Handtest-Fenster beenden, beim
#                                                       ersten Mal Daten anlegen, Fenster öffnen
#   sh docs/berichte/P09b-daten/handtest.sh liste       Overlays und Quellen anzeigen (nur lesen)
#   sh docs/berichte/P09b-daten/handtest.sh aufraeumen  Fenster beenden, alles löschen
# Daten: Overlay «Handtest P09b» 1920x1080, von hinten nach vorne:
#   1 viertel.png (Testbild aus tests/daten/exif: links oben rot, rechts oben grün, links unten
#     blau, rechts unten gelb), 2 dasselbe Bild als JPEG mit EXIF-Ausrichtung 6 (gespeichert
#     hochkant), 3 bild3.png 15° gedreht, 4 alpha.webm (Wiederholen), 5 Text in zwei Zeilen.
# Overlay «Handtest Zweit» 1280x720 mit dem Testbild als einziger Quelle (Einfügen über zwei
# Overlays).
# Das Fenster beenden «start» und «aufraeumen» mit SIGTERM (derselbe Weg wie der Schliessknopf,
# Punkt 14); der Kern beendet sich mit dem letzten Fenster.
set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
H=/tmp/dss-handtest-p09b
D="$H/daten"
SOCK="$XDG_RUNTIME_DIR/dss-handtest-p09b.sock"
CLI="$R/build/src/cli/dss-cli --datenordner $D"
M="$R/prototypes/media"
E="$R/tests/daten/exif"
SYMBOLS="$HOME/.config/dons-streaming-studio/symbole"

# Läuft ein Prozess mit dieser Nummer und diesem Namen?
running() {
    [ -n "$1" ] && [ "$(cat "/proc/$1/comm" 2>/dev/null)" = "$2" ]
}

stop() {
    APP=$(cat "$H/app.pid" 2>/dev/null || true)
    CORE=$(cat "$H/kern.pid" 2>/dev/null || true)
    if running "$APP" dss-app; then
        kill -TERM "$APP"
        echo "Voriges Handtest-Fenster beendet."
    fi
    i=0
    while running "$CORE" dss-core && [ $i -lt 100 ]; do
        sleep 0.1
        i=$((i + 1))
    done
    if running "$CORE" dss-core; then
        kill -TERM "$CORE"
    fi
    rm -f "$H/app.pid" "$H/kern.pid"
}

create() {
    mkdir -p "$H"
    # Dateinamen für die Liste «Quellen»
    cp "$E/orientierung-1.png" "$H/viertel.png"
    cp "$E/orientierung-6.jpg" "$H/viertel-exif6.jpg"
    $CLI overlay-neu "Handtest P09b" 1920 1080 >/dev/null
    $CLI quelle-bild handtest-p09b "$H/viertel.png" >/dev/null
    $CLI quelle-bild handtest-p09b "$H/viertel-exif6.jpg" >/dev/null
    $CLI quelle-bild handtest-p09b "$M/bild3.png" >/dev/null
    $CLI quelle-video handtest-p09b "$M/alpha.webm" --wiederholen >/dev/null
    $CLI quelle-text handtest-p09b "Willkommen im Stream
Zweite Zeile" "$R/src/common/schriften/be-vietnam-pro/BeVietnamPro-Regular.ttf" >/dev/null
    $CLI quelle-geometrie handtest-p09b 1 100 100 640 320 0 >/dev/null
    $CLI quelle-geometrie handtest-p09b 2 900 100 640 320 0 >/dev/null
    $CLI quelle-geometrie handtest-p09b 3 100 560 512 256 15 >/dev/null
    $CLI quelle-geometrie handtest-p09b 4 1400 500 400 400 0 >/dev/null
    $CLI quelle-geometrie handtest-p09b 5 700 780 1100 260 0 >/dev/null
    $CLI overlay-neu "Handtest Zweit" 1280 720 >/dev/null
    $CLI quelle-bild handtest-zweit "$H/viertel.png" >/dev/null
    $CLI quelle-geometrie handtest-zweit 1 50 50 320 160 0 >/dev/null
    # Text 96 px: dss-cli kann die Grösse nicht setzen, also in der Overlay-Datei (font_size)
    F=$(grep -l '"handtest-p09b"' "$D"/overlays/*.json)
    python3 - "$F" <<'P'
import json, sys
p = sys.argv[1]
o = json.load(open(p))
o["sources"][4]["text"]["font_size"] = 96
json.dump(o, open(p, "w"), indent=4)
P
    echo "Handtest-Daten angelegt in $D"
}

case "$1" in
start)
    stop
    if [ ! -d "$D" ]; then
        create
    fi
    # Jedes Mal die Standardanordnung der Docks (eigene, leere Einstellungen), dazu eine Kopie
    # der eigenen Symbole des Nutzers
    rm -rf "$H/cfg"
    mkdir -p "$H/cfg/dons-streaming-studio"
    if [ -d "$SYMBOLS" ]; then
        cp -R "$SYMBOLS" "$H/cfg/dons-streaming-studio/symbole"
    fi
    "$R/build/src/core/dss-core" --datenordner "$D" --verbindung "$SOCK" 2>> "$H/kern.log" &
    echo $! > "$H/kern.pid"
    sleep 0.5
    XDG_CONFIG_HOME="$H/cfg" "$R/build/src/app/dss-app" --verbindung "$SOCK" \
        --einheit dss-nicht-vorhanden.service 2>> "$H/fenster.log" &
    echo $! > "$H/app.pid"
    echo "Handtest-Fenster geöffnet (Protokoll: $H/fenster.log, $H/kern.log)."
    ;;
liste)
    $CLI overlay-liste
    $CLI quelle-liste handtest-p09b
    $CLI quelle-liste handtest-zweit
    ;;
aufraeumen)
    stop
    rm -rf "$H" "$SOCK"
    echo "Handtest-Daten gelöscht."
    ;;
*)
    echo "Aufruf: sh docs/berichte/P09b-daten/handtest.sh start|liste|aufraeumen" >&2
    exit 2
    ;;
esac
