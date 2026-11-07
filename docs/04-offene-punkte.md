# 04 – Offene Punkte

Stand: 2026-11-07. Hier steht nur Offenes. Entschiedenes und Erledigtes liegt in `docs/archiv/04-erledigte-punkte.md`. Die Nummern bleiben; neue Punkte zählen ab 92 weiter. Erledigte Punkte wandern ins Archiv.

## Aus P10b, noch nicht ausdrücklich bestätigt

86. Aus P10b: Die Reiter von Docks, die übereinander liegen (zum Beispiel «Overlays», «Quellen», «Medien» bei einer gespeicherten Anordnung), haben keinen Stil des Designs: Im Bild des Fensters aus dem Test (`QWidget::grab`, 2026-11-04) ist der gewählte Reiter weiss mit kaum lesbarem hellem Text. Das gilt schon vor P10b für jede Anordnung mit Reitern. Nicht im Auftrag; zu entscheiden, ob die Reiter (`QTabBar` des Hauptfensters) einen Stil wie OBS bekommen. Handtest P10b (2026-11-07): Der Nutzer hat keine Einwände gemeldet, aber nicht einzeln dazu Stellung genommen.
90. Aus P10b, Annahmen zu bestätigen (Bericht P10b): (a) Beim Ablegen auf der Leinwand liegt die **Mitte** der neuen Quelle an der Mausstelle (nicht die linke obere Ecke). (b) F2 und Doppelklick im Dock «Medien» öffnen einen kleinen Dialog für den Namen wie bei Overlays und Quellen, nicht ein Feld in der Zeile. (c) Ungenutzte Dateien sind kursiv; eine Farbe geht nicht, weil das Stylesheet des Designs die Farbe der Zeilen festlegt. (d) Das Vorschaubild steht in der Spalte «Name» (40x24 Pixel), es gibt keine eigene Spalte. (e) Sortieren gilt nur im Fenster und wird nicht gespeichert; Ordner stehen immer vor den Dateien. (f) Ersatznamen nehmen das Datum der letzten Änderung der Datei in `media/` (Begründung in `docs/06-overlay-format.md`, «Medienliste»). Handtest P10b (2026-11-07): Der Nutzer hat keine Einwände gemeldet, aber nicht einzeln dazu Stellung genommen.

## Zu entscheiden

87. Aus P10b: Bleibt ein Test mit eigenem Kern stehen, weil er abstürzt, läuft sein Kern weiter (Elternprozess 1). Am 2026-11-04 um 10:23 liefen noch zwei Kerne von `tst_fensterbearbeiten` von 10:04 und 10:05 (Läufe, die an einer Prüfzeile abstürzten, die vor dem Commit entfernt wurde); beendet mit `kill` und Ordner gelöscht. Nicht im Auftrag; zu entscheiden, ob Testkerne sich beenden, wenn der Test endet (zum Beispiel `prctl(PR_SET_PDEATHSIG)`, https://man7.org/linux/man-pages/man2/prctl.2.html, nur für Tests).
88. Aus P10b: Erlaubte Endungen für Videos sind weiterhin nur `.webm` und `.mp4` (`docs/06-overlay-format.md`, «media/»). Eine `.mov` (das Beispiel im Entscheid zu Punkt 81 nennt «zaehler.mov (ProRes)») weist der Kern beim Hinzufügen ab («Endung «mov» nicht erlaubt …»); der Hinweis zu ProRes kommt also nur bei ProRes in `.mp4` (so geprüft). Nicht im Auftrag; zu entscheiden, ob `.mov` (und `.mkv`) erlaubt werden.
89. Aus P10b: Schriften im Dock «Medien» lassen sich nicht auf die Leinwand oder in die Liste «Quellen» ziehen (Ziehen gibt nur Bild- und Videodateien weiter, Entscheid E-28 nennt «neue Quelle»; eine Schrift ist keine Quelle). Zu entscheiden, ob Ziehen einer Schrift eine Textquelle mit dieser Schrift anlegen soll.
91. Aus dem Umbau (2026-11-07): `tst_fensterbearbeiten`, `mediaDockSavedLayout(echte fenster.ini (Kopie))` schlägt fehl, solange die echte `~/.config/dons-streaming-studio/fenster.ini` den Schlüssel `fenster/medien` enthält: Die Prüfzeile (`tests/tst_fensterbearbeiten.cpp:1773`) verlangt eine Datei von vor P10b, das Fenster ab P10b schreibt den Schlüssel beim Schliessen (`src/app/mainwindow.cpp:286`). Die Zeile hängt also an den Daten des Nutzers, nicht am Code; vermutlich seit dem Handtest von P10b mit der installierten Fassung (nicht geprüft). Der Rest dieser Testzeile läuft dadurch nicht mehr. Zu entscheiden, wie die Zeile zu festen Testdaten kommt (zum Beispiel eine eingefrorene Kopie unter `tests/daten/`).
- Ohne Nummer, aus «Entschieden nach P09»: Zuschneiden von Quellen später als eigenen Punkt besprechen (E-25 enthält es nicht).

## Entschieden, Umsetzung steht aus

79. Aus P10: Videos ohne Angaben zur Farbe (Matrix, Grundfarben) zeigt Chromium (Qt WebEngine 6.8.2) wie BT.601 (`smpte170m`) mit Umrechnung der Grundfarben; der Editor (swscale) rechnet nicht um. Gemessen am 2026-11-04 mit `tests/daten/video-drehung/` (Viertel rot, grün, blau): Seite Rot 248/36/0, Grün 64/252/0, Blau 27/34/255; mit Kennung BT.709 255/24/0, 0/216/0, 0/14/255. Abweichung der Videos gegen den Editor im Mittel 10.4, höchstens 95 (Bericht P10). Gehört zur festen Grenze des Bildvergleichs (P11). Entscheid 2026-11-04: Der Editor passt sich der Browserquelle an (Videos ohne Farbangabe wie Chromium umrechnen, Videos mit Angabe nach der Angabe); Dateien bleiben unverändert. Umsetzung in P11.
80. Aus P10: Die Drehung von WebM-Videos (Matroska, zum Beispiel `ProjectionPoseRoll`) neutralisiert der Server nicht; nur JPEG, PNG und MP4/MOV. Ob der Editor und Chromium bei WebM mit Drehung dasselbe zeigen, ist nicht geprüft (keine Testdatei). Entscheid 2026-11-04: In P11 eine WebM-Testdatei mit Drehangabe erzeugen und Editor gegen Seite vergleichen; nur bei Abweichung neutralisiert der Server auch bei WebM.
- Ohne Nummer, aus «Entschieden vor P06»: Wird NDI gebaut, gilt der Markenhinweis in «Hilfe → Über» immer; das kommt mit dem NDI-Baustein.

## Später zu klären

2. NDI-SDK: Lizenzbedingungen für die private Nutzung und der Bezug für Linux sind nicht geprüft.
3. Chat-Bot: Umfang und Streamer.bot-Import werden geklärt, wenn das Modul an der Reihe ist. Die Daten der alten Installation sind vorhanden.
4. Live-Modul: Inhalt wird später geklärt.
5. Funktionsprioritäten (MUSS, SOLL, KANN) der übrigen Module: pro Modul festlegen, wenn es an der Reihe ist. Editor und Quelle Text sind entschieden (`docs/03-funktionen-entwurf.md`).
13. Aus P02: Für NDI offen sind Farbmatrix und Bereich bei UYVY, Vormultiplikation bei UYVA und ob die Umwandlung besser das NDI-SDK übernimmt (laut NDI-Dokumentation empfohlen). Alpha im Abgriff bei transparenter Seite nicht geprüft.
32. Aus P05: Werte der ausgegrauten Bedienelemente in «Steuerung» (30 Bilder/s, Raster 20 px, Einrasten an) sind vom Bild übernommen und nicht entschieden (E-10, E-13).

## Nicht geprüft

6. Machbarkeitstests T-03 und T-05 siehe unten. T-05 kommt mit P10c, T-03 wird später eingeplant.
9. Aus P01: Der GPU-Weg von Qt (Qt Quick) ist nicht gemessen; A und C nutzen QPainter in Software.
12. Aus P02 (M2): Qt WebEngine rendert minimiert nicht (Abgriff schwarz), verdeckt und ausserhalb des Bildschirms schon. Mit zwei Overlays im selben Prozess nur 26 bis 29 neue Bilder pro Sekunde; Abgriff in getrennten Threads oder Prozessen nicht gemessen.
14. Aus P03: Die Startprüfung beendet `dss-app` per SIGTERM (das Programm schliesst dann sein Fenster). Das Schliessen über den Schliessknopf des Fenstermanagers prüft nur der Handtest (bestanden am 2026-11-02); für eine automatische Prüfung fehlt ein Werkzeug wie `xdotool` (nicht installiert).
15. Aus P03: Der Installationsweg von `scripts/einrichten.sh` (fehlendes Paket, `sudo apt install Name=Version`) ist nicht geprüft, weil auf dem Studio-PC alle Pakete schon installiert waren.
21. Aus P04: Erlaubte Dateiendungen (Bild `png`, `jpg`, `jpeg`; Video `webm`, `mp4`; Schrift `ttf`, `otf`) sind eine Annahme; geprüft wird nur die Endung, nicht der Inhalt. Siehe Punkt 88.
25. Aus P04: Dass die temporäre Datei von `tokens.json` schon vor dem Schreiben des Tokens die Rechte 0600 hat, ist nicht geprüft (`strace` nicht installiert); geprüft sind die Rechte der fertigen Datei.
- Rest aus Punkt 81: Ob die Browserquelle in OBS (eigenes Chromium) HEVC, MPEG-4 Part 2 und ProRes abspielt, ist nicht geprüft.
- Rest aus Punkt 85: Der echte Fall (Kabel ziehen, Kern starten, Kabel einstecken) ist nicht geprüft.

## Machbarkeitstests

| Nr. | Test | Entscheidet über | Stand |
|---|---|---|---|
| T-03 | NDI mit Alpha senden (1080p30, dann 1080p60, dann zwei Ausgänge), CPU, Verluste und Verzögerung am OBS-PC messen | NDI, Technikwahl | Netzwerk besteht (2026-11-03), offen |
| T-05 | obs-websocket: Verbinden, Szenenwechsel erkennen, Wiederverbinden nach OBS-Neustart | Anbindung an OBS | Netzwerk besteht (2026-11-03), offen, kommt mit P10c |
