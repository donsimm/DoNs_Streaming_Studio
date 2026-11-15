# 04 – Offene Punkte

Stand: 2026-11-15 (Punkt 3: Umfang des Chat-Bots und Übernahme der Zuschauer-Daten entschieden; davor P12: neue Punkte 103 bis 112; davor Punkt 102 erledigt und ins Archiv verschoben, P11: Punkte 79, 80 und 101 erledigt, neue Punkte 96 bis 102). Hier steht nur Offenes. Entschiedenes und Erledigtes liegt in `docs/archiv/04-erledigte-punkte.md`. Die Nummern bleiben; neue Punkte zählen ab 113 weiter. Erledigte Punkte wandern ins Archiv.

## Aus P10b, noch nicht ausdrücklich bestätigt


## Zu entscheiden

87. Aus P10b: Bleibt ein Test mit eigenem Kern stehen, weil er abstürzt, läuft sein Kern weiter (Elternprozess 1). Am 2026-11-04 um 10:23 liefen noch zwei Kerne von `tst_fensterbearbeiten` von 10:04 und 10:05 (Läufe, die an einer Prüfzeile abstürzten, die vor dem Commit entfernt wurde); beendet mit `kill` und Ordner gelöscht. Nicht im Auftrag; zu entscheiden, ob Testkerne sich beenden, wenn der Test endet (zum Beispiel `prctl(PR_SET_PDEATHSIG)`, https://man7.org/linux/man-pages/man2/prctl.2.html, nur für Tests).
88. Aus P10b: Erlaubte Endungen für Videos sind weiterhin nur `.webm` und `.mp4` (`docs/06-overlay-format.md`, «media/»). Eine `.mov` (das Beispiel im Entscheid zu Punkt 81 nennt «zaehler.mov (ProRes)») weist der Kern beim Hinzufügen ab («Endung «mov» nicht erlaubt …»); der Hinweis zu ProRes kommt also nur bei ProRes in `.mp4` (so geprüft). Nicht im Auftrag; zu entscheiden, ob `.mov` (und `.mkv`) erlaubt werden.
89. Aus P10b: Schriften im Dock «Medien» lassen sich nicht auf die Leinwand oder in die Liste «Quellen» ziehen (Ziehen gibt nur Bild- und Videodateien weiter, Entscheid E-28 nennt «neue Quelle»; eine Schrift ist keine Quelle). Zu entscheiden, ob Ziehen einer Schrift eine Textquelle mit dieser Schrift anlegen soll.
- Ohne Nummer, aus «Entschieden nach P09»: Zuschneiden von Quellen später als eigenen Punkt besprechen (E-25 enthält es nicht).
103. Aus P12b b5: Ausgang und Versatz in der Zeile «Ton Videos» (Dock «Steuerung») lassen sich auch in der Anzeige «nur Anzeige» ändern. **Entschieden (Nutzer, 2026-11-14): so lassen**, bedienbar wie die Einstellungen; «nur Anzeige» schützt die Overlays, nicht die Einstellungen des Programms. Eine eigene Sperre «Live», die alles sperrt, wäre eine Idee für später.
104. Aus P12b b5: Der Regler «Lautstärke» (Dock «Eigenschaften», Bereich «Video») hat keinen eigenen Stil im Thema (`src/app/theme.cpp`). Zu entscheiden mit dem Handtest (H4, Bildschirmfoto).
105. Aus P12b (Rückfall nach M3): Startet OBS mit der Szene im Programm, lädt die Browserquelle womöglich, bevor OBS mit dem Kern verbunden ist; dann meldet der Kern «OBS nicht verbunden», schickt kein `obs_active`, und die Seite bleibt bis zum nächsten Szenenwechsel stumm. Nicht geprüft. Möglicher Ausbau: beim Verbinden mit OBS die offenen Seiten mit unbekanntem Zustand noch einmal fragen.
106. Aus P12b b4: «Abweichung zuletzt» in der Zeile «Ton: Stopp …» ist kurz nach einem Start oder Einstieg nicht aussagekräftig (der Mischer rechnet sie im ersten Abruf gegen die alte Lage, im Test bis −945 ms). Für H3 zählen die Zeilen «Ton: Lage …». Zu entscheiden: im Mischer beim Start auf 0 setzen oder in der Zeile weglassen.
107. Aus P12b b3: Fällt PipeWire weg (Neustart von PipeWire), bleibt der Ausgang im Zustand `no_pipewire` bis zum Neustart des Kerns, ohne neue Verbindung. Nicht geprüft. Zu entscheiden, ob der Kern sich neu verbinden soll.
108. Aus P12b b3: `dss-cli einstellungen` zeigt Ausgang und Versatz des Tons (`[ton-video]`) nicht. Lautstärke und Stumm zeigt `dss-cli quelle-liste` seit P12b b6.

## Entschieden, Umsetzung steht aus

- Ohne Nummer, aus «Entschieden vor P06»: Wird NDI gebaut, gilt der Markenhinweis in «Hilfe → Über» immer; das kommt mit dem NDI-Baustein.

## Später zu klären

2. NDI-SDK: Lizenzbedingungen für die private Nutzung und der Bezug für Linux sind nicht geprüft.
3. Chat-Bot: **Umfang entschieden (Nutzer, 2026-11-15):** Aktionssystem wie Streamer.bot mit Editor im Studio, mitgelieferte Aktionen für die ersten Streams, kein Skript-Schritt (Plan `docs/archiv/prompts/P14-chatbot.md`). **Zuschauer-Daten entschieden (Nutzer, 2026-11-15):** alles übernehmen, was da ist, vor dem ersten Stream, über die Twitch-ID, nur im Datenordner des Studios (P14-1, Abschnitt z; Lesen der LiteDB-Dateien ist P14 Frage 4). Offen bleibt der Import der Aktionen: eigener Baustein nach P14, das Datenmodell von P14 ist dafür angelegt. Die Daten der alten Installation sind vorhanden und dürfen gelesen werden (ohne `auth.db` und `auth-backup.db`).
4. Live-Modul: Inhalt wird später geklärt.
5. Funktionsprioritäten (MUSS, SOLL, KANN) der übrigen Module: pro Modul festlegen, wenn es an der Reihe ist. Editor und Quelle Text sind entschieden (`docs/03-funktionen-entwurf.md`).
13. Aus P02: Für NDI offen sind Farbmatrix und Bereich bei UYVY, Vormultiplikation bei UYVA und ob die Umwandlung besser das NDI-SDK übernimmt (laut NDI-Dokumentation empfohlen). Alpha im Abgriff bei transparenter Seite nicht geprüft.
32. Aus P05: Werte der ausgegrauten Bedienelemente in «Steuerung» (30 Bilder/s, Raster 20 px, Einrasten an) sind vom Bild übernommen und nicht entschieden (E-10, E-13).
92. Ereignisfluss (Regel 14): nicht gebaut, P10c nutzt Qt-Signale von `ObsConnection`, verbunden in `CoreService::setObsConnection`. Gebaut wird er mit der Twitch-Verbindung, im Plan dort festzulegen: Felder eines Ereignisses, Abonnieren, ob Ereignisse über den Socket zum Fenster gehen. Kommt E-18 früher, ist er dort der erste Nutzer und wird dort gebaut.
93. Aus P10c: Die Meldung im Dialog «Einstellungen» (`settingsProblem`) rechnet ihre feste Höhe mit `lineSpacing() * 3`. Im Fenster «Quelle in OBS anlegen» reichte diese Rechnung im Test nicht (45 statt 48 Pixel für drei Zeilen, `tst_fensterbearbeiten`, 2026-11-07), dort misst sie nun drei gesetzte Zeilen. Der Test setzt nur das Stylesheet, nicht die Schrift der Anwendung (`src/app/main.cpp`); ob die Rechnung auch mit der echten Schrift zu knapp ist, und ob beim Dialog «Einstellungen» eine dreizeilige Meldung abgeschnitten wird, ist nicht geprüft. Nicht im Auftrag.

## Nicht geprüft

6. Machbarkeitstests T-03 und T-05 siehe unten. T-05 ist mit P10c gebaut und mit dem echten OBS bestanden (Handtest P10c, 2026-11-08); T-03 wird später eingeplant.
9. Aus P01: Der GPU-Weg von Qt (Qt Quick) ist nicht gemessen; A und C nutzen QPainter in Software.
12. Aus P02 (M2): Qt WebEngine rendert minimiert nicht (Abgriff schwarz), verdeckt und ausserhalb des Bildschirms schon. Mit zwei Overlays im selben Prozess nur 26 bis 29 neue Bilder pro Sekunde; Abgriff in getrennten Threads oder Prozessen nicht gemessen.
14. Aus P03: Die Startprüfung beendet `dss-app` per SIGTERM (das Programm schliesst dann sein Fenster). Das Schliessen über den Schliessknopf des Fenstermanagers prüft nur der Handtest (bestanden am 2026-11-02); für eine automatische Prüfung fehlt ein Werkzeug wie `xdotool` (nicht installiert).
15. Aus P03: Der Installationsweg von `scripts/einrichten.sh` (fehlendes Paket, `sudo apt install Name=Version`) ist nicht geprüft, weil auf dem Studio-PC alle Pakete schon installiert waren.
21. Aus P04: Erlaubte Dateiendungen (Bild `png`, `jpg`, `jpeg`; Video `webm`, `mp4`; Schrift `ttf`, `otf`) sind eine Annahme; geprüft wird nur die Endung, nicht der Inhalt. Siehe Punkt 88.
25. Aus P04: Dass die temporäre Datei von `tokens.json` schon vor dem Schreiben des Tokens die Rechte 0600 hat, ist nicht geprüft (`strace` nicht installiert); geprüft sind die Rechte der fertigen Datei.
- Rest aus Punkt 81: HEVC spielt die Browserquelle in OBS 32.2.2 nicht ab (Prüfung V, 2026-11-08, Bericht P11). Ob sie MPEG-4 Part 2 und ProRes in MP4 abspielt, ist nicht geprüft.
96. Aus P11: Urbanist 800 und Be Vietnam Pro klein liegen im Editor neben beiden Browsern (Seite–Editor 6.19 bzw. 6.01 im Mittel, in allen 30 Läufen gleich; Prüfung V: OBS–Editor bis 6.5, OBS–Seite 0.6 bis 2.4); die anderen Textbereiche 0.82 bis 1.53. Ursache nicht untersucht. `tst_bildvergleich` hält beide als bekannte Werte fest.
97. Aus P11: Der Abgriff von Qt WebEngine (Chromium 122) enthält im Overlay «Gesamt» von `tst_bildvergleich` 48641 Pixel mit Farbe grösser als Alpha (ungültig vormultipliziert), vermutlich im WebM mit Alpha. Ob das an Qt WebEngine oder am Abgriff liegt und ob OBS dasselbe zeigt, ist nicht geprüft.
98. Aus P11: AV1 ist nicht gemessen (keine Testdatei). Die Regel «AV1 ohne Farbangabe wie BT.601» (`src/media/videocolor.h`) stammt nur aus dem Quelltext von Chromium.
99. Aus P11: Videos mit nicht quadratischen Pixeln: Chromium wählt die Matrix ohne Farbangabe nach `natural_size` (mit Seitenverhältnis der Pixel), der Editor nach der Höhe des Bildes. Nicht geprüft.
100. Aus P11: Videos mit Angabe der Grundfarben oder der Übertragung, aber ohne Matrix: Der Editor nimmt die Höhenregel; ob Chromium gleich wählt, ist nicht geprüft.
- Rest aus Punkt 85: Der echte Fall (Kabel ziehen, Kern starten, Kabel einstecken) ist nicht geprüft.
109. Aus P12b b5: Die Lautstärke wirkt linear (`volume` / 100 als Faktor im Mischer). Ob das nach Gehör passt, ist nicht geprüft; Handtest N2/N3.
110. Aus P12b: Am echten Ausgang nicht gemessen: `pw_time.delay` (an der virtuellen Senke immer 0), der hörbare Zeitpunkt und damit Startwert, Vorzeichen und Bereich (±500 ms) von `versatz_ms`. Kommt mit H2 (ersetzt M4).
111. Aus P12b: In OBS nicht geprüft: verschachtelte Szenen (Rückfall `GetSourceActive`, `videoActive`), Verhalten mit «shutdown», Gang zwischen den Rechnern über 60 min (H3, M3 nur rund 3 min mit −5,2 ms/min), Ruhe des Glättens bei der Streuung aus M3 (p95 24,9 ms), Ursache der rund 20 Bildaufrufe je Sekunde bei 30 Bildern/s.
112. Aus P12b b1 (Klick 197, Z3c): Beim Wechsel des Quantums von PipeWire kann ein kurzes Stück Ton fehlen, ohne Lageverschiebung und ohne gemeldeten Unterlauf. Knoten und Hörbarkeit nicht geklärt; der Kern schreibt abgebrochene Zyklen ins Protokoll. Ob im Betrieb ein Programm das Quantum ändert, ist nicht geprüft.

## Machbarkeitstests

| Nr. | Test | Entscheidet über | Stand |
|---|---|---|---|
| T-03 | NDI mit Alpha senden (1080p30, dann 1080p60, dann zwei Ausgänge), CPU, Verluste und Verzögerung am OBS-PC messen | NDI, Technikwahl | Netzwerk besteht (2026-11-03), offen |
| T-05 | obs-websocket: Verbinden, Szenenwechsel erkennen, Wiederverbinden nach OBS-Neustart | Anbindung an OBS | gebaut mit P10c, gegen Testserver geprüft (2026-11-07), mit dem echten OBS im Handtest P10c bestanden (2026-11-08) |
