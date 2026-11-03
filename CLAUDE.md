# CLAUDE.md – Regeln für Claude Code im Projekt DoNs Streaming Studio

Lies diese Datei zu Beginn jeder Sitzung, danach die Dokumente unter `docs/`. Der Auftrag der Sitzung steht in der Datei unter `prompts/`, die dir genannt wird.

## Projekt

Private Overlay-Software für einen Twitch-Stream. Nur ein Nutzer, kein Verkauf. Sie mischt, nimmt nicht auf und streamt nicht selbst. Sie erzeugt pro Overlay eine Leinwand und gibt sie aus: hauptsächlich als Browserquelle, später per NDI (höchstens 2 Ausgänge). Module: Editor (Hauptfokus) und Chat-Bot zuerst, später Live-Modul, Zuschauer-Minispiele, Musikplayer. Details: `docs/01-festlegungen.md`.

## Umgebung (Zielsystem = Entwicklungssystem)

- Claude Code läuft direkt auf dem Studio-PC: Debian 13 (trixie), Xfce unter X11, Kernel 6.12, Intel i7-4771 (4 Kerne / 8 Threads, AVX2), 32 GB DDR3, NVIDIA GTX 1660 SUPER mit Treiber 550.163.01, Bildschirm 1920x1080 bei 60 Hz. Gemessene Ist-Werte: `docs/02-umgebung.md`.
- OBS läuft auf einem anderen Rechner (Streaming-PC, zurzeit Zorin OS, Wechsel zu Nobara geplant). Seit 2026-11-03 besteht das Netzwerk: Studio-PC `192.168.1.9` (Kabel `enp3s0`), Streaming-PC `192.168.1.116` (Kabel), beide am Router fest reserviert (`docs/02-umgebung.md`, «Netzwerk»). Claude Code hat keinen Zugang zum Streaming-PC: Prüfungen dort (Browserquelle in OBS, NDI, obs-websocket) macht der Nutzer im Handtest; Ergebnisse werden nicht erfunden.
- Technik und Betriebssystem sind noch nicht endgültig entschieden. Das Betriebssystem ist vorläufig Debian 13 mit X11.

## Regeln

1. Baue nur, was im aktuellen Prompt steht. Alles andere kommt in `docs/04-offene-punkte.md`.
2. Nichts raten. Fehlt eine Angabe oder scheint sie falsch, schreibe es als `[OFFEN]` in den Abschlussbericht.
3. Ungeprüftes wird als «nicht geprüft» gekennzeichnet. Messwerte stammen nur von diesem Gerät und stehen mit Datum und Befehl im Bericht.
4. Externe Aussagen (Programmverhalten, Protokolle, Paketversionen) gegen die aktuelle Dokumentation prüfen und die Quelle (URL) im Bericht oder Code-Kommentar nennen. Immer alle Quellen nennen.
5. Pakete: Versionen festhalten. Neue Pakete nur mit Begründung im Bericht installieren (`sudo apt install` fragt den Nutzer). Keine Pakete im System ändern, die der Prompt nicht verlangt.
6. Tests laufen mit echtem Fenstersystem (X11-Sitzung), nicht nur offscreen. Wo ein Start geprüft wird: mindestens 20 Starts, Exit-Code und sauberes Beenden prüfen.
7. Tests nutzen echte Daten (Kopie davon) und, wo es einen Handtest gibt, dieselben Daten wie dieser.
8. Ein Commit pro Funktion. Neue grössere Teile lassen sich per Schalter abschalten.
9. Logik und Daten laufen ohne die Oberflächen-Bibliothek und werden dort getestet. Die Oberfläche ist dünn und enthält keine Logik. Keine Layout-Änderungen in Ereignis-Behandlern.
10. Abnahme erst nach Messung und Tests auf dem Studio-PC. Ein Handtest durch den Nutzer nur, wo er etwas prüft, das Claude Code nicht selbst prüfen kann: Aussehen und Bedienung (Fenster, Leinwand, Browserquelle in OBS), Geräte und Rechner ausserhalb des Studio-PCs, Eingaben, die nur der Nutzer machen kann. Was Claude Code auf dem Studio-PC schon geprüft hat, wiederholt der Nutzer nicht. Der Bericht nennt, was der Nutzer prüfen soll und warum; gibt es nichts, steht dort «kein Handtest nötig».
11. Lage und Grösse von Quellen sind ganze Pixel der Overlay-Auflösung, keine Prozentwerte.
12. Zugangsdaten (Twitch-Token, OBS-Passwort) nie im Klartext in Konfiguration, Protokoll oder Tests.
13. Oberflächentexte und Dokumentation auf Deutsch in Schweizer Rechtschreibung (kein «ß», «Grösse», «ss»). Bezeichner im Code auf Englisch, Kommentare auf Deutsch.
14. Module sind fest eingebaut. Jedes Modul nutzt dieselbe Schnittstelle zum Ereignisfluss. Es gibt kein Plugin-System, auch nicht für Dritte.
15. Der Abschlussbericht nennt: geänderte Dateien, ausgeführte Befehle und Tests mit Ergebnis, nicht erledigte Punkte, getroffene Annahmen, Quellen.
16. Technisch bleiben. Keine Aussagen über die eigene Leistung oder Intelligenz.
17. Commits enden mit den Zeilen aus der Sitzungsvorgabe (Co-Authored-By). Am Ende jedes Auftrags pusht Claude Code selbst: zuerst `git pull --rebase origin main`, dann `git push origin main` (erlaubt in `.claude/settings.json`, entschieden vom Nutzer am 2026-11-02). Andere Pushes (andere Zweige, `--force`) nur mit ausdrücklichem Auftrag des Nutzers.
18. Claude Code installiert nie in `~/.local/opt/dons-streaming-studio/` (kein `scripts/installieren.sh` gegen diesen Ort) und verändert die echten Daten `~/.local/share/dons-streaming-studio` nicht. Es arbeitet mit der Version aus dem Repo (`build/`) und deren Entwicklungsordner (`~/.local/share/dons-streaming-studio-entwicklung`, Einheit `dss-core-entwicklung.service`); Prüfungen der Installation nur mit den Testoptionen von `scripts/installieren.sh` und eigenen Testdaten (entschieden vom Nutzer, P10a, 2026-11-03).
