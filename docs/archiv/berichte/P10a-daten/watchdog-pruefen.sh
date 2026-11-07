#!/usr/bin/env bash
# Watchdog-Prüfung: Kern starten, 25 s laufen lassen (läuft er weiter?), dann SIGSTOP,
# Zeit bis zur neuen PID messen. Läufe: $1
U=dss-core-watchdogtest.service
systemctl --user start $U || exit 1
P0=$(systemctl --user show -p MainPID --value $U)
sleep 25
P1=$(systemctl --user show -p MainPID --value $U)
echo "läuft nach 25 s: PID $P0 -> $P1 ($( [[ $P0 == $P1 ]] && echo gleich || echo NEU ))"
for i in $(seq 1 ${1:-3}); do
    P=$(systemctl --user show -p MainPID --value $U)
    T0=$(date +%s.%N)
    kill -STOP $P
    while :; do
        N=$(systemctl --user show -p MainPID --value $U)
        S=$(systemctl --user show -p SubState --value $U)
        [[ "$N" != "$P" && "$N" != 0 && "$S" == running ]] && break
        sleep 0.05
    done
    T1=$(date +%s.%N)
    echo "Lauf $i: angehalten PID $P, neu PID $N nach $(echo "$T1 - $T0" | bc) s"
    sleep 2
done
echo "NRestarts=$(systemctl --user show -p NRestarts --value $U)"
systemctl --user stop $U
echo "Zustand nach stop: $(systemctl --user is-active $U)"
