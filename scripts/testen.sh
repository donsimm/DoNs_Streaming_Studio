#!/usr/bin/env bash
# Tests mit kurzer Ausgabe für Agenten: ctest --output-on-failure, volle Ausgabe nach
# BAUORDNER/testen.log (bei jedem Lauf überschrieben). Auf dem Bildschirm steht je fehlgeschlagenem
# Test Name, Grund und die Zeilen FAIL!, Loc:, Actual, Expected, QWARN, QCRITICAL, QFATAL (höchstens
# 40 je Test), dazu eine Schlusszeile. --voll TESTNAME lässt genau diesen Test mit -V laufen und
# zeigt alles (Fehlersuche; -R und -E gelten dann nicht). Rückgabewert wie ctest.
# Aufruf: scripts/testen.sh [-B BAUORDNER] [-R REGEX] [-E REGEX] [-j N] [--voll TESTNAME]
set -uo pipefail

AUFRUF="Aufruf: scripts/testen.sh [-B BAUORDNER] [-R REGEX] [-E REGEX] [-j N] [--voll TESTNAME]"
B=build; FILTER=(); J=""; VOLL=""
while [[ $# -gt 0 ]]; do
    [[ $# -ge 2 ]] || { echo "$AUFRUF" >&2; exit 2; }
    case $1 in
        -B) B=$2 ;;
        -R|-E) FILTER+=("$1" "$2") ;;
        -j) [[ $2 =~ ^[0-9]+$ ]] || { echo "$AUFRUF" >&2; exit 2; }; J=$2 ;;
        --voll) VOLL=$2 ;;
        *) echo "Unbekannte Option: $1. $AUFRUF" >&2; exit 2 ;;
    esac
    shift 2
done

BA=$(realpath -m -- "$B")
LOG=$BA/testen.log
mkdir -p "$BA"

if [[ -n $VOLL ]]; then
    ctest --test-dir "$BA" -R "^$VOLL\$" -V ${J:+-j "$J"} 2>&1 | tee "$LOG"
    exit "${PIPESTATUS[0]}"
fi

ctest --test-dir "$BA" --output-on-failure ${J:+-j "$J"} "${FILTER[@]}" > "$LOG" 2>&1
rc=$?

# Auswertung: Eine Ergebniszeile «3/7 Test  #3: NAME .....***Failed  0.12 sec» öffnet den Block
# des Tests; die Ausgabe von --output-on-failure folgt direkt danach und endet an der nächsten
# Start- oder Ergebniszeile. Ohne passende Zeile (z. B. Absturz ohne QtTest-Meldung) kommen die
# letzten 10 nicht leeren Zeilen der Ausgabe.
awk -v rc="$rc" -v log_="$B/testen.log" -v max=40 '
function abschliessen(   i, k) {
    if (name == "") return
    print "FEHLGESCHLAGEN " name " (" grund ")"
    k = 0
    for (i = 1; i <= n; i++)
        if (zeile[i] ~ /FAIL!|Loc:|Actual|Expected|QWARN|QCRITICAL|QFATAL/ && k < max) { print "  " zeile[i]; k++ }
    if (k == 0) for (i = (n > 10 ? n - 9 : 1); i <= n; i++) print "  " zeile[i]
    name = ""; n = 0
}
/^ *[0-9]+\/[0-9]+ Test +#[0-9]+: / {
    abschliessen()
    g = $0; sub(/^[^:]*: [^ ]+ \.*/, "", g)
    gsub(/\*\*\*/, " ", g); sub(/ +[0-9.]+ sec *$/, "", g); gsub(/ +/, " ", g); sub(/^ /, "", g)
    if (g ~ /^Skipped/) uebersprungen[++nueber] = $4
    else if (g !~ /^Passed/) { name = $4; grund = g; fehlnamen[++nfehl] = $4 }
    next
}
/^ +Start +[0-9]+: / { abschliessen(); next }
/tests passed, [0-9]+ tests? failed out of [0-9]+/ {
    abschliessen()
    for (i = 1; i <= NF; i++) { if ($i == "failed") f = $(i - 2); if ($i == "of") m = $(i + 1) }
    zusammenfassung = 1; next
}
/^No tests were found/ { keine = 1 }
/^Total Test time \(real\)/ { dauer = $(NF - 1); sub(/\./, ",", dauer) }
name != "" && $0 !~ /^[[:space:]]*$/ { zeile[++n] = $0 }
{ rest[++nrest % 10] = $0 }
END {
    abschliessen()
    if (keine) { print "Tests: 0 von 0 bestanden (keine Tests gefunden, Filter prüfen)"; exit }
    if (!zusammenfassung) {
        for (i = (nrest > 10 ? nrest - 9 : 1); i <= nrest; i++) print rest[i % 10]
        print "Tests: Auswertung nicht möglich (volle Ausgabe: " log_ ")"; exit
    }
    s = "Tests: " (m - f) " von " m " bestanden (" dauer " s)"
    if (nfehl > 0) {
        s = s ", fehlgeschlagen:"
        for (i = 1; i <= nfehl; i++) s = s (i > 1 ? "," : "") " " fehlnamen[i]
    }
    if (nueber > 0) {
        s = s ", übersprungen:"
        for (i = 1; i <= nueber; i++) s = s (i > 1 ? "," : "") " " uebersprungen[i]
    }
    print s
}' "$LOG"

exit "$rc"
