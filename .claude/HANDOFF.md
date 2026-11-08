# Übergabe DoNs Streaming Studio

Stand: 2026-11-08. Einstiegspunkt jeder Sitzung, nach `CLAUDE.md`. Nur der aktuelle Stand; Verlauf bis 2026-11-08 (Abnahme P10b, Handtest P10c, Antworten zum Ton für P12, Stufe «Vor der Installation») in `docs/archiv/uebergabe-2026-11-08.md`. Gepflegt vom Projektleiter-Agenten.

## Stand

- Abgenommen sind die Bausteine bis und mit P10c (`docs/07-bauplan.md`). Die Software kann: Overlays mit Ordnern, Quellen Text, Bild, Video und WebM mit Alpha, Bearbeiten auf der Leinwand, Transformieren wie OBS, Dock «Medien», Browserquelle in OBS (über obs-websocket angelegt und nachgeführt), Kern als Benutzerdienst, Installation mit Umwandeln und Sicherung.
- P10c: abgenommen 2026-11-08, installiert `ca8f539`, Kern mit OBS verbunden, auf `main` gepusht.
- **Echte Daten:** Laut Nutzer (2026-11-03) ist im Datenordner noch nichts Wichtiges. Der Nutzer meldet, ab wann die Software offiziell im Einsatz ist. Regel 18 gilt schon jetzt.
- Ab dem nächsten Baustein gelten die Handtest-Rechte von Lotse (`CLAUDE.md`, «Umgebung» und Regel 10).

## Nächster Schritt

1. Testgrenze `schedulerMaxDelay` erledigt (2026-11-08, auf `main` und gepusht): untere Grenzen 945/1890 ms statt 950/1900 ms. Geprüft: Bau, `ctest` 42 von 42 ohne Skipped, `tst_aenderungen` 50 von 50 (erstes Speichern 949 bis 1053 ms, zweites 1910 bis 2121 ms). Übernahme auf `main` und Push nach Freigabe des Nutzers.
2. P11 (Bildvergleich, Punkte 79 und 80): Plan mit Entscheiden in `docs/archiv/prompts/P11-bildvergleich.md`, Branch `lotse/p11`. Läuft: P11a (Commits 1–3), danach Prüfung V durch Lotse, dann P11b.
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

## P11b fertig (08.11. ~21:00, Session 6880dc81)
- `lotse/p11`, Commits cba891b…3020d52 (Videofarben Punkt 79, WebM-Drehung Punkt 80, GetSourceScreenshot vormultipliziert, feste Grenze mit 9 absichtlichen Fehlern, Doku/Bericht `docs/archiv/berichte/P11-bericht.md`). Nicht gepusht. Agent: ctest 45/45 (frischer Klon), Wiederholungen 50/50, Grenze 30/30, Leistung 90,8 % Kern.
- Nächster Schritt: Prüfung N durch Lotse (Aufruf und Erwartung im Bericht; Szene «test 2», danach zurück auf «Szene» und «DSS Bildvergleich» gelöscht prüfen). Danach Freigabe main/Push durch Dominik.
- Entscheide Dominik 08.11. ~22:05: Punkt 101 → «Text über dem Video» bekommt einen eigenen bekannten Wert (Grenze nicht aufweichen); Punkt 102 → eigener Auftrag nach P11: Ursache belegen, Test robust machen, Wiederholungen. Prüfung N gemacht 08.11. ~22:10 (Log `build/pruefung-p11/pruefung-n.log`), bestanden: Videofarbe OBS–Editor mittel ≤ 1.23, «Video BT.709 1280x720» 1.29; WebM-Drehung 90/180/270 überall «rot grün blau gelb»; Alpha: «Pixel mit Farbe grösser als Alpha: 0», PNG-Alpha OBS–Editor 0.000 (Erklärung aus 5b bestätigt); HEVC wie erwartet (OBS und Seite spielen es nicht ab, Editor schon → 153); Punkt 101 als XFAIL. Szene danach wieder «Szene», keine Quelle «DSS Bildvergleich» übrig, kein Stream/keine Aufnahme. Nächster Schritt: Punkt 101 auf `lotse/p11` umsetzen (Agent, erst wenn der Bau des Eingangs in Lotse Web fertig ist, Entscheid Dominik 08.11.), dann Freigabe main/Push; danach Punkt 102 als eigener Auftrag.
- (Frühere Fragestellung:) Punkt 101 (Schriftgewicht nur 1,96-fach der Text-Grenze: lassen / «Text über Video» eigener Wert / grösserer Gewichtsschritt), Punkt 102 (`tst_kernverbindung::settingsReadAndChange` 2× gescheitert, vermutlich Zeitfehler im Test).
- Dominik löscht: `build/frischer-klon-p11/`, `build/pruefung-p11/` (nach Prüfung N).

## P11 Punkt 101 fertig (08.11. ~23:35)
- Commits 357717e (Test: eigener Wert «Text über dem Video», Grenze Text 1.9, QEXPECT_FAIL entfernt) und f01784e (Doku). «Falsches Schriftgewicht» jetzt 2,06-fach. Agent: Grenze 30/30, Wiederholungen 50/50, ctest 45/45; Lotse: tst_bildvergleich einzeln grün. Prüfung N nicht wiederholt (nur Testgrenzen geändert).
- Offen im Bericht P11/Archiv: Vermerk «Prüfung N steht aus» nachführen (erledigt 22:10).
- Nächster Schritt: Freigabe main/Push durch Dominik; danach in `CLAUDE.md` eintragen, dass Lotse die Szene «test 2» anlegen/nutzen darf; dann Punkt 102 als eigener Auftrag.
- Dominik löscht zusätzlich: `build/pruefung-p11-101/`, `build/p11-101-einzeln.log`.
