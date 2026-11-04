#!/usr/bin/env bash
# Prüfung der Installation mit Medienliste (P10b): scripts/installieren.sh nur mit den
# Testoptionen (CLAUDE.md Regel 18). Daten = frische Kopie der echten Daten
# (~/.local/share/dons-streaming-studio, nur gelesen), also ohne medien.json wie im Handtest.
# Geprüft wird: Sicherung unter <Daten>-sicherungen/<Zeitstempel>/ gleich den Daten vorher,
# danach medien.json mit Ersatznamen, Format aktuell, sonst nichts geändert. Kein Token in der
# Ausgabe (tokens.json wird nur kopiert und verglichen).
#
#   docs/berichte/P10b-daten/installieren-pruefen.sh ARBEIT
set -u
REPO="$(cd "$(dirname "$(readlink -f "$0")")/../../.." && pwd)"
WORK="$(readlink -m "${1:?Arbeitsordner fehlt}")"
UNIT=dss-core-installtest.service
REAL="$HOME/.local/share/dons-streaming-studio"
rm -rf "$WORK" && mkdir -p "$WORK"
cp -a "$REAL" "$WORK/daten"
cp -a "$REAL" "$WORK/vorher"
OPTS=(--ziel "$WORK/ziel" --datenordner "$WORK/daten" --einheit "$UNIT"
      --menue-ordner "$WORK/menue" --symbol-ordner "$WORK/symbole")

echo "== Daten vorher: $(find "$WORK/daten" -type f | wc -l) Dateien, medien.json $([[ -e "$WORK/daten/medien.json" ]] && echo da || echo fehlt)"
echo "== Installation ($(date '+%F %T'))"
"$REPO/scripts/installieren.sh" "${OPTS[@]}" --bau-ordner "$WORK/bau" > "$WORK/installation.txt" 2>&1
echo "Exit $?"
grep -E "älteres Format|umgewandelt|Sicherung|gesichert|Einsatz, Commit|Warnung|warning|FEHLER" "$WORK/installation.txt"

echo "== Sicherung"
BACKUPS="$WORK/daten-sicherungen"
ls "$BACKUPS"
B="$BACKUPS/$(ls "$BACKUPS" | tail -1)"
diff -r "$WORK/vorher" "$B" > /dev/null && echo "Sicherung gleich den Daten vorher" || echo "FEHLER: Sicherung weicht ab"

echo "== Daten nachher"
diff -rq "$WORK/vorher" "$WORK/daten" | sed "s|$WORK/||g"
"$WORK/ziel/bin/dss-core" --datenordner "$WORK/daten" --format-pruefen; echo "format-pruefen Exit $?"
"$WORK/ziel/bin/dss-cli" --datenordner "$WORK/daten" medien-liste; echo "medien-liste Exit $?"
"$WORK/ziel/bin/dss-core" --version

"$REPO/scripts/dienst-einrichten.sh" --einsatz "$WORK/ziel" --einheit "$UNIT" --entfernen > /dev/null
[[ -e "$HOME/.config/systemd/user/$UNIT" ]] && echo "FEHLER: Einheit noch da" || echo "Testeinheit entfernt"
rm -rf "$WORK/vorher" "$WORK/daten" "$BACKUPS" "$WORK/bau"
echo "Kopien der Daten gelöscht"
