#!/usr/bin/env bash
# TEST N Mal über ctest (Umgebung wie ctest), vor jedem Lauf Prüfung der X11-Sitzung
# (aktiv, nicht gesperrt; light-locker sperrt nach 600 s ohne Eingabe). Bei einem Fehler wird
# die ganze Ausgabe gespeichert. Aufruf: scripts/wiederholen.sh BAUORDNER TEST N AUSGABEORDNER
B=$1; T=$2; N=$3; O=$4; mkdir -p "$O"
SID=$(loginctl list-sessions --no-legend | awk '$3=="dominik" && $4=="seat0"{print $1; exit}')
ok=0
for i in $(seq 1 $N); do
    st=$(loginctl show-session "$SID" -p Active -p LockedHint --value | tr '\n' ' ')
    if [[ "$st" != "yes no " ]]; then echo "$T: Abbruch vor Lauf $i, Sitzung: $st ($(date '+%T'))"; break; fi
    t0=$(date +%s.%N)
    (cd "$B" && ctest -R "^$T\$" -V > "$O/$T-lauf.log" 2>&1)
    rc=$?
    d=$(echo "$(date +%s.%N) - $t0" | bc)
    if [[ $rc == 0 ]]; then ok=$((ok+1)); else cp "$O/$T-lauf.log" "$O/$T-fehler-$i.log"; echo "$T Lauf $i: FEHLER ($d s)"; fi
    grep -h "QINFO.*Übernehmen" "$O/$T-lauf.log" | sed "s/^.*Übernehmen/Lauf $i: Übernehmen/" >> "$O/$T-messungen.txt"
done
echo "$T: $ok von $N bestanden ($(date '+%F %T'))"
