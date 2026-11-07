#!/usr/bin/env bash
# Prüfung der Installation mit Overlay-Server (P10): scripts/installieren.sh nur mit den
# Testoptionen (CLAUDE.md Regel 18), Daten = Kopie von tests/daten/formate (Format 1 bis 4 mit
# Tokens). Danach startet das installierte dss-core mit eigener kern.ini (Port PORT) und eigener
# Verbindung (kurzer Pfad unter $XDG_RUNTIME_DIR, Unix-Sockets höchstens 107 Byte); geprüft wird, dass die Seite aus <Ziel>/share/dons-streaming-studio/web kommt, die
# Daten auf Format 4 umgewandelt sind und eine falsche Adresse die leere Seite bekommt. Kein Token
# in der Ausgabe.
#
#   docs/berichte/P10-daten/installieren-pruefen.sh ARBEIT [PORT]
set -u
REPO="$(cd "$(dirname "$(readlink -f "$0")")/../../.." && pwd)"
WORK="$(readlink -m "${1:?Arbeitsordner fehlt}")"
PORT="${2:-24950}"
UNIT=dss-core-installtest.service
rm -rf "$WORK" && mkdir -p "$WORK"
cp -a "$REPO/tests/daten/formate" "$WORK/daten" && rm "$WORK/daten/LIESMICH.md"
chmod 600 "$WORK/daten/tokens.json"
OPTS=(--ziel "$WORK/ziel" --datenordner "$WORK/daten" --einheit "$UNIT"
      --menue-ordner "$WORK/menue" --symbol-ordner "$WORK/symbole")

echo "== Installation ($(date '+%F %T'))"
"$REPO/scripts/installieren.sh" "${OPTS[@]}" --bau-ordner "$WORK/bau" > "$WORK/installation.txt" 2>&1
echo "Exit $?"; grep -E "umgewandelt|Sicherung|Einsatz, Commit|Warnung|warning" "$WORK/installation.txt"
ls "$WORK/ziel/share/dons-streaming-studio/web"
cmp "$REPO/web/overlay.js" "$WORK/ziel/share/dons-streaming-studio/web/overlay.js" && echo "overlay.js wie im Repo"
grep -ho '"format_version": *[0-9]*' "$WORK"/daten/overlays/*.json | sort | uniq -c

echo "== Installiertes dss-core mit Port $PORT ($(date '+%F %T'))"
printf '[overlay-server]\nport=%s\nadresse-im-heimnetz=192.168.1.9\n' "$PORT" > "$WORK/kern.ini"
"$WORK/ziel/bin/dss-core" --datenordner "$WORK/daten" --einstellungen "$WORK/kern.ini" \
    --verbindung "${XDG_RUNTIME_DIR:-/tmp}/dss-p10-installtest.sock" 2> "$WORK/kern.log" &
CORE=$!
for _ in $(seq 50); do grep -q "lauscht auf 192.168.1.9" "$WORK/kern.log" && break; sleep 0.1; done
grep -E "gestartet|Einstellungen:|lauscht" "$WORK/kern.log"
ss -ltnH "sport = :$PORT" | awk '{print "ss:", $4}'
ID=f3ab0d90-c6d2-40d3-87a3-4251c27d5d51   # Overlay «format-drei»
TOKEN=$(python3 -c "import json,sys; print(json.load(open(sys.argv[1]))['tokens'][sys.argv[2]])" "$WORK/daten/tokens.json" "$ID")
BASE="http://192.168.1.9:$PORT/o/format-drei/$TOKEN"
code() { curl -s -o "$WORK/antwort" -w "%{http_code} %{content_type}" "$1"; }
echo "Seite: $(code "$BASE/")";      cmp -s "$WORK/antwort" "$REPO/web/overlay.html" && echo "  = web/overlay.html"
echo "overlay.js: $(code "$BASE/overlay.js")"
echo "events: $(curl -s -m 1 "$BASE/events" | head -c 200 | grep -o '^event: overlay' | head -1)"
echo "falscher Token: $(code "http://192.168.1.9:$PORT/o/format-drei/${TOKEN%?}x/")"
grep -q "meta http-equiv=\"refresh\"" "$WORK/antwort" && echo "  = leere Seite"
kill -TERM "$CORE"; wait "$CORE"; echo "dss-core Exit $?"
grep -E "unbekannte Adresse|beendet" "$WORK/kern.log"
grep -c -- "$TOKEN" "$WORK/kern.log" | sed 's/^/Token im Protokoll (Anzahl): /'

"$REPO/scripts/dienst-einrichten.sh" --einsatz "$WORK/ziel" --einheit "$UNIT" --entfernen > /dev/null
[[ -e "$HOME/.config/systemd/user/$UNIT" ]] && echo "FEHLER: Einheit noch da" || echo "Testeinheit entfernt"
