#!/usr/bin/env bash
# Bau mit kurzer Ausgabe für Agenten. Die volle Ausgabe von cmake kommt nach BAUORDNER/bauen.log
# (bei jedem Lauf überschrieben). Auf dem Bildschirm stehen nur Diagnosen (Compiler, Linker, CMake,
# AutoMoc/AutoUic/rcc), jede höchstens einmal, Fehler vor Warnungen, zusammen höchstens 60 Zeilen,
# und eine Schlusszeile.
# Fehlt BAUORDNER/CMakeCache.txt, wird der Bauordner zuerst mit -DCMAKE_BUILD_TYPE=Debug eingerichtet.
# Rückgabewert wie cmake --build. Relative Pfade gelten ab dem aktuellen Ordner.
# Aufruf: scripts/bauen.sh [-B BAUORDNER] [-j N] [--target ZIEL ...]
set -uo pipefail

AUFRUF="Aufruf: scripts/bauen.sh [-B BAUORDNER] [-j N] [--target ZIEL ...]"
B=build; J=2; ZIELE=()
while [[ $# -gt 0 ]]; do
    case $1 in
        -B) [[ $# -ge 2 ]] || { echo "$AUFRUF" >&2; exit 2; }; B=$2; shift 2 ;;
        -j) [[ $# -ge 2 && $2 =~ ^[0-9]+$ ]] || { echo "$AUFRUF" >&2; exit 2; }; J=$2; shift 2 ;;
        --target) shift; while [[ $# -gt 0 && $1 != -* ]]; do ZIELE+=("$1"); shift; done ;;
        *) echo "Unbekannte Option: $1. $AUFRUF" >&2; exit 2 ;;
    esac
done

QUELLE=$(cd "$(dirname "$0")/.." && pwd)
BA=$(realpath -m -- "$B")
LOG=$BA/bauen.log
mkdir -p "$BA"
: > "$LOG"

rc=0
if [[ ! -f $BA/CMakeCache.txt ]]; then
    cmake -S "$QUELLE" -B "$BA" -DCMAKE_BUILD_TYPE=Debug >> "$LOG" 2>&1 || rc=$?
fi
if [[ $rc == 0 ]]; then
    ARGS=(--build "$BA" -j "$J")
    [[ ${#ZIELE[@]} -gt 0 ]] && ARGS+=(--target "${ZIELE[@]}")
    cmake "${ARGS[@]}" >> "$LOG" 2>&1
    rc=$?
fi

# Auswertung des Protokolls. Eine Diagnose beginnt mit einer Kopfzeile (Fehler, Warnung, Linker,
# CMake, AutoMoc); danach gehören note:-Zeilen, Code-Ausschnitte (« 12 | ...», «   | ^») und bei
# CMake/AutoMoc eingerückte Zeilen dazu. Ein gleicher Block (Header in mehreren Übersetzungseinheiten)
# wird nur einmal gezeigt und gezählt. «Fehler:»/«Warnung:» decken die deutsche Ausgabe von gcc ab;
# der Doppelpunkt hält «make: *** ... Fehler 1» fern. Zeilen von ld und collect2 (Folgefehler) werden
# gezeigt, aber nur «undefined reference» zählt als Fehler.
awk -v rc="$rc" -v log_="$B/bauen.log" -v max=60 '
function kopfart(z) {
    if (z ~ /: (note|Anmerkung):/) return ""
    if (z ~ /^collect2/) return "I"
    if (z ~ /(error|Fehler):/ || z ~ /undefined reference|nicht definierter Verweis/ ||
        z ~ /^CMake Error/ || z ~ /(AutoMoc|AutoUic|AutoRcc|AutoGen).*[Ee]rror/ || z ~ /RCC: Error/) return "E"
    if (z ~ /(warning|Warnung):/ || z ~ /(AutoMoc|AutoUic|AutoRcc|AutoGen).*[Ww]arning/) return "W"
    if (z ~ /(^|[\/ ])ld: / || z ~ /collect2/) return "I"
    return ""
}
function fortsetzung(z) {
    if (z ~ /: (note|Anmerkung):/ || z ~ /: +required from/) return 1
    if (z ~ /^ +[0-9]* \|/ || z ~ /^ +\+\+\+ \|/) return 1
    if (eingerueckt && (z ~ /^ / || z == "") && nfolge < 15) return 1
    return 0
}
function abschliessen(   i, t) {
    if (n == 0) return
    while (n > 0 && blk[n] == "") n--
    t = blk[1]
    for (i = 2; i <= n; i++) t = t "\n" blk[i]
    if (!(t in gesehen)) {
        gesehen[t] = 1
        if (art == "E") fehler++
        else if (art == "W") warnungen++
        if (art == "W") wtext[++nw] = t; else etext[++ne] = t
    }
    n = 0
}
function zeigen(text,   z, i, k) {
    k = split(text, z, "\n")
    for (i = 1; i <= k; i++) {
        if (gezeigt < max) { print z[i]; gezeigt++ } else gekuerzt = 1
    }
}
{
    if ($0 !~ /^\[ *[0-9]+(\/[0-9]+\]|%\])/) { rest[++nrest % 15] = $0 }
    a = kopfart($0)
    if (a != "") {
        abschliessen()
        art = a; n = 1; blk[1] = $0; nfolge = 0
        eingerueckt = ($0 ~ /^CMake Error/ || $0 ~ /Auto(Moc|Uic|Rcc|Gen)|RCC:/)
        next
    }
    if (n > 0 && fortsetzung($0)) { blk[++n] = $0; nfolge++; next }
    abschliessen()
}
END {
    abschliessen()
    # Fehler und Linkerzeilen zuerst, damit die Begrenzung sie nicht hinter Warnungen abschneidet
    for (i = 1; i <= ne; i++) zeigen(etext[i])
    for (i = 1; i <= nw; i++) zeigen(wtext[i])
    if (gekuerzt) print "… gekürzt nach " max " Zeilen, alle Diagnosen in " log_
    if (rc != 0 && fehler == 0 && gezeigt == 0) {
        # Kein Fehler erkannt: die letzten Zeilen ohne Fortschrittszeilen zeigen
        print "Keine Diagnose erkannt, letzte Zeilen des Protokolls:"
        for (i = (nrest > 15 ? nrest - 14 : 1); i <= nrest; i++) print rest[i % 15]
    }
    if (rc == 0 && warnungen == 0) print "Bau ok (0 Warnungen)"
    else if (rc == 0) print "Bau ok, " warnungen " Warnungen"
    else print "Bau FEHLGESCHLAGEN: " fehler+0 " Fehler, " warnungen+0 " Warnungen (volle Ausgabe: " log_ ")"
}' "$LOG"

exit "$rc"
