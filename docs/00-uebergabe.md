# 00 – Übergabe an den nächsten Chat

Stand: 2026-11-03. Diese Datei zuerst lesen, danach `CLAUDE.md` und die Dokumente 01 bis 08 unter `docs/`. Die Festlegungen stehen auch im Claude Doc: https://claude.ai/artifact/HpSYSw4X1Gx5SUPZAAhW13 (gleicher Inhalt wie die Dateien, Repo ist massgebend bei Abweichungen).

## Kurzfassung

Private Overlay-Software für den Twitch-Stream des Nutzers. Pro Overlay eine Leinwand, Ausgabe hauptsächlich als Browserquelle in OBS, später per NDI (höchstens 2 Ausgänge). Module: Editor (Hauptfokus) und Chat-Bot zuerst, später Live-Modul, Zuschauer-Minispiele, Musikplayer. Programmiert wird sie von Claude Code direkt auf dem Studio-PC. Seit P03 (2026-11-02) gibt es das Projektgerüst: Kern `dss-core`, Fenster `dss-app`, Bibliothek `dss_common`, Einrichtungsskript, Tests. Bericht: `docs/berichte/P03-bericht.md`. Seit P04 (2026-11-02) gibt es das Overlay-Modell mit Speichern und Laden, Token-Datei und das Werkzeug `dss-cli`; Format: `docs/06-overlay-format.md`, Bericht: `docs/berichte/P04-bericht.md` (abgenommen, einschliesslich Handtest). Seit P05 (2026-11-02) gelten die Nachträge zum Modell (ss in der Adresse, Video wiederholen), und das Fenster `dss-app` hat das Gerüst nach dem Bild, noch ohne Daten; Bericht: `docs/berichte/P05-bericht.md` (abgenommen, einschliesslich Handtest). Seit P06 (2026-11-02) sieht das Fenster aus wie OBS Studio (Design «Yami», Schrift Open Sans als Datei), Zahlenfelder mit Pfeilen, Verbindungsknöpfe mit vier Zuständen (alle grau); Bericht: `docs/berichte/P06-bericht.md` (abgenommen, einschliesslich Handtest). Seit P07 (2026-11-02) läuft der Kern als Benutzerdienst von systemd (`dss-core.service`, gestartet vom Fenster, nicht bei der Anmeldung), sperrt den Datenordner und schickt Overlays und Leistungswerte über einen Unix-Socket an das Fenster (`docs/08-kern-schnittstelle.md`); das Fenster zeigt Overlays, Quellen sowie CPU, RAM und LAN an. Eigene Symbole liegen in `~/.config/dons-streaming-studio/symbole/`. Bericht: `docs/berichte/P07-bericht.md` (abgenommen, einschliesslich Handtest). Seit P08 (2026-11-03) lassen sich Overlays, Ordner, Quellen und ihre Eigenschaften im Fenster bearbeiten; jede Änderung geht als Nachricht an den Kern (Schnittstelle Version 2), nur der Kern schreibt Dateien. Reihenfolge und Ordner der Liste stehen in `liste.json`, Be Vietnam Pro liegt im Repo. Bericht: `docs/berichte/P08-bericht.md` (abgenommen, einschliesslich Handtest).

## Entschieden (vom Nutzer bestätigt, 2026-11-02)

| Thema | Entscheid |
|---|---|
| Betriebssystem | Debian 13 mit Xfce unter X11 |
| Technik | C++ mit Qt 6, eigene Leinwand: Der Editor zeichnet selbst. Für die Browserquelle in OBS gibt es einen zweiten Zeichner als Webseite |
| Kern (Hintergrundprogramm) | getrennt vom Fenster, Sprache C++ |
| Fenster | dünn, ohne Logik, Oberfläche wie auf dem Bild des Nutzers (`docs/bilder/oberflaeche.png`) |
| Module | fest eingebaut, gleiche Schnittstelle zum Ereignisfluss, kein Plugin-System, auch nicht für Dritte |
| OBS | kein OBS-Plugin; Browserquelle, später NDI |
| Netz | Overlay-Server im Heimnetz, Token pro Overlay, kein Port am Router; Studio-PC per Kabel mit fester Adresse |
| Lastziele | 1080p30, bis zu 2 NDI-Ausgänge, bis zu 10 Quellen pro Overlay |
| Abbild der Installation (Clonezilla) | verzichtet |
| Nicht Teil | Spenden und Tip Jar |

Funktionsideen aus dem alten Projekt (`donstv_Studio`) bleiben als Ideen erhalten. Technik des alten Projekts wird nicht übernommen. Das alte Repo ist nur zum Lesen da.

## Messungen (auf dem Studio-PC)

Berichte: `docs/messungen/P01-bericht.md`, `docs/messungen/P02-bericht.md`. Wichtigste Ergebnisse:

- Alle drei Prototypen starteten 20 von 20 Mal sauber (P01).
- Qt Multimedia 6.8.2 liefert VP9-WebM mit Alpha als NV12, also ohne Alpha. Ein eigener Dekodierweg über libavcodec mit `libvpx-vp9` stellt den Alpha korrekt dar (P02, M1).
- Qt WebEngine rendert minimiert nicht; verdeckt und ausserhalb des Bildschirms schon. Mit zwei Overlays im selben Prozess nur 26 bis 29 neue Bilder pro Sekunde (P02, M2). Dieser Weg wird nicht weiterverfolgt, bleibt aber als Referenz.
- NDI ist nicht gemessen (Netzwerk fehlt).

## Offen

Vollständige Liste: `docs/04-offene-punkte.md`. Vor dem ersten Bau-Prompt zu klären:

1. Erledigt (2026-11-02): Prioritäten des Editors, Tabelle E-01 bis E-19 in `docs/03-funktionen-entwurf.md`. Umfang der Quelle Text ebenfalls erledigt: TX-01 bis TX-16.
2. Erledigt (2026-11-02): Konzept der zwei Zeichner, K-01 bis K-08 in `docs/01-festlegungen.md` (Text zeichnen beide Zeichner selbst). Zeilenumbruch rechnet nur der Kern.

Kann warten: NDI-SDK (Lizenz und Linux-Bezug nicht geprüft), T-03 bis T-05 (brauchen das Netzwerk), Chat-Bot-Umfang und Streamer.bot-Import (Daten der alten Installation hat der Nutzer), Live-Modul, was Qt 6.8.2 unter Debian 13 für Webserver und Twitch mitbringt (klärt der Prompt für den Kern).

## Nächste Schritte

1. P03 ausgeführt und abgenommen (2026-11-02), einschliesslich Handtest. P04 (Overlay-Modell und Format der Overlay-Datei, K-01) ausgeführt und abgenommen (2026-11-02), einschliesslich Handtest. P05 (`prompts/P05-fenster-geruest.md`) ausgeführt (2026-11-02): Nachträge zum Modell und Fenster-Gerüst; abgenommen, einschliesslich Handtest.
2. Weitere Bau-Prompts nach dem Bauplan `docs/07-bauplan.md` (Reihenfolge P06 bis P12 bestätigt am 2026-11-02). P06: Aussehen wie OBS (`prompts/P06-aussehen-obs.md`) abgenommen (2026-11-02), einschliesslich Handtest. P07 (`prompts/P07-kern-dienst.md`) abgenommen (2026-11-02), einschliesslich Handtest. Neue offene Punkte 35 bis 43 in `docs/04-offene-punkte.md`. Offene Punkte aus P07 sind alle besprochen (`docs/04-offene-punkte.md`, «Entschieden nach P07»). P08 (`prompts/P08-bearbeiten.md`, Bearbeiten im Fenster) abgenommen (2026-11-03), einschliesslich Handtest; neue offene Punkte 44 bis 51 in `docs/04-offene-punkte.md`. Offene Punkte aus P08 alle besprochen (`docs/04-offene-punkte.md`, «Entschieden nach P08»). P09 (`prompts/P09-zeichner-editor.md`, Zeichner im Editor) abgenommen (2026-11-03), einschliesslich Handtest; Bericht `docs/berichte/P09-bericht.md`, neue offene Punkte 52 bis 59 in `docs/04-offene-punkte.md`. Als Nächstes Handtest P09, danach P10 (Browserquelle).
3. Sobald das Netzwerk besteht: T-03 bis T-05.

## Rolle des Chats (Vorgabe des Nutzers, 2026-11-02, bis auf Weiteres)

- Der Chat arbeitet als IT-Projektplaner. Bei ihm laufen alle Informationen zusammen; seine Aufgabe ist, den Überblick zu behalten und Struktur ins Projekt zu bringen.
- Er plant die nächsten Schritte sorgfältig und vorausschauend und schreibt daraus die Prompts und Aufträge für Claude Code.
- Alle Änderungen und offenen Punkte bespricht er einzeln mit dem Nutzer.
- Werkzeug dafür: `docs/07-bauplan.md` (Reihenfolge der Bausteine), `docs/04-offene-punkte.md` (offene Punkte), `docs/01-festlegungen.md` (Entscheide).

## Arbeitsweise mit dem Nutzer

- Pro Nachricht eine Sache: eine Frage, eine Prüfung oder ein Ablauf. Eine Nachricht darf mehrere Teilschritte enthalten, wenn sie zusammen eine Prüfung bilden; nie zwei Prüfungen oder zwei Themen gleichzeitig (präzisiert vom Nutzer am 2026-11-03). Fragen nummerieren, mit einer Ziffer beantwortbar.
- Jeder Befehl für das Terminal beginnt mit `cd ~/DoNs_Streaming_Studio &&`, weil das Terminal oft im Benutzerordner steht. Schritte, die etwas schliessen oder beenden, stehen deutlich als eigener Schritt da.
- Klar sagen, ob etwas ein Token oder ein Passwort ist, und ob für einen Prompt eine neue Claude-Code-Sitzung nötig ist.
- Kein Hin- und Herkopieren zwischen Claude Code und Chat: Aufträge und Berichte liegen als Dateien im Repo.
- Zugangsdaten nie im Chat oder im Klartext (CLAUDE.md, Regel 12).
- Offizielle Dokumente und Bewerbungen nach Schweizer Standard; Texte in Schweizer Rechtschreibung (kein «ß»).
- Technisch bleiben, nicht über die eigene Leistung sprechen, immer alle Quellen nennen.
- Bei Unklarheit nachfragen statt raten. Wenig Fachbegriffe, keine wechselnden Bezeichnungen für dieselbe Sache.

## Zugriff auf das Repo

Repo: https://github.com/donsimm/DoNs_Streaming_Studio (Branch `main`). Der Chat braucht Schreibrechte auf dieses Repo, sonst kann er nicht pushen. Vom Studio-PC aus wird per SSH gepusht (seit 2026-11-02): Schlüssel `~/.ssh/id_ed25519` ohne Passphrase, bei GitHub als «Studio-PC» hinterlegt, Adresse des Repos `git@github.com:donsimm/DoNs_Streaming_Studio.git`. Claude Code kann damit selbst pushen, eine Eingabe von Benutzername oder Token ist nicht mehr nötig.
