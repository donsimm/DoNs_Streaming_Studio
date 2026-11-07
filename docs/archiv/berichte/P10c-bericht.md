# P10c – Bericht Anbindung an OBS (T-05, E-29)

Stand: 2026-11-07. Plan: `~/.claude/lotse/berichte/dss-p10c-plan.md` (Entscheide F1 bis F12 wie empfohlen). Gebaut und geprüft auf Stufe «Baustein», **der Handtest mit dem echten OBS steht aus**. Beginn 2026-11-07 16:45:40 +0100; Ende und Dauer stehen in der Rückmeldung an Lotse. Vergleich P10b: 333 Minuten.

## Geändert

- **Protokoll und Verbindung (`src/obs/`, neu, `dss_obs`, ohne Qt Gui):** `obsprotocol` (obs-websocket 5.x, RPC 1, Anmeldung), `ObsConnection` (Verbinden, Wiederverbinden, aktuelle Szene), `ObsSecretStore` (Passwort über `systemd-creds --user`, Datei `obs-passwort.cred`), `ObsSources` (Quellen anlegen, abgleichen, nachführen, `obs-quellen.json`). Testserver `tests/obstestserver.h`. `scripts/pakete.txt`, `CMakeLists.txt`.
- **Kern:** `src/core/coreservice.*`, `src/core/main.cpp`, `src/common/coresettings.*` (`kern.ini` `[obs]`), `src/common/protocol.*` (Schnittstelle Version 11: `obs_status`, `obs_sources`, `obs_create_source`, `obs_add_to_scene`, Felder in `settings` und `update_settings`).
- **Fenster:** `src/common/coreclient.*`, `src/common/windowdata.*`, `src/common/browsersource.*`, `src/app/settingsdialog.*` (Bereich «OBS»), `src/app/headerbar.*` (OBS-Knopf öffnet Einstellungen), `src/app/docks.*` (Zeile «OBS»), `src/app/obssourcedialog.*` (neu), `src/app/mainwindow*`, `src/app/theme.cpp`.
- **Tests:** neu `tst_obsprotokoll`, `tst_obsverbindung`, `tst_obspasswort`, `tst_obsquellen`; erweitert `tst_protocol`, `tst_windowdata`, `tst_kerneinstellungen`, `tst_kernverbindung`, `tst_fensterbearbeiten`. In `mediaDragAndDrop` liegt der Punkt zum Ablegen nun in der Ansicht, weil Zeilen breiter sind als die Ansicht; dass dies einen früheren Fehlschlag verursachte, ist nicht bewiesen.
- **Dokumente:** `docs/01`, `04` (Punkte 92 bis 95), `05`, `06`, `07`, `08`.

## Geprüft

Alle Läufe 2026-11-07 auf dem Studio-PC, X11-Sitzung aktiv (`/sys/class/tty/tty0/active` = `tty7`). Stufe «Baustein».

| Prüfung | Befehl | Ergebnis |
|---|---|---|
| Bau | `cmake --build build -j8` | Exit 0, keine Warnung und kein Fehler in der gefilterten Ausgabe |
| Tests | `ctest --test-dir build` | 42 von 42, 156 s |
| Frischer Klon | `git clone --branch lotse/p10c`, `cmake -S -B`, `cmake --build -j8`, `ctest` | Stand ohne diesen Bericht. Bau Exit 0, keine Warnung; 42 von 42, 176 s |
| Startprüfung | `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p10c/start/startpruefung` | je 20 von 20 sauber: `app-sigterm`, `app-kern-sigterm`, `core-sigterm`, `core-sigint` |
| Dienstprüfung | `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p10c/dienst` | bestanden. Neustart nach `kill -9` 1.092 bis 1.106 s, Fenster wieder verbunden 1.845 bis 2.900 s |
| Wiederholung | `scripts/wiederholen.sh <build> tst_fensterbearbeiten 50 <Ordner>`, Ende 18:42:11 | **49 von 50**, siehe unten |
| Oberfläche | `ui-consistency:verifying`, getrennter Prüfer, mit Kalibrierung | siehe unten |

**Fehlschlag in `tst_fensterbearbeiten`:**

- Der 50er-Lauf wurde gemacht, weil der Test während des Baus wechselnd scheiterte (`mediaDragAndDrop`, `textProperties`, `transformButtonsAndShortcuts`, `canvasImage`, `transformCommands`, teils `hasFocus()` falsch oder Menü ohne Eintrag). Die letzten drei Fehlschläge vor dem vollen Lauf gingen auf einen echten Fehler zurück: Die Höhe der Meldung reichte nicht, der Abbruch liess das Fenster offen und OBS eingeschaltet (Folgefehler `settingsDialogObs`, `mediaDragAndDrop`).
- Lauf 1 von 50 scheiterte in `textProperties`: Inhalt «Willkommen\nim Streamzt» statt «Willkommen\nim Stream». Der Test tippt nur «im Stream» über `QTest::keyClicks` (`tests/tst_fensterbearbeiten.cpp:470`). Woher «z» und «t» kamen, ist nicht bewiesen. Der Code fügt keine Zeichen an; die naheliegende Erklärung sind echte Tasten, die während des Laufs im Testfenster mit Fokus ankamen. Die anderen 49 Läufe bestanden. Die Rohausgabe liegt unter `build/pruefung-p10c/wiederholen/` (nicht im Repo).
- Ob die früheren wechselnden Fehlschläge in Fokus und Menüs dieselbe Ursache hatten, ist **nicht geprüft**.
- Ein erster Aufruf mit relativem Ausgabepfad führte gar keinen Test aus (`0 von 50`, je unter 2 ms, Aufruffehler). Er zählt nicht.

**Oberfläche:**

- Kalibrierung: Kopie von `obssourcedialog.cpp` mit eingebauten Unterschieden (eigenes Meldungsfenster mit Schliessen, Meldung ohne feste Höhe). Ein Prüfer ohne Zugriff auf das Original fand beide.
- Ein neuer Prüfer verglich alle Seiten zusammen, derselbe prüfte die Korrekturen nach. Behoben: Szenenwahl nach dem Wiederverbinden, Zustand und Meldung des Fensters nach `src/common` (Regel 9), eigene Meldung ohne Kern, Felder gemeinsam gesperrt, Fokus beim Einfügen, Platzhalter des Passworts bei unbekanntem Stand, Beschriftung mit dem Feld verknüpft, doppelte Logik, Graustufe der Zeile, Eigenschaft `state`, gleiche Wortlaute für die Zustände, Ersetzen veralteter Zustandstexte.
- Kosten geschätzt: Prüfer zusammen rund 30 Dateien und 25 Suchen, Muster vor dem Bau rund 15 Dateien und 15 Suchen.

**Nicht geprüft:**

- Alles gegen das echte OBS, ebenso Nobara 44 (Dienst, Passwort, Port, firewalld nach dem Wechsel neu prüfen).
- Aussehen im laufenden Fenster und die Höhen in der echten Schrift der Anwendung. `tst_fensterbearbeiten` setzt nur das Stylesheet; die 48 gegen 45 Pixel gelten für diese Umgebung.
- Der Ablauf «Kern und OBS wechseln, offenes Fenster» für das Ersetzen der Zustandstexte: nur im Code nachvollzogen, kein Test.
- Sehr lange Namen in «Der Name «…» ist in OBS schon vergeben.» können die drei Zeilen sprengen.
- 50 Wiederholungen anderer Tests und `schalter-lauf`: laut Plan nicht verlangt.
- Stufe «Vor der Installation»: nicht ausgeführt, kommt vor Prüfung 11.

Prüfsummen der echten Daten wurden nicht erhoben. Die Prüfungen liefen mit `build/` und eigenen Ordnern, `scripts/installieren.sh` wurde nicht aufgerufen.

## Handtest

Grund (Regel 10): Nur das echte OBS auf dem Streaming-PC zeigt Anmeldung, Quellen und Bild; Aussehen und Bedienung beurteilt der Nutzer. Abläufe gegen den Testserver sind geprüft und müssen nicht nachgemessen werden.

**Nicht während eines Streams.** Es entstehen Testquellen im echten OBS, und neue Quellen sind sofort sichtbar (F5). Die Prüfungen 1 bis 10 nutzen die Version aus dem Repo und den Entwicklungsordner. Vorher alle Fenster von DoNs Streaming Studio schliessen. In den Prüfungen 1 bis 10 startet das Fenster einmal in Prüfung 1 und bleibt offen.

### Prüfung 1 – Verbinden und Passwort

Gefragt ist das **Passwort von obs-websocket**. Es kommt nur in den Dialog, nie in einen Chat (Regel 12).

```
cd ~/DoNs_Streaming_Studio && git switch lotse/p10c && cmake --build build -j8 && systemctl --user stop dss-core-entwicklung.service; build/src/cli/dss-cli overlay-neu "Handtest P10c" 1920 1080 && build/src/cli/dss-cli quelle-bild handtest-p10c tests/daten/exif/orientierung-1.png && build/src/app/dss-app
```

1. Auf den OBS-Knopf im Kopfbereich klicken. Erwartet: Der Dialog «Einstellungen» öffnet sich.
2. «Mit OBS verbinden» ankreuzen, Adresse `192.168.1.116`, Port `4455`, ein **falsches** Passwort, «OK». Erwartet: OBS-Knopf rot, Tooltip «OBS hat das Passwort abgelehnt. Im Dialog «Einstellungen» neu eingeben.».
3. Dialog wieder öffnen. Erwartet: Passwortfeld leer, Platzhalter «gespeichert; leer lassen = behalten».
4. Richtiges Passwort, «OK». Erwartet: Knopf grün, Tooltip «OBS: verbunden (OBS 33.0.1, Szene «…»)».

### Prüfung 2 – Szenenwechsel

In OBS die Szene wechseln. Erwartet: Der Tooltip des OBS-Knopfes nennt innerhalb von etwa 1 s die neue Szene (Grenzwert aus dem Plan gesetzt, kein Messwert).

### Prüfung 3 – Neustart von OBS

1. OBS schliessen. Erwartet: Knopf rot, Tooltip «OBS unter 192.168.1.116:4455 nicht erreichbar. Neuer Versuch läuft.».
2. OBS wieder öffnen. Erwartet: Knopf von selbst wieder grün, ohne Eingabe.

### Prüfung 4 – Quelle anlegen

1. In OBS eine Szene wählen, die nicht im Stream läuft. Im Studio das Overlay «Handtest P10c» wählen. Erwartet im Dock «Steuerung», Zeile «OBS»: «Noch keine Quelle in OBS», Knopf «In OBS anlegen …».
2. Knopf klicken. Erwartet: Fenster «Quelle in OBS anlegen», Name «Handtest P10c», die aktuelle Szene ist vorgewählt.
3. «Anlegen». Erwartet: Fenster schliesst, Statuszeile «Quelle «Handtest P10c» in OBS angelegt», Zeile «Quelle «Handtest P10c» in OBS», Knopf «In weitere Szene einfügen …».
4. In OBS ansehen. Erwartet: Die Quelle zeigt dasselbe wie die Leinwand, der Hintergrund ist durchsichtig, Grösse 1920x1080.
5. «In weitere Szene einfügen …», eine zweite Szene wählen, «Einfügen». Erwartet: In OBS steht dieselbe Quelle in beiden Szenen.

### Prüfung 5 – Neuer Token

Der Token ist Teil der Adresse und steht in OBS im Klartext, wie beim Eintragen von Hand.

1. «Neuer Token» klicken. Erwartet: Rückfrage «Die alte Adresse funktioniert danach nicht mehr. OBS wird nachgeführt. Fortfahren?».
2. Bestätigen. Erwartet: Die Quelle in OBS zeigt nach kurzer Zeit wieder das Overlay, ohne Kopieren.

### Prüfung 6 – Neuer Token bei geschlossenem OBS

1. OBS schliessen. Erwartet: Zeile «Quelle «Handtest P10c» in OBS, OBS gerade nicht verbunden», rot.
2. «Neuer Token» klicken und bestätigen.
3. OBS öffnen. Erwartet: Knopf grün, die Quelle zeigt das Overlay, ohne Kopieren.

### Prüfung 7 – Neuer Port und neue Grösse

1. Einstellungen: Port des Overlay-Servers von 8081 auf 8082, «OK». Erwartet: Die Quelle zeigt nach kurzer Zeit wieder das Overlay.
2. Im Dock «Steuerung» die Auflösung auf 1280x720 ändern. Erwartet: In OBS hat die Quelle 1280x720, ihre Lage in der Szene bleibt.

Die Adresse im Heimnetz lässt sich nur auf eine Adresse dieses PCs ändern und ist hier deshalb nicht Teil der Prüfung; die Tests führen sie gegen den Testserver nach.

### Prüfung 8 – Fehlerfälle

1. Quelle in OBS umbenennen. Erwartet: Die Zeile zeigt den neuen Namen.
2. Quelle in OBS löschen. Erwartet: «Quelle in OBS fehlt», rot und fett, Knopf «In OBS anlegen …». Von selbst entsteht nichts (F7).
3. Knopf klicken, als Name den einer vorhandenen Quelle in OBS eintragen, «Anlegen». Erwartet: Fenster bleibt offen, «Der Name «…» ist in OBS schon vergeben.».
4. Freien Namen eintragen, «Anlegen». Erwartet: Die Quelle entsteht.
5. Overlay «Handtest P10c» im Studio löschen. Erwartet: Hinweis «Die Quelle «…» bleibt in OBS und ist leer. Das Studio löscht nichts in OBS.», die Quelle bleibt in OBS.

### Prüfung 9 – Aussehen und Bedienung

Neue Texte und Anordnung, nur der Nutzer kann sie beurteilen:

- **Dialog «Einstellungen»:** Bereich «OBS» mit Titel und Linie; «Mit OBS verbinden», «Adresse von OBS», «Port von obs-websocket», «Passwort», «Passwort löschen». Neuer Hinweistext oben. «Passwort löschen» wirkt erst mit «OK».
- **Dock «Steuerung»:** Zeile «OBS» unter der Meldung; zwei Zeilen Platz, Längeres im Tooltip. Lesbar auch mit «OBS: eingerichtet, aber nicht verbunden»? Hier steht zweimal «OBS», weil der Text derselbe ist wie am Knopf.
- **Fenster:** «Quelle in OBS anlegen» und «Quelle in weitere Szene einfügen». Das zweite Fenster ist aus F6 abgeleitet, weil es eine Szenenwahl braucht; der Name ist dort nur zu lesen.
- **Zu entscheiden:** Punkt 94 (Klick auf den OBS-Knopf ohne Kern) und Punkt 95 (alte Abweisung bleibt stehen), `docs/04-offene-punkte.md`.

### Prüfung 10 – Aufräumen

1. In OBS alle Testquellen aus den Prüfungen 4 und 8 löschen.
2. **Das Fenster der Entwicklung schliessen.**
3. Dann:

```
cd ~/DoNs_Streaming_Studio && systemctl --user stop dss-core-entwicklung.service; rm -rf ~/.local/share/dons-streaming-studio-entwicklung ~/.config/dons-streaming-studio-entwicklung build/pruefkopie-7f3a91c2 build/frischer-klon-p10c build/pruefung-p10c && ls ~/.local/share/ ~/.config/ | grep dons-streaming-studio
```

Erwartet: Es bleiben die echten Ordner `dons-streaming-studio` und `dons-streaming-studio-sicherungen`. Das gelöschte Passwort war nur die verschlüsselte Datei der Entwicklung.

### Prüfung 11 – Echte Installation

Erst nach den Prüfungen 1 bis 10 und nach der Stufe «Vor der Installation», die Lotse beauftragt. Nur der Nutzer installiert (Regel 18). Das Fenster der installierten Version muss geschlossen sein.

```
cd ~/DoNs_Streaming_Studio && scripts/installieren.sh
```

Dann die Version aus dem Anwendungsmenü starten und OBS einrichten wie in Prüfung 1; das Passwort ist je Version getrennt. Erwartet: Knopf grün. Eine von Hand angelegte Browserquelle mit genau der richtigen Adresse wird übernommen: Die Zeile im Dock nennt sie (F10).

## Nicht erledigt, `[OFFEN]`

- `[OFFEN]` Handtest und Abnahme, damit T-05 gegen das echte OBS.
- `[OFFEN]` Ursache der Tasten im Lauf 1 und der früheren wechselnden Fehlschläge in `tst_fensterbearbeiten`. Während Fenstertests sollte auf dem Studio-PC niemand tippen.
- `[OFFEN]` Punkte 93 bis 95 in `docs/04-offene-punkte.md`, Punkt 92 für den Ereignisfluss.
- `[OFFEN]` Ob die Felder von `GetInputList` und `GetSceneList` bei OBS 33.0.1 so aussehen wie im Quelltext `master` gelesen; der Testserver bildet diese Lesart nach. Zeigt Prüfung 4.
- `.claude/HANDOFF.md` ist für den Agenten schreibgesperrt und nicht geändert; die Änderung meldet die Rückmeldung an Lotse.
- Im Prüfordner liegen Reste, weil `rm` nicht erlaubt war: `build/pruefkopie-7f3a91c2`, `build/frischer-klon-p10c`, `build/pruefung-p10c`. Git ignoriert `build/`; Prüfung 10 entfernt sie.

## Annahmen, Interna-Entscheide

**Aus dem Plan, Abschnitt 3 und 7:**

| Entscheid | Begründung |
|---|---|
| Qt WebSockets statt eigenem Client | ein Paket statt eigener Protokoll-Umsetzung, Testserver inklusive |
| Eigene Bibliothek `src/obs/` | gleiches Muster wie `src/server`, ohne Qt Gui |
| `update_settings` trägt auch das Passwort | eine Antwort, vorhandener Dialogablauf bleibt |
| Zuordnung über `inputUuid` in `obs-quellen.json` | bleibt beim Umbenennen stabil, enthält keinen Token |
| Zuordnung bleibt, wenn die Quelle fehlt | sonst ginge sie beim Wechsel der Szenensammlung verloren |
| Handänderung der URL wird beim Abgleich überschrieben (Annahme) | die Kopplung ist das Ziel von E-29 |
| Nur `CurrentProgramSceneChanged` für die aktuelle Szene | Vorschau im Studio-Modus ist für P10c ohne Nutzen |
| Eigener Prüfwert für die Adresse von OBS | `validateHomeAddress` schliesst `127.x` aus, die Tests brauchen `127.0.0.1` |
| Kein neuer Schalter | Standard «aus» reicht |
| Eine Handtest-Runde am Ende | Hauptrisiken waren vor dem Bau geklärt |
| Kein Ereignisfluss in P10c, Qt-Signale an einer Stelle | ein Sender, ein Empfänger, kein Modul als Abonnent; die Felder sind erst mit Twitch bekannt. Punkt 92 hält den Baustein fest |

**Beim Bau:**

| Entscheid | Begründung |
|---|---|
| `systemd-creds` mit stdout statt Ausgabedatei (Abweichung von Plan 1.4) | keine Zwischendatei; Ablage selbst atomar |
| Neue `QWebSocket` je Versuch | sauberer Zustand nach Abbruch |
| `ObsSources` nutzt `ObsConnection` als `friend`, statt Anfragen öffentlich zu machen | Plan 1.5a: nach aussen nur Signale und Abfragen |
| `InputCreated` löst nach 200 ms einen Abgleich aus | neue Quellen von Hand werden ohne Neustart übernommen (F10) |
| `GetVersion` zusätzlich zur Begrüssung | die Begrüssung nennt die Version von OBS nicht |
| `obs_password`: fehlt = behalten, leer = löschen | eindeutig ohne zusätzliches Feld |
| Umbenennen der aktuellen Szene ohne `currentSceneChanged` | Name kommt über `scenesChanged` und `obs_status` |
| `TcpGate` im Testserver | tote Verbindung lässt sich nur so nachstellen |
| Abgleich hält den letzten Stand bei Trennung | die Zeile bleibt lesbar, solange OBS weg ist |
| Fehlende `width` oder `height` zählen als Unterschied | `GetInputSettings` liefert keine Standardwerte; so stimmt die Grösse auch bei übernommenen Quellen |
| `InputRemoved` während eines Sammlungswechsels ignoriert | sonst erschiene jede Quelle kurz als fehlend |
| `SettingsForm`, Löschen des Passworts erst mit «OK» | Logik in `src/common` (Regel 9), «Abbrechen» bleibt folgenlos |
| Ein Formular im Dialog | Spalten bleiben bündig; Titel und Linie zwei Zeilen, keine Auslagerung von `propertySection` |
| Name beim Einfügen nur zu lesen statt Zeile ausblenden | keine Änderung der Anordnung (Regel 9) |
| Höhe der festen Meldungen aus gesetzten Zeilen in den neuen Teilen | `lineSpacing()` war im Test zu knapp; `settingsProblem` bleibt, Punkt 93 |
| Meldung des Kerns wird vom Zustand nicht ersetzt | «Verbindung zu OBS abgebrochen …» kommt mit dem Zustand zusammen; Folge in Punkt 95 |
| OBS-Knopf im Dock nicht an `edit` gebunden | eine Quelle anlegen ändert kein Overlay |
| Commit zu Schritt 4 fasst Einstellungen, Protokoll und Kern zusammen (Regel 8) | dieselbe Stelle und dieselbe Version der Schnittstelle |
| Commit zu den Befunden der Oberflächenprüfung fasst mehrere kleine Punkte zusammen (Regel 8) | dieselben Stellen, dieselbe Version |
| 50er-Lauf trotz «kein 50er-Lauf» im Plan | Plan und CLAUDE.md verlangen ihn nach einem Fehlschlag |

Risiko, gegeben: Der Token steht in der Szenensammlung von OBS im Klartext, wie beim Eintragen von Hand.

**Wortlaute neu:** alle Texte in `src/common/windowdata.cpp` (`settingsDialogHint`, `obsPasswordPlaceholder`, `obsSourceRow`, `obsSourceDialogState`), `src/app/obssourcedialog.cpp`, `src/obs/obsconnection.cpp`, `src/obs/obssources.cpp`. `dss-cli` nutzt `newTokenQuestion()` ohne Angabe und fragt also unverändert.

## Quellen

- obs-websocket (Stand `master`, gelesen 2026-11-07): https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md, https://raw.githubusercontent.com/obsproject/obs-websocket/master/src/utils/Obs_ArrayHelper.cpp, https://raw.githubusercontent.com/obsproject/obs-websocket/master/src/requesthandler/RequestHandler_Inputs.cpp, https://raw.githubusercontent.com/obsproject/obs-websocket/master/src/requesthandler/RequestHandler_SceneItems.cpp, https://raw.githubusercontent.com/obsproject/obs-websocket/master/src/requesthandler/rpc/Request.cpp
- OBS: https://obsproject.com/kb/remote-control-guide, https://raw.githubusercontent.com/obsproject/obs-browser/master/obs-browser-source.cpp
- Qt: https://doc.qt.io/qt-6.8/qwebsocket.html, https://doc.qt.io/qt-6.8/qwebsockethandshakeoptions.html, https://doc.qt.io/qt-6.8/qwebsocketserver.html, https://doc.qt.io/qt-6.8/qsavefile.html
- Passwort: https://www.freedesktop.org/software/systemd/man/latest/systemd-creds.html
- Weitere Quellen des Plans: `~/.claude/lotse/berichte/dss-p10c-plan.md`, Abschnitt 5.
- Vorbereitung (Angaben von Lotse, 2026-11-07): OBS 33.0.1, obs-websocket 5.7.6, RPC 1, `qt6-websockets-dev` und `libqt6websockets6` 6.8.2-3.
