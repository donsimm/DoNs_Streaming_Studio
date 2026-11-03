#!/usr/bin/env bash
# P10-vor, Teil A 1: Watchdog beim Übernehmen einer Datei von 4 GiB im eigenen Thread. Eigene
# Testeinheit, eigener Datenordner, eigene Verbindung, eigene kern.ini (nie die echten Daten,
# Regel 18). Erwartet: kein Neustart (NRestarts 0, gleiche PID), Antworten während des Übernehmens.
# Aufruf: watchdog-gross.sh ARBEITSORDNER (auf der Platte, wird angelegt und am Ende gelöscht)
set -u
REPO="$(cd "$(dirname "$(readlink -f "$0")")/../../.." && pwd)"
W="$(readlink -m "$1")"
U=dss-core-watchdogtest.service
SOCK="$XDG_RUNTIME_DIR/dss-watchdogtest.sock"
mkdir -p "$W/daten" "$W/config"
"$REPO/build/src/cli/dss-cli" --datenordner "$W/daten" overlay-neu "Watchdog Test" 1920 1080 || exit 1
echo "Datei erzeugen: $(date '+%T')"
head -c 4G /dev/urandom > "$W/gross-4gib.png"
ls -l "$W/gross-4gib.png" | awk '{print "Grösse", $5, "Byte"}'
"$REPO/scripts/dienst-einrichten.sh" --einheit $U --datenordner "$W/daten" --verbindung "$SOCK" \
    --einstellungen "$W/config/kern.ini" | grep -E "ExecStart|Watchdog" | sed "s|$W|<Arbeit>|g"
systemctl --user start $U || exit 1
P0=$(systemctl --user show -p MainPID --value $U)
echo "WatchdogUSec=$(systemctl --user show -p WatchdogUSec --value $U)"
SINCE="$(date '+%F %T')"
echo "Übernehmen start: $(date '+%T'), PID $P0"
python3 "$(dirname "$0")/watchdog-gross.py" "$SOCK" "$W/gross-4gib.png" &
PY=$!
while kill -0 $PY 2>/dev/null; do
    P=$(systemctl --user show -p MainPID --value $U)
    [[ "$P" != "$P0" ]] && echo "NEUE PID $P um $(date '+%T')"
    sleep 1
done
wait $PY; echo "Exit Python: $?"
sleep 12 # länger als WatchdogSec nach dem Übernehmen
P1=$(systemctl --user show -p MainPID --value $U)
echo "PID vorher $P0, nachher $P1 ($( [[ $P0 == $P1 ]] && echo gleich || echo NEU )), NRestarts=$(systemctl --user show -p NRestarts --value $U)"
journalctl --user -u $U --since "$SINCE" --no-pager -o cat | grep -E "Übernehme|Übernommen|atchdog|WATCHDOG|SIGABRT|core-dump" | sed "s|$W|<Arbeit>|g"
ls -l "$W/daten/media" | awk 'NR>1{print "media:", $5, $9}'
systemctl --user stop $U
"$REPO/scripts/dienst-einrichten.sh" --einheit $U --entfernen >/dev/null && echo "Testeinheit entfernt"
rm -rf "$W" && echo "Arbeitsordner gelöscht"
