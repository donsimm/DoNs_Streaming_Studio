# CLAUDE.md – Regeln für Claude Code im Projekt DoNs Streaming Studio

Lies diese Datei zu Beginn jeder Sitzung, danach die Dokumente unter `docs/`. Der Auftrag der Sitzung steht in der Datei unter `prompts/`, die dir genannt wird.

## Projekt

Private Overlay-Software für einen Twitch-Stream. Nur ein Nutzer, kein Verkauf. Sie mischt, nimmt nicht auf und streamt nicht selbst. Sie erzeugt pro Overlay eine Leinwand und gibt sie aus: hauptsächlich als Browserquelle, später per NDI (höchstens 2 Ausgänge). Module: Editor (Hauptfokus) und Chat-Bot zuerst, später Live-Modul, Zuschauer-Minispiele, Musikplayer. Details: `docs/01-festlegungen.md`.

## Umgebung (Zielsystem = Entwicklungssystem)

- Claude Code läuft direkt auf dem Studio-PC: Debian 13 (trixie), Xfce unter X11, Kernel 6.12, Intel i7-4771 (4 Kerne / 8 Threads, AVX2), 32 GB DDR3, NVIDIA GTX 1660 SUPER mit Treiber 550.163.01, Bildschirm 1920x1080 bei 60 Hz. Gemessene Ist-Werte: `docs/02-umgebung.md`.
- OBS läuft auf einem anderen Rechner (Streaming-PC, Nobara). Das Netzwerk dazwischen besteht noch nicht. Tests, die es brauchen (NDI, Browserquelle in OBS, obs-websocket), sind zurückgestellt und werden nicht erfunden.
- Technik und Betriebssystem sind noch nicht endgültig entschieden. Das Betriebssystem ist vorläufig Debian 13 mit X11.

## Regeln

1. Baue nur, was im aktuellen Prompt steht. Alles andere kommt in `docs/04-offene-punkte.md`.
2. Nichts raten. Fehlt eine Angabe oder scheint sie falsch, schreibe es als `[OFFEN]` in den Abschlussbericht.
3. Ungeprüftes wird als «nicht geprüft» gekennzeichnet. Messwerte stammen nur von diesem Gerät und stehen mit Datum und Befehl im Bericht.
4. Externe Aussagen (Programmverhalten, Protokolle, Paketversionen) gegen die aktuelle Dokumentation prüfen und die Quelle (URL) im Bericht oder Code-Kommentar nennen. Immer alle Quellen nennen.
5. Pakete: Versionen festhalten. Neue Pakete nur mit Begründung im Bericht installieren (`sudo apt install` fragt den Nutzer). Keine Pakete im System ändern, die der Prompt nicht verlangt.
6. Tests laufen mit echtem Fenstersystem (X11-Sitzung), nicht nur offscreen. Wo ein Start geprüft wird: mindestens 20 Starts, Exit-Code und sauberes Beenden prüfen.
7. Prüfung mit denselben Daten wie der Handtest (Kopie der echten Daten).
8. Ein Commit pro Funktion. Neue grössere Teile lassen sich per Schalter abschalten.
9. Logik und Daten laufen ohne die Oberflächen-Bibliothek und werden dort getestet. Die Oberfläche ist dünn und enthält keine Logik. Keine Layout-Änderungen in Ereignis-Behandlern.
10. Abnahme erst nach Messung auf dem Studio-PC und Handtest durch den Nutzer.
11. Lage und Grösse von Quellen sind ganze Pixel der Overlay-Auflösung, keine Prozentwerte.
12. Zugangsdaten (Twitch-Token, OBS-Passwort) nie im Klartext in Konfiguration, Protokoll oder Tests.
13. Oberflächentexte und Dokumentation auf Deutsch in Schweizer Rechtschreibung (kein «ß», «Grösse», «ss»). Bezeichner im Code auf Englisch, Kommentare auf Deutsch.
14. Module sind fest eingebaut. Jedes Modul nutzt dieselbe Schnittstelle zum Ereignisfluss. Es gibt kein Plugin-System, auch nicht für Dritte.
15. Der Abschlussbericht nennt: geänderte Dateien, ausgeführte Befehle und Tests mit Ergebnis, nicht erledigte Punkte, getroffene Annahmen, Quellen.
16. Technisch bleiben. Keine Aussagen über die eigene Leistung oder Intelligenz.
17. Commits enden mit den Zeilen aus der Sitzungsvorgabe (Co-Authored-By). Kein Push ohne Auftrag des Nutzers.
