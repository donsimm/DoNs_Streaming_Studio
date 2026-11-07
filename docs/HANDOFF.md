# Übergabe DoNs Streaming Studio

Stand: 2026-11-07. Einstiegspunkt jeder Sitzung, nach `CLAUDE.md`. Diese Datei hält nur den aktuellen Stand; die frühere Übergabe liegt unter `docs/archiv/00-uebergabe.md`. Gepflegt vom Projektleiter-Agenten.

## Stand

- Abgenommen sind die Bausteine bis und mit P10b (`docs/07-bauplan.md`). Die Software kann heute: Overlays mit Ordnern, Quellen Text, Bild, Video und WebM mit Alpha, Bearbeiten auf der Leinwand, Transformieren wie OBS, Dock «Medien», Browserquelle in OBS, Kern als Benutzerdienst, Installation mit Umwandeln und Sicherung.
- **P10b abgenommen (2026-11-07):**
  - Der Nutzer meldete zu den Handtest-Prüfungen 1 bis 4 aus dem Bericht P10b «ok, passt alles». Das schliesst die Installation und den Blick auf OBS ein.
  - Von Lotse am selben Tag geprüft: installiert ist Commit `add656b` (Installiert 2026-11-07 13:16:36), `medien.json` ist angelegt, 10 Dateien in `media/`, Sicherung `2026-11-07_13-16-36`.
- **Noch offen aus P10b:** Prüfung 5 (Aufräumen der Entwicklungsdaten), macht der Nutzer. Punkte 86 und 90: Der Nutzer hat keine Einwände gemeldet, aber nicht einzeln Stellung genommen; sie gelten nicht als ausdrücklich bestätigt (`docs/04-offene-punkte.md`).
- **Echte Daten:** Laut Nutzer (2026-11-03) ist im Datenordner noch nichts Wichtiges. Der Nutzer meldet, ab wann die Software offiziell im Einsatz ist. Regel 18 gilt schon jetzt.

## Nächster Schritt

1. Probelauf des schlanken Ablaufs mit einer echten kleinen Änderung, Stufe «Klein» (Schritt 9 des Umbaus, zusammen mit dem Nutzer).
2. Danach P10c (Anbindung an OBS: T-05, E-29), P11 (Bildvergleich), P12 (Ton) nach `docs/07-bauplan.md`.

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

Studio-PC `192.168.1.9`, Streaming-PC `192.168.1.116`, beide Kabel, am Router reserviert (`docs/02-umgebung.md`). Die Adresse der Browserquelle kommt bis P10c über Mousepad und `ssh` auf den Streaming-PC.

## Zugriff auf das Repo

Repo: https://github.com/donsimm/DoNs_Streaming_Studio (Branch `main`). Vom Studio-PC aus per SSH: Schlüssel `~/.ssh/id_ed25519` ohne Passphrase, bei GitHub als «Studio-PC» hinterlegt, Adresse `git@github.com:donsimm/DoNs_Streaming_Studio.git`. Pushen nur nach Freigabe des Nutzers, durch Lotse (Regel 17).
