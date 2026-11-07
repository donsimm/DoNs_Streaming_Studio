#!/bin/sh
# Handtest P09: eigener Datenordner, eigener Kern, eigene Verbindung und eigenes XDG_CONFIG_HOME
# unter /tmp/dss-handtest-p09. Die Daten des Nutzers (~/.local/share/dons-streaming-studio),
# fenster.ini und der Dienst dss-core.service werden nicht berührt.
#   sh docs/berichte/P09-daten/handtest.sh start       voriges Handtest-Fenster beenden, beim
#                                                      ersten Mal Daten anlegen, Fenster öffnen
#   sh docs/berichte/P09-daten/handtest.sh liste       Overlay und Quellen anzeigen (nur lesen)
#   sh docs/berichte/P09-daten/handtest.sh aufraeumen  Fenster beenden, alles löschen
# Daten: Ordner «Handtest Ordner» mit Overlay «Handtest P09» 1920x1080, von hinten nach vorne:
#   1 bild1.png gestreckt, 2 bild3.png 15° gedreht, 3 alpha.webm (Wiederholen) über bild1.png,
#   4 video.webm (ohne Einstellung, 10 s), 5 Text in zwei Zeilen (Be Vietnam Pro, 96 px).
# Das Fenster beenden «start» und «aufraeumen» mit SIGTERM (derselbe Weg wie der Schliessknopf,
# Punkt 14); der Kern beendet sich mit dem letzten Fenster.
set -e
R=$(cd "$(dirname "$0")/../../.." && pwd)
H=/tmp/dss-handtest-p09
D="$H/daten"
SOCK="$XDG_RUNTIME_DIR/dss-handtest-p09.sock"
CLI="$R/build/src/cli/dss-cli --datenordner $D"
M="$R/prototypes/media"

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
    $CLI overlay-neu "Handtest P09" 1920 1080 >/dev/null
    $CLI quelle-bild handtest-p09 "$M/bild1.png" >/dev/null
    $CLI quelle-bild handtest-p09 "$M/bild3.png" >/dev/null
    $CLI quelle-video handtest-p09 "$M/alpha.webm" --wiederholen >/dev/null
    $CLI quelle-video handtest-p09 "$M/video.webm" >/dev/null
    $CLI quelle-text handtest-p09 "Willkommen im Stream
Zweite Zeile" "$R/src/common/schriften/be-vietnam-pro/BeVietnamPro-Regular.ttf" >/dev/null
    $CLI quelle-geometrie handtest-p09 1 100 100 768 512 0 >/dev/null
    $CLI quelle-geometrie handtest-p09 2 1200 150 512 256 15 >/dev/null
    $CLI quelle-geometrie handtest-p09 3 300 100 512 512 0 >/dev/null
    $CLI quelle-geometrie handtest-p09 4 1100 500 640 360 0 >/dev/null
    $CLI quelle-geometrie handtest-p09 5 120 760 1100 280 0 >/dev/null
    # Text 96 px: dss-cli kann die Grösse nicht setzen, also in der Overlay-Datei (font_size)
    F=$(grep -l '"handtest-p09"' "$D"/overlays/*.json)
    python3 - "$F" <<'P'
import json, sys
p = sys.argv[1]
o = json.load(open(p))
o["sources"][4]["text"]["font_size"] = 96
json.dump(o, open(p, "w"), indent=4)
P
    # Ordner von Hand wie in P08 (dss-cli kennt keine Ordner)
    ID=$(basename "$F" .json)
    cat > "$D/liste.json" <<J
{"entries":[{"expanded":true,"id":"0b9e6c1a-2d3f-4a5b-8c7d-9e0f1a2b3c4d","kind":"folder","name":"Handtest Ordner","overlays":["$ID"]}],"format_version":1}
J
    echo "Handtest-Daten angelegt in $D"
}

case "$1" in
start)
    stop
    if [ ! -d "$D" ]; then
        create
    fi
    # Jedes Mal die Standardanordnung der Docks (eigene, leere Einstellungen)
    rm -rf "$H/cfg"
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
    $CLI quelle-liste handtest-p09
    ;;
aufraeumen)
    stop
    rm -rf "$H" "$SOCK"
    echo "Handtest-Daten gelöscht."
    ;;
*)
    echo "Aufruf: sh docs/berichte/P09-daten/handtest.sh start|liste|aufraeumen" >&2
    exit 2
    ;;
esac
