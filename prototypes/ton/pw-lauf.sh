#!/bin/bash
# Nachtlauf P12b, Abschnitt b1: Messung Z3 mit pw-messen (Ausgabe über pw_stream), dazu der
# Gegenlauf Z3c-Q mit ton-messen (QAudioSink) unter denselben Bedingungen.
# Plan: docs/archiv/prompts/P12-ton.md, Abschnitt 3, «b1». Unhörbar: Der Ton geht nur in eine
# virtuelle Senke (pw-loopback, dss_p12a_ausgabe → dss_p12a_aufnahme), nie auf ein echtes Gerät.
# Nur nachts: clock.force-quantum gilt für den ganzen Rechner (Frage 4 des Nutzers, 2026-11-11).
#
# Aufruf (aus jedem Ordner):
#   bash ~/DoNs_Streaming_Studio/prototypes/ton/pw-lauf.sh          voller Lauf, etwa 20 min
#   bash ~/DoNs_Streaming_Studio/prototypes/ton/pw-lauf.sh --kurz   Probelauf, etwa 4 min
#
# Ablauf (alle Programme als «systemd-run --user» wie der Dienst):
#   0.    Prüfen: Prototyp gebaut (sonst bauen), keine Reste dss_p12a, Standardausgang und
#         clock.force-quantum merken. pw-loopback anlegen, prüfen: Knoten da, Standardausgang
#         unverändert, keine Verbindung von dss_p12a… zu einem echten Gerät (sonst Abbruch).
#   Z3a   Liste der Ausgänge im Dienst (pw-messen --liste), Vergleich mit M1a; Bibliotheken von
#         pw-messen (E3: ohne libQt6Gui und libQt6Multimedia). Fehlen die Messknoten: Abbruch.
#   Z3b   Ruhe: node.latency 40 ms, 120 s
#   Z3c   Quantum: 40 ms, 180 s; bei 60 / 100 / 140 s clock.force-quantum 2048 / 256 / zurück auf den
#         gemerkten Wert (Marken für pw-messen in z3c-zeiten.txt)
#   Z3c-Q Gegenlauf wie M1c: ton-messen --klicks, Puffer 40 ms, 180 s, dieselben Wechsel
#   Z3d   Last (Frage 1 = Ja des Nutzers): 40 ms, 2 × 300 s, daneben scripts/leistung.py --video in
#         Schleife (braucht eine aktive, nicht gesperrte X-Sitzung). Sonst Ersatzlast: zwei ffmpeg
#         dekodieren 1080p in Echtzeit (im Protokoll vermerkt).
#   Aufräumen (trap, auch bei Abbruch): Last und Programme beenden, clock.force-quantum zurück,
#         pw-loopback beenden, Reste mit pw-cli destroy entfernen; danach prüfen: keine
#         dss_p12a-Knoten, Quantum und Standardausgang wie vorher. Abweichungen stehen als
#         «ABWEICHUNG» im Protokoll, Exit-Code dann 3.
#   Probelauf (--kurz): Z3b 15 s, Z3c und Z3c-Q 45 s (Wechsel bei 15 / 25 / 35 s), Z3d 2 × 30 s.
#
# Ergebnis: docs/archiv/messungen/P12b-daten/<Datum-Zeit>[-kurz]/ (ergebnis.txt fasst zusammen;
# z3*.csv, z3*-aufrufe.csv, z3*-zusammenfassung.txt, z3*-zeiten.txt, z3a-*.txt, pw-lauf.log).
# Die Abzüge z3c-pw-dump.json und z3cq-pw-dump.json enthalten Benutzer- und Rechnernamen und
# Adressen Dritter: nicht committen (wie P12a). Last-Rohdaten von leistung.py: build/p12b-nacht/.
#
# Quellen: https://docs.pipewire.org/page_man_pw-loopback_1.html (--capture-props,
#   --playback-props), https://docs.pipewire.org/page_man_pw-metadata_1.html (-n settings),
#   https://docs.pipewire.org/page_man_pw-cli_1.html (destroy),
#   https://www.freedesktop.org/software/systemd/man/latest/systemd-run.html (--user --wait --pipe
#   --collect), https://www.freedesktop.org/software/systemd/man/latest/loginctl.html
#   (alle nicht abgerufen, kein Webzugang; Aufrufe wie in nachtlauf.sh, dort im Nachtlauf P12a erprobt)
set -u

REPO=$(cd "$(dirname "$0")/../.." && pwd)
PROTO=$REPO/prototypes/ton
BIN=$PROTO/build/ton-messen
PWBIN=$PROTO/build/pw-messen
SINK=dss_p12a_ausgabe
SRC=dss_p12a_aufnahme
LATENZ=40
M1A=$REPO/docs/archiv/messungen/P12a-daten/2026-11-11-0343/m1-liste.txt

MODUS=voll
for a in "$@"; do
    case "$a" in
        --kurz) MODUS=kurz ;;
        *) echo "unbekannt: $a" >&2; exit 64 ;;
    esac
done
if [ "$MODUS" = kurz ]; then
    RUHE_S=15; QUANT_S=45; QUANT_T=(15 25 35); LAST_S=30
else
    RUHE_S=120; QUANT_S=180; QUANT_T=(60 100 140); LAST_S=300
fi

STAMP=$(date +%Y-%m-%d-%H%M)
OUT=$REPO/docs/archiv/messungen/P12b-daten/$STAMP$([ "$MODUS" = kurz ] && echo -kurz)
LASTOUT=$REPO/build/p12b-nacht/$STAMP
mkdir -p "$OUT" "$LASTOUT" || exit 1
LOG=$OUT/pw-lauf.log
ERGEBNIS=$OUT/ergebnis.txt

log() { echo "$(date '+%F %T') $*" | tee -a "$LOG"; }
zustand() { pw-dump 2>/dev/null | "$BIN" --pw-zustand 2>>"$LOG"; }
wert() { zustand | awk -v k="$1" '$1 == k { $1 = ""; sub(/^ /, ""); print; exit }'; }
knoten_ids() { zustand | awk '$1 == "knoten" { print $2 }'; }

LOOP_PID=""
LAST_PID=""
QUANT_VORHER=""
SENKE_VORHER=""
ABWEICHUNG=0

last_stopp() {
    if [ -n "$LAST_PID" ] && kill -0 "$LAST_PID" 2>/dev/null; then
        kill -TERM -- "-$LAST_PID" 2>/dev/null   # ganze Prozessgruppe (setsid)
        sleep 2
        kill -KILL -- "-$LAST_PID" 2>/dev/null
        wait "$LAST_PID" 2>/dev/null
        log "Last beendet"
    fi
    LAST_PID=""
}

aufraeumen() {
    local rc=$?
    trap - EXIT INT TERM HUP
    log "Aufräumen (Exit-Code bis hier $rc)"
    last_stopp
    systemctl --user stop "dss-p12b-*-$$.service" 2>/dev/null
    if [ -n "$QUANT_VORHER" ]; then
        pw-metadata -n settings 0 clock.force-quantum "$QUANT_VORHER" >/dev/null 2>&1
    fi
    if [ -n "$LOOP_PID" ] && kill -0 "$LOOP_PID" 2>/dev/null; then
        kill -TERM "$LOOP_PID"; wait "$LOOP_PID" 2>/dev/null
    fi
    sleep 1
    for id in $(knoten_ids); do
        log "Rest dss_p12a, Knoten $id: pw-cli destroy $id"
        pw-cli destroy "$id" >/dev/null 2>&1
    done
    sleep 1
    # Nachprüfung
    local rest q s
    rest=$(knoten_ids | wc -l)
    q=$(wert force_quantum)
    s=$(wert standard_senke)
    if [ "$rest" -ne 0 ]; then log "ABWEICHUNG: noch $rest Knoten dss_p12a"; ABWEICHUNG=1; fi
    if [ -n "$QUANT_VORHER" ] && [ "$q" != "$QUANT_VORHER" ]; then
        log "ABWEICHUNG: clock.force-quantum ist $q, vorher $QUANT_VORHER"; ABWEICHUNG=1
    fi
    if [ -n "$SENKE_VORHER" ] && [ "$s" != "$SENKE_VORHER" ]; then
        log "ABWEICHUNG: Standardausgang ist $s, vorher $SENKE_VORHER"; ABWEICHUNG=1
    fi
    [ "$ABWEICHUNG" -eq 0 ] && log "Aufräumen geprüft: keine dss_p12a-Knoten, clock.force-quantum $q, Standardausgang $s"
    {
        echo "P12b Z3 $STAMP ($MODUS), Protokoll: pw-lauf.log"
        echo
        for f in "$OUT"/*-zusammenfassung.txt; do
            [ -e "$f" ] || continue
            echo "===== $(basename "$f")"; cat "$f"; echo
        done
        for f in "$OUT"/z3a-*.txt; do
            [ -e "$f" ] || continue
            echo "===== $(basename "$f")"; cat "$f"; echo
        done
        echo "===== Aufräumen"
        grep -E "Aufräumen geprüft|ABWEICHUNG|ABBRUCH" "$LOG"
    } > "$ERGEBNIS"
    log "Ergebnis: $ERGEBNIS"
    if [ "$ABWEICHUNG" -ne 0 ]; then exit 3; fi
    exit "$rc"
}
trap aufraeumen EXIT
trap 'log "Signal, Abbruch"; exit 130' INT TERM HUP

sitzung_aktiv() {
    [ "$(cat /sys/class/tty/tty0/active 2>/dev/null)" = tty7 ] || return 1
    local s
    for s in $(loginctl list-sessions --no-legend 2>/dev/null | awk '{ print $1 }'); do
        if [ "$(loginctl show-session "$s" -p Name --value)" = "$USER" ] &&
           [ "$(loginctl show-session "$s" -p Type --value)" = x11 ] &&
           [ "$(loginctl show-session "$s" -p Active --value)" = yes ]; then
            return 0
        fi
    done
    return 1
}

# Programm im Benutzerdienst wie der Kern; Ausgabe ins Protokoll
dienst() { # Name, Programm, Argumente …
    local name=$1; shift
    systemd-run --user --wait --collect --pipe --quiet --unit="dss-p12b-$name-$$" -- "$@" 2>&1 |
        grep -v '^qt.multimedia.ffmpeg' | tee -a "$LOG"
    return "${PIPESTATUS[0]}"
}

pruefen_sicher() {
    local z
    z=$(zustand)
    echo "$z" | sed 's/^/  pw: /' >> "$LOG"
    if echo "$z" | grep -q '^gefahr'; then
        log "ABBRUCH: Verbindung von dss_p12a zu einem echten Gerät: $(echo "$z" | grep '^gefahr' | tr '\n' ' ')"
        exit 2
    fi
    if [ "$(echo "$z" | awk '$1 == "standard_senke" { print $2 }')" != "$SENKE_VORHER" ]; then
        log "ABBRUCH: Standardausgang hat sich geändert"
        exit 2
    fi
}

# Lauf mit Wechsel des Quantums wie M1c: Teil, Programm, Argumente …
# Zeiten der Wechsel (Wanduhr in ms) in $OUT/<Teil>-zeiten.txt; pw-messen liest sie am Ende als Marken.
quantum_lauf() {
    local teil=$1; shift
    local zeiten=$OUT/$teil-zeiten.txt
    : > "$zeiten"
    dienst "$teil" "$@" > /dev/null &
    local pid=$! start i=0 q
    start=$(date +%s)
    # Abzug des Graphen während der Messung (Treiber, Quantum, Latenzen), für die Auswertung
    ( sleep 10; pw-dump > "$OUT/$teil-pw-dump.json" 2>/dev/null ) &
    for q in 2048 256 "$QUANT_VORHER"; do
        while [ $(( $(date +%s) - start )) -lt "${QUANT_T[$i]}" ]; do sleep 0.2; done
        pw-metadata -n settings 0 clock.force-quantum "$q" >/dev/null 2>&1
        echo "$(date +%s%3N) clock.force-quantum $q" >> "$zeiten"
        log "$teil: clock.force-quantum $q (gelesen: $(wert force_quantum))"
        i=$((i + 1))
    done
    wait "$pid" || log "$teil: Exit $?"
}

# ---------- 0. Vorbereitung
log "P12b Z3 ($MODUS), Ausgabe $OUT"
log "Rechner: $(uname -n), Last (1/5/15 min): $(cut -d' ' -f1-3 /proc/loadavg)"
if [ ! -x "$BIN" ] || [ ! -x "$PWBIN" ]; then
    log "Prototyp fehlt, baue (vor jeder Messung)"
    cmake -S "$PROTO" -B "$PROTO/build" >>"$LOG" 2>&1 && cmake --build "$PROTO/build" -j8 >>"$LOG" 2>&1 ||
        { log "ABBRUCH: Bau fehlgeschlagen"; exit 1; }
fi
if [ -n "$(knoten_ids)" ]; then
    log "Reste dss_p12a von einem früheren Lauf, entferne sie"
    for id in $(knoten_ids); do pw-cli destroy "$id" >/dev/null 2>&1; done
fi
QUANT_VORHER=$(wert force_quantum)
SENKE_VORHER=$(wert standard_senke)
log "Vorher: clock.force-quantum ${QUANT_VORHER:-?}, Standardausgang ${SENKE_VORHER:-?}"
if [ -z "$QUANT_VORHER" ] || [ -z "$SENKE_VORHER" ]; then
    log "ABBRUCH: Zustand von PipeWire nicht lesbar"; exit 1
fi

pw-loopback --channels 2 --name dss_p12a \
    --capture-props="media.class=Audio/Sink node.name=$SINK node.description=\"DSS P12a Messung Ausgabe (stumm)\" audio.position=[FL,FR]" \
    --playback-props="media.class=Audio/Source node.name=$SRC node.description=\"DSS P12a Messung Aufnahme\" audio.position=[FL,FR]" \
    >>"$LOG" 2>&1 &
LOOP_PID=$!
sleep 2
if ! kill -0 "$LOOP_PID" 2>/dev/null; then log "ABBRUCH: pw-loopback läuft nicht"; LOOP_PID=""; exit 1; fi
if [ "$(knoten_ids | wc -l)" -lt 2 ]; then log "ABBRUCH: Knoten $SINK/$SRC fehlen"; exit 1; fi
pruefen_sicher
log "pw-loopback läuft (PID $LOOP_PID), Knoten: $(zustand | awk '$1 == "knoten" { printf "%s=%s ", $3, $2 }')"

# ---------- Z3a
log "Z3a: Liste der Ausgänge im Benutzerdienst"
dienst z3a-liste "$PWBIN" --liste > "$OUT/z3a-liste.txt"
if ! grep -q "^  $SINK " "$OUT/z3a-liste.txt" || ! grep -q "^  $SRC " "$OUT/z3a-liste.txt"; then
    log "ABBRUCH (E3): pw-messen findet $SINK oder $SRC im Dienst nicht (siehe z3a-liste.txt)"
    exit 2
fi
# Vergleich mit M1a: jeder Knoten aus m1-liste.txt (Ausgänge und Eingänge) auch in z3a-liste.txt?
if [ -f "$M1A" ]; then
    awk '/^  / { print $1 }' "$M1A" | while read -r n; do
        if grep -q "^  $n " "$OUT/z3a-liste.txt"; then echo "gleich wie M1a: $n"; else echo "FEHLT gegenüber M1a: $n"; fi
    done > "$OUT/z3a-vergleich-m1a.txt"
    awk '/^  / { print $1 }' "$OUT/z3a-liste.txt" | while read -r n; do
        grep -q "^  $n " "$M1A" || echo "neu gegenüber M1a: $n"
    done >> "$OUT/z3a-vergleich-m1a.txt"
    log "Z3a: Vergleich mit M1a: $(grep -c '^gleich' "$OUT/z3a-vergleich-m1a.txt") gleich, $(grep -c '^FEHLT' "$OUT/z3a-vergleich-m1a.txt") fehlen, $(grep -c '^neu' "$OUT/z3a-vergleich-m1a.txt") neu"
else
    log "Z3a: $M1A fehlt, kein Vergleich mit M1a"
fi
ldd "$PWBIN" | awk '{ print $1 }' | grep -E 'Qt6|pipewire' > "$OUT/z3a-bibliotheken.txt"
if grep -qE 'libQt6(Gui|Multimedia)' "$OUT/z3a-bibliotheken.txt"; then
    log "Z3a ABWEICHUNG (E3): pw-messen hängt von Qt Gui oder Qt Multimedia ab (z3a-bibliotheken.txt)"
else
    log "Z3a (E3): pw-messen ohne libQt6Gui und libQt6Multimedia: $(tr '\n' ' ' < "$OUT/z3a-bibliotheken.txt")"
fi

# ---------- Z3b
pruefen_sicher
log "Z3b: Ruhe, node.latency $LATENZ ms, $RUHE_S s"
dienst z3b "$PWBIN" --klicks --ausgang "$SINK" --eingang "$SRC" --latenz-ms "$LATENZ" \
    --dauer-s "$RUHE_S" --aus "$OUT/z3b" > /dev/null || log "Z3b: Exit $?"
sleep 5

# ---------- Z3c
pruefen_sicher
log "Z3c: Quantum, node.latency $LATENZ ms, $QUANT_S s"
quantum_lauf z3c "$PWBIN" --klicks --ausgang "$SINK" --eingang "$SRC" --latenz-ms "$LATENZ" \
    --dauer-s "$QUANT_S" --aus "$OUT/z3c" --marken "$OUT/z3c-zeiten.txt"
sleep 5

# ---------- Z3c-Q
pruefen_sicher
log "Z3c-Q: Gegenlauf ton-messen (QAudioSink), Puffer $LATENZ ms, $QUANT_S s"
quantum_lauf z3cq "$BIN" --klicks --ausgang "$SINK" --eingang "$SRC" --puffer-ms "$LATENZ" \
    --dauer-s "$QUANT_S" --aus "$OUT/z3cq"
sleep 5

# ---------- Z3d
LASTART=""
if sitzung_aktiv && [ -x "$REPO/build/src/app/dss-app" ] && [ -x "$REPO/build/src/core/dss-core" ]; then
    LASTART="leistung.py --video (2 Videos 1080p30 im Editor) in Schleife"
    setsid bash -c "n=0; while :; do n=\$((n+1)); python3 '$REPO/scripts/leistung.py' --video --out '$LASTOUT/last-'\$n >>'$LASTOUT/leistung.log' 2>&1; done" &
else
    LASTART="Ersatzlast: 2× ffmpeg dekodiert h264-1920x1080-bt709.mp4 in Echtzeit nach RGB (X-Sitzung nicht aktiv oder build/ fehlt)"
    setsid bash -c "for k in 1 2; do ffmpeg -nostdin -loglevel error -stream_loop -1 -re -i '$REPO/tests/daten/bildvergleich/h264-1920x1080-bt709.mp4' -vf format=rgb24 -f null - & done; wait" &
fi
LAST_PID=$!
log "Z3d: Last gestartet: $LASTART"
sleep 10
for k in 1 2; do
    pruefen_sicher
    log "Z3d: Last, Lauf $k, node.latency $LATENZ ms, $LAST_S s"
    dienst "z3d-$k" "$PWBIN" --klicks --ausgang "$SINK" --eingang "$SRC" --latenz-ms "$LATENZ" \
        --dauer-s "$LAST_S" --aus "$OUT/z3d-$k" > /dev/null || log "Z3d Lauf $k: Exit $?"
done
last_stopp
echo "Last in Z3d: $LASTART" > "$OUT/z3d-last-art-zusammenfassung.txt"

log "Messungen fertig"
exit 0
