#!/bin/bash
# Nachtlauf P12a: M1 (Ausgabe im Umfeld des Kerns) und M2 (Uhr der Seite in Qt WebEngine).
# Plan: docs/archiv/prompts/P12-ton.md, Abschnitt 2. Unhörbar: Der Ton geht nur in eine virtuelle
# Senke (pw-loopback, dss_p12a_ausgabe → dss_p12a_aufnahme), nie auf ein echtes Gerät.
#
# Aufruf (aus jedem Ordner):
#   bash ~/DoNs_Streaming_Studio/prototypes/ton/nachtlauf.sh          voller Lauf, etwa 90 min
#   bash ~/DoNs_Streaming_Studio/prototypes/ton/nachtlauf.sh --kurz   Probelauf, etwa 5 min
#   … --nur-m1 | --nur-m2                                               nur ein Teil
#
# Ablauf:
#   0. Prüfen: Prototyp gebaut (sonst bauen), keine Reste dss_p12a, Standardausgang und
#      clock.force-quantum merken. pw-loopback anlegen, prüfen: Knoten da, Standardausgang
#      unverändert, keine Verbindung von dss_p12a… zu einem echten Gerät (sonst Abbruch).
#   M1 (ton-messen --klicks als «systemd-run --user» wie der Dienst):
#      a) Geräteliste im Dienst; findet Qt Multimedia die Senke nicht: Abbruch von M1 (Kriterium)
#      b) Ruhe: Puffer 10, 20, 40, 80 ms, je 120 s
#      c) Quantum: Puffer 40 ms, 180 s; bei 60 s clock.force-quantum 2048, bei 100 s 256, bei 140 s
#         zurück auf den gemerkten Wert (Entscheid Lotse 2026-11-09: nur nachts, mit trap zurück)
#      d) Last: Puffer 20 und 40 ms, je 300 s, daneben scripts/leistung.py --video in Schleife
#         (ungünstigster Fall; braucht eine aktive, nicht gesperrte X-Sitzung). Sonst Ersatzlast:
#         zwei ffmpeg dekodieren 1080p in Echtzeit (im Protokoll vermerkt).
#   M2 (ohne Last): ton-messen --server --folgen (Dienst) und seite-zeigen (Fenster, braucht X),
#      60 min blitz-piep.webm mit Wiederholen. Fenster zuerst ausserhalb des Bildschirms; kommen
#      nach 30 s keine 300 Meldungen «bild», neu mit Fenster auf dem Bildschirm (0,0).
#   Aufräumen (trap, auch bei Abbruch): Last und Programme beenden, clock.force-quantum zurück,
#      pw-loopback beenden, Reste mit pw-cli destroy entfernen; danach prüfen: keine dss_p12a-Knoten,
#      Quantum und Standardausgang wie vorher. Abweichungen stehen als «ABWEICHUNG» im Protokoll,
#      Exit-Code dann 3.
#
# Ergebnis: docs/archiv/messungen/P12a-daten/<Datum-Zeit>/ (ergebnis.txt fasst zusammen;
# m1-*.csv und -zusammenfassung.txt, m2-meldungen.csv.gz, m2-folgen.csv, nachtlauf.log).
# Last-Rohdaten von leistung.py: build/p12a-nacht/ (nicht im Repo).
#
# Quellen: https://docs.pipewire.org/page_man_pw-loopback_1.html (--capture-props,
#   --playback-props), https://docs.pipewire.org/page_man_pw-metadata_1.html (-n settings),
#   https://docs.pipewire.org/page_man_pw-cli_1.html (destroy),
#   https://www.freedesktop.org/software/systemd/man/latest/systemd-run.html (--user --wait --pipe
#   --collect), https://www.freedesktop.org/software/systemd/man/latest/loginctl.html
set -u

REPO=$(cd "$(dirname "$0")/../.." && pwd)
PROTO=$REPO/prototypes/ton
BIN=$PROTO/build/ton-messen
SEITE=$PROTO/build/seite-zeigen
SINK=dss_p12a_ausgabe
SRC=dss_p12a_aufnahme
PORT=24990

MODUS=voll
M1=1
M2=1
for a in "$@"; do
    case "$a" in
        --kurz) MODUS=kurz ;;
        --nur-m1) M2=0 ;;
        --nur-m2) M1=0 ;;
        *) echo "unbekannt: $a" >&2; exit 64 ;;
    esac
done
if [ "$MODUS" = kurz ]; then
    RUHE_S=15; QUANT_S=45; QUANT_T=(15 25 35); LAST_S=30; M2_S=60
else
    RUHE_S=120; QUANT_S=180; QUANT_T=(60 100 140); LAST_S=300; M2_S=3600
fi

STAMP=$(date +%Y-%m-%d-%H%M)
OUT=$REPO/docs/archiv/messungen/P12a-daten/$STAMP$([ "$MODUS" = kurz ] && echo -kurz)
LASTOUT=$REPO/build/p12a-nacht/$STAMP
mkdir -p "$OUT" "$LASTOUT" || exit 1
LOG=$OUT/nachtlauf.log
ERGEBNIS=$OUT/ergebnis.txt

log() { echo "$(date '+%F %T') $*" | tee -a "$LOG"; }
zustand() { pw-dump 2>/dev/null | "$BIN" --pw-zustand 2>>"$LOG"; }
wert() { zustand | awk -v k="$1" '$1 == k { $1 = ""; sub(/^ /, ""); print; exit }'; }
knoten_ids() { zustand | awk '$1 == "knoten" { print $2 }'; }

LOOP_PID=""
LAST_PID=""
SEITE_PID=""
M2_EINHEIT=""
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
    if [ -n "$SEITE_PID" ] && kill -0 "$SEITE_PID" 2>/dev/null; then
        kill -TERM "$SEITE_PID"; wait "$SEITE_PID" 2>/dev/null
    fi
    if [ -n "$M2_EINHEIT" ]; then
        systemctl --user stop "$M2_EINHEIT" 2>/dev/null
    fi
    systemctl --user stop "dss-p12a-*-$$.service" 2>/dev/null
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
        echo "P12a Nachtlauf $STAMP ($MODUS), Protokoll: nachtlauf.log"
        echo
        for f in "$OUT"/*-zusammenfassung.txt; do
            [ -e "$f" ] || continue
            echo "===== $(basename "$f")"; cat "$f"; echo
        done
        echo "===== Aufräumen"
        grep -E "Aufräumen geprüft|ABWEICHUNG" "$LOG"
    } > "$ERGEBNIS"
    [ -f "$OUT/m2-meldungen.csv" ] && gzip -f "$OUT/m2-meldungen.csv"
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

# Prototyp im Benutzerdienst wie der Kern; Ausgabe ins Protokoll
dienst() { # Name, Argumente …
    local name=$1; shift
    systemd-run --user --wait --collect --pipe --quiet --unit="dss-p12a-$name-$$" -- "$BIN" "$@" 2>&1 |
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

# ---------- 0. Vorbereitung
log "P12a Nachtlauf ($MODUS), Ausgabe $OUT"
log "Rechner: $(uname -n), Last (1/5/15 min): $(cut -d' ' -f1-3 /proc/loadavg)"
if [ ! -x "$BIN" ] || [ ! -x "$SEITE" ]; then
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

# ---------- M1
if [ "$M1" -eq 1 ]; then
    log "M1a: Geräteliste im Benutzerdienst"
    dienst m1-liste --liste > "$OUT/m1-liste.txt"
    if ! grep -q "^  $SINK " "$OUT/m1-liste.txt" || ! grep -q "^  $SRC " "$OUT/m1-liste.txt"; then
        log "M1 ABBRUCH (Kriterium): QMediaDevices findet $SINK oder $SRC im Dienst nicht (siehe m1-liste.txt)"
    else
        for p in 10 20 40 80; do
            pruefen_sicher
            log "M1b: Ruhe, Puffer $p ms, $RUHE_S s"
            dienst "m1-ruhe-$p" --klicks --ausgang "$SINK" --eingang "$SRC" --puffer-ms "$p" \
                --dauer-s "$RUHE_S" --aus "$OUT/m1-ruhe-$p" > /dev/null || log "M1b Puffer $p: Exit $?"
        done

        pruefen_sicher
        log "M1c: Quantum, Puffer 40 ms, $QUANT_S s"
        dienst m1-quantum --klicks --ausgang "$SINK" --eingang "$SRC" --puffer-ms 40 \
            --dauer-s "$QUANT_S" --aus "$OUT/m1-quantum" > /dev/null &
        QPID=$!
        start=$(date +%s)
        # Abzug des Graphen während der Messung (Treiber, Quantum, Latenzen), für die Auswertung
        ( sleep 10; pw-dump > "$OUT/m1-quantum-pw-dump.json" 2>/dev/null ) &
        i=0
        for q in 2048 256 "$QUANT_VORHER"; do
            while [ $(( $(date +%s) - start )) -lt "${QUANT_T[$i]}" ]; do sleep 0.2; done
            pw-metadata -n settings 0 clock.force-quantum "$q" >/dev/null 2>&1
            echo "$(date +%s%3N) clock.force-quantum $q" >> "$OUT/m1-quantum-zeiten.txt"
            log "M1c: clock.force-quantum $q (gelesen: $(wert force_quantum))"
            i=$((i + 1))
        done
        wait "$QPID" || log "M1c: Exit $?"

        LASTART=""
        if sitzung_aktiv && [ -x "$REPO/build/src/app/dss-app" ] && [ -x "$REPO/build/src/core/dss-core" ]; then
            LASTART="leistung.py --video (2 Videos 1080p30 im Editor) in Schleife"
            setsid bash -c "n=0; while :; do n=\$((n+1)); python3 '$REPO/scripts/leistung.py' --video --out '$LASTOUT/last-'\$n >>'$LASTOUT/leistung.log' 2>&1; done" &
        else
            LASTART="Ersatzlast: 2× ffmpeg dekodiert h264-1920x1080-bt709.mp4 in Echtzeit nach RGB (X-Sitzung nicht aktiv oder build/ fehlt)"
            setsid bash -c "for k in 1 2; do ffmpeg -nostdin -loglevel error -stream_loop -1 -re -i '$REPO/tests/daten/bildvergleich/h264-1920x1080-bt709.mp4' -vf format=rgb24 -f null - & done; wait" &
        fi
        LAST_PID=$!
        log "M1d: Last gestartet: $LASTART"
        sleep 10
        for p in 20 40; do
            pruefen_sicher
            log "M1d: Last, Puffer $p ms, $LAST_S s, Last (1 min) $(cut -d' ' -f1 /proc/loadavg)"
            dienst "m1-last-$p" --klicks --ausgang "$SINK" --eingang "$SRC" --puffer-ms "$p" \
                --dauer-s "$LAST_S" --aus "$OUT/m1-last-$p" > /dev/null || log "M1d Puffer $p: Exit $?"
        done
        last_stopp
        echo "Last in M1d: $LASTART" > "$OUT/m1-last-art-zusammenfassung.txt"
    fi
fi

# ---------- M2
if [ "$M2" -eq 1 ]; then
    if ! sitzung_aktiv; then
        log "M2 ausgelassen: X-Sitzung nicht aktiv oder gesperrt (Fenster braucht X)"
    else
        sleep 30   # Last aus M1 abklingen lassen
        pruefen_sicher
        [ -f "$OUT/m1-liste.txt" ] || dienst m2-liste --liste > "$OUT/m1-liste.txt"
        FOLGEN=()
        if grep -q "^  $SINK " "$OUT/m1-liste.txt"; then
            FOLGEN=(--folgen "$REPO/tests/daten/ton/blitz-piep.webm" --ausgang "$SINK" --puffer-ms 40)
        else
            log "M2: Qt findet $SINK nicht, Server ohne Nachführen"
        fi
        log "M2: Server (Dienst) und Messseite in Qt WebEngine, $M2_S s, Nachführen: ${FOLGEN[*]:-nein}"
        M2_EINHEIT="dss-p12a-m2-$$"
        systemd-run --user --collect --quiet --unit="$M2_EINHEIT" -- "$BIN" --server --port "$PORT" \
            --daten "$REPO/tests/daten/ton" --dauer-s $((M2_S + 120)) --aus "$OUT/m2" "${FOLGEN[@]}"
        sleep 3
        if ! systemctl --user is-active --quiet "$M2_EINHEIT"; then
            log "M2 ABBRUCH: Server läuft nicht"
            journalctl --user -u "$M2_EINHEIT" --no-pager -o cat >> "$LOG" 2>&1
            M2_EINHEIT=""
        else
            URL="http://127.0.0.1:$PORT/messseite.html?datei=blitz-piep.webm"
            DISPLAY=${DISPLAY:-:0} "$SEITE" --url "$URL&seite=m2-ausserhalb" --dauer-s "$M2_S" \
                --lage -1400,0 >>"$LOG" 2>&1 &
            SEITE_PID=$!
            sleep 30
            n=$(grep -c ',bild,' "$OUT/m2-meldungen.csv" 2>/dev/null)
            n=${n:-0}
            log "M2: nach 30 s $n Meldungen «bild» (Fenster ausserhalb des Bildschirms)"
            if [ "$n" -lt 300 ]; then
                log "M2: zu wenige Meldungen ausserhalb des Bildschirms, neu mit Fenster bei 0,0"
                kill -TERM "$SEITE_PID"; wait "$SEITE_PID" 2>/dev/null
                DISPLAY=${DISPLAY:-:0} "$SEITE" --url "$URL&seite=m2-sichtbar" --dauer-s "$M2_S" \
                    --lage 0,0 >>"$LOG" 2>&1 &
                SEITE_PID=$!
            fi
            wait "$SEITE_PID"
            log "M2: Seite beendet (Exit $?)"
            SEITE_PID=""
            sleep 2
            systemctl --user stop "$M2_EINHEIT"   # SIGTERM: Prototyp schreibt die Auswertung
            journalctl --user -u "$M2_EINHEIT" --no-pager -o cat 2>/dev/null |
                grep -v '^qt.multimedia.ffmpeg' > "$OUT/m2-ausgabe.txt"
            M2_EINHEIT=""
            sleep 1
        fi
    fi
fi
log "Messungen fertig"
exit 0
