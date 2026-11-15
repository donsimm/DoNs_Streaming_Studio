#!/usr/bin/env bash
# Führt durch die Einrichtung der App bei Twitch (P13, Plan Abschnitt 3) und trägt die Client-ID
# in die Version in Arbeit ein. Aufruf aus jedem Ordner:
#
#   ~/DoNs_Streaming_Studio/scripts/twitch-einrichten.sh
#
# Ablauf:
#   1. Nennt die Voraussetzung (Zwei-Faktor-Anmeldung am Twitch-Konto des Kanals).
#   2. Öffnet die Seite zum Registrieren der App im Browser und zeigt, was einzutragen ist.
#   3. Fragt nach der Client-ID, prüft die Form und trägt sie über den laufenden Kern ein
#      (dss-cli einstellung-twitch-client-id). Das Client Secret gibt es hier nicht: Es kommt
#      im Studio dazu (Einstellungen → Twitch).
#   4. Nennt die nächsten Schritte im Studio.
#
# Optionen (vor allem für Tests):
#   --client-id ID       nicht fragen, diese Client-ID eintragen
#   --ohne-browser       den Browser nicht öffnen, nur die Adresse zeigen
#   --cli PFAD           anderes dss-cli (Standard: build/src/cli/dss-cli im Repo)
#   --verbindung PFAD    an dss-cli weitergeben (Socket eines Testkerns)
#
# Quellen: https://dev.twitch.tv/docs/authentication/register-app/
#
# Exit-Code: 0 = eingetragen, 1 = Fehler (Kern läuft nicht, Client-ID ungültig), 2 = falscher Aufruf.
set -u

REPO="$(cd "$(dirname "$(readlink -f "$0")")/.." && pwd)"
CLI="$REPO/build/src/cli/dss-cli"
CLI_ARGS=()
CLIENT_ID=""
CLIENT_ID_SET=0
BROWSER=1
CREATE_URL="https://dev.twitch.tv/console/apps/create"
# Wie kTwitchClientIdMaxLength in src/common/coresettings.h.
MAX_LENGTH=64

usage() {
    sed -n '2,23p' "$0" | sed 's/^# \{0,1\}//' >&2
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --client-id)
            [[ $# -ge 2 ]] || usage
            CLIENT_ID="$2"; CLIENT_ID_SET=1; shift 2 ;;
        --ohne-browser) BROWSER=0; shift ;;
        --cli)
            [[ $# -ge 2 ]] || usage
            CLI="$(readlink -m "$2")"; shift 2 ;;
        --verbindung)
            [[ $# -ge 2 ]] || usage
            CLI_ARGS+=("$1" "$(readlink -m "$2")"); shift 2 ;;
        -h|--help) usage ;;
        *) echo "FEHLER: unbekanntes Argument: $1" >&2; usage ;;
    esac
done

if [[ ! -x "$CLI" ]]; then
    echo "FEHLER: $CLI fehlt. Zuerst bauen: cmake -S . -B build && cmake --build build -j8" >&2
    exit 1
fi

# Form der Client-ID: nur Kleinbuchstaben und Ziffern, nicht leer. Der Kern erlaubt auch
# Grossbuchstaben (kern.ini [twitch]); Twitch vergibt nur Kleinbuchstaben und Ziffern.
valid_client_id() {
    [[ "$1" =~ ^[a-z0-9]+$ && ${#1} -le $MAX_LENGTH ]]
}

# Leerzeichen und Tabulatoren am Anfang und Ende (beim Einfügen oft dabei) entfernen.
trim() {
    local s="$1"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

echo "Twitch für DoNs Streaming Studio einrichten"
echo
echo "1. Voraussetzung"
echo "   Am Twitch-Konto des Kanals muss die Zwei-Faktor-Anmeldung aktiv sein (Pflicht von Twitch)."
echo
echo "2. App bei Twitch registrieren"
echo "   Seite: $CREATE_URL"
if [[ $BROWSER -eq 1 ]]; then
    if xdg-open "$CREATE_URL" > /dev/null 2>&1; then
        echo "   (im Browser geöffnet)"
    else
        echo "   Der Browser liess sich nicht öffnen. Bitte die Seite von Hand öffnen."
    fi
fi
echo "   Dort eintragen:"
echo "   - Name: eindeutig, zum Beispiel «DoNs Streaming Studio <Kanalname>»"
echo "   - OAuth Redirect URL: http://localhost"
echo "   - Category: passend aus der Liste dort, zum Beispiel «Application Integration»"
echo "   - Client-Typ, falls vorhanden: «Confidential» (vertraulich)"
echo "   Nach dem Anlegen zeigt die Seite der App die Client-ID und den Knopf für ein neues Secret."
echo "   Das Secret kommt nicht hierher, sondern danach ins Studio (Schritt 4)."
echo "   Das Secret ist ein Passwort. Nicht in den Chat und nicht in eine Datei kopieren."
echo
echo "3. Client-ID eintragen"
echo "   Das ist kein Passwort und kein Token."

while :; do
    if [[ $CLIENT_ID_SET -eq 0 ]]; then
        if ! read -r -p "   Client-ID einfügen und Enter drücken: " CLIENT_ID; then
            echo
            echo "FEHLER: keine Client-ID eingegeben" >&2
            exit 1
        fi
    fi
    CLIENT_ID="$(trim "$CLIENT_ID")"
    if valid_client_id "$CLIENT_ID"; then
        break
    fi
    echo "   Die Client-ID ist ungültig: nur Kleinbuchstaben und Ziffern, nicht leer, höchstens $MAX_LENGTH Zeichen." >&2
    [[ $CLIENT_ID_SET -eq 0 ]] || exit 1
done

while :; do
    # Nur die Fehlermeldung von dss-cli behalten (stderr), die Ausgabe verwerfen.
    ERROR="$("$CLI" "${CLI_ARGS[@]}" einstellung-twitch-client-id "$CLIENT_ID" 2>&1 > /dev/null)"
    STATUS=$?
    [[ $STATUS -eq 0 ]] && break
    # Exit-Code 4 von dss-cli: Kern läuft nicht.
    if [[ $STATUS -eq 4 ]]; then
        echo "   Zuerst das Studio öffnen." >&2
        if [[ $CLIENT_ID_SET -eq 0 ]] && read -r -p "   Danach Enter drücken (Ctrl+C bricht ab): " _; then
            continue
        fi
        exit 1
    fi
    echo "FEHLER: Client-ID nicht eingetragen: $ERROR" >&2
    exit 1
done
echo "   Eingetragen: $CLIENT_ID"
echo
echo "4. Nächste Schritte im Studio"
echo "   - Auf den Twitch-Knopf klicken (öffnet Einstellungen → Twitch), «Mit Twitch"
echo "     verbinden» einschalten, das Client Secret eingeben und mit «OK» speichern."
echo "   - Bei «Kanal» auf «Anmelden …» klicken und den Code im Browser bestätigen."
echo "   - Danach bei «Bot» auf «Anmelden …» klicken und den Code in einem privaten Browserfenster"
echo "     mit dem Bot-Konto bestätigen."
echo "   - Im eigenen Chat das Bot-Konto zum Moderator machen: /mod <Botname>"
exit 0
