#!/usr/bin/env bash
# Prüfung von Punkt 69 (P10-vor, Teil C 1): scripts/installieren.sh leert <Ziel>/bin/ und
# <Ziel>/share/dons-streaming-studio/ erst nach erfolgreichem Bau und Prüfen des Formats.
# Nur mit den Testoptionen (CLAUDE.md Regel 18): Ziel, Daten, Menü und Symbole im Ordner
# ARBEIT, Einheit dss-core-installtest.service, Daten = Kopie von tests/daten/formate.
#
#   docs/berichte/P10-vor-daten/installieren-pruefen.sh ARBEIT
set -u
REPO="$(cd "$(dirname "$(readlink -f "$0")")/../../.." && pwd)"
WORK="$(readlink -m "${1:?Arbeitsordner fehlt}")"
UNIT=dss-core-installtest.service
rm -rf "$WORK" && mkdir -p "$WORK"
cp -a "$REPO/tests/daten/formate" "$WORK/daten" && rm "$WORK/daten/LIESMICH.md"
chmod 600 "$WORK/daten/tokens.json"
OPTS=(--ziel "$WORK/ziel" --datenordner "$WORK/daten" --einheit "$UNIT"
      --menue-ordner "$WORK/menue" --symbol-ordner "$WORK/symbole")
sums() { (cd "$1" && find . -type f ! -name '*.lock' -print0 | sort -z | xargs -0 sha256sum); }

echo "== Lauf 1: erste Installation ($(date '+%F %T'))"
"$REPO/scripts/installieren.sh" "${OPTS[@]}" --bau-ordner "$WORK/bau" > "$WORK/lauf1.txt" 2>&1
echo "Exit $?"; grep -E "Programmordner|umgewandelt|Sicherung|Einsatz, Commit" "$WORK/lauf1.txt"

# Veraltete Dateien, die eine neue Version nicht mitbringt, und ein Merkmal in version.txt
touch "$WORK/ziel/bin/veraltet-programm" "$WORK/ziel/share/dons-streaming-studio/symbole/veraltet.svg"
sums "$WORK/daten" > "$WORK/daten-vorher.txt"
ls -l --time-style=+%T "$WORK/ziel/bin" "$WORK/ziel/share/dons-streaming-studio/symbole/veraltet.svg"

echo "== Lauf 2: neue Installation mit veralteten Dateien ($(date '+%F %T'))"
"$REPO/scripts/installieren.sh" "${OPTS[@]}" --bau-ordner "$WORK/bau" > "$WORK/lauf2.txt" 2>&1
echo "Exit $?"; grep -E "Programmordner|Format aktuell|Einsatz, Commit" "$WORK/lauf2.txt"
for f in bin/veraltet-programm share/dons-streaming-studio/symbole/veraltet.svg; do
    [[ -e "$WORK/ziel/$f" ]] && echo "FEHLER: $f noch da" || echo "weg: $f"
done
ls "$WORK/ziel/bin" "$WORK/ziel/share/dons-streaming-studio"
cat "$WORK/ziel/version.txt"
sums "$WORK/daten" > "$WORK/daten-nachher.txt"
cmp -s "$WORK/daten-vorher.txt" "$WORK/daten-nachher.txt" && echo "Daten unverändert" || echo "FEHLER: Daten verändert"

echo "== Lauf 3: absichtlich scheiternder Bau ($(date '+%F %T'))"
touch "$WORK/ziel/bin/veraltet-programm" "$WORK/ziel/share/dons-streaming-studio/symbole/veraltet.svg"
sums "$WORK/ziel" > "$WORK/ziel-vorher.txt"
git clone -q "$REPO" "$WORK/klon"
echo '#error absichtlicher Fehler (Prüfung Punkt 69)' >> "$WORK/klon/src/core/main.cpp"
"$WORK/klon/scripts/installieren.sh" "${OPTS[@]}" --bau-ordner "$WORK/bau-klon" > "$WORK/lauf3.txt" 2>&1
echo "Exit $?"; grep -E "FEHLER|Programmordner|absichtlicher" "$WORK/lauf3.txt" | head -5
sums "$WORK/ziel" > "$WORK/ziel-nachher.txt"
cmp -s "$WORK/ziel-vorher.txt" "$WORK/ziel-nachher.txt" && echo "Ziel unverändert (nichts gelöscht)" \
    || { echo "FEHLER: Ziel verändert"; diff "$WORK/ziel-vorher.txt" "$WORK/ziel-nachher.txt"; }
sums "$WORK/daten" > "$WORK/daten-nachher3.txt"
cmp -s "$WORK/daten-vorher.txt" "$WORK/daten-nachher3.txt" && echo "Daten unverändert" || echo "FEHLER: Daten verändert"

"$REPO/scripts/dienst-einrichten.sh" --einsatz "$WORK/ziel" --einheit "$UNIT" --entfernen > /dev/null
[[ -e "$HOME/.config/systemd/user/$UNIT" ]] && echo "FEHLER: Einheit noch da" || echo "Testeinheit entfernt"
