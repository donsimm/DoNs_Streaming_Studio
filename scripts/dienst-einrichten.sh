#!/usr/bin/env bash
# Richtet den Kern dss-core als Benutzerdienst von systemd ein (P07, Teil C), oder entfernt ihn.
#
# Die Einheit startet NICHT bei der Anmeldung (kein Abschnitt [Install], kein «enable»): Das
# Fenster dss-app startet sie mit «systemctl --user start dss-core.service». Nach einem Absturz
# des Kerns startet systemd ihn neu (Restart=on-failure); nach ordentlichem Beenden (Exit-Code 0,
# SIGTERM, «systemctl --user stop») nicht. Protokoll: journalctl --user -u dss-core.service
# Keine Systemdienste, kein sudo. Darf mehrfach laufen.
#
#   scripts/dienst-einrichten.sh                 Einheit anlegen bzw. erneuern
#   scripts/dienst-einrichten.sh --entfernen     Einheit anhalten und entfernen
#
# Nur für Tests (damit nie die echten Daten berührt werden):
#   --einheit NAME.service   anderer Name der Einheit (Standard dss-core.service)
#   --datenordner PFAD       an dss-core weitergeben
#   --verbindung PFAD        an dss-core weitergeben
#
# Die Einheit startet das gebaute dss-core aus diesem Repo (build/src/core/dss-core); der Pfad
# wird hier ermittelt, nicht fest eingetragen. Nach einem Verschieben des Repos neu ausführen.
#
# Einstellungen nach systemd.service(5) und systemd.unit(5):
#   https://www.freedesktop.org/software/systemd/man/latest/systemd.service.html
#   https://www.freedesktop.org/software/systemd/man/latest/systemd.unit.html
#
# Exit-Code: 0 = erledigt, 1 = Fehler, 2 = falscher Aufruf.
set -u

REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
CORE="$REPO/build/src/core/dss-core"
UNIT="dss-core.service"
REMOVE=0
CORE_ARGS=()

usage() {
    sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --entfernen) REMOVE=1; shift ;;
        --einheit)
            [[ $# -ge 2 ]] || usage
            UNIT="$2"; shift 2 ;;
        --datenordner|--verbindung)
            [[ $# -ge 2 ]] || usage
            CORE_ARGS+=("$1" "$(readlink -m "$2")"); shift 2 ;;
        -h|--help) usage ;;
        *) echo "FEHLER: unbekanntes Argument: $1" >&2; usage ;;
    esac
done

if [[ ! "$UNIT" =~ ^[A-Za-z0-9_.-]+\.service$ ]]; then
    echo "FEHLER: Name der Einheit muss auf .service enden: $UNIT" >&2
    exit 2
fi

UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
UNIT_FILE="$UNIT_DIR/$UNIT"

if [[ $REMOVE -eq 1 ]]; then
    # Anhalten nur, wenn systemd die Einheit kennt; fehlt sie, ist nichts zu tun.
    if systemctl --user cat "$UNIT" > /dev/null 2>&1; then
        systemctl --user stop "$UNIT" || { echo "FEHLER: $UNIT lässt sich nicht anhalten" >&2; exit 1; }
        echo "angehalten: $UNIT"
    fi
    if [[ -e "$UNIT_FILE" ]]; then
        rm -f -- "$UNIT_FILE" || exit 1
        echo "entfernt: $UNIT_FILE"
    else
        echo "nicht vorhanden: $UNIT_FILE"
    fi
    systemctl --user daemon-reload || exit 1
    # Zustand «failed» vergessen, falls vorhanden (Fehler, wenn unbekannt, ist ohne Bedeutung).
    systemctl --user reset-failed "$UNIT" > /dev/null 2>&1 || true
    exit 0
fi

if [[ ! -x "$CORE" ]]; then
    echo "FEHLER: $CORE fehlt. Zuerst bauen: cmake -S . -B build && cmake --build build -j8" >&2
    exit 1
fi

# Argumente für ExecStart in doppelten Anführungszeichen (systemd.service(5), «Command lines»).
quote() {
    local s="${1//\\/\\\\}"
    s="${s//\"/\\\"}"
    printf '"%s"' "$s"
}
EXEC="$(quote "$CORE")"
for a in "${CORE_ARGS[@]}"; do
    EXEC+=" $(quote "$a")"
done

mkdir -p "$UNIT_DIR" || exit 1
TMP="$(mktemp "$UNIT_DIR/.$UNIT.XXXXXX")" || exit 1
cat > "$TMP" <<EOF
# Angelegt von $REPO/scripts/dienst-einrichten.sh – nicht von Hand ändern.
[Unit]
Description=DoNs Streaming Studio – Kern (dss-core)
Documentation=file://$REPO/docs/08-kern-schnittstelle.md

[Service]
# Der Kern meldet «READY=1», sobald er Verbindungen annimmt; erst dann kehrt «systemctl start» zurück.
Type=notify
ExecStart=$EXEC
# Neustart nach Absturz (Exit-Code ungleich 0 oder Signal wie SIGKILL, SIGSEGV), nicht nach
# ordentlichem Beenden (Exit-Code 0, SIGTERM, SIGINT, SIGHUP, SIGPIPE) und nicht nach «stop».
Restart=on-failure
RestartSec=1s
# Exit-Code 3: Datenordner gesperrt oder Verbindung belegt (anderer Kern läuft); ein Neustart
# würde wieder scheitern.
RestartPreventExitStatus=3
# SIGHUP: Datenordner neu laden.
ExecReload=/bin/kill -HUP \$MAINPID

# Kein Abschnitt [Install]: Die Einheit startet nicht bei der Anmeldung, nur durch das Fenster.
EOF

if [[ -e "$UNIT_FILE" ]] && cmp -s "$TMP" "$UNIT_FILE"; then
    rm -f -- "$TMP"
    echo "unverändert: $UNIT_FILE"
else
    mv -f -- "$TMP" "$UNIT_FILE" || exit 1
    echo "geschrieben: $UNIT_FILE"
fi
systemctl --user daemon-reload || exit 1

echo "ExecStart: $EXEC"
echo "Zustand: $(systemctl --user is-active "$UNIT" 2>/dev/null) (Start durch das Fenster dss-app oder: systemctl --user start $UNIT)"
if systemctl --user is-active --quiet "$UNIT"; then
    echo "Hinweis: $UNIT läuft; eine geänderte Einheit gilt erst nach: systemctl --user restart $UNIT"
fi
exit 0
