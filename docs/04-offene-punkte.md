# 04 – Offene Punkte und nächste Schritte

Stand: 2026-11-02 (nach P07).

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

26. Aus P05: Der X-Bildschirmschoner startet nach 600 s ohne Eingabe (`xset q`), danach sperrt `light-locker` die Sitzung (Wechsel zum Anmeldebildschirm auf VT 8). Gesperrt bleiben neue Fenster ungezeichnet (`IsUnMapped`); die Startprüfung meldet dann «nicht sauber». Erledigt (2026-11-02): Bildschirmschoner und light-locker auf Wunsch des Nutzers ganz abgeschaltet, siehe `docs/02-umgebung.md`.
27. Aus P05: Farbe der Verbindungsknöpfe je Zustand ist eine Annahme (rot = nicht verbunden, grün = verbunden, nach dem Bild). Erledigt (2026-11-02): vier Zustände grün, orange, rot, grau, siehe `docs/01-festlegungen.md`, Abschnitt «Verbindungsanzeige im Kopfbereich». Umgesetzt in P06 (2026-11-02): Eigenschaft `state` mit vier Werten, Farben aus Yami (`--green3`, `--yellow2`, `--red3`, `--grey1`), alle Knöpfe grau; siehe `docs/berichte/P06-bericht.md`.
28. Aus P05: Die Symbole unter `src/app/symbole/` enthalten ihre Farbe selbst (SVG); sie folgen einem späteren hellen Design (E-16) nicht von allein. Erledigt in P06 (2026-11-02): Symbole aus `loadSymbol` werden einfarbig in Textfarbe gezeichnet (Datei als Maske). Ausnahme siehe Punkt 34.
29. Aus P05: Schrift des Fensters ist die Standardschrift von Qt auf dem Studio-PC (DejaVu Sans); das Bild des Nutzers zeigt eine andere Schrift (nicht bestimmt). Welche Schrift das Fenster nutzen soll, ist offen. Entschieden (2026-11-02): Aussehen wie OBS Studio (Design «Yami»), Schrift Open Sans, siehe `docs/01-festlegungen.md`, Abschnitt «Aussehen des Fensters». Erledigt in P06 (2026-11-02): Open Sans 3.003 als Datei unter `src/app/schriften/`, 10pt wie OBS.
30. Aus P05: Die Zahlenfelder (Eigenschaften, Steuerung) zeigen wegen des Stylesheets keine Pfeile wie auf dem Bild. Erledigt in P06 (2026-11-02): Pfeile aus OBS, gut sichtbar; Feldbreite so, dass der Wert nicht abgeschnitten wird.
31. Aus P05: Ändern von «Wiederholen» bei einer bestehenden Video-Quelle gibt es im Modell noch nicht (nur beim Hinzufügen); kommt mit den Eigenschaften im Fenster.
32. Aus P05: Werte der ausgegrauten Bedienelemente in «Steuerung» (30 Bilder/s, Raster 20 px, Einrasten an) sind vom Bild übernommen und nicht entschieden (E-10, E-13).
33. Aus P06: `tst_overlaystore` (Fall `formatDocExample`) schlägt in einem frischen Checkout fehl. `media_vorbereiten` erzeugt `prototypes/media/` dort neu, und das neue `alpha.webm` hat eine andere Prüfsumme als die Datei, auf die das Beispiel in `docs/06-overlay-format.md` verweist. Besteht schon vor P06; im Arbeitsordner des Studio-PCs besteht der Test. Erledigt in P07 (2026-11-02): Der Test kopiert die Testdateien unter den Namen, die das Beispiel nennt, und verlangt die Prüfsumme nicht; im frischen Klon besteht er (`docs/berichte/P07-bericht.md`).
34. Aus P06: Die Bilder im Stylesheet (Pfeile der Zahlenfelder und der Auswahlliste, Kontrollkästchen, Knöpfe im Dock-Titel) sind die Dateien des dunklen OBS-Designs und werden nicht eingefärbt. Für ein helles Design (E-16) braucht es eigene Dateien.
35. Aus P07: Das Fenster startet die Einheit `dss-core.service` nur einmal je Start des Fensters (beim ersten gescheiterten Verbindungsversuch). Hält der Nutzer den Kern bei offenem Fenster an (`systemctl --user stop`), versucht das Fenster nur noch die Verbindung, startet den Dienst aber nicht wieder. Ob es das tun soll, ist offen.
36. Aus P07: systemd startet einen abgestürzten Kern höchstens 5 Mal in 10 s neu (Vorgabe `DefaultStartLimitBurst=5`, `DefaultStartLimitIntervalSec=10s`, https://www.freedesktop.org/software/systemd/man/latest/systemd-system.conf.html; am Studio-PC `systemctl --user show dss-core.service -p StartLimitIntervalUSec -p StartLimitBurst` → 10s, 5). Danach bleibt die Einheit «failed», bis das Fenster neu gestartet wird. Dieses Verhalten ist nicht geprüft.
37. Aus P07: Die Liste der Overlays sortiert nach Codepunkten, nicht nach deutscher Sortierung: «Überblick» steht nach «Zuschauer» (ergänzt Punkt 22).
38. Aus P07: CPU-Auslastung ohne die Felder `guest` und `guest_nice` aus `/proc/stat` (Annahme: der Kernel zählt sie schon in `user`/`nice`). Der Verlauf LAN im Kopfbereich zeigt die Summe aus Empfangen und Senden (Annahme); der Text zeigt beide getrennt.
39. Aus P07: LAN misst die Netzwerkkarte der Standardroute. Auf dem Studio-PC ist das zurzeit `enx2aea2d88f6c1` (USB-Tethering), nicht das Kabel `enp3s0`. Ob die Anzeige eine feste Karte zeigen soll, ist offen.
40. Aus P07: Reihenfolge der Quellen im Dock: vorderste Quelle oben wie in OBS (Annahme); in der Datei und in `dss-cli` ist Nr. 1 die hinterste.
41. Aus P07: Der Kern meldet sich bei systemd nur mit `READY=1` und `STOPPING=1`, ohne Watchdog. Ein hängender Kern (läuft, antwortet aber nicht) wird nicht erkannt und nicht neu gestartet.
42. Aus P07: Die Einheit startet `build/src/core/dss-core` aus dem Repo. Nach dem Verschieben des Repos oder einem Bau in einen anderen Ordner muss `scripts/dienst-einrichten.sh` neu laufen. Ein Installationsort ausserhalb des Repos ist nicht entschieden.
43. Aus P07: Normales Schliessen über den Schliessknopf des Fenstermanagers (Kern beendet sich) prüft nur der Handtest; automatisch geprüft ist der Weg über SIGTERM, der dasselbe `close()` auslöst (siehe Punkt 14).

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
- Umsetzung von Punkt 17 (Feld für Wiederholen) und 19 im Modell: mit P05, erledigt (2026-11-02).

## Entschieden vor P06 (2026-11-02)

- Symbole der Verbindungsknöpfe: P06 zeichnet jede Symboldatei einfarbig in Textfarbe. Der Nutzer ersetzt die Kürzel-Dateien später selbst durch eigene Dateien (zum Beispiel die einfarbigen Symbole aus seinem alten Repo `donstv_Studio`, `assets/icons/connections/`, Herkunft Simple Icons). Claude legt keine Markenlogos ab.
- Offen: Markenhinweis für NDI («NDI® is a registered trademark of Vizrt NDI AB», laut README des alten Repos) in «Hilfe → Über», sobald der Nutzer ein NDI-Symbol einsetzt oder NDI gebaut wird. Erledigt in P07 (2026-11-02): «Hilfe → Über» zeigt den Hinweis, wenn `~/.config/dons-streaming-studio/symbole/ndi.svg` vorhanden ist (Wortlaut: https://docs.ndi.video/docs/sdk/licensing). Wird NDI gebaut, gilt der Hinweis immer; das kommt mit dem NDI-Baustein.

## Entschieden nach P06 (2026-11-02)

- Eigene Symbole des Nutzers liegen ausserhalb des Repos in `~/.config/dons-streaming-studio/symbole/`. Das Fenster sucht Symbole zuerst dort und nimmt nur, wenn dort keine Datei mit dem Namen liegt, die Datei aus `src/app/symbole/`. Grund: Das Repo ist öffentlich, und Claude Code committet am Ende jedes Auftrags (Regel 17). Umsetzung als erster Teil von P07, erledigt (2026-11-02).
- Punkt 33 (Test `formatDocExample` schlägt in frischem Checkout fehl): wird als kleiner Teil von P07 behoben; der Test darf nicht von zufällig erzeugten Testdateien abhängen.
- Punkt 34 (helle Fassungen der Bilder im Design): erledigt, weil es kein helles Design gibt (E-16 gestrichen, Entscheid des Nutzers 2026-11-02).
