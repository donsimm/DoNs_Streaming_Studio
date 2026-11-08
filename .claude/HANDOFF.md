# Übergabe DoNs Streaming Studio

Stand: 2026-11-07. Einstiegspunkt jeder Sitzung, nach `CLAUDE.md`. Diese Datei hält nur den aktuellen Stand; die frühere Übergabe liegt unter `docs/archiv/00-uebergabe.md`. Gepflegt vom Projektleiter-Agenten.

## Stand

- Abgenommen sind die Bausteine bis und mit P10b, P10c ist gebaut (`docs/07-bauplan.md`). Die Software kann heute: Overlays mit Ordnern, Quellen Text, Bild, Video und WebM mit Alpha, Bearbeiten auf der Leinwand, Transformieren wie OBS, Dock «Medien», Browserquelle in OBS, Kern als Benutzerdienst, Installation mit Umwandeln und Sicherung.
- **P10b abgenommen (2026-11-07):**
  - Der Nutzer meldete zu den Handtest-Prüfungen 1 bis 4 aus dem Bericht P10b «ok, passt alles». Das schliesst die Installation und den Blick auf OBS ein.
  - Von Lotse am selben Tag geprüft: installiert ist Commit `add656b` (Installiert 2026-11-07 13:16:36), `medien.json` ist angelegt, 10 Dateien in `media/`, Sicherung `2026-11-07_13-16-36`.
- **Echte Daten:** Laut Nutzer (2026-11-03) ist im Datenordner noch nichts Wichtiges. Der Nutzer meldet, ab wann die Software offiziell im Einsatz ist. Regel 18 gilt schon jetzt.

## Nächster Schritt

P10c ist gebaut (2026-11-07): Branch `lotse/p10c`, nichts gepusht, Bericht `docs/archiv/berichte/P10c-bericht.md`, Schnittstelle Version 11, neue Punkte ab 96, Dauer 119 Minuten. `ctest` von Lotse in der echten Sitzungsumgebung: 42 von 42, keiner übersprungen. Gegen echtes OBS und Nobara 44 ist nichts geprüft.

1. Handtest des Nutzers, Prüfungen 1 bis 10 im Bericht, nicht während eines Streams.
2. Stufe «Vor der Installation» (eigener Auftrag), dann Prüfung 11, dann Push nach Freigabe.
3. P11 (Bildvergleich), P12 (Ton; zuerst Grundfrage klären), danach die Twitch-Verbindung mit dem Ereignisfluss (Punkt 92). Bericht zur Bauplan-Prüfung: `~/.claude/lotse/berichte/dss-bauplan-pruefung.md`.

Hinweis: `tst_fensterbearbeiten` scheiterte im 50er-Lauf einmal mit «im Streamzt», vermutlich Tasten ins Testfenster, nicht bewiesen. Reste in `build/` (`pruefkopie-7f3a91c2`, `frischer-klon-p10c` mit 1,9 GB, `pruefung-p10c`) entfernt Prüfung 10.

## Arbeitsweise mit dem Nutzer

- Der Nutzer will den Ablauf klar und knapp. Vorschläge mit Begründung und Gegenargument sind willkommen.
- Fragen kommen gebündelt im Auswahldialog, höchstens 4 pro Dialog, jede mit Empfehlung (entschieden 2026-11-07; ersetzt «pro Nachricht eine Sache»).
- Der Nutzer entscheidet, was sichtbar oder bedienbar ist. Interna entscheidet der Agent und listet sie gesammelt mit Begründung auf.
- Jede Handtest-Prüfung ist ein eigener Abschnitt. Schritte, die etwas schliessen oder beenden, stehen deutlich als eigener Schritt da.
- Für jeden Auftrag an Claude Code den kompletten Befehl zum Start angeben (Wunsch des Nutzers 2026-11-03).
- Jeder Befehl für das Terminal beginnt mit `cd ~/DoNs_Streaming_Studio &&`, weil das Terminal oft im Benutzerordner steht.
- Klar sagen, ob etwas ein Token oder ein Passwort ist, und ob eine neue Claude-Code-Sitzung nötig ist.
- Kein Hin- und Herkopieren zwischen Sitzungen: Aufträge und Berichte liegen als Dateien im Repo oder in der Rückmeldung an Lotse.
- Zugangsdaten nie im Chat oder im Klartext (Regel 12).
- Offizielle Dokumente und Bewerbungen nach Schweizer Standard; Texte in Schweizer Rechtschreibung (kein «ß»).
- Technisch bleiben, nicht über die eigene Leistung sprechen, immer alle Quellen nennen.
- Bei Unklarheit nachfragen statt raten. Wenig Fachbegriffe, keine wechselnden Bezeichnungen für dieselbe Sache.
- Vermeidbare Fehler ärgern den Nutzer (falscher Ordner, fehlende Datei im Commit, Befehl ohne `cd`). Nach jedem Commit mit `git show --stat` prüfen, dass alle Dateien drin sind.
- Markenlogos legt kein Agent selbst ab. Die eigenen Symbole des Nutzers liegen in `~/.config/dons-streaming-studio/symbole/` (nicht im Repo).

## Netzwerk

Studio-PC `192.168.1.9`, Streaming-PC `192.168.1.116`, beide Kabel, am Router reserviert (`docs/02-umgebung.md`). Mit P10c führt der Kern die Adresse in OBS nach, sobald sie abgenommen ist.

Seit 2026-11-08 läuft auf dem Streaming-PC das Zielsystem Nobara 44 GNOME (Wayland), Kernel 7.3.1, alle Updates installiert. Dazu laufen OBS 32.2.2 (rpm) und obs-websocket 5.7.4 auf Port 4455 mit Anmeldung, vom Studio-PC aus erreichbar (von Lotse geprüft, ohne Anmeldung). Die Firewall lässt 1025–65535 offen, die Grafik ist eine RTX 4090 mit NVIDIA 595, ein NDI-Plugin fehlt. Die OBS-Einstellung (Host, Port, Passwort) gibt es erst in `lotse/p10c`, sie gehört zum Handtest. `docs/02-umgebung.md` nennt noch Zorin und muss nachgeführt werden. Lotse liest per SSH mit (`CLAUDE.md`, Zeile 30).

## Zugriff auf das Repo

Repo: https://github.com/donsimm/DoNs_Streaming_Studio (Branch `main`). Vom Studio-PC aus per SSH: Schlüssel `~/.ssh/id_ed25519` ohne Passphrase, bei GitHub als «Studio-PC» hinterlegt, Adresse `git@github.com:donsimm/DoNs_Streaming_Studio.git`. Pushen nur nach Freigabe des Nutzers, durch Lotse (Regel 17).

## Antworten Ton (P12), Fenster «Fragen», 2026-11-07
Bündel `ton-1`, bestätigt 21:35. Daten, keine Baufreigabe. Zuerst käme ein Messtest nach der Abnahme von P10c.
- Ton der Videos über das Mischpult: Ja, der Kern spielt ihn ab.
- Szene mit Video wird in OBS aktiv: Video und Ton beginnen von vorne.
- Im Editor: Kein Ton, Vorhören vielleicht später.
- RØDECaster zum Streaming-PC: USB, eigener Kanal für den Studio-PC möglich.

## Handtest P10c, 2026-11-08 (von Lotse nachgeführt)
- Vorbereitung von Lotse lesend geprüft: OBS 32.2.2 auf Nobara läuft, Port 4455 vom Studio-PC erreichbar, `systemd-creds --user encrypt/decrypt` geht. In OBS vom Nutzer angelegt: Szene «test 2», Farbquelle «Testfarbe».
- Prüfungen 1 bis 9: vom Nutzer «ok» gemeldet. Tooltip nennt OBS 32.2.2 statt 33.0.1 aus dem Bericht (nur Beispielwert).
- Entscheide des Nutzers: Punkt 94 so lassen, Punkt 95 so lassen (beide wie empfohlen). In `docs/04-offene-punkte.md` beim nächsten Studio-Auftrag als entschieden nachführen.
- Prüfung 10 (Aufräumen): vom Nutzer «ok», von Lotse nachgeprüft. **P10c ist abgenommen (Prüfungen 1 bis 10, 2026-11-08).** Nächster Schritt: Stufe «Vor der Installation» (Auftrag in `~/.claude/lotse/berichte/dss-vorbereitung-p10c-installation-2026-11-07.md`, Teil 2; offene Frage im Panel: Wiederholungen des Fenstertests, Nutzer fragte «alternativen? nur 30?»), dann Prüfung 11, dann Push nach Freigabe.
- **Stufe «Vor der Installation» (2026-11-08, 16:19 bis 17:06):** bestanden, Bericht `docs/archiv/berichte/P10c-vor-bericht.md` (`b50777f`). `tst_fensterbearbeiten` 50 von 50. Befund: `tst_aenderungen` / `schedulerMaxDelay` im Klon 3 von 51 mit 949 ms gegen Grenze 950 ms (Grenze aus P08 ohne Reserve), danach als kleiner eigener Auftrag korrigieren (Entscheid Nutzer: zuerst installieren). Befund Skript: `installieren-pruefen.sh` meldet «Sicherung weicht ab» ohne Sicherung. Reste `build/vorinstall-klon`, `build/pruefung-vor-install/` löscht der Nutzer.
