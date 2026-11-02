# P02 – Messprototypen 2: Dekodierweg für A, Bildabgriff für B

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` und den Bericht `docs/messungen/P01-bericht.md`.

## Ausgangslage

P01 hat gezeigt: Alle drei Varianten starten sauber und brauchen bei 1080p30 mit 10 Quellen wenig CPU. WebM mit Alpha stellt nur Variante B (Qt WebEngine) richtig dar. Bei A (C++/Qt) und C (PyQt6) liefert Qt Multimedia 6.8.2 das Bild als `NV12` ohne Alpha. OBS löst das mit dem FFmpeg-Decoder `libvpx` bzw. `libvpx-vp9`, wenn der Strom `alpha_mode=1` hat (OBS-Commit https://git.tjdev.de/mirror/obs-studio/commit/f2ad6ab0762c1433f7919ceb3fc41a57532366fe, aus Zusammenfassung, nicht im Code geprüft).

Der Nutzer entscheidet zwischen A und B. Dafür fehlen zwei Messungen, die ohne Netzwerk möglich sind. Betriebssystem ist entschieden (Debian 13, Xfce unter X11) und nicht Gegenstand dieses Auftrags. Variante C wird nicht weiterverfolgt.

## Messung M1: Variante A mit eigenem Dekodierweg (libvpx-vp9)

Ziel: Zeigen, ob Variante A WebM mit Alpha korrekt darstellen kann, wenn sie wie OBS direkt über FFmpeg mit `libvpx-vp9` dekodiert, und was das kostet.

- Neuer Prototyp unter `prototypes/a-cpp-qt-libvpx/`, ausgehend von `prototypes/a-cpp-qt/`. Das Overlay bleibt dasselbe wie in T-02 (gleiche 10 Quellen, gleiche Lage, gleiche Testdateien aus `prototypes/messung/media_erzeugen.sh`).
- Nur die beiden Videoquellen (`video.webm` und `alpha.webm`) werden über libavformat/libavcodec dekodiert. Für den Alpha-Strom wählt der Prototyp ausdrücklich den Decoder `libvpx-vp9`, für `video.webm` den Standard. Das Dekodieren läuft in einem eigenen Thread. Fertige Bilder gehen als `QImage` im Format mit Alpha (zum Beispiel `Format_ARGB32_Premultiplied`) an die Zeichenfläche.
- Prüfen und im Bericht belegen: Alpha stimmt (Sichtprüfung per Bildschirmfoto wie bei P01 und eine Zahl, zum Beispiel der Alpha-Mittelwert im ersten Bild), Videobilder pro Sekunde, Einbrüche (Abstand grösser als 50 ms), CPU und RAM über 60 Sekunden mit demselben Skript `t02.py`, wo möglich unverändert.
- Als Vergleich dienen die gemessenen Werte von A und B aus P01. Wenn du die Messung der Vergleichsvarianten wiederholst, nenne es im Bericht.

## Messung M2: Variante B, Bilder ohne Fenster abgreifen

Ziel: Messen, was es kostet, aus Qt WebEngine Bilder in 1920x1080 mit 30 Bildern pro Sekunde in den Speicher zu holen. NDI braucht das später. NDI selbst wird **nicht** gemessen und das NDI-SDK wird **nicht** installiert (Lizenz und Bezug sind nicht geprüft).

- Neuer Prototyp unter `prototypes/b-web-capture/`, ausgehend von `prototypes/b-web/`. Die Seite `overlay.html` bleibt dieselbe.
- Das Fenster soll Bilder der Seite in den Speicher liefern (zum Beispiel als `QImage` oder Puffer in BGRA), ohne sie auf den Bildschirm zu legen. Suche dazu in der Qt-6.8-Dokumentation einen passenden Weg (zum Beispiel `QWidget::grab`, `QQuickRenderControl`, offscreen-Plattform von Qt). Nenne im Bericht, welchen Weg du gewählt hast, warum, und welche du verworfen hast, jeweils mit Quelle (URL).
- Zu prüfen ist besonders: Rendert Chromium weiter mit 30 Bildern pro Sekunde, wenn das Fenster nicht sichtbar oder verdeckt ist? Chromium kann Darstellung in solchen Fällen drosseln. Messe sichtbar und nicht sichtbar getrennt. Regel 6 aus `CLAUDE.md` (echtes Fenstersystem) gilt: die Messung läuft in der echten X11-Sitzung, das Fenster darf dabei verdeckt oder minimiert sein.
- Messen, jeweils 60 Sekunden: erreichte Abgriffe pro Sekunde, Einbrüche, CPU (gesamt und je Prozess), RAM, GPU-Auslastung. Zusätzlich die Zeit für die Umwandlung BGRA nach UYVY im Speicher (NDI bevorzugt laut NDI-Dokumentation UYVY/UYVA; Quelle https://docs.ndi.video/all/developing-with-ndi/sdk/performance-and-implementation). Das ist nur eine Schätzung des NDI-Aufwands. Die eigentliche NDI-Codierung ist nicht gemessen.
- Messung mit einem Overlay, dann mit zwei Overlays gleichzeitig (entspricht zwei NDI-Ausgängen).

## Nicht Teil dieses Auftrags

- NDI-SDK, NDI-Senden, obs-websocket, Browserquelle in OBS (Netzwerk fehlt)
- Twitch, Chat-Bot, Speicher, Konfiguration, endgültige Oberfläche
- Variante C und Änderungen an den P01-Prototypen (sie bleiben unverändert)

## Pakete

Für M1 werden voraussichtlich die Entwicklungspakete von FFmpeg gebraucht (`libavcodec-dev`, `libavformat-dev`, `libavutil-dev`, `libswscale-dev`, gegebenenfalls `pkg-config`). Das ist eine Annahme: prüfe mit `apt-cache policy` und `apt-cache depends`, welche wirklich nötig sind. Nenne dem Nutzer die endgültige Liste mit Begründung, **bevor** du etwas installierst, und halte die Versionen im Bericht fest. Alles andere nur mit Begründung. Installationen laufen über `sudo apt install` und der Nutzer bestätigt sie im zweiten Terminal.

## Abnahme

1. M1: Der Prototyp baut und startet. Die Messwerte über 60 Sekunden stehen im Bericht, ebenso der Befehl. Die Sichtprüfung der WebM-Alpha ist als Bildschirmfoto abgelegt.
2. M2: Der Prototyp baut und startet. Die Messwerte (sichtbar, nicht sichtbar, ein und zwei Overlays) stehen im Bericht, mit den Befehlen.
3. Eine Vergleichstabelle A (P01), A mit libvpx (M1), B (P01), B mit Abgriff (M2) steht im Bericht.
4. Der Nutzer hat Bericht und Bildschirmfotos geprüft.

## Bericht

Schreibe den Bericht nach `docs/messungen/P02-bericht.md` nach dem Muster von `P01-bericht.md`: geänderte Dateien, alle Befehle, Messtabellen, installierte Versionen, nicht Erledigtes als `[OFFEN]`, getroffene Annahmen, Quellen. Nenne nur Quellen, die du wirklich abgerufen hast, und kennzeichne die übrigen als «nicht abgerufen». Eine Einschätzung zur Wahl zwischen A und B darfst du nennen, mit Begründung und als solche gekennzeichnet. Der Entscheid liegt beim Nutzer.

Committe pro Prototyp einzeln. Pushe am Ende auf `main` (der Nutzer erteilt diesen Auftrag hiermit; für den Push braucht der Nutzer sein GitHub-Zugangstoken).
