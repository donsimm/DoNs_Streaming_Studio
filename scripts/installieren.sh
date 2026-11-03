#!/usr/bin/env bash
# Installiert die Version im Einsatz von DoNs Streaming Studio (P10a, Teil B). Nur der Nutzer
# führt es gegen den echten Ort aus; Claude Code nur mit --ziel und eigenen Testdaten (CLAUDE.md).
#
#   scripts/installieren.sh
#
# Ablauf (darf mehrfach laufen):
#   1. Bricht ab, wenn ein Fenster der installierten Version läuft (<ZIEL>/bin/dss-app).
#   2. Baut im Release-Modus in einen eigenen Build-Ordner (Standard build-einsatz/ im Repo),
#      mit DSS_INSTALLED_EDITION=ON und dem Commit für --version, ohne Tests.
#   3. Hält den Kern im Einsatz an (systemctl --user stop dss-core.service) und startet ihn
#      danach nicht wieder; er startet mit dem nächsten Fenster.
#   4. Prüft das Format der echten Daten (~/.local/share/dons-streaming-studio) mit dem neuen
#      dss-core. Sind Dateien in älterem Format, sichert es zuerst den ganzen Datenordner nach
#      ~/.local/share/dons-streaming-studio-sicherungen/<JJJJ-MM-TT_hh-mm-ss>/ und lässt dann
#      den neuen dss-core umwandeln. Scheitert das, bleiben die Dateien unverändert, das Skript
#      nennt die Sicherung und installiert nicht. Sind Dateien in neuerem Format, als die neue
#      Version lesen kann, installiert es nicht.
#   5. Installiert nach ~/.local/opt/dons-streaming-studio/ (bin/, share/dons-streaming-studio/).
#   6. Schreibt die Einheit dss-core.service (scripts/dienst-einrichten.sh --einsatz), den
#      Menüeintrag ~/.local/share/applications/dons-streaming-studio.desktop und das
#      Programm-Symbol ~/.local/share/icons/hicolor/<N>x<N>/apps/dons-streaming-studio.png.
#   7. Meldet die installierte Version (Version aus CMake und Commit).
#
# Nur zum Prüfen (alle fünf zusammen, damit ein Test nie die echten Daten trifft):
#   --ziel DIR           statt ~/.local/opt/dons-streaming-studio
#   --datenordner DIR    statt ~/.local/share/dons-streaming-studio
#   --einheit NAME       statt dss-core.service
#   --menue-ordner DIR   statt ~/.local/share/applications
#   --symbol-ordner DIR  statt ~/.local/share/icons/hicolor
# Weitere Optionen:
#   --verbindung PFAD    an dss-core weitergeben (nur zusammen mit --ziel)
#   --bau-ordner DIR     statt build-einsatz/ im Repo
#
# Quellen:
#   Desktop-Datei: https://specifications.freedesktop.org/desktop-entry/latest/recognized-keys.html
#   Kategorien:    https://specifications.freedesktop.org/menu/latest/category-registry.html
#   Symbole:       https://specifications.freedesktop.org/icon-theme/latest/
#   cmake --install: https://cmake.org/cmake/help/v3.31/manual/cmake.1.html#install-a-project
#
# Exit-Code: 0 = installiert, 1 = Fehler (nichts installiert), 2 = falscher Aufruf,
# 3 = Fenster der installierten Version läuft.
set -u

REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
TARGET="$HOME/.local/opt/dons-streaming-studio"
DATA_DIR="$DATA_HOME/dons-streaming-studio"
UNIT="dss-core.service"
MENU_DIR="$DATA_HOME/applications"
ICON_DIR="$DATA_HOME/icons/hicolor"
BUILD_DIR="$REPO/build-einsatz"
CONNECTION=""
TEST_OPTIONS=0

usage() {
    sed -n '2,41p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    [[ "$1" == -h || "$1" == --help ]] && usage
    [[ $# -ge 2 ]] || usage
    case "$1" in
        --ziel) TARGET="$(readlink -m "$2")"; TEST_OPTIONS=$((TEST_OPTIONS | 1)) ;;
        --datenordner) DATA_DIR="$(readlink -m "$2")"; TEST_OPTIONS=$((TEST_OPTIONS | 2)) ;;
        --einheit) UNIT="$2"; TEST_OPTIONS=$((TEST_OPTIONS | 4)) ;;
        --menue-ordner) MENU_DIR="$(readlink -m "$2")"; TEST_OPTIONS=$((TEST_OPTIONS | 8)) ;;
        --symbol-ordner) ICON_DIR="$(readlink -m "$2")"; TEST_OPTIONS=$((TEST_OPTIONS | 16)) ;;
        --verbindung) CONNECTION="$(readlink -m "$2")" ;;
        --bau-ordner) BUILD_DIR="$(readlink -m "$2")" ;;
        *) echo "FEHLER: unbekanntes Argument: $1" >&2; usage ;;
    esac
    shift 2
done

fail() {
    echo "FEHLER: $*" >&2
    exit 1
}

# Entweder keine oder alle Optionen zum Prüfen: Ein Test mit nur --ziel würde sonst den echten
# Kern anhalten und die echten Daten umwandeln.
if [[ $TEST_OPTIONS -ne 0 && $TEST_OPTIONS -ne 31 ]]; then
    echo "FEHLER: --ziel, --datenordner, --einheit, --menue-ordner und --symbol-ordner nur alle zusammen" >&2
    exit 2
fi
if [[ $TEST_OPTIONS -eq 31 && ( "$UNIT" == dss-core.service || "$UNIT" == dss-core-entwicklung.service ) ]]; then
    echo "FEHLER: zum Prüfen eine eigene Einheit, nicht $UNIT" >&2
    exit 2
fi
if [[ -n "$CONNECTION" && $TEST_OPTIONS -ne 31 ]]; then
    echo "FEHLER: --verbindung nur zum Prüfen (mit --ziel)" >&2
    exit 2
fi
if [[ ! "$UNIT" =~ ^[A-Za-z0-9_.-]+\.service$ ]]; then
    echo "FEHLER: Name der Einheit muss auf .service enden: $UNIT" >&2
    exit 2
fi

# 1. Läuft ein Fenster der installierten Version? /proc/<pid>/exe zeigt auf das Programm, nach
#    dem Ersetzen der Datei mit « (deleted)» (proc(5)).
APP="$TARGET/bin/dss-app"
running=()
for exe in /proc/[0-9]*/exe; do
    link="$(readlink "$exe" 2>/dev/null)" || continue
    if [[ "$link" == "$APP" || "$link" == "$APP (deleted)" ]]; then
        pid="${exe#/proc/}"
        running+=("${pid%/exe}")
    fi
done
if [[ ${#running[@]} -gt 0 ]]; then
    echo "ABBRUCH: Das Fenster von DoNs Streaming Studio läuft (PID ${running[*]})." >&2
    echo "Bitte zuerst schliessen, dann das Skript nochmals starten. Nichts wurde geändert." >&2
    exit 3
fi

# 2. Bauen. Version aus CMake (project(... VERSION ...)), Commit aus git.
VERSION="$(sed -n 's/^project(DoNsStreamingStudio VERSION \([0-9.]*\) .*/\1/p' "$REPO/CMakeLists.txt")"
COMMIT="$(git -C "$REPO" rev-parse --short HEAD 2>/dev/null)" || fail "Commit nicht lesbar (git)"
if [[ -n "$(git -C "$REPO" status --porcelain --untracked-files=no 2>/dev/null)" ]]; then
    COMMIT="$COMMIT-geändert"
    echo "Hinweis: Das Repo hat Änderungen, die nicht committet sind; Commit $COMMIT."
fi
echo "Baue DoNs Streaming Studio $VERSION (Commit $COMMIT) in $BUILD_DIR …"
mkdir -p "$BUILD_DIR" || fail "Ordner $BUILD_DIR lässt sich nicht anlegen"
LOG="$BUILD_DIR/installieren.log"
cmake -S "$REPO" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DDSS_INSTALLED_EDITION=ON \
    -DDSS_BUILD_TESTS=OFF -DDSS_COMMIT="$COMMIT" -DCMAKE_INSTALL_PREFIX="$TARGET" > "$LOG" 2>&1 \
    || { cat "$LOG" >&2; fail "cmake (Einrichten) gescheitert, nichts installiert"; }
cmake --build "$BUILD_DIR" -j"$(nproc)" >> "$LOG" 2>&1 \
    || { tail -30 "$LOG" >&2; fail "Bau gescheitert, nichts installiert (Protokoll: $LOG)"; }
NEW_CORE="$BUILD_DIR/src/core/dss-core"
"$NEW_CORE" --version | grep -q "(Einsatz, Commit $COMMIT)" || fail "$NEW_CORE ist nicht die Version im Einsatz"

# 3. Kern im Einsatz anhalten (läuft er nicht, ist nichts zu tun).
if systemctl --user is-active --quiet "$UNIT"; then
    systemctl --user stop "$UNIT" || fail "$UNIT lässt sich nicht anhalten"
    echo "Kern angehalten: $UNIT (startet mit dem nächsten Fenster)"
fi

# 4. Format der Daten prüfen, bei Bedarf sichern und umwandeln.
if [[ -d "$DATA_DIR" ]]; then
    "$NEW_CORE" --datenordner "$DATA_DIR" --format-pruefen
    rc=$?
    case $rc in
        0) ;;
        4)
            BACKUP_ROOT="$DATA_DIR-sicherungen"
            BACKUP="$BACKUP_ROOT/$(date '+%Y-%m-%d_%H-%M-%S')"
            [[ -e "$BACKUP" ]] && fail "Sicherung $BACKUP gibt es schon; in einer Sekunde nochmals starten"
            mkdir -p "$BACKUP_ROOT" || fail "Ordner $BACKUP_ROOT lässt sich nicht anlegen"
            # cp -a: Rechte (tokens.json 0600) und Zeiten bleiben; danach Inhalt vergleichen.
            cp -a "$DATA_DIR" "$BACKUP" || fail "Sicherung nach $BACKUP gescheitert, nichts umgewandelt"
            diff -r -q "$DATA_DIR" "$BACKUP" > /dev/null \
                || fail "Sicherung $BACKUP stimmt nicht mit $DATA_DIR überein, nichts umgewandelt"
            echo "Sicherung des Datenordners: $BACKUP"
            if ! "$NEW_CORE" --datenordner "$DATA_DIR" --umwandeln; then
                echo "FEHLER: Umwandeln gescheitert; die Dateien in $DATA_DIR sind unverändert." >&2
                echo "Sicherung: $BACKUP" >&2
                echo "Nichts installiert; die bisherige Version bleibt." >&2
                exit 1
            fi
            echo "Umgewandelt; Sicherung der alten Dateien: $BACKUP"
            ;;
        5) fail "Die Daten in $DATA_DIR haben ein neueres Format, als diese Version lesen kann; nichts installiert" ;;
        *) fail "Format der Daten in $DATA_DIR nicht prüfbar (Exit-Code $rc); nichts installiert" ;;
    esac
else
    echo "Datenordner $DATA_DIR fehlt noch; der Kern legt ihn beim ersten Start an."
fi

# 5. Installieren
cmake --install "$BUILD_DIR" > /dev/null || fail "cmake --install nach $TARGET gescheitert"
printf 'DoNs Streaming Studio %s\nCommit %s\nInstalliert %s aus %s\n' \
    "$VERSION" "$COMMIT" "$(date '+%Y-%m-%d %H:%M:%S %z')" "$REPO" > "$TARGET/version.txt" \
    || fail "$TARGET/version.txt lässt sich nicht schreiben"

# 6. Einheit, Menüeintrag, Programm-Symbol
UNIT_ARGS=(--einsatz "$TARGET" --einheit "$UNIT")
if [[ $TEST_OPTIONS -eq 31 ]]; then
    UNIT_ARGS+=(--datenordner "$DATA_DIR")
    [[ -n "$CONNECTION" ]] && UNIT_ARGS+=(--verbindung "$CONNECTION")
fi
"$REPO/scripts/dienst-einrichten.sh" "${UNIT_ARGS[@]}" > /dev/null || fail "Einheit $UNIT nicht eingerichtet"
echo "Einheit: $UNIT"

for png in "$REPO"/assets/branding/symbol/dons-streaming-studio-*.png; do
    n="${png##*-}"
    n="${n%.png}"
    mkdir -p "$ICON_DIR/${n}x${n}/apps" || fail "Ordner $ICON_DIR/${n}x${n}/apps lässt sich nicht anlegen"
    install -m 0644 "$png" "$ICON_DIR/${n}x${n}/apps/dons-streaming-studio.png" || fail "Symbol $n nicht kopiert"
done

mkdir -p "$MENU_DIR" || fail "Ordner $MENU_DIR lässt sich nicht anlegen"
DESKTOP="$MENU_DIR/dons-streaming-studio.desktop"
# Exec in doppelten Anführungszeichen (Pfad mit Leerzeichen); StartupWMClass = WM_CLASS des
# Fensters («dss-app», mit xprop geprüft 2026-11-03).
cat > "$DESKTOP.neu" <<EOF
[Desktop Entry]
Type=Application
Version=1.5
Name=DoNs Streaming Studio
Comment=Overlays für den Twitch-Stream bearbeiten
Exec="$APP"
Icon=dons-streaming-studio
Terminal=false
Categories=AudioVideo;Video;
StartupWMClass=dss-app
EOF
mv -f "$DESKTOP.neu" "$DESKTOP" || fail "$DESKTOP lässt sich nicht schreiben"
echo "Menüeintrag: $DESKTOP"

# 7. Version melden
echo "Installiert nach $TARGET:"
"$TARGET/bin/dss-app" --version || fail "$APP startet nicht"
"$TARGET/bin/dss-core" --version || fail "$TARGET/bin/dss-core startet nicht"
exit 0
