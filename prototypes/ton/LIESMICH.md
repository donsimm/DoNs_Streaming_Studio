# Prototyp P12a: Ton von Videos (Messtest)

Plan: `docs/archiv/prompts/P12-ton.md`, Abschnitt 2. Nicht Teil des Projekts, nicht in `ctest`.

```sh
cd ~/DoNs_Streaming_Studio && cmake -S prototypes/ton -B prototypes/ton/build && cmake --build prototypes/ton/build -j8
```

| Programm | Inhalt |
|---|---|
| `ton-messen` | Qt Core, Network, Multimedia mit `QCoreApplication` wie der Kern; libavformat/libavcodec/libswresample |
| `seite-zeigen` | Qt WebEngine Widgets, zeigt die Messseite (M2) |
| `messseite.html` | spielt ein Video stumm und meldet je Bild `mediaTime`, `presentationTime`, `expectedDisplayTime` (`requestVideoFrameCallback`) per `POST /melden`; in OBS Start von vorne bei «aktiv», Halt bei «inaktiv» |
| `nachtlauf.sh` | M1 und M2 nachts, unhörbar (siehe Kopf des Skripts) |

Betriebsarten von `ton-messen` (Kopf von `main.cpp`):

- `--liste`: Ausgänge und Eingänge über `QMediaDevices`.
- `--stille`: nur Nullen mit Lautstärke 0 (Kurzprobe auf einem echten Ausgang).
- `--dekodieren <Datei>`: Ton laden (48 kHz Stereo, Lage nach den Zeitstempeln) und Pieps finden.
- `--klicks`: M1, Klicks spielen und im selben Prozess aufnehmen. Nur auf Ausgänge `dss_p12a…`.
- `--server`: Messseite und Videos ausliefern, Meldungen aufzeichnen und auswerten. Mit `--folgen` spielt der Prototyp den Ton nach den Meldungen nach.
- `--selbsttest`: Server ohne Browser und ohne Ton prüfen.
- `--pw-zustand`: `pw-dump` von stdin auswerten (für `nachtlauf.sh`).

## Abweichung vom Plan

M1 sollte die Klicks von der Überwachung (Monitor) einer Null-Senke aufnehmen. Qt 6.8 listet aber
keine Monitor-Quellen (`--liste` am 2026-11-09: nur die zwei ALSA-Eingänge). Der Nachtlauf legt
deshalb mit `pw-loopback` eine virtuelle Senke `dss_p12a_ausgabe` an, deren Ton in eine virtuelle
Quelle `dss_p12a_aufnahme` geht. Die gemessene Verzögerung enthält damit den Weg durch
`pw-loopback`; Streuung, Aussetzer und Sprünge bleiben vergleichbar.

## M3 (Lotse, OBS)

Nicht geprüft: ob die Firewall des Studio-PCs Port 24990 vom Streaming-PC zulässt.

1. Auf dem Studio-PC, im Vordergrund (zeigt jede Meldung ausser «bild» sofort an, Ende mit Ctrl+C):
   ```sh
   cd ~/DoNs_Streaming_Studio && prototypes/ton/build/ton-messen --server --lauschen 192.168.1.9 --port 24990 --aus build/p12a-m3/m3
   ```
2. In OBS in der Szene «test 2» eine Browserquelle 1280x720 mit
   `http://192.168.1.9:24990/messseite.html?datei=blitz-piep.webm&seite=obs` anlegen
   (`restart_when_active` und `shutdown` aus lassen; die Seite ist stumm).
3. Erwartet: «hallo» mit `obs: true` und `pluginVersion`, «stufe». Szenen-Wechsel auf «test 2» und
   zurück (je 5-mal): «aktiv» `true`/`false`, «sichtbar», «start»/«stop».
4. Zustand beim Laden einer schon aktiven Quelle: Quelle neu laden, während «test 2» im Programm
   ist. Laut Quellcode von obs-browser kommt dann kein «aktiv»; Rückfall `GetSourceActive`. Für den
   Gang über 60 min `…&sofort=1` verwenden (läuft ohne «aktiv»).
5. Bildrate: `GetVideoSettings` (`fpsNumerator/fpsDenominator`); weicht sie von 30 ab, die
   Testdaten neu erzeugen (`tests/daten/ton/LIESMICH.md`).
6. Ctrl+C: Auswertung in `build/p12a-m3/m3-zusammenfassung.txt` (Gang und Schwankung je Seite).

## M4 (Nutzer und Lotse)

Wie M3, dazu `--folgen tests/daten/ton/blitz-piep.webm --ausgang <Teil der Kennung des Ausgangs>
--versatz-ms 0 --lautstaerke 0.25`. Der Ausgang muss ausdrücklich genannt werden; Klicks
(`--klicks`) gehen nie auf ein echtes Gerät.
