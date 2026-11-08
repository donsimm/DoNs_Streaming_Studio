# Übergabe DoNs Streaming Studio

Stand: 2026-11-08. Einstiegspunkt jeder Sitzung, nach `CLAUDE.md`. Nur der aktuelle Stand; Verlauf bis 2026-11-08 (Abnahme P10b, Handtest P10c, Antworten zum Ton für P12, Stufe «Vor der Installation») in `docs/archiv/uebergabe-2026-11-08.md`. Gepflegt vom Projektleiter-Agenten.

## Stand

- Abgenommen sind die Bausteine bis und mit P10c (`docs/07-bauplan.md`). Die Software kann: Overlays mit Ordnern, Quellen Text, Bild, Video und WebM mit Alpha, Bearbeiten auf der Leinwand, Transformieren wie OBS, Dock «Medien», Browserquelle in OBS (über obs-websocket angelegt und nachgeführt), Kern als Benutzerdienst, Installation mit Umwandeln und Sicherung.
- P10c: abgenommen 2026-11-08, installiert `ca8f539`, Kern mit OBS verbunden, auf `main` gepusht.
- **Echte Daten:** Laut Nutzer (2026-11-03) ist im Datenordner noch nichts Wichtiges. Der Nutzer meldet, ab wann die Software offiziell im Einsatz ist. Regel 18 gilt schon jetzt.
- Ab dem nächsten Baustein gelten die Handtest-Rechte von Lotse (`CLAUDE.md`, «Umgebung» und Regel 10).

## Nächster Schritt

1. Testgrenze `schedulerMaxDelay` erledigt (2026-11-08, Branch `lotse/scheduler-grenze`, noch nicht auf `main`): untere Grenzen 945/1890 ms statt 950/1900 ms. Geprüft: Bau, `ctest` 42 von 42 ohne Skipped, `tst_aenderungen` 50 von 50 (erstes Speichern 949 bis 1053 ms, zweites 1910 bis 2121 ms). Übernahme auf `main` und Push nach Freigabe des Nutzers.
2. P11 (Bildvergleich, Punkte 79 und 80).
3. P12 (Ton; zuerst Grundfrage klären, Antworten des Nutzers im Archiv der Übergabe).
4. Twitch-Verbindung mit dem Ereignisfluss (Punkt 92). Bericht zur Bauplan-Prüfung: `~/.claude/lotse/berichte/archiv/dss-bauplan-pruefung.md`.

Offen aus der Stufe «Vor der Installation», noch nicht in `docs/04-offene-punkte.md`: `scripts/installieren-pruefen.sh` meldet «Sicherung weicht ab», auch wenn keine Sicherung nötig war. Die Reste `build/vorinstall-klon` und `build/pruefung-vor-install/` löscht der Nutzer.

## Arbeitsweise mit dem Nutzer

- Der Nutzer will den Ablauf klar und knapp. Vorschläge mit Begründung und Gegenargument sind willkommen.
- Fragen an den Nutzer: nach `CLAUDE.md`, «Rollen».
- Lange Befehle für den Nutzer nur als kurzes Skript mit einer Zeile Aufruf (entschieden 2026-11-08). Das Terminal steht oft im Benutzerordner: Der Aufruf muss dort funktionieren (voller Pfad oder `cd ~/DoNs_Streaming_Studio &&`).
- Jede Handtest-Prüfung ist ein eigener Abschnitt. Schritte, die etwas schliessen oder beenden, stehen deutlich als eigener Schritt da.
- Klar sagen, ob etwas ein Token oder ein Passwort ist, und ob eine neue Claude-Code-Sitzung nötig ist.
- Kein Hin- und Herkopieren zwischen Sitzungen: Aufträge und Berichte liegen als Dateien im Repo oder in der Rückmeldung an Lotse.
- Bei Unklarheit nachfragen statt raten. Wenig Fachbegriffe, keine wechselnden Bezeichnungen für dieselbe Sache.
- Vermeidbare Fehler ärgern den Nutzer (falscher Ordner, fehlende Datei im Commit, Befehl ohne `cd`). Nach jedem Commit mit `git show --stat` prüfen, dass alle Dateien drin sind.
- Markenlogos legt kein Agent selbst ab. Die eigenen Symbole des Nutzers liegen in `~/.config/dons-streaming-studio/symbole/` (nicht im Repo).

## Netzwerk und Repo

- Rechner, Adressen und Zugang: `CLAUDE.md`, «Umgebung», und `docs/02-umgebung.md`.
- Repo: https://github.com/donsimm/DoNs_Streaming_Studio (Branch `main`), Adresse `git@github.com:donsimm/DoNs_Streaming_Studio.git`, Schlüssel `~/.ssh/id_ed25519` (bei GitHub «Studio-PC»). Pushen nur nach Freigabe des Nutzers, durch Lotse (Regel 17).
