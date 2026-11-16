#!/usr/bin/env bash
# Übernimmt die Zuschauer-Daten aus Streamer.bot in die Version im Einsatz (P14 z1b, Plan 9.4).
# Aufruf aus jedem Ordner:
#
#   ~/DoNs_Streaming_Studio/scripts/zuschauer-import.sh [--ueberschreiben]
#
# Ablauf:
#   1. Nimmt den Export im Datenordner der Version im Einsatz
#      (~/.local/share/dons-streaming-studio/import/export/, erstellt mit
#      scripts/streamerbot-export/, LIESMICH.md dort) und nennt den Ordner.
#   2. Zeigt den Probelauf (schreibt nichts): Anzahlen je Ziel, Namen der Variablen, Zuschauer
#      ohne ID, Werte, die der Import ersetzen würde.
#   3. Fragt «Endgültig übernehmen? (j/n)». Bei «j» sichert der Kern zuerst die betroffenen
#      Dateien nach <Datenordner>/import/sicherung-<zeit>/ und schreibt dann.
#   Der Export bleibt im Datenordner, bis du ihn löschst (für Wiederholungen).
#
# Voraussetzung: Das Studio im Einsatz ist installiert (P14-1) und sein Kern läuft
# (systemctl --user start dss-core). Nur der Kern schreibt die Dateien (K-03).
#
# Optionen:
#   --ueberschreiben     auch Werte ersetzen, die im Studio seit dem letzten Import geändert wurden
# Nur für Tests (nie gegen die echten Daten nötig):
#   --cli PFAD           statt ~/.local/opt/dons-streaming-studio/bin/dss-cli
#   --datenordner DIR    statt ~/.local/share/dons-streaming-studio (nur für den Ordner des Exports)
#   --verbindung PFAD    an dss-cli weitergeben (Kern, der importiert)
#
# Exit-Code: 0 = übernommen oder abgelehnt, 1 = Fehler, 2 = falscher Aufruf, 4 = Kern läuft nicht.
set -u

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
CLI="$HOME/.local/opt/dons-streaming-studio/bin/dss-cli"
DATA_DIR="$DATA_HOME/dons-streaming-studio"
CONNECTION=()
OVERWRITE=()

usage() {
    sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage ;;
        --ueberschreiben) OVERWRITE=(--ueberschreiben); shift ;;
        --cli|--datenordner|--verbindung)
            [[ $# -ge 2 ]] || usage
            case "$1" in
                --cli) CLI="$(readlink -m "$2")" ;;
                --datenordner) DATA_DIR="$(readlink -m "$2")" ;;
                --verbindung) CONNECTION=(--verbindung "$(readlink -m "$2")") ;;
            esac
            shift 2 ;;
        *) echo "Unbekannte Option: $1" >&2; usage ;;
    esac
done

EXPORT="$DATA_DIR/import/export"
if [[ ! -x "$CLI" ]]; then
    echo "FEHLER: $CLI fehlt; ist das Studio installiert?" >&2
    exit 1
fi
if [[ ! -d "$EXPORT" ]]; then
    echo "FEHLER: Export $EXPORT fehlt (erstellen mit scripts/streamerbot-export/, LIESMICH.md)." >&2
    exit 1
fi

echo "Ordner des Exports: $EXPORT"
echo "(Er bleibt dort, bis du ihn löschst.)"
echo
"$CLI" "${CONNECTION[@]}" zuschauer-import "$EXPORT" "${OVERWRITE[@]}"
rc=$?
[[ $rc -eq 0 ]] || exit "$rc"
echo
read -r -p "Endgültig übernehmen? (j/n) " answer
if [[ "$answer" != j && "$answer" != J ]]; then
    echo "Nichts übernommen."
    exit 0
fi
"$CLI" "${CONNECTION[@]}" zuschauer-import "$EXPORT" --endgueltig "${OVERWRITE[@]}"
rc=$?
[[ $rc -eq 0 ]] || exit "$rc"
echo "Übernommen. Der Export liegt weiter in $EXPORT."
