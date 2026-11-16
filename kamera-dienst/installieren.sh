#!/usr/bin/env bash
# Installiert oder aktualisiert den Kamera-Dienst auf dem Streaming-PC (P15, Plan 6). Startet der
# Nutzer auf dem Studio-PC; Claude Code nur mit den Optionen zum Prüfen (CLAUDE.md, Regel 18).
#
#   kamera-dienst/installieren.sh <streaming-pc>
#
# Ablauf (darf mehrfach laufen, dasselbe Skript aktualisiert):
#   1. Stellt die Dateien des Dienstes in einem Ordner zusammen (dss_camera/ ohne __pycache__,
#      system-einrichten.sh, dss-kamera.service, LIESMICH.md, version.txt mit Version und Commit).
#   2. Kopiert sie mit tar über ssh in einen Ordner unter /tmp auf dem Streaming-PC (kein rsync
#      nötig) und startet dort diesen Ablauf weiter (--hier); danach wird der Ordner gelöscht.
#   3. Gibt es schon eine Installation, sichert es das Programm und die Daten nach
#      ~/.local/share/dss-kamera-sicherungen/<JJJJ-MM-TT_hh-mm-ss>/ (programm/, daten/).
#   4. Hält den Dienst an, ersetzt die Dateien in ~/.local/opt/dss-kamera/ (nur die eigenen,
#      Dateien, die die neue Version nicht mehr mitbringt, bleiben so nicht liegen).
#   5. Schreibt die Einheit ~/.config/systemd/user/dss-kamera.service, schaltet sie ein, startet
#      den Dienst neu und prüft GET /api/v1/status über 127.0.0.1.
#   Die Daten (~/.local/share/dss-kamera/) und dienst.ini (~/.config/dss-kamera/) ändert es nie.
#
# Nur zum Prüfen (alle drei zusammen, damit ein Test nie die echte Installation trifft):
#   --ziel DIR           statt ~/.local/opt/dss-kamera
#   --datenordner DIR    statt ~/.local/share/dss-kamera (Sicherungen nach DIR-sicherungen)
#   --ohne-einheit       keine Einheit, Dienst weder anhalten noch starten
#   --lokal              statt <streaming-pc>: auf diesem PC, ohne ssh (nur mit den drei oben)
# Mit <streaming-pc> gelten --ziel und --datenordner auf dem Streaming-PC.
#
# Exit-Code: 0 = installiert, 1 = Fehler, 2 = falscher Aufruf, 3 = installiert, aber
# GET status antwortet nicht.
set -u

SELF_DIR="$(cd "$(dirname "$(readlink -f "$0")")" && pwd)"
CONFIG_HOME="${XDG_CONFIG_HOME:-$HOME/.config}"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
TARGET="$HOME/.local/opt/dss-kamera"
DATA_DIR="$DATA_HOME/dss-kamera"
UNIT="dss-kamera.service"
HOST=""
LOCAL=0
HERE=0
NO_UNIT=0
TEST_OPTIONS=0
# Optionen zum Prüfen, unverändert an den zweiten Teil (--hier) weitergegeben.
FORWARD=()
# Was zum Programm gehört; nur das wird beim Aktualisieren ersetzt.
FILES=(dss_camera system-einrichten.sh dss-kamera.service LIESMICH.md version.txt)

usage() {
    sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help) usage ;;
        --ziel|--datenordner)
            [[ $# -ge 2 && -n "$2" ]] || usage
            FORWARD+=("$1" "$2")
            if [[ "$1" == --ziel ]]; then
                TARGET="$2"; TEST_OPTIONS=$((TEST_OPTIONS | 1))
            else
                DATA_DIR="$2"; TEST_OPTIONS=$((TEST_OPTIONS | 2))
            fi
            shift 2 ;;
        --ohne-einheit) NO_UNIT=1; TEST_OPTIONS=$((TEST_OPTIONS | 4)); FORWARD+=("$1"); shift ;;
        --lokal) LOCAL=1; shift ;;
        --hier) HERE=1; shift ;;
        -*) echo "FEHLER: unbekanntes Argument: $1" >&2; usage ;;
        *)
            [[ -z "$HOST" ]] || { echo "FEHLER: nur ein <streaming-pc>" >&2; usage; }
            HOST="$1"; shift ;;
    esac
done

fail() {
    echo "FEHLER: $*" >&2
    exit 1
}

if [[ $TEST_OPTIONS -ne 0 && $TEST_OPTIONS -ne 7 ]]; then
    echo "FEHLER: --ziel, --datenordner und --ohne-einheit nur alle zusammen" >&2
    exit 2
fi
if [[ $HERE -eq 0 ]]; then
    if [[ $LOCAL -eq 1 && -n "$HOST" ]]; then
        echo "FEHLER: entweder <streaming-pc> oder --lokal" >&2; exit 2
    fi
    if [[ $LOCAL -eq 0 && -z "$HOST" ]]; then
        usage
    fi
    if [[ $LOCAL -eq 1 && $TEST_OPTIONS -ne 7 ]]; then
        echo "FEHLER: --lokal nur zum Prüfen (mit --ziel, --datenordner und --ohne-einheit)" >&2
        exit 2
    fi
elif [[ $LOCAL -eq 1 || -n "$HOST" ]]; then
    echo "FEHLER: --hier ohne <streaming-pc> und ohne --lokal" >&2
    exit 2
fi

# ---------------------------------------------------------------------------------------------
# Teil 1 (Studio-PC): Dateien zusammenstellen und senden.
if [[ $HERE -eq 0 ]]; then
    for name in dss_camera system-einrichten.sh dss-kamera.service LIESMICH.md; do
        [[ -e "$SELF_DIR/$name" ]] || fail "$SELF_DIR/$name fehlt"
    done
    STAGE="$(mktemp -d "${TMPDIR:-/tmp}/dss-kamera-paket.XXXXXX")" || fail "kein Ordner unter /tmp"
    trap 'rm -rf "$STAGE"' EXIT
    tar -C "$SELF_DIR" --exclude=__pycache__ -cf - dss_camera system-einrichten.sh \
        dss-kamera.service LIESMICH.md installieren.sh | tar -C "$STAGE" -xf - \
        || fail "Dateien nicht kopierbar"
    VERSION="$(sed -n 's/^SERVICE_VERSION = "\(.*\)"$/\1/p' "$SELF_DIR/dss_camera/__init__.py")"
    COMMIT="$(git -C "$SELF_DIR" rev-parse --short HEAD 2>/dev/null || echo unbekannt)"
    if [[ -n "$(git -C "$SELF_DIR" status --porcelain -- . 2>/dev/null)" ]]; then
        COMMIT="$COMMIT (mit Änderungen)"
    fi
    printf 'Kamera-Dienst %s, Commit %s\n' "${VERSION:-unbekannt}" "$COMMIT" > "$STAGE/version.txt"

    if [[ $LOCAL -eq 1 ]]; then
        bash "$STAGE/installieren.sh" --hier "${FORWARD[@]}"
        exit $?
    fi

    # Pfade nur für die entfernte Shell maskieren (printf %q), ~ wertet sie dort aus.
    REMOTE_ARGS=""
    for arg in "${FORWARD[@]}"; do
        if [[ "$arg" == "~/"* ]]; then
            REMOTE_ARGS+=" ~/$(printf '%q' "${arg#\~/}")"
        else
            REMOTE_ARGS+=" $(printf '%q' "$arg")"
        fi
    done
    echo "Kopiere den Kamera-Dienst nach $HOST ..."
    REMOTE_DIR="$(ssh -- "$HOST" 'mktemp -d /tmp/dss-kamera-neu.XXXXXX')" \
        || fail "ssh $HOST geht nicht"
    [[ "$REMOTE_DIR" =~ ^/tmp/dss-kamera-neu\.[A-Za-z0-9]+$ ]] || fail "unerwartete Antwort von $HOST"
    tar -C "$STAGE" -cf - . | ssh -- "$HOST" "tar -C $REMOTE_DIR -xf -" \
        || { ssh -- "$HOST" "rm -rf $REMOTE_DIR"; fail "Kopieren nach $HOST gescheitert"; }
    ssh -- "$HOST" "bash $REMOTE_DIR/installieren.sh --hier$REMOTE_ARGS; rc=\$?; rm -rf $REMOTE_DIR; exit \$rc"
    exit $?
fi

# ---------------------------------------------------------------------------------------------
# Teil 2 (--hier, auf dem Streaming-PC oder mit --lokal): installieren.
SOURCE="$SELF_DIR"
for name in "${FILES[@]}"; do
    [[ -e "$SOURCE/$name" ]] || fail "$SOURCE/$name fehlt; --hier nur über installieren.sh <streaming-pc>"
done
TARGET="$(readlink -m "$TARGET")"
DATA_DIR="$(readlink -m "$DATA_DIR")"
[[ "$TARGET" != "$SOURCE" ]] || fail "Ziel und Quelle sind derselbe Ordner"
echo "Installiere $(cat "$SOURCE/version.txt") nach $TARGET"

# 3. Sicherung, nur wenn schon eine Version installiert ist.
if [[ -d "$TARGET/dss_camera" ]]; then
    BACKUP="$DATA_DIR-sicherungen/$(date +%Y-%m-%d_%H-%M-%S)"
    [[ -e "$BACKUP" ]] && BACKUP="$BACKUP-$$"
    mkdir -p "$BACKUP" || fail "Sicherung $BACKUP nicht anlegbar"
    cp -a "$TARGET" "$BACKUP/programm" || fail "Sicherung des Programms gescheitert ($BACKUP)"
    if [[ -d "$DATA_DIR" ]]; then
        cp -a "$DATA_DIR" "$BACKUP/daten" || fail "Sicherung der Daten gescheitert ($BACKUP)"
    fi
    echo "Sicherung: $BACKUP"
fi

# 4. Dienst anhalten, eigene Dateien ersetzen.
if [[ $NO_UNIT -eq 0 ]] && systemctl --user is-active --quiet "$UNIT"; then
    systemctl --user stop "$UNIT" || fail "$UNIT lässt sich nicht anhalten"
fi
mkdir -p "$TARGET" || fail "$TARGET nicht anlegbar"
for name in "${FILES[@]}"; do
    rm -rf "${TARGET:?}/$name"
done
cp -a "${FILES[@]/#/$SOURCE/}" "$TARGET/" || fail "Kopieren nach $TARGET gescheitert"
chmod 755 "$TARGET/system-einrichten.sh"

if [[ $NO_UNIT -eq 1 ]]; then
    echo "Installiert (ohne Einheit, Dienst nicht gestartet)."
    exit 0
fi

# 5. Einheit schreiben, einschalten, neu starten, Status prüfen.
UNIT_DIR="$CONFIG_HOME/systemd/user"
mkdir -p "$UNIT_DIR" || fail "$UNIT_DIR nicht anlegbar"
cp "$TARGET/dss-kamera.service" "$UNIT_DIR/$UNIT" || fail "Einheit nicht schreibbar"
systemctl --user daemon-reload || fail "systemctl --user daemon-reload gescheitert"
systemctl --user enable "$UNIT" || fail "$UNIT lässt sich nicht einschalten"
systemctl --user restart "$UNIT" || fail "$UNIT startet nicht (journalctl --user -u dss-kamera)"

if [[ ! -f "$CONFIG_HOME/dss-kamera/dienst.ini" ]]; then
    echo "Hinweis: $CONFIG_HOME/dss-kamera/dienst.ini fehlt, der Dienst ist nur über 127.0.0.1"
    echo "erreichbar. Vorlage in $TARGET/LIESMICH.md, danach: systemctl --user restart $UNIT"
fi

# Port aus dienst.ini mit derselben Prüfung wie der Dienst; bis 15 s auf GET status warten.
(cd "$TARGET" && python3 - <<'EOF'
import json
import sys
import time
import urllib.request

from dss_camera import config

settings = config.load(config.default_path())
if "127.0.0.1" not in settings.listen_addresses or "127.0.0.1" not in settings.allowed_addresses:
    print("GET status nicht prüfbar: 127.0.0.1 fehlt in lauschadressen oder erlaubte-adressen.")
    sys.exit(1)
url = f"http://127.0.0.1:{settings.port}/api/v1/status"
deadline = time.monotonic() + 15
while True:
    try:
        with urllib.request.urlopen(url, timeout=3) as response:
            body = json.loads(response.read().decode("utf-8"))
        print(f"GET status: ok, Version {body.get('service_version', '?')}, Port {settings.port}.")
        sys.exit(0)
    except (OSError, ValueError) as error:
        if time.monotonic() > deadline:
            print(f"GET status antwortet nicht ({url}): {error}")
            sys.exit(1)
        time.sleep(0.5)
EOF
) || { echo "Protokoll: journalctl --user -u dss-kamera" >&2; exit 3; }
echo "Installiert und gestartet."
