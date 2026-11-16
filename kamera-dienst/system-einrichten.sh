#!/usr/bin/env bash
# Öffnet den Port des Kamera-Dienstes in firewalld nur für den Studio-PC (P15, Plan 5, Schicht 3).
# Startet der Nutzer einmal auf dem Streaming-PC, sudo gibt er selbst ein:
#
#   sudo ~/.local/opt/dss-kamera/system-einrichten.sh <studio-pc>
#   sudo ~/.local/opt/dss-kamera/system-einrichten.sh --entfernen <studio-pc>
#
# Eine reiche Regel wie die bestehende für obs-websocket (4455): nur IPv4, nur TCP, nur vom
# Studio-PC. Andere Absender trifft das Ziel der Zone (die Zone öffnet den Bereich 1025–65535
# nicht mehr, Schritt 0 vom 2026-11-15). Die Regel kommt in die dauerhafte Einstellung, danach
# lädt firewalld neu. Keine udev-Regel (Schritt 0: Autosuspend stört nicht).
# Zeigt zuerst, was es tut. Darf mehrfach laufen: Eine vorhandene Regel wird nicht doppelt
# angelegt, eine fehlende nicht entfernt. --entfernen macht alles rückgängig.
#
# Optionen:
#   --port N       Port des Dienstes (Standard 8093; wie «port» in dienst.ini)
#   --zone NAME    Zone von firewalld (Standard: die einzige aktive Zone)
#   --entfernen    Regel entfernen statt anlegen
#   --trocken      nur die geplanten firewall-cmd-Befehle ausgeben, nichts ausführen, ohne sudo
#
# Quellen (nicht abgerufen):
#   https://firewalld.org/documentation/man-pages/firewall-cmd.html
#     (--permanent, --zone, --add-rich-rule, --remove-rich-rule, --query-rich-rule, --reload,
#      --get-active-zones)
#   https://firewalld.org/documentation/man-pages/firewalld.richlanguage.html
#     (rule family="ipv4" source address="…" port port="…" protocol="tcp" accept)
#
# Exit-Code: 0 = erledigt, 1 = Fehler, 2 = falscher Aufruf.
set -u

PORT=8093
ZONE=""
REMOVE=0
DRY=0
STUDIO=""

usage() {
    sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage ;;
        --port|--zone)
            [[ $# -ge 2 && -n "$2" ]] || usage
            if [[ "$1" == --port ]]; then PORT="$2"; else ZONE="$2"; fi
            shift 2 ;;
        --entfernen) REMOVE=1; shift ;;
        --trocken) DRY=1; shift ;;
        -*) echo "FEHLER: unbekanntes Argument: $1" >&2; usage ;;
        *)
            [[ -z "$STUDIO" ]] || { echo "FEHLER: nur eine Adresse für <studio-pc>" >&2; usage; }
            STUDIO="$1"; shift ;;
    esac
done

[[ -n "$STUDIO" ]] || usage
# Nur eine einzelne IPv4-Adresse, kein Netz und kein Name: Die Regel soll genau einen PC treffen.
valid_ipv4() {
    local IFS=. part
    [[ "$1" =~ ^[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}\.[0-9]{1,3}$ ]] || return 1
    for part in $1; do
        [[ ${#part} -eq 1 || "$part" != 0* ]] || return 1
        (( part <= 255 )) || return 1
    done
}
valid_ipv4 "$STUDIO" || { echo "FEHLER: «$STUDIO» ist keine IPv4-Adresse" >&2; exit 2; }
# Wie dienst.ini (dss_camera/config.py): 1024 bis 32767.
if [[ ! "$PORT" =~ ^[0-9]{4,5}$ ]] || (( PORT < 1024 || PORT > 32767 )); then
    echo "FEHLER: Port «$PORT» ungültig, erlaubt sind 1024 bis 32767" >&2
    exit 2
fi
if [[ -n "$ZONE" && ! "$ZONE" =~ ^[A-Za-z0-9_-]+$ ]]; then
    echo "FEHLER: Zone «$ZONE» ungültig" >&2
    exit 2
fi

fail() {
    echo "FEHLER: $*" >&2
    exit 1
}

if [[ $DRY -eq 0 ]]; then
    [[ $EUID -eq 0 ]] || fail "braucht sudo: sudo $0 $*"
    command -v firewall-cmd >/dev/null || fail "firewall-cmd fehlt (firewalld nicht installiert)"
    firewall-cmd --state >/dev/null 2>&1 || fail "firewalld läuft nicht"
    if [[ -z "$ZONE" ]]; then
        # --get-active-zones: Name der Zone auf einer Zeile ohne Einrückung, darunter eingerückt
        # ihre Schnittstellen und Quellen.
        mapfile -t zones < <(firewall-cmd --get-active-zones | grep -v '^[[:space:]]' | grep -v '^$')
        [[ ${#zones[@]} -eq 1 ]] || fail "mehrere oder keine aktive Zone (${zones[*]}), bitte --zone NAME"
        ZONE="${zones[0]%% *}"
    fi
elif [[ -z "$ZONE" ]]; then
    ZONE="<aktive-zone>"
fi

RULE="rule family=\"ipv4\" source address=\"$STUDIO\" port port=\"$PORT\" protocol=\"tcp\" accept"

if [[ $REMOVE -eq 1 ]]; then
    ACTION="--remove-rich-rule"
    echo "Entferne die Firewall-Regel für den Kamera-Dienst (Zone $ZONE, dauerhaft):"
else
    ACTION="--add-rich-rule"
    echo "Öffne Port $PORT/tcp nur für $STUDIO (Zone $ZONE, dauerhaft):"
fi
echo "  firewall-cmd --permanent --zone=$ZONE $ACTION='$RULE'"
echo "  firewall-cmd --reload"

if [[ $DRY -eq 1 ]]; then
    echo "Trockenlauf: nichts ausgeführt."
    exit 0
fi

if firewall-cmd --permanent --zone="$ZONE" --query-rich-rule="$RULE" >/dev/null 2>&1; then
    present=1
else
    present=0
fi
if [[ $REMOVE -eq 0 && $present -eq 1 ]]; then
    echo "Regel ist schon da, nichts zu tun."
    exit 0
fi
if [[ $REMOVE -eq 1 && $present -eq 0 ]]; then
    echo "Regel ist nicht da, nichts zu tun."
    exit 0
fi
firewall-cmd --permanent --zone="$ZONE" "$ACTION=$RULE" || fail "firewall-cmd $ACTION gescheitert"
firewall-cmd --reload || fail "firewall-cmd --reload gescheitert"
echo "Erledigt. Prüfen: firewall-cmd --zone=$ZONE --list-rich-rules"
