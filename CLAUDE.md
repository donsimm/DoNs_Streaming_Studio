# CLAUDE.md – Regeln für Claude Code im Projekt DoNs Streaming Studio

**Lesepflicht zu Beginn jeder Sitzung:** diese Datei, `.claude/HANDOFF.md` und nur die Dokumente zum Thema der Aufgabe. Der Auftrag kommt vom Projektleiter-Agenten oder von Lotse; nur grosse Bausteine haben eine eigene Prompt-Datei.

| Thema | Dokument |
|---|---|
| Entscheide, zwei Zeichner (K-01 bis K-08), Orte je Version | `docs/01-festlegungen.md` |
| Umgebung, Messwerte, Netzwerk | `docs/02-umgebung.md` |
| Funktionen und Prioritäten | `docs/03-funktionen-entwurf.md` |
| Offene Punkte | `docs/04-offene-punkte.md` |
| Quellen | `docs/05-quellen.md` |
| Format der Dateien | `docs/06-overlay-format.md` |
| Reihenfolge der Bausteine | `docs/07-bauplan.md` |
| Schnittstelle Fenster–Kern, Overlay-Server | `docs/08-kern-schnittstelle.md` |
| Frühere Entscheide, Berichte, Messungen, Prompts (nur nachschlagen) | `docs/archiv/` |

## Rollen (entschieden vom Nutzer, 2026-11-07)

- Lotse ist der Hauptagent des Nutzers und führt den Push aus (Regel 17). Der Projektleiter-Agent arbeitet unter Lotse und ersetzt den früheren Chat: Er plant die Bausteine, schreibt die Aufträge und führt Bauplan, offene Punkte und Übergabe.
- Der Nutzer entscheidet alles, was sichtbar oder bedienbar ist. Interna entscheidet der Agent und listet sie gesammelt mit Begründung im Bericht auf.
- Fragen an den Nutzer kommen in der Sitzung gebündelt im Auswahldialog, höchstens 4 pro Dialog, jede mit Empfehlung. Ins Panel von Lotse kommen nur Fragen, die warten können (entschieden vom Nutzer, 2026-11-08).

## Projekt

Private Overlay-Software für einen Twitch-Stream, ein Nutzer, kein Verkauf. Sie mischt, nimmt nicht auf und streamt nicht selbst. Sie erzeugt pro Overlay eine Leinwand und gibt sie aus: hauptsächlich als Browserquelle, später per NDI (höchstens 2 Ausgänge). Module: Editor (Hauptfokus) und Chat-Bot zuerst, später Live-Modul, Zuschauer-Minispiele, Musikplayer.

## Umgebung (Zielsystem = Entwicklungssystem)

- Claude Code läuft direkt auf dem Studio-PC `192.168.1.9`: Debian 13, Xfce unter X11, 8 Threads, 32 GB, GTX 1660 SUPER, Bildschirm 1920x1080 (`docs/02-umgebung.md`). Technik und Betriebssystem entschieden: C++ mit Qt 6 auf Debian 13 mit Xfce unter X11 (`docs/01-festlegungen.md`).
- OBS läuft auf dem Streaming-PC `192.168.1.116` (Nobara 44 GNOME, Wayland). Systemangaben beider Rechner führt nur `~/projekte/privat/it-system/SYSTEM.md`; was das Studio braucht, steht in `docs/02-umgebung.md`.
- Lotse erreicht den Streaming-PC per SSH (`ssh dominik@192.168.1.116`, Schlüssel vom Studio-PC). Dort wird nur gelesen (Pakete, Logs, Konfiguration, offene Ports). Änderungen gibt es erst nach dem OK des Nutzers, `sudo` gibt der Nutzer selbst ein. Gestartete Agenten nutzen den Zugang nur, wenn ihr Auftrag es ausdrücklich erlaubt.
- Ausnahme in angekündigten Handtests, ab dem Baustein nach P10c (entschieden vom Nutzer, 2026-11-08): Lotse darf
  - OBS auf dem Streaming-PC per SSH schliessen und wieder öffnen, nie während eines Streams (vorher Prozess und Ausgabe prüfen);
  - obs-websocket nur für Tests nutzen (Quellen prüfen und anlegen, Szene wechseln, Bild einer Quelle holen). Das Passwort liegt mit `systemd-creds --user` verschlüsselt in `~/.config/lotse/obs-websocket.cred` (Name `obs-websocket`, nicht in der Sicherung). Der Nutzer legt es selbst an, es kommt nie in einen Chat, ein Protokoll oder eine Datei im Klartext;
  - Bildschirmfotos vom Studio-PC machen und auswerten, Ablage nur im Scratchpad.

## Prüfstufen (entschieden vom Nutzer, 2026-11-07)

| Stufe | Wann | Prüfung |
|---|---|---|
| Klein | kleine Änderung | Bau (`cmake --build build -j8`) und `ctest --test-dir build`. Handtest nur, wenn sichtbar oder bedienbar |
| Baustein | Baustein aus dem Bauplan | dazu frischer Klon (Bau und `ctest`), `scripts/startpruefung.py` und `scripts/dienstpruefung.py`. 50 Wiederholungen (`scripts/wiederholen.sh`) nur für betroffene Tests, bei Änderungen an Takt, Threads oder Video oder nach einem Fehlschlag |
| Vor der Installation | bevor der Nutzer installiert | dazu Release-Bau und `scripts/installieren-pruefen.sh` (Testoptionen, Kopie der echten Daten), Prüfsummen von Installation und echten Daten vorher und nachher. Alle Schalter einzeln (`scripts/schalter-lauf.sh`) nur auf Wunsch |

Bei allen Prüfungen: Neben Wiederholungsläufen, Start- und Dienstprüfung keine Last (nicht bauen, Zeitgrenzen der Tests scheitern sonst). Fenstertests nie gleichzeitig. Vor Fensterprüfungen sicherstellen, dass die X11-Sitzung aktiv und nicht gesperrt ist. `dss-app` ausserhalb von `ctest` und `startpruefung.py` nur mit eigenem `XDG_CONFIG_HOME` starten, sonst wird die `fenster.ini` des Nutzers überschrieben.

## Regeln

Die Nummern bleiben, der Code verweist auf sie.

1. Baue nur, was im aktuellen Auftrag steht. Alles andere kommt in `docs/04-offene-punkte.md`. Erledigte Punkte wandern nach `docs/archiv/04-erledigte-punkte.md`, Nummern bleiben.
2. Nichts raten. Fehlt eine Angabe oder scheint sie falsch, schreibe es als `[OFFEN]` in den Bericht.
3. Ungeprüftes wird als «nicht geprüft» gekennzeichnet. Messwerte stammen nur von diesem Gerät und stehen mit Datum und Befehl im Bericht.
4. Externe Aussagen (Programmverhalten, Protokolle, Paketversionen) gegen die aktuelle Dokumentation prüfen und die Quelle (URL) im Bericht oder Code-Kommentar nennen. Immer alle Quellen nennen.
5. Pakete: Versionen festhalten. Neue Pakete nur mit Begründung im Bericht installieren (`sudo apt install` fragt den Nutzer). Keine Pakete im System ändern, die der Auftrag nicht verlangt.
6. Tests laufen mit echtem Fenstersystem (X11-Sitzung), nicht nur offscreen. Wo ein Start geprüft wird: mindestens 20 Starts, Exit-Code und sauberes Beenden prüfen.
7. Tests nutzen echte Daten (einmal als Kopie unter `tests/daten/` eingefroren) und, wo es einen Handtest gibt, dieselben Daten wie dieser. Tests in `ctest` lesen zur Laufzeit nicht aus dem Benutzerordner (entschieden vom Nutzer, 2026-11-07, Punkt 91). Ausnahme ist die Stufe «Vor der Installation»: `scripts/installieren-pruefen.sh` kopiert absichtlich den aktuellen Stand der echten Daten.
8. Ein Commit pro Funktion. Ändern mehrere kleine Punkte dieselbe Stelle und dieselbe Version der Schnittstelle, dürfen sie in einen Commit, mit Begründung im Bericht (entschieden vom Nutzer, 2026-11-04). Neue Schalter nur bei riskanten Teilen; die 17 bestehenden bleiben, Bauten mit abgeschalteten Schaltern laufen nicht mehr bei jedem Baustein (entschieden vom Nutzer, 2026-11-07).
9. Logik und Daten laufen ohne die Oberflächen-Bibliothek (ohne Qt Widgets und ohne Fenster) und werden dort getestet. Die Oberfläche ist dünn und enthält keine Logik. Keine Layout-Änderungen in Ereignis-Behandlern. Zeichnen (`QPainter`, `QImage`, Schriften) darf Qt Gui nutzen; Tests des Zeichners laufen ohne Fenster (entschieden vom Nutzer, 2026-11-03).
10. Abnahme erst nach Messung und Tests auf dem Studio-PC. Ein Handtest nur, wo etwas zu prüfen ist, das Claude Code im Bau nicht selbst prüfen kann: Aussehen und Bedienung (Fenster, Leinwand, Browserquelle in OBS, NDI, Bild und Ton), Geräte und Rechner ausserhalb des Studio-PCs, Eingaben, die nur der Nutzer machen kann. Der Handtest-Bericht teilt in «prüft Lotse» (mit den Rechten aus «Umgebung») und «prüft der Nutzer», je mit Grund (entschieden vom Nutzer, 2026-11-08, ab dem Baustein nach P10c). Was Claude Code schon geprüft hat, wiederholt der Nutzer nicht. Gibt es nichts zu prüfen, steht dort «kein Handtest nötig». Ergebnisse werden nicht erfunden.
11. Lage und Grösse von Quellen sind ganze Pixel der Overlay-Auflösung, keine Prozentwerte.
12. Zugangsdaten (Twitch-Token, OBS-Passwort) nie im Klartext in Konfiguration, Protokoll oder Tests.
13. Oberflächentexte und Dokumentation auf Deutsch in Schweizer Rechtschreibung (kein «ß», «Grösse», «ss»). Bezeichner im Code auf Englisch, Kommentare auf Deutsch.
14. Module sind fest eingebaut. Jedes Modul nutzt dieselbe Schnittstelle zum Ereignisfluss. Es gibt kein Plugin-System, auch nicht für Dritte.
15. Der Bericht folgt der Kurzvorlage unten.
16. Technisch bleiben. Keine Aussagen über die eigene Leistung oder Intelligenz.
17. Commits enden mit den Zeilen aus der Sitzungsvorgabe (Co-Authored-By). Agenten committen nur lokal. Gepusht wird nur nach Freigabe des Nutzers, und zwar durch Lotse (entschieden vom Nutzer, 2026-11-07). `--force` nur mit ausdrücklichem Auftrag des Nutzers.
18. Claude Code installiert nie in `~/.local/opt/dons-streaming-studio/` (kein `scripts/installieren.sh` gegen diesen Ort) und verändert die echten Daten `~/.local/share/dons-streaming-studio` nicht. Es arbeitet mit der Version aus dem Repo (`build/`) und deren Entwicklungsordner (`~/.local/share/dons-streaming-studio-entwicklung`, Einheit `dss-core-entwicklung.service`); Prüfungen der Installation nur mit den Testoptionen von `scripts/installieren.sh` und eigenen Testdaten (entschieden vom Nutzer, 2026-11-03).
19. Claude Code antwortet dem Nutzer im Terminal auf Deutsch in Schweizer Rechtschreibung (Wunsch des Nutzers, 2026-11-03).

## Kurzvorlage für den Bericht

Kleine Änderung: als Rückmeldung an Lotse. Baustein: zusätzlich als Datei unter `docs/archiv/berichte/`, knapp; Rohdaten nur bei Auffälligkeiten ins Repo. Der Bericht nennt keine Kennung des eigenen Commits (ändert sich beim Rebase).

- **Geändert:** Dateien, nach Funktion
- **Geprüft:** Stufe, Befehle mit Datum und Ergebnis; Ungeprüftes als «nicht geprüft»
- **Handtest:** geteilt in «prüft Lotse» und «prüft der Nutzer», je was und warum, jede Prüfung ein eigener Abschnitt; sonst «kein Handtest nötig»
- **Nicht erledigt, `[OFFEN]`**
- **Annahmen, Interna-Entscheide:** gesammelt mit Begründung
- **Quellen**
