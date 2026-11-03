# 04 – Offene Punkte und nächste Schritte

Stand: 2026-11-03 (nach P09, Handtest offen).

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
11. Aus P02 (M1): 63 Abstände über 50 ms bei den Videobildern, höhere GPU-Auslastung (36,5 % statt 11,6 %) und etwas langsamerer Start als A; Ursachen nicht geprüft. Im Editor ab P09 (eigener Weg nach M1, Bilder mit Zeitpunkt in einer Warteschlange, Takt nach geplanten Zeitpunkten): 0 Abstände über 50 ms in 60 s, siehe `docs/berichte/P09-bericht.md`.
12. Aus P02 (M2): Qt WebEngine rendert minimiert nicht (Abgriff schwarz), verdeckt und ausserhalb des Bildschirms schon. Mit zwei Overlays im selben Prozess nur 26 bis 29 neue Bilder pro Sekunde; Abgriff in getrennten Threads oder Prozessen nicht gemessen.
13. Aus P02: Für NDI offen sind Farbmatrix und Bereich bei UYVY, Vormultiplikation bei UYVA und ob die Umwandlung besser das NDI-SDK übernimmt (laut NDI-Dokumentation empfohlen). Alpha im Abgriff bei transparenter Seite nicht geprüft.
14. Aus P03: Die Startprüfung beendet `dss-app` per SIGTERM (das Programm schliesst dann sein Fenster). Das Schliessen über den Schliessknopf des Fenstermanagers prüft nur der Handtest (bestanden am 2026-11-02); für eine automatische Prüfung fehlt ein Werkzeug wie `xdotool` (nicht installiert).
15. Aus P03: Der Installationsweg von `scripts/einrichten.sh` (fehlendes Paket, `sudo apt install Name=Version`) ist nicht geprüft, weil auf dem Studio-PC alle Pakete schon installiert waren.
16. Aus P03: Satz «Es gibt noch keinen Programmcode» in `docs/00-uebergabe.md`: erledigt (2026-11-02).
17. Aus P04: Eigenschaften der Quelle Video (Wiederholen, Ton, Start/Stopp) sind nicht entschieden.
18. Aus P04: Drehrichtung (im Uhrzeigersinn oder dagegen) ist nicht festgelegt; beide Zeichner brauchen dieselbe.
19. Aus P04: Adresse aus dem Namen: `ß` und Buchstaben ohne Zerlegung in Unicode (zum Beispiel `ł`, `æ`, `ø`) werden nach der Regel zu `-` («Straße» → `stra-e`). Ob `ß` → `ss` gewünscht ist, ist offen.
20. Aus P04: Grösse einer neuen Quelle: vorläufig Lage 0/0 in der Grösse des Overlays. Die natürliche Grösse von Bild und Video zu lesen braucht einen Dekoder; zu entscheiden, wenn der Zeichner kommt. In P08 unverändert: Auch Quellen, die im Fenster hinzugefügt werden, beginnen bei 0/0 in der Grösse des Overlays. Erledigt in P09 (2026-11-03): Neue Bild- und Videoquellen bekommen die Grösse der Datei (Bild: Pixel, Video: erstes Bild), Lage 0/0; ist sie nicht lesbar, die Grösse des Overlays mit Hinweis (`change_result.warning` im Fenster, «Warnung:» bei `dss-cli`).
21. Aus P04: Erlaubte Dateiendungen (Bild `png`, `jpg`, `jpeg`; Video `webm`, `mp4`; Schrift `ttf`, `otf`) sind eine Annahme; geprüft wird nur die Endung, nicht der Inhalt.
22. Aus P04: Reihenfolge der Overlays in der Liste: vorläufig nach Name sortiert, nicht gespeichert. Erledigt in P08 (2026-11-03): Reihenfolge und Ordner in `liste.json` (`docs/06-overlay-format.md`, Abschnitt «Liste»); nach Name nur noch als Rückfall bei kaputter Datei.
23. Aus P04: Be Vietnam Pro (Standardschrift, E-11) ist auf dem Studio-PC nicht installiert (`fc-list`); Tests nutzen DejaVu Sans. Herkunft und Lizenz der Schriftdatei klären, wenn der Zeichner kommt. Erledigt in P08 (2026-11-03): Be Vietnam Pro 1.002 (18 Schnitte, SIL Open Font License 1.1) aus dem Repo google/fonts unter `src/common/schriften/be-vietnam-pro/`, Standardschrift neuer Textquellen; siehe `docs/berichte/P08-bericht.md`.
24. Aus P04: Ungenutzte Kopien unter `media/` werden nicht gelöscht (Auftrag P04).
25. Aus P04: Dass die temporäre Datei von `tokens.json` schon vor dem Schreiben des Tokens die Rechte 0600 hat, ist nicht geprüft (`strace` nicht installiert); geprüft sind die Rechte der fertigen Datei.

26. Aus P05: Der X-Bildschirmschoner startet nach 600 s ohne Eingabe (`xset q`), danach sperrt `light-locker` die Sitzung (Wechsel zum Anmeldebildschirm auf VT 8). Gesperrt bleiben neue Fenster ungezeichnet (`IsUnMapped`); die Startprüfung meldet dann «nicht sauber». Erledigt (2026-11-02): Bildschirmschoner und light-locker auf Wunsch des Nutzers ganz abgeschaltet, siehe `docs/02-umgebung.md`.
27. Aus P05: Farbe der Verbindungsknöpfe je Zustand ist eine Annahme (rot = nicht verbunden, grün = verbunden, nach dem Bild). Erledigt (2026-11-02): vier Zustände grün, orange, rot, grau, siehe `docs/01-festlegungen.md`, Abschnitt «Verbindungsanzeige im Kopfbereich». Umgesetzt in P06 (2026-11-02): Eigenschaft `state` mit vier Werten, Farben aus Yami (`--green3`, `--yellow2`, `--red3`, `--grey1`), alle Knöpfe grau; siehe `docs/berichte/P06-bericht.md`.
28. Aus P05: Die Symbole unter `src/app/symbole/` enthalten ihre Farbe selbst (SVG); sie folgen einem späteren hellen Design (E-16) nicht von allein. Erledigt in P06 (2026-11-02): Symbole aus `loadSymbol` werden einfarbig in Textfarbe gezeichnet (Datei als Maske). Ausnahme siehe Punkt 34.
29. Aus P05: Schrift des Fensters ist die Standardschrift von Qt auf dem Studio-PC (DejaVu Sans); das Bild des Nutzers zeigt eine andere Schrift (nicht bestimmt). Welche Schrift das Fenster nutzen soll, ist offen. Entschieden (2026-11-02): Aussehen wie OBS Studio (Design «Yami»), Schrift Open Sans, siehe `docs/01-festlegungen.md`, Abschnitt «Aussehen des Fensters». Erledigt in P06 (2026-11-02): Open Sans 3.003 als Datei unter `src/app/schriften/`, 10pt wie OBS.
30. Aus P05: Die Zahlenfelder (Eigenschaften, Steuerung) zeigen wegen des Stylesheets keine Pfeile wie auf dem Bild. Erledigt in P06 (2026-11-02): Pfeile aus OBS, gut sichtbar; Feldbreite so, dass der Wert nicht abgeschnitten wird.
31. Aus P05: Ändern von «Wiederholen» bei einer bestehenden Video-Quelle gibt es im Modell noch nicht (nur beim Hinzufügen); kommt mit den Eigenschaften im Fenster. Erledigt in P08 (2026-11-03): Feld `loop` in `update_source`, Kontrollkästchen «Wiederholen» im Dock «Eigenschaften».
32. Aus P05: Werte der ausgegrauten Bedienelemente in «Steuerung» (30 Bilder/s, Raster 20 px, Einrasten an) sind vom Bild übernommen und nicht entschieden (E-10, E-13).
33. Aus P06: `tst_overlaystore` (Fall `formatDocExample`) schlägt in einem frischen Checkout fehl. `media_vorbereiten` erzeugt `prototypes/media/` dort neu, und das neue `alpha.webm` hat eine andere Prüfsumme als die Datei, auf die das Beispiel in `docs/06-overlay-format.md` verweist. Besteht schon vor P06; im Arbeitsordner des Studio-PCs besteht der Test. Erledigt in P07 (2026-11-02): Der Test kopiert die Testdateien unter den Namen, die das Beispiel nennt, und verlangt die Prüfsumme nicht; im frischen Klon besteht er (`docs/berichte/P07-bericht.md`).
34. Aus P06: Die Bilder im Stylesheet (Pfeile der Zahlenfelder und der Auswahlliste, Kontrollkästchen, Knöpfe im Dock-Titel) sind die Dateien des dunklen OBS-Designs und werden nicht eingefärbt. Für ein helles Design (E-16) braucht es eigene Dateien.
35. Aus P07: Das Fenster startet die Einheit `dss-core.service` nur einmal je Start des Fensters (beim ersten gescheiterten Verbindungsversuch). Hält der Nutzer den Kern bei offenem Fenster an (`systemctl --user stop`), versucht das Fenster nur noch die Verbindung, startet den Dienst aber nicht wieder. Ob es das tun soll, ist offen.
36. Aus P07: systemd startet einen abgestürzten Kern höchstens 5 Mal in 10 s neu (Vorgabe `DefaultStartLimitBurst=5`, `DefaultStartLimitIntervalSec=10s`, https://www.freedesktop.org/software/systemd/man/latest/systemd-system.conf.html; am Studio-PC `systemctl --user show dss-core.service -p StartLimitIntervalUSec -p StartLimitBurst` → 10s, 5). Danach bleibt die Einheit «failed», bis das Fenster neu gestartet wird. Dieses Verhalten ist nicht geprüft.
37. Aus P07: Die Liste der Overlays sortiert nach Codepunkten, nicht nach deutscher Sortierung: «Überblick» steht nach «Zuschauer» (ergänzt Punkt 22). Erledigt in P08 (2026-11-03): Die Reihenfolge bestimmt der Nutzer per Maus (`liste.json`); neue Overlays kommen ans Ende der obersten Ebene. Nach Codepunkten sortiert nur noch der Rückfall bei kaputter Datei.
38. Aus P07: CPU-Auslastung ohne die Felder `guest` und `guest_nice` aus `/proc/stat` (Annahme: der Kernel zählt sie schon in `user`/`nice`). Der Verlauf LAN im Kopfbereich zeigt die Summe aus Empfangen und Senden (Annahme); der Text zeigt beide getrennt.
39. Aus P07: LAN misst die Netzwerkkarte der Standardroute. Auf dem Studio-PC ist das zurzeit `enx2aea2d88f6c1` (USB-Tethering), nicht das Kabel `enp3s0`. Ob die Anzeige eine feste Karte zeigen soll, ist offen.
40. Aus P07: Reihenfolge der Quellen im Dock: vorderste Quelle oben wie in OBS (Annahme); in der Datei und in `dss-cli` ist Nr. 1 die hinterste. In P08 auch beim Bearbeiten so umgesetzt (Ziehen, «nach vorne», «nach hinten»).
41. Aus P07: Der Kern meldet sich bei systemd nur mit `READY=1` und `STOPPING=1`, ohne Watchdog. Ein hängender Kern (läuft, antwortet aber nicht) wird nicht erkannt und nicht neu gestartet. Erledigt in P10a (2026-11-03): Watchdog nach sd_notify, `WatchdogSec=10s`, `TimeoutAbortSec=5s`; ein mit `kill -STOP` angehaltener Kern läuft nach 9,1 bis 11,0 s wieder (3 Läufe, Bericht P10a). Beim Übernehmen grosser Dateien meldet sich der Kern zusätzlich jede Sekunde.
42. Aus P07: Die Einheit startet `build/src/core/dss-core` aus dem Repo. Nach dem Verschieben des Repos oder einem Bau in einen anderen Ordner muss `scripts/dienst-einrichten.sh` neu laufen. Ein Installationsort ausserhalb des Repos ist nicht entschieden.
43. Aus P07: Normales Schliessen über den Schliessknopf des Fenstermanagers (Kern beendet sich) prüft nur der Handtest; automatisch geprüft ist der Weg über SIGTERM, der dasselbe `close()` auslöst (siehe Punkt 14).
44. Aus P08: Die deutsche Übersetzung von Qt wird nicht geladen, weil `qtbase_de.qm` (Paket qt6-translations-l10n 6.8.2-2) «ß» enthält (zum Beispiel «Schließen», «Größe»). Texte von Qt selbst bleiben deshalb englisch, etwa im Kontextmenü der Textfelder («Undo», «Copy», «Paste»). Datei- und Farbwahl sind Dialoge von Qt bzw. des Systems; ob dort Texte mit «ß» erscheinen, ist nicht geprüft. [OFFEN]: Englisch lassen, eigene Übersetzung ohne «ß» oder Übersetzung von Qt trotz «ß». Erledigt in P09 (2026-11-03): Der Bau kopiert `qtbase_de.qm`, ersetzt «ß» durch «ss» und das Fenster lädt die Kopie (`tst_qmfile`).
45. Aus P08: Variable Schriften: Die Grundform einer variablen Datei (`variable = True`, Gewicht als Bereich) kommt nicht in die Auswahl, benannte Schnitte derselben Datei schon (gleiche Datei, eigenes Gewicht). Auf dem Studio-PC gibt es keine variable Schrift (alle 191 Zeilen von `fc-list` mit `variable = False`, 2026-11-03), der Weg ist also nur mit erfundenen Testzeilen geprüft. Ein Zeichner müsste bei einer solchen Datei die Achse `wght` selbst setzen; das Overlay-Format speichert nur Datei und Gewicht. [OFFEN] bis P09. Erledigt in P09 (2026-11-03): Der Zeichner setzt bei einer Datei mit Achse `wght` (Tabelle `fvar`) die gewählte Stärke; geprüft mit Urbanist[wght] im Repo unter `tests/daten/schriften/` (`tst_leinwand`).
46. Aus P08: Dateiwahl (Bild, Video, Schriftdatei, Bild ersetzen) und Farbwahl prüft kein automatischer Test, nur der Handtest. Die Tests schicken dieselben Änderungen direkt.
47. Aus P08: Die Auflösung eines bestehenden Overlays lässt sich im Fenster nicht ändern, nur beim Anlegen wählen (E-01). Eine Änderungsart dafür gibt es noch nicht. Erledigt in P09 (2026-11-03): `set_overlay_size`, Felder Breite und Höhe im Dock «Steuerung».
48. Aus P08: Eintrag in einem Ordner gewählt: Die Fläche links neben dem Eintrag (Einzug) erscheint ebenfalls blau. Rein kosmetisch; ein Versuch über `QTreeView::branch` blendete die Pfeile der Ordner aus und wurde verworfen. Erledigt in P09 (2026-11-03): eigenes Zeichnen der Fläche links (`OverlayTree::drawBranches`), Pfeile bleiben.
49. Aus P08: Das Dock «Eigenschaften» ist in der Standardanordnung 215 px hoch. Bei einer Textquelle braucht es die Bildlaufleiste, um alle Felder zu sehen.
50. Aus P08 (Annahmen): Eine neu hinzugefügte Quelle wird gewählt (wie ein neues Overlay laut Auftrag). Zwei Fenster, die dasselbe Feld gleichzeitig ändern: Es gilt die Änderung, die zuletzt beim Kern ankommt; keine Warnung. Nach `kill -9` des Kerns gehen höchstens die Änderungen der letzten Sekunde verloren (verzögertes Speichern, `docs/08-kern-schnittstelle.md`, «Speichern»).
51. Aus P08: Die Knöpfe zum Ausrichten unten im Dock «Eigenschaften» bleiben ausgegraut (E-10, SOLL, laut Auftrag).
52. Aus P09 (Annahme) [OFFEN]: Text, der über die Box hinausgeht, wird am Rand der Box abgeschnitten. Die Box wächst nicht mit, die Schrift wird nicht verkleinert.
53. Aus P09: Ein Video beginnt von vorne, wenn es eingeblendet wird oder wenn «Wiederholen» bzw. «Am Ende anhalten» ändert (neuer Decoder). Ein ausgeblendetes Video wird nicht dekodiert. Annahme, nicht entschieden.
54. Aus P09: Modell, Datei und `dss-cli` erlauben `loop` und `hold_at_end` beide `true`; «Am Ende anhalten» hat dann keine Wirkung (im Fenster ausgegraut).
55. Aus P09: Bedienen mit der Maus weicht in Punkten von OBS ab, die nicht entschieden sind: ohne Umschalttaste kein Einrasten beim Drehen (OBS rastet nahe 0°, 45° usw. und nahe dem Startwinkel ein), Seitenverhältnis aus der Box beim Beginn des Ziehens (OBS: aus der Grösse der Datei), kein Spiegeln durch Ziehen über die Gegenseite. Liste und Quellen in `docs/berichte/P09-bericht.md`. Erledigt in P09b (2026-11-03): Einrasten mit Umschalttaste in 1°-Schritten, ohne Umschalttaste keines (entschieden); Seitenverhältnis an der Ecke aus der Originalgrösse der Datei (Text: Box beim Beginn); Ziehen über die Gegenseite spiegelt (Felder `flip_horizontal`, `flip_vertical`, Format 3, Schnittstelle 4). Siehe `docs/berichte/P09b-bericht.md`.
56. Aus P09: Ohne `CMAKE_BUILD_TYPE` baut CMake im Debug-Modus (Vorgabe in `CMakeLists.txt`). Mit dem Mess-Overlay (10 Quellen, 2 Videos) braucht `dss-app` im Debug-Bau 60 % eines Kerns, im Release-Bau 50 % (Bericht P09). Ob der Einsatz (fester Installationsort, P10) im Release-Modus gebaut wird, ist offen. Erledigt in P10a (2026-11-03): `scripts/installieren.sh` baut im Release-Modus nach `~/.local/opt/dons-streaming-studio/`.
57. Aus P09: Ist eine Schrift derselben Familie und desselben Schnitts mehrfach installiert, kann Qt eine andere Datei nehmen als die der Quelle; das Fenster meldet es dann auf der Konsole («Qt wählt für … eine andere Datei derselben Familie»). Auf dem Studio-PC nicht aufgetreten.
58. Aus P09: EXIF-Ausrichtung bei JPEG: nicht geprüft (kein Testbild mit Ausrichtung); Kern (Grösse) und Zeichner lesen das Bild ohne eigene Drehung. Erledigt in P09b (2026-11-03): Kern (Grösse) und Zeichner des Editors beachten die Ausrichtung 1 bis 8 bei JPEG (APP1) und PNG (`eXIf` vor `IDAT`); geprüft mit eigenen Testbildern `tests/daten/exif/` (`tst_imageorientation`, `tst_medien`, `tst_leinwand`).
59. Aus P09: Zeichnet die Leinwand nicht (Fenster verdeckt, Prozess stark beschäftigt), laufen die Videos in Echtzeit weiter; Bilder, die mehr als 100 ms überfällig sind, verwirft der Decoder (Zähler «verworfen»).
60. Aus P09b: Die Befehle «Transformieren» haben keine Tastenkürzel. OBS hat Ctrl+Shift+C/V (kopieren/einfügen), Ctrl+R (zurücksetzen), Ctrl+F (einpassen), Ctrl+S (strecken), Ctrl+D (zentrieren) (`frontend/forms/OBSBasic.ui`, Zeilen 1726-1785). Nicht im Auftrag; zu entscheiden, auch weil Ctrl+S sonst «Speichern» bedeutet. Erledigt in P10a (2026-11-03): Kürzel wie OBS im Menü, wirksam auf der Leinwand und im Dock «Quellen», nicht in Textfeldern.
61. Aus P09b: «Auf Leinwand einpassen» und «Auf Leinwand strecken» setzen feste Pixel. OBS setzt eine Begrenzung (bounds), die auch nach einem Wechsel der Auflösung oder der Datei gilt. «Transformation einfügen» übernimmt die Grösse in Pixeln (wie im Auftrag), OBS den Massstab. Einfügen in eine Quelle mit anderer Originalgrösse gibt deshalb dieselbe Pixelgrösse, nicht dasselbe Verhältnis zur Datei.
62. Aus P09b: Die Befehle gelten der einen gewählten Quelle; OBS wendet sie auf alle gewählten an. Mehrfachauswahl gibt es im Editor nicht.
63. Aus P09b: «Transformation bearbeiten …» (Dialog in OBS) gibt es nicht; die Werte stehen im Dock «Eigenschaften», Bereich «Transformation». Nicht im Auftrag.
64. Aus P09b: Drehen um 90° hält die linke obere Ecke des umschliessenden Kastens (wie OBS). Ist der Unterschied von Breite und Höhe ungerade, wird die Lage auf ganze Pixel gerundet; viermal 90° kann dann um 1 Pixel verschoben enden (OBS rechnet mit Bruchteilen). Annahme, nicht entschieden.
65. Aus P09b: Drehung aus den Metadaten eines Videos (zum Beispiel Display-Matrix in MP4) wird nicht beachtet; der Auftrag nennt nur die EXIF-Ausrichtung von Bildern. Nicht geprüft, ob es solche Videos im Einsatz gibt. Erledigt in P10a (2026-11-03) für Kern und Editor (Display-Matrix nach der Logik von ffmpeg, Testvideos `tests/daten/video-drehung`); der zweite Zeichner folgt mit P10, die Prüfung mit OBS ebenso.
66. Aus P09b (gefunden beim Einzelbau der Commits, Fehler aus P09): `tst_medien` schlug im frischen Klon in 2 von 13 ctest-Läufen fehl (2026-11-03, Stände `58297c7` und `4a37c2c`; beim zweiten mit Ausgabe: `oneFramePerTick`, «150 Takte: 50 ohne neues Bild, 50 mit mehr als einem»; beim ersten ohne Ausgabe, welcher Fall, ist nicht bekannt). Auf dem Endstand `--repeat until-fail:10` 10 von 10 bestanden. `alpha.webm` hat Zeitstempel in ganzen Millisekunden (Zeitbasis 1/1000: 0, 33, 67, 100 ms; `ffprobe -show_entries packet=pts_time`). Vermutung, nicht geprüft: Liegt der Takt der Leinwand (genau n/30 s) bis etwa 1/3 ms vor diesen Zeitpunkten, kommt jedes dritte Bild einen Takt zu spät und das nächste doppelt. Das würde auch auf der Leinwand ein Ruckeln alle 100 ms geben. Player und Test sind in P09b nicht geändert; zu entscheiden, ob `frameAt` eine Toleranz (zum Beispiel 1 ms) bekommt. Erledigt in P10a (2026-11-03): Gemessen wie die Leinwand, 334 Läufe zu 45 Takten mit Versatz 0 bis 33,3 ms: vorher 3 Läufe ungleichmässig (90 von 15 030 Takten), nur mit Toleranz 1 ms 6 (180), mit Toleranz und Anker 0. Der Anker: Der Beginn eines Videos ist der Zeitpunkt der ersten Abfrage mit Bild, nicht der Zeitpunkt des Dekodierens; die Bilder liegen dann auf dem Raster des Takts. Bis zur ersten Abfrage wartet der Decoder mit vollem Vorrat (Punkt 59 gilt ab dann). `tst_medien` und `tst_fensterbearbeiten` je 50 von 50 (Bericht P10a).
67. Aus P10a: Das Übernehmen einer Datei (Kopie und zweimal SHA-256) läuft im Kern in der Hauptschleife: 1 GiB 16 s, 4 GiB 64 s auf dem Studio-PC (2026-11-03, `dss-cli quelle-bild`, Datei im Seitencache). So lange beantwortet der Kern keine Nachrichten, und die Leistungswerte stehen. Der Watchdog ist dagegen geschützt (Punkt 41). Nicht im Auftrag; zu entscheiden, ob das Übernehmen in einen eigenen Thread kommt.
68. Aus P10a: Wird der Kern beim Übernehmen einer Datei beendet (Absturz, SIGKILL), kann eine temporäre Datei von `QSaveFile` in `media/` liegen bleiben. Nicht geprüft, Aufräumen gibt es nicht.
69. Aus P10a: `cmake --install` überschreibt und ergänzt, entfernt aber keine Dateien, die eine neuere Version nicht mehr mitbringt (zum Beispiel ein umbenanntes Symbol unter `share/dons-streaming-studio/symbole/`). Bisher ohne Folgen; zu entscheiden, ob `installieren.sh` den Ordner `share/` vorher leert.
70. Aus P10a: Das Zurückschreiben, wenn beim Umwandeln eine spätere Datei nicht geschrieben werden kann (`convertDataFormat`), ist nicht geprüft; ein Fehler genau dann liess sich ohne Eingriff in den Code nicht erzeugen. Geprüft sind: Sperre, neueres Format, unlesbare Datei, fehlender Ordner (Dateien jeweils unverändert).
71. Aus P10a: Bis zur ersten Installation durch den Nutzer zeigt die bisherige Einheit `dss-core.service` noch auf `build/src/core/dss-core`. Dieser Bau ist jetzt die Version in Arbeit und nutzt den Entwicklungsordner. Das Fenster aus `build/` startet `dss-core-entwicklung.service`, die erst `scripts/dienst-einrichten.sh` anlegt. `installieren.sh` schreibt `dss-core.service` neu (Programm aus `~/.local/opt/…`). Erledigt (2026-11-03): erste Installation durch den Nutzer im Handtest P10a, Commit `6d83c93`.
72. Aus P10a: `tst_fensterbearbeiten::headerGaugesWidth` scheiterte einmal in 50 Wiederholungen (Fenster 1280 breit, Leistungsanzeige noch für 1920). Korrigiert nur im Test (`acdace6`, wartet, bis beide zur Sollbreite passen); Ursache nicht geprüft (Vermutung: verspätete Bestätigung des Fenstermanagers). Entscheid 2026-11-03: In P10 läuft der Test wieder 50 Mal; scheitert er erneut, sucht Claude Code die Ursache, bevor P10 abgeschlossen wird.

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

## Entschieden nach P07 (2026-11-02)

- Punkt 35: so lassen. Wird der Kern bei offenem Fenster absichtlich angehalten, startet das Fenster ihn nicht wieder. Neu starten durch Schliessen und Öffnen des Fensters.
- Punkt 43: erledigt durch den Handtest P07 (Schliessen mit dem Schliessknopf, Kern danach `inactive`).
- Punkt 39: so lassen. Das Handy per USB ist eine Übergangslösung, bis das Heimnetz per Kabel besteht; die Anzeige wechselt von selbst aufs Kabel, sobald es die Standardroute ist.
- Punkt 40: entschieden, vorderste Quelle oben wie in OBS. Datei und `dss-cli` bleiben bei «Nr. 1 = hinterste».
- Punkt 37 (und 22): entschieden, Overlays sind per Maus frei verschiebbar, Reihenfolge wird gespeichert; dazu Ordner mit einer Ebene (keine Unterordner). Umsetzung in P08, erledigt (2026-11-03, Handtest offen). Siehe `docs/01-festlegungen.md`, Abschnitt «Overlay-Liste».
- Punkt 36 (Neustartgrenze von systemd): so lassen; das Fenster zeigt dann «nicht verbunden».
- Punkt 38 (Annahmen zu CPU und LAN-Verlauf): so lassen; Werte vom Nutzer mit der Leistungsanzeige des Systems abgeglichen.
- Punkt 41 (hängender Kern wird nicht erkannt): mit P10 lösen.
- Punkt 42 (Kern startet aus dem Repo-Ordner): mit P10 lösen, durch einen festen Installationsort ausserhalb des Repos und ein Installationsskript. Grund: Sobald die Browserquelle im Stream genutzt wird, soll eine laufende Claude-Code-Sitzung die Programme im Einsatz nicht austauschen.

## Entschieden im Handtest P08 (2026-11-03)

- Neu E-23: Kopfbereich fest über die ganze Fensterbreite; links oder rechts angedockte Docks beginnen darunter. Umsetzung als erster Teil von P09.
- Punkt 48: Die blaue Fläche links neben einem gewählten Eintrag im Ordner wird entfernt (P09). An dieser Stelle kommt mit P10 der Schalter «Ausgabe ein/aus» pro Overlay (E-24).

## Entschieden nach P08 (2026-11-03)

- Punkt 44: eigene Übersetzung ohne «ß». Die Übersetzung von Qt (`qtbase_de.qm`) wird beim Bau kopiert, «ß» durch «ss» ersetzt und geladen. Umsetzung mit P09.
- Punkt 45: so lassen (nur benannte Schnitte). P09 sorgt dafür, dass der Zeichner bei einer variablen Schriftdatei die gewählte Stärke (Achse `wght`) setzt.
- Punkt 47: Auflösung eines bestehenden Overlays nachträglich änderbar, im Dock «Steuerung» (Breite und Höhe in Pixeln, Wunsch des Nutzers: nicht über Rechtsklick). Quellen behalten ihre Pixelwerte; was danach ausserhalb liegt, ist teilweise ausserhalb der Leinwand. Umsetzung mit P09.
- Punkt 49: so lassen, die Bildlaufleiste im Dock «Eigenschaften» ist in Ordnung.
- Punkt 50: alle drei Annahmen vom Nutzer bestätigt (neue Quelle wird gewählt; letzte Änderung beim Kern gilt; nach hartem Absturz des Kerns höchstens 1 s Änderungen verloren).
- Punkt 46 (Datei- und Farbwahl nur im Handtest geprüft): im Handtest P08 geprüft, in Ordnung; keine weitere Massnahme.
- Punkt 51 (Ausrichten ausgegraut): gehört zu E-10 (SOLL), keine Massnahme bis dahin.

## Wünsche aus dem Handtest P09 (2026-11-03)

- Einrasten beim Drehen mit Umschalttaste in 1°-Schritten statt 15° (entschieden, siehe `docs/01-festlegungen.md`). Umsetzung mit dem nächsten Baustein. Erledigt in P09b (2026-11-03).
- Leistungsanzeige (CPU, RAM, LAN) im Kopfbereich: entschieden, feste Breite 600 Pixel für alle drei zusammen (200 je Anzeige), rechts im Kopfbereich. Wäre das mehr als ein Drittel der Fensterbreite, wird sie auf ein Drittel verkleinert. Der Platz zwischen Verbindungsknöpfen und Anzeige bleibt leer. Umsetzung mit dem nächsten Baustein. Erledigt in P09b (2026-11-03).
- Symbole fehlten im Handtest-Fenster, weil `handtest.sh` ein eigenes `XDG_CONFIG_HOME` nutzt; kein Fehler der Software. Im normalen Fenster geprüft, in Ordnung (2026-11-03).

## Entschieden nach P09 (2026-11-03)

- Punkt 52: Text über die Box hinaus wird am Rand der Box abgeschnitten (bestätigt). Gilt auch für die Browserquelle (P10).
- Punkt 53: Ein Video beginnt beim Einblenden von vorne; ausgeblendet wird es nicht abgespielt (bestätigt). Gilt auch für die Browserquelle (P10).
- Punkt 55: a) Drehen ohne Umschalttaste rastet nicht ein (so lassen). b) Seitenverhältnis an der Ecke nach der Originalgrösse der Datei, wie OBS (bei Text: nach der Box). c) Ziehen über die Gegenseite hinaus spiegelt die Quelle, wie OBS. Folge von c: neue Felder «waagrecht gespiegelt» und «senkrecht gespiegelt» in Modell, Datei und Schnittstelle; beide Zeichner müssen spiegeln. Umsetzung mit dem nächsten Baustein. Erledigt in P09b (2026-11-03) für Modell, Datei, Schnittstelle, `dss-cli` und den Zeichner des Editors; der zweite Zeichner folgt mit P10 (`docs/01-festlegungen.md`, «Zeichnen im Editor»).
- Spiegeln zusätzlich per Befehl (Rechtsklick), Teil von E-25. Neue Funktionen E-25 (Transformieren wie OBS) und E-26 (Dock «Eigenschaften» in Bereiche geteilt), beide MUSS, mit dem nächsten Baustein. Zuschneiden: später als eigener Punkt besprechen. Erledigt in P09b (2026-11-03), ohne Zuschneiden; Abweichungen von OBS in den Punkten 60 bis 64.
- Punkt 54: so lassen (beide Felder erlaubt, «Am Ende anhalten» im Fenster ausgegraut).
- Punkt 56: Der feste Installationsort (P10) wird im Release-Modus gebaut.
- Punkt 57: so lassen (Meldung auf der Konsole).
- Punkt 59: so lassen (Videos laufen in Echtzeit weiter).
- Punkt 58: Editor und Kern beachten die EXIF-Ausrichtung von Bildern, wie der Browser (Standard `image-orientation: from-image`, bei Seiten ohne HTTPS nicht abschaltbar, https://developer.mozilla.org/en-US/docs/Web/CSS/image-orientation). Grund: K-07, beide Zeichner zeigen dasselbe. Umsetzung mit dem nächsten Baustein. Erledigt in P09b (2026-11-03) für Kern und Editor; der zweite Zeichner folgt mit P10.

## Wünsche aus dem Handtest P09b (2026-11-03)

- Verbindungsknöpfe waagrecht in der Mitte des Kopfbereichs, mittig zwischen den Reitern «Overlays / Chat-Bot» und der Leistungsanzeige.
- Logos in den Verbindungsknöpfen etwa zwei Drittel der heutigen Grösse, ohne eigenen Rand ums Logo; der Rahmen des Knopfes (Farbe des Zustands) bleibt die einzige Umrandung.
- Dock «Eigenschaften», Bereich «Transformation»: drei kleine Symbolknöpfe in der Titelzeile, am rechten Rand des Docks (nicht direkt neben dem Titel): Kopieren, Einfügen, Zurücksetzen. Hinweis beim Darüberfahren; «Einfügen» ausgegraut, solange nichts kopiert ist; Wirkung wie die Befehle aus E-25.
- Umsetzung: mit dem nächsten Baustein (P10), Teil «Vorab».

## Entschieden nach P09b (2026-11-03)

- Punkt 66: im Vorab-Teil von P10 klären und beheben. Zuerst messen, ob die Videobilder ungleichmässig kommen (Ruckeln alle 100 ms bei Zeitstempeln in ganzen Millisekunden); dann Zeitpunkt mit kleiner Toleranz rechnen (Vorschlag 1 ms), `tst_medien` 50 Mal ohne Fehler.
- Punkt 60: Tastenkürzel genau wie OBS: Strg+Umschalt+C / Strg+Umschalt+V (Transformation kopieren / einfügen), Strg+R (zurücksetzen), Strg+F (einpassen), Strg+S (strecken), Strg+D (zentrieren). Entscheid des Nutzers, auch für Strg+S. Umsetzung im Vorab-Teil von P10.
- Punkt 61: so lassen. «Einpassen» und «Strecken» setzen feste Pixel (keine Begrenzung wie in OBS); «Einfügen» überträgt die Pixelgrösse, nicht den Massstab.
- Punkt 62: neue Funktion E-27 «Mehrfachauswahl wie OBS», SOLL, später.
- Punkt 63: so lassen, kein eigener Dialog «Transformation bearbeiten»; die Werte stehen im Dock «Eigenschaften».
- Punkt 64: so lassen, Rundung auf ganze Pixel nach Regel 11.
- Punkt 65: Der Editor beachtet die Drehung aus den Metadaten von Videos (zum Beispiel Display-Matrix in MP4), wie bei der EXIF-Ausrichtung von Bildern (K-07). Umsetzung im Vorab-Teil von P10; P10 prüft mit einem Testvideo, ob die Browserquelle in OBS sie ebenfalls beachtet.

## Entschieden nach P10a (2026-11-03)

- Punkt 67 (Übernehmen grosser Dateien in der Hauptschleife): Das Übernehmen (Kopie, SHA-256) kommt in einen eigenen Thread des Kerns, damit der Kern während des Übernehmens weiter Nachrichten beantwortet und die Browserquelle versorgt. Umsetzung in P10, zusammen mit dem Webserver.
- Punkt 68 (Reste von `QSaveFile` nach Absturz): Der Kern löscht beim Start die vorläufigen Dateien von `QSaveFile` in `media/` (beim Start läuft keine Übernahme, die Sperre hält nur ein Kern). Umsetzung in P10, zusammen mit Punkt 67.
- Punkt 69 (alte Dateien nach `cmake --install`): `installieren.sh` leert `<Ziel>/bin/` und `<Ziel>/share/dons-streaming-studio/` erst nach erfolgreichem Bau und Prüfen des Formats, direkt vor dem Kopieren. Die Daten sind nicht betroffen. Umsetzung in P10.
- Punkt 70 (Zurückschreiben beim Umwandeln ungeprüft): `convertDataFormat` bekommt eine Testmöglichkeit, die nur im Test greift (Scheitern beim Schreiben der n-ten Datei); ein Test prüft, dass danach alle Dateien wie vorher sind. Umsetzung in P10.
- Anker gegen das Ruckeln (Annahme im Bericht P10a, Punkt 66): übernommen. Ein Video beginnt im Editor, wenn die Leinwand das erste Bild abholt, also beim Anzeigen von vorne (wie «Video beim Einblenden von vorne» in der Browserquelle, K-07). Nicht geprüft: Verhalten beim Wechsel auf ein anderes Overlay und zurück (läuft weiter oder beginnt neu). P10 prüft das und legt es für beide Zeichner gleich fest.
- Punkt 72 (Korrektur nur im Test): siehe Punkt 72; Prüfung in P10.

## Wünsche nach P10a (2026-11-03)

- Neues Dock für die Mediendateien (`media/` im Datenordner): zeigt die Dateien mit Dateityp und Grösse; die Dateien lassen sich in Ordner einteilen. Baustein: eigener Baustein P10b direkt nach P10 (Entscheid des Nutzers 2026-11-03). Funktion E-28, Stufe MUSS (Entscheid 2026-11-03). Einzelheiten:
  - Namen (Entscheid 2026-11-03): Der Kern hält beim Übernehmen den ursprünglichen Dateinamen fest (neue Datei im Datenordner, zum Beispiel `medien.json`). Jeder Name lässt sich im Dock umbenennen; das ändert nur den Namen, nicht die Datei und nicht die Verweise der Overlays. Bestehende Dateien ohne Namen erhalten einen Ersatznamen aus Typ und Datum (zum Beispiel «Bild vom 2026-11-02»). Wird dieselbe Datei nochmals hinzugefügt, bleibt der bisherige Name.
  - Ordner (Entscheid 2026-11-03): beliebig tief verschachtelt (Ordner im Ordner, anders als die Overlay-Liste mit einer Ebene). Ordner sind nur eine Einteilung in der Liste; die Dateien bleiben in `media/`, die Verweise der Overlays ändern sich nicht. Zu beachten: Die Dateien in `media/` heissen nach dem SHA-256 ihres Inhalts (`docs/06-overlay-format.md`), der ursprüngliche Dateiname wird bisher nicht gespeichert.
  - Löschen (Entscheid 2026-11-03): Im Dock lassen sich nur Dateien löschen, die keine Quelle nutzt. Bei einer genutzten Datei ist «Löschen» ausgegraut, und das Dock zeigt, in welchen Overlays sie vorkommt.
  - Ziehen ins Overlay (Wunsch und Entscheid 2026-11-03): Dateien aus dem Dock mit der Maus auf die Leinwand ziehen (neue Quelle an der Stelle der Maus, Originalgrösse) oder in die Liste des Docks «Quellen» (neue Quelle an der abgelegten Stelle der Reihenfolge, in der Mitte der Leinwand). Die Datei ist schon in `media/`, es wird nichts kopiert. Ziehen aus dem Dateimanager: nicht gewünscht.
  - Ansicht (Entscheid 2026-11-03): Liste mit Spalten wie im Dateimanager: kleines Vorschaubild, Name, Typ, Grösse; Ordner auf- und zuklappbar; Sortieren per Klick auf den Spaltenkopf.
  - Hinzufügen (Entscheid 2026-11-03): Knopf «Hinzufügen» unten im Dock, Dateiauswahl für mehrere Dateien; die Dateien kommen in den gewählten Ordner, ohne dass eine Quelle entsteht. Übernehmen im eigenen Thread des Kerns (Punkt 67).
  - Lage (Entscheid 2026-11-03): beim ersten Start eigenes Dock «Medien» in der unteren Reihe zwischen «Quellen» und «Eigenschaften»; lösbar, verschiebbar, über das Menü «Docks» ein- und ausblendbar wie die anderen Docks.
