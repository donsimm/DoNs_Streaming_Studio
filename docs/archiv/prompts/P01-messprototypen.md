# P01 – Messprototypen T-01 und T-02

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des geklonten Repos. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md` und alle Dokumente unter `docs/`.

## Ziel

Auf dem Studio-PC messen, welche Technik die Oberfläche und die Darstellung der Overlay-Leinwand trägt. Es entsteht noch keine Software, nur kleine Messprototypen und ein Bericht. Daraus entscheiden Nutzer und Claude Betriebssystem und Technik.

Es werden drei Varianten verglichen:

- A: C++ mit Qt 6, nativ
- B: ein unabhängiger Kern mit Web-Oberfläche (Chromium als Darstellung)
- C: Python mit PyQt6 (Debian-Pakete)

Die Prototypen liegen unter `prototypes/a-cpp-qt/`, `prototypes/b-web/`, `prototypes/c-pyqt/`. Jeder ist klein und hat nur den Zweck der Messung. Wenn eine Variante auf diesem System nicht baubar oder startbar ist, ist das ein Ergebnis und kein Fehler: im Bericht festhalten, mit der Fehlermeldung.

## Test T-01: Start mit echtem Fenstersystem

Pro Variante ein Fenster, das einen Moment sichtbar ist und sich sauber beendet. Mindestens 20 Starts hintereinander in der echten X11-Sitzung (kein Xvfb, kein offscreen). Je Start: Exit-Code, Dauer bis zum ersten Bild, sauberes Beenden. Ergebnis als Tabelle.

## Test T-02: Overlay 1920x1080 mit Animationen

Pro Variante ein Overlay mit 1920x1080 Pixeln, das enthält:

- 10 Quellen: Text mit Animation (Schweben, Atmen), Bilder, ein Video, eine WebM-Datei mit Alpha-Kanal
- Schachbrettmuster als Hintergrund für die Transparenz

Messen, jeweils 60 Sekunden lang: CPU-Last (gesamt und des Prozesses), RAM des Prozesses, erreichte Bildrate bei Ziel 30 Bildern pro Sekunde, Einbrüche. Messen mit `top`/`pidstat` oder gleichwertigen Programmen; Befehle im Bericht festhalten. Zusätzlich prüfen: Wird der Alpha-Kanal der WebM-Datei korrekt dargestellt (Sichtprüfung durch den Nutzer, Bildschirmfoto im Bericht)?

Testdateien (Bilder, Video, WebM mit Alpha) erzeugst du selbst, soweit möglich; fehlt dir ein Werkzeug dazu (zum Beispiel ffmpeg), nenne es dem Nutzer mit Begründung, bevor du es installierst.

## Nicht Teil dieses Auftrags

- NDI, obs-websocket, Browserquelle in OBS (das Netzwerk besteht noch nicht)
- Twitch, Chat-Bot, Speicher, Konfiguration
- Die endgültige Oberfläche

## Pakete

Es sind nur diese Pakete und ihre Abhängigkeiten vorgesehen: `build-essential`, `cmake`, `qt6-base-dev`, `qt6-webengine-dev`, `qt6-multimedia-dev`, `python3-pyqt6`, `python3-pyqt6.qtwebengine`, `python3-pyqt6.qtmultimedia`, `ffmpeg`, `sysstat`. Alles andere nur mit Begründung.

`qt6-multimedia-dev` (A) und `python3-pyqt6.qtmultimedia` (C) wurden während der Ausführung ergänzt (vom Nutzer bestätigt am 2026-11-02): Ohne Chromium braucht Qt für Video und WebM das Modul Qt Multimedia. Sonst hätten A und C in T-02 kein Video und keine WebM mit Alpha. Installationen laufen über `sudo apt install` und der Nutzer bestätigt sie. Halte die installierten Versionen im Bericht fest.

## Abnahme

1. Für jede Variante steht im Bericht, ob sie gebaut und gestartet werden konnte.
2. T-01 hat pro baubarer Variante 20 Starts mit Tabelle.
3. T-02 hat pro baubarer Variante die Messwerte über 60 Sekunden, den Befehl, mit dem gemessen wurde, und die Sichtprüfung der WebM-Alpha.
4. Der Nutzer hat den Bericht gelesen und die Bildschirmfotos geprüft.

## Bericht

Schreibe den Bericht nach `docs/messungen/P01-bericht.md`. Er enthält: geänderte Dateien, alle Befehle mit Ausgabe (gekürzt, wo sehr lang), Messtabellen, installierte Versionen, nicht Erledigtes als `[OFFEN]`, getroffene Annahmen, Quellen. Keine Empfehlung zur Technikwahl als Entscheid: du darfst eine Einschätzung mit Begründung nennen, sie als solche kennzeichnen. Der Entscheid liegt beim Nutzer.

Committe pro Prototyp einzeln. Pushe nur auf Anweisung des Nutzers.
