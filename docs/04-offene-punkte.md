# 04 – Offene Punkte und nächste Schritte

Stand: 2026-11-02.

## Offen

1. Betriebssystem: entschieden (2026-11-02), Debian 13 mit Xfce unter X11. Technik: entschieden (2026-11-02), C++ mit Qt 6 und eigener Leinwand (Variante A); siehe `docs/01-festlegungen.md`. Sprache des Kerns: entschieden (2026-11-02), C++. Konzept der zwei Zeichner: entschieden (2026-11-02), K-01 bis K-08 in `docs/01-festlegungen.md`. Zeilenumbruch rechnet nur der Kern (entschieden 2026-11-02). Die Messungen zu B dienen nur noch als Referenz.
2. NDI-SDK: Lizenzbedingungen für die private Nutzung und der Bezug für Linux sind nicht geprüft.
3. Chat-Bot: Umfang und Streamer.bot-Import werden geklärt, wenn das Modul an der Reihe ist. Die Daten der alten Installation sind vorhanden.
4. Live-Modul: Inhalt wird später geklärt.
5. Funktionsprioritäten (MUSS, SOLL, KANN): pro Modul festlegen. Editor: entschieden (2026-11-02), Tabelle E-01 bis E-19 in `docs/03-funktionen-entwurf.md`. Umfang der Quelle Text: entschieden (2026-11-02), TX-01 bis TX-16. Zeilenumbruch (TX-11) rechnet nur der Kern (K-04).
6. Zurückgestellt, bis das Netzwerk besteht: T-03 (NDI), T-04 (Browserquelle in OBS), T-05 (obs-websocket).
7. Abbild der sauberen Installation (Clonezilla): entschieden (2026-11-02), der Nutzer verzichtet darauf.
8. Aus P01: Qt Multimedia 6.8.2 (Debian) liefert VP9-WebM mit Alpha als `NV12`, also ohne Alpha. Erledigt mit P02 (M1): Ein eigener Dekodierweg über libavcodec mit `libvpx-vp9` stellt den Alpha in A korrekt dar. Siehe `docs/messungen/P02-bericht.md`.
9. Aus P01: Der GPU-Weg von Qt (Qt Quick) ist nicht gemessen; A und C nutzen QPainter in Software.
10. Aus P01: Kernel-Angabe in `docs/02-umgebung.md` auf 6.12.113 korrigiert (erledigt).
11. Aus P02 (M1): 63 Abstände über 50 ms bei den Videobildern, höhere GPU-Auslastung (36,5 % statt 11,6 %) und etwas langsamerer Start als A; Ursachen nicht geprüft.
12. Aus P02 (M2): Qt WebEngine rendert minimiert nicht (Abgriff schwarz), verdeckt und ausserhalb des Bildschirms schon. Mit zwei Overlays im selben Prozess nur 26 bis 29 neue Bilder pro Sekunde; Abgriff in getrennten Threads oder Prozessen nicht gemessen.
13. Aus P02: Für NDI offen sind Farbmatrix und Bereich bei UYVY, Vormultiplikation bei UYVA und ob die Umwandlung besser das NDI-SDK übernimmt (laut NDI-Dokumentation empfohlen). Alpha im Abgriff bei transparenter Seite nicht geprüft.
14. Aus P03: Die Startprüfung beendet `dss-app` per SIGTERM (das Programm schliesst dann sein Fenster). Das Schliessen über den Schliessknopf des Fenstermanagers prüft nur der Handtest (bestanden am 2026-11-02); für eine automatische Prüfung fehlt ein Werkzeug wie `xdotool` (nicht installiert).
15. Aus P03: Der Installationsweg von `scripts/einrichten.sh` (fehlendes Paket, `sudo apt install Name=Version`) ist nicht geprüft, weil auf dem Studio-PC alle Pakete schon installiert waren.
16. Aus P03: Satz «Es gibt noch keinen Programmcode» in `docs/00-uebergabe.md`: erledigt (2026-11-02).
17. Aus P04: Eigenschaften der Quelle Video (Wiederholen, Ton, Start/Stopp) sind nicht entschieden.
18. Aus P04: Drehrichtung (im Uhrzeigersinn oder dagegen) ist nicht festgelegt; beide Zeichner brauchen dieselbe.
19. Aus P04: Adresse aus dem Namen: `ß` und Buchstaben ohne Zerlegung in Unicode (zum Beispiel `ł`, `æ`, `ø`) werden nach der Regel zu `-` («Straße» → `stra-e`). Ob `ß` → `ss` gewünscht ist, ist offen.
20. Aus P04: Grösse einer neuen Quelle: vorläufig Lage 0/0 in der Grösse des Overlays. Die natürliche Grösse von Bild und Video zu lesen braucht einen Dekoder; zu entscheiden, wenn der Zeichner kommt.
21. Aus P04: Erlaubte Dateiendungen (Bild `png`, `jpg`, `jpeg`; Video `webm`, `mp4`; Schrift `ttf`, `otf`) sind eine Annahme; geprüft wird nur die Endung, nicht der Inhalt.
22. Aus P04: Reihenfolge der Overlays in der Liste: vorläufig nach Name sortiert, nicht gespeichert.
23. Aus P04: Be Vietnam Pro (Standardschrift, E-11) ist auf dem Studio-PC nicht installiert (`fc-list`); Tests nutzen DejaVu Sans. Herkunft und Lizenz der Schriftdatei klären, wenn der Zeichner kommt.
24. Aus P04: Ungenutzte Kopien unter `media/` werden nicht gelöscht (Auftrag P04).
25. Aus P04: Dass die temporäre Datei von `tokens.json` schon vor dem Schreiben des Tokens die Rechte 0600 hat, ist nicht geprüft (`strace` nicht installiert); geprüft sind die Rechte der fertigen Datei.

## Machbarkeitstests

| Nr. | Test | Entscheidet über | Stand |
|---|---|---|---|
| T-01 | Oberfläche: Start mit echtem Fenstersystem, mindestens 20 Mal, Exit-Code und Beenden prüfen | Technik der Oberfläche | Prompt P01 |
| T-02 | Overlay 1920x1080 mit Animationen und WebM mit Alpha rendern, CPU und Bildrate messen | Rendern der Vorschau | Prompt P01 |
| T-03 | NDI mit Alpha senden (1080p30, dann 1080p60, dann zwei Ausgänge), CPU, Verluste und Verzögerung am OBS-PC messen | NDI, Technikwahl | wartet auf Netzwerk |
| T-04 | OBS-PC lädt ein Overlay als Browserquelle vom Studio-PC über das Heimnetz, Live-Aktualisierung prüfen | Hauptweg der Ausgabe | wartet auf Netzwerk |
| T-05 | obs-websocket: Verbinden, Szenenwechsel erkennen, Wiederverbinden nach OBS-Neustart | Anbindung an OBS | wartet auf Netzwerk |

## Reihenfolge

1. Repo und Umgebung (dieser Stand).
2. Prompt P01: Messprototypen für T-01 und T-02, Bericht in `docs/messungen/`.
3. Entscheid über Betriebssystem und Technik anhand des Berichts.
4. Dokumente für das Projekt fertigstellen, danach Prompts für die Bausteine schreiben.
5. Sobald das Netzwerk besteht: T-03 bis T-05.

## Entschieden nach P04 (2026-11-02)

- Punkt 17 (Video): Wiederholen pro Quelle wählbar, Standard «nicht wiederholen»; Browserquelle immer stumm; Ton über eine Audiokarte des Studio-PCs (E-20, E-21). Siehe `docs/01-festlegungen.md`, Abschnitt «Overlay-Daten».
- Punkt 18 (Drehrichtung): im Uhrzeigersinn.
- Punkt 19 (ß in der Adresse): «ß» → «ss», «æ» → «ae», «ø» → «oe», «ł» → «l».
- Umsetzung von Punkt 17 (Feld für Wiederholen) und 19 im Modell: mit P05.
