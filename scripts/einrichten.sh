#!/usr/bin/env bash
# Richtet den Studio-PC zum Bauen und Testen ein.
#
# Liest scripts/pakete.txt (Name und erwartete Version je Zeile), installiert
# fehlende Pakete mit «sudo apt install» (der Nutzer bestätigt) und prüft danach,
# ob alle Pakete mit der erwarteten Version installiert sind.
# Darf mehrfach laufen: Sind alle Pakete da, wird nichts installiert.
# Kein «apt update», kein Entfernen von Paketen.
#
#   scripts/einrichten.sh [Paketliste]
#
# Exit-Code: 0 = alles da und Versionen wie erwartet, 1 = Paket fehlt oder Version weicht ab,
# 2 = Paketliste fehlt oder ist fehlerhaft.
set -u

LIST="${1:-$(dirname "$(readlink -f "$0")")/pakete.txt}"
if [[ ! -r "$LIST" ]]; then
    echo "FEHLER: Paketliste nicht lesbar: $LIST" >&2
    exit 2
fi

names=()
versions=()
while read -r name version rest; do
    [[ -z "${name:-}" || "$name" == \#* ]] && continue
    if [[ -z "${version:-}" || -n "${rest:-}" ]]; then
        echo "FEHLER: Zeile in $LIST muss «Name Version» sein: $name ${version:-} ${rest:-}" >&2
        exit 2
    fi
    names+=("$name")
    versions+=("$version")
done < "$LIST"

# Installierte Version, leer wenn nicht installiert.
# dpkg-query(1): ${db:Status-Status} ist «installed», wenn das Paket vollständig installiert ist.
installed_version() {
    local out
    out=$(LANG=C dpkg-query -W -f='${db:Status-Status} ${Version}' "$1" 2>/dev/null) || return 0
    [[ "$out" == installed\ * ]] && echo "${out#installed }"
}

missing=()
for i in "${!names[@]}"; do
    [[ -z "$(installed_version "${names[$i]}")" ]] && missing+=("${names[$i]}=${versions[$i]}")
done

if (( ${#missing[@]} > 0 )); then
    echo "Fehlende Pakete: ${missing[*]}"
    echo "Installation mit: sudo apt install ${missing[*]}"
    # Ohne -y: apt fragt den Nutzer. Mit «Name=Version» installiert apt genau die erwartete Version.
    if ! sudo apt install "${missing[@]}"; then
        echo "FEHLER: apt install ist fehlgeschlagen oder wurde abgebrochen." >&2
    fi
else
    echo "Alle Pakete aus $LIST sind installiert, es wird nichts installiert."
fi

echo
echo "Prüfung:"
problems=0
for i in "${!names[@]}"; do
    have=$(installed_version "${names[$i]}")
    want=${versions[$i]}
    if [[ -z "$have" ]]; then
        printf '  FEHLT       %-20s erwartet %s\n' "${names[$i]}" "$want"
        problems=$((problems + 1))
    elif [[ "$have" != "$want" ]]; then
        printf '  ABWEICHUNG  %-20s erwartet %s, installiert %s\n' "${names[$i]}" "$want" "$have"
        problems=$((problems + 1))
    else
        printf '  ok          %-20s %s\n' "${names[$i]}" "$have"
    fi
done

for tool in cmake g++ make; do
    if ! command -v "$tool" >/dev/null; then
        printf '  FEHLT       Programm %s\n' "$tool"
        problems=$((problems + 1))
    fi
done

echo
if (( problems > 0 )); then
    echo "ACHTUNG: $problems Abweichung(en). Bitte prüfen und scripts/pakete.txt nur mit Begründung anpassen."
    exit 1
fi
echo "Alles da."
exit 0
