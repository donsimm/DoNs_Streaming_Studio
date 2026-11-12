# P12 – Ton von Videos (Plan)

Plan des Projektleiter-Agenten vom 2026-11-09, nachgeführt am 2026-11-11 für G4 = P (Abschnitte 3 bis 8). Grundlage: Bauplan P12 (E-20, E-21), `docs/01-festlegungen.md` («Ton von Videos», 2026-11-02), Antworten des Nutzers in `docs/archiv/uebergabe-2026-11-08.md` («Antworten Ton (P12)») und unten («Antworten des Nutzers»), Messbericht `docs/archiv/messungen/P12a-bericht.md` (M1, M2, Z0). Ausführung in Teilen: P12a (Messtest), Halt mit Ergebnis an Lotse, P12b (Prototyp mit `pw_stream`, Halt, dann Bau in Abschnitten), Handtest.

## Entscheide des Nutzers

- 2026-11-02: Der Studio-PC spielt den Ton über eine Audiokarte des Systems, von dort geht er über das Mischpult zum Streaming-PC. Ein Audioausgang für alle Quellen, wählbar. Die Browserquelle in OBS spielt Videos immer stumm (kein doppelter Ton).
- 2026-11-07 (Bündel `ton-1`):
  - Der Kern spielt den Ton.
  - Wird eine Szene mit Video in OBS aktiv, beginnen Video und Ton von vorne.
  - Im Editor gibt es keinen Ton, Vorhören vielleicht später.
  - Der RØDECaster hängt per USB am Streaming-PC; ein eigener Kanal für den Studio-PC ist möglich.

## Wichtigste Befunde

| Befund | Beleg |
|---|---|
| Heute startet ein Video in OBS beim Laden der Seite und läuft weiter, auch wenn die Szene nicht aktiv ist. Die Seite kennt keine Szenen. «Von vorne beim Aktivieren» ist also neu. | `web/overlay.js:116-141` (`autoplay`, `muted`); `src/obs/obssources.cpp:365-367` (nur `url`, `width`, `height` gesetzt) |
| Der Kern abonniert heute nur General, Config, Scenes und Inputs (1\|2\|4\|8). | `src/obs/obsprotocol.h:36-37` |
| In OBS läuft CEF/Chromium 146.0.7680.177 (Prüfung V), auf dem Studio-PC Qt WebEngine mit Chromium 122. | `docs/archiv/berichte/P11-bericht.md:3` |
| Audio auf dem Studio-PC läuft über PipeWire 1.4.2-1 mit `pipewire-pulse`, `pipewire-alsa` und `wireplumber` 0.5.8-2. PulseAudio ist nicht installiert, `libpulse0` 17.0+dfsg1-2+b1 schon. | `/var/lib/dpkg/status` |
| PipeWire-Standard (nicht verändert, nur auskommentiert): Rate 48000, Quantum 1024, min. 32, max. 2048. 1024/48000 ergibt 21,3 ms je Quantum. | `/usr/share/pipewire/pipewire.conf:43-47`; im Benutzerordner keine eigene Konfiguration (`~/.config/pipewire` fehlt) |
| `qt6-multimedia-dev` 6.8.2-8 ist installiert. `libqt6multimedia6` hängt von `libqt6gui6`, `libpulse0`, `libpipewire`, `libavcodec61` und `libswresample5` ab. Folge: Qt Multimedia im Kern zieht Qt Gui in den Kern. | `/var/lib/dpkg/status:21208-21218` |
| `QAudioSink` (6.8) bietet `setBufferSize`, `bytesFree`, `processedUSecs`, `elapsedUSecs` und `setVolume`, aber **keine Abfrage der Verzögerung des Geräts**. | `/usr/include/x86_64-linux-gnu/qt6/QtMultimedia/qaudiosink.h:44-55` |
| `libswresample-dev` 7:7.1.5-0+deb13u1 ist installiert, steht aber nicht in `scripts/pakete.txt`. `swr_set_compensation` gleicht Gangunterschiede weich aus, `swr_drop_output` und `swr_inject_silence` springen. | `/var/lib/dpkg/status:24695-24705`; `/usr/include/x86_64-linux-gnu/libswresample/swresample.h:330-437` |
| obs-browser meldet der Seite «aktiv» (im Programm) und «sichtbar» über `window.obsstudio` mit den Ereignissen `obsSourceActiveChanged` und `obsSourceVisibleChanged` (`event.detail.active` bzw. `.visible`). Einstellungen `restart_when_active` und `shutdown`. **Nicht geprüft** (Webzugriff gesperrt). | https://github.com/obsproject/obs-browser/blob/master/README.md, https://raw.githubusercontent.com/obsproject/obs-browser/master/obs-browser-source.cpp |
| obs-websocket meldet mit `InputActiveStateChanged` (Abo mit hohem Datenaufkommen, eigenes Bit) und `GetSourceActive`, ob eine Quelle im Programm ist. **Nicht geprüft.** | https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md |
| Grenzen für den Versatz von Ton und Bild: ITU-R BT.1359-1 (erkennbar etwa +45 ms Ton früher, −125 ms Ton später) und EBU R37 (+40 / −60 ms). **Nicht geprüft**, nur als Zielrahmen. | https://www.itu.int/rec/R-REC-BT.1359, https://tech.ebu.ch/publications/r037 |

## 1. Grundfrage

**Kann der Kern auf dem Studio-PC den Ton eines Videos so über einen Audioausgang und den RØDECaster in OBS bringen, dass er zum Bild der Browserquelle passt?** Das heisst:

- Er beginnt beim Aktivieren der Szene zusammen mit dem Bild.
- Der feste Versatz lässt sich mit einem Wert ausgleichen. Die Streuung bleibt klein, Ziel ±20 ms.
- Der Ton läuft auch bei wiederholten Videos über Stunden nicht weg.

Die Grundfrage hat vier Teile:

| Teil | Frage |
|---|---|
| G1 Weg zum Mischpult | USB an einen zweiten USB-Anschluss des RØDECaster oder Kabel vom Line-Ausgang des Studio-PCs? Modell `[OFFEN]` (Frage 1) |
| G2 Wer gibt den Start vor | Die Seite in OBS (Ereignis «aktiv») oder der Kern über obs-websocket? |
| G3 Uhr | Bild und Ton laufen auf verschiedenen Uhren: Videouhr von Chromium auf dem Streaming-PC, Audiogerät am Studio-PC oder RØDECaster. Wie gross ist der Gangunterschied, und wie wird er nachgeführt? |
| G4 Ausgabe im Kern | Reicht `QAudioSink` (ohne neues Paket, aber mit Qt Gui im Kern und ohne Abfrage der Verzögerung), oder braucht es PipeWire direkt? |

### Varianten

| Teil | Variante | Bewertung |
|---|---|---|
| G1 | **A: USB an einen zweiten USB-Anschluss** des RØDECaster (laut RØDE bei Pro II und Duo, **nicht geprüft**) | Empfehlung: digital, kein Brummen zwischen zwei PCs, eigener Kanal, und der RØDECaster gibt den Takt vor |
| G1 | B: Klinke vom Line-Ausgang des Mainboards in einen Eingang | geht mit jedem Modell; Risiko Brummschleife und Pegel |
| G1 | C (Gegenvorschlag, widerspricht dem Entscheid vom 2026-11-02): Browserquelle gibt den Ton direkt an OBS | synchron ohne Aufwand, aber kein Mischpult und doppelte Logik zur Lautstärke. Nur als Rückfall, falls P12a scheitert; dann entscheidet der Nutzer |
| G2 | **S1: Die Seite führt.** Die Seite in OBS erkennt «aktiv», startet das Video von vorne und meldet Start und Lage an den Kern. Der Kern folgt | Empfehlung: geht auch mit verschachtelten Szenen, die Seite kennt den echten Bildstart. Rückfall für den Zustand beim Laden: `GetSourceActive` |
| G2 | S2: Der Kern führt über `InputActiveStateChanged` und schickt per SSE «Start» an die Seite | Startverzögerung des Videos unbekannt, braucht ohnehin Rückmeldung, also S1 mit einem Umweg |
| G2 | S3: beide starten «jetzt», ohne Rückmeldung | verworfen: Versatz und Gang nicht beherrscht |
| G4 | Q: `QAudioSink`/`QMediaDevices` | **verworfen** (Nutzer, 2026-11-11): Abbruchbedingung von M1 ausgelöst, Sprung +42,67 ms beim Wechsel des Quantums auf 2048 (M1c). Bleibt nur Rückfallweg, falls b1 scheitert (dann zuerst Z1 messen) |
| G4 | **P: PipeWire direkt (`pw_stream`, genaue Verzögerung)** | **entschieden** (Nutzer, 2026-11-11), mit Prototyp zuerst (P12b, Abschnitt b1). Braucht `libpipewire-0.3-dev` (Nutzer installiert) |

## 2. P12a – Messtest

**Stand 2026-11-11:** M1 und M2 gemessen (Nachtlauf 11.11.), M1d nach der Zeit neu ausgewertet (Z0), alles im Messbericht `docs/archiv/messungen/P12a-bericht.md`. M2 wird nicht wiederholt. M3 macht Lotse mit dem Nutzer zusammen, vor Abschnitt b4 (b4 hängt davon ab). M4 siehe Frage 5 in Abschnitt 7.

Grund für die Teilung: Ohne die Antworten zu G1 bis G4 lässt sich der Bau nicht festlegen. Ausserdem brauchen M3 und M4 Rechte und Geräte, die der bauende Agent nicht hat.

**Schritt 0 (Agent):** Quellen aus «Befunde» und «Quellen» lesen und die Tabelle bestätigen oder korrigieren. Weicht eine Angabe ab, die G2 oder G4 trägt: Halt und Meldung an Lotse.

**Commits P12a (Regel 8):**
1. Testdaten `tests/daten/ton/` mit `erzeugen.sh` (ffmpeg 7.1.5, lavfi) und `LIESMICH.md`. Inhalt:
   - «Blitz und Piep»: 60 s, 1280x720, Bildrate wie OBS (Lotse liest sie mit `GetVideoSettings`), alle 2 s ein weisses Bild mit gleichzeitigem Piep von 1 kHz und 1 Bild Länge, Zähler im Bild.
   - je einmal als H.264/AAC in MP4 und als VP9/Opus in WebM;
   - dazu Mono, 44,1 kHz, 5.1 und ein Video ohne Ton.
   - P12b nutzt dieselben Dateien.
2. Prototyp `prototypes/ton/` (eigene CMake-Datei, nicht in `ctest`):
   - `ton-messen` (Qt Core und Qt Multimedia, `QCoreApplication`) spielt einen Testton bzw. den Ton der Testdatei;
   - eine kleine Messseite (`messseite.html`) spielt das Video stumm und meldet über `requestVideoFrameCallback` (`mediaTime`) per `POST` die Lage;
   - der Prototyp führt den Ton nach diesen Meldungen nach.
3. Messbericht `docs/archiv/messungen/P12a-bericht.md` mit Datum, Befehlen und Rohwerten bei Auffälligkeiten.

**Messungen:**

| Nr. | Was | Wer, Gerät | Wie | Erwartung | Abbruch |
|---|---|---|---|---|---|
| M1 | Ausgabe im Kern-Umfeld: Liste der Ausgänge, Startverzögerung, Streuung, Aussetzer bei Puffer 10/20/40/80 ms, auch unter Last (`scripts/leistung.py`, ungünstigster Fall, 10 min) | Agent, Studio-PC, **unhörbar** über eine vorübergehende Null-Senke (`pw-cli create-node adapter … support.null-audio-sink`, `pipewire-bin` 1.4.2-1, danach entfernt). Lauf als `systemd-run --user`, wie der Dienst | Prototyp schreibt Klicks und nimmt sie im selben Prozess mit `QAudioSource` von der Überwachung der Senke auf | Streuung ≤ 5 ms, 0 Aussetzer ab 20–40 ms Puffer | `QMediaDevices` oder `QAudioSink` gehen ohne `QGuiApplication` bzw. im Dienst nicht. Oder Aussetzer erst ab Puffer > 100 ms. Oder die Verzögerung springt um > 10 ms, wenn sich das Quantum von PipeWire ändert. Dann Halt, Variante P vorschlagen |
| M2 | Uhr der Seite in Qt WebEngine: Schwankung der Meldungen beim Eintreffen, `mediaTime` gegen `steady_clock` des Kerns, Gang über 60 min (Video wiederholt) | Agent, Studio-PC, nachts, ohne Last daneben | Messseite im Fenster ausserhalb des Bildschirms wie `tst_browserquelle` | Schwankung p95 ≤ 5 ms; Gang ≤ 3 ms/min (≈ 50 ppm) | `requestVideoFrameCallback` fehlt und `currentTime` streut > 10 ms, oder p95 > 20 ms |
| M3 | OBS 32.2.2: Ist `window.obsstudio` da? Kommen `obsSourceActiveChanged` und `obsSourceVisibleChanged` beim Wechsel auf «test 2» und zurück (je 5×)? Zustand beim Laden einer schon aktiven Quelle; Zustand mit «shutdown»; Meldungen und Gang über 60 min gegen den Studio-PC; Bildrate von OBS (`GetVideoSettings`) | **Lotse**, nachts, nie während eines Streams (vorher Prozess und Ausgabe prüfen). Eigene Testquelle in «test 2», danach löschen, Szene zurück auf «Szene» | Messseite vom Prototyp auf dem Studio-PC | Ereignisse kommen jedes Mal; Gang wie M2 | Kein «aktiv»-Ereignis: Rückfall auf `InputActiveStateChanged` prüfen. Geht beides nicht: Halt |
| M4 | Ganzer Weg: Versatz Ton–Bild in OBS (Mittel und Streuung über 30 Blitze), Hörtest | **Nutzer**: Kabel bzw. USB nach Frage 1, Kanal am RØDECaster, Hören. **Lotse**: Aufnahme 60 s in «test 2» (nur mit Frage 4 A), Datei per `scp` holen, Auswertung mit ffmpeg (`signalstats`, `silencedetect`), Aufnahme danach löschen | Prototyp spielt auf dem echten Ausgang nach den Meldungen der Seite in OBS | Streuung (max − min) ≤ 40 ms; Mittel als Startwert für «Versatz»; Ton sauber, ohne Brummen | Streuung > 40 ms (nicht mit festem Wert ausgleichbar), oder Brummen bei Variante B: Halt, Varianten an den Nutzer |

→ **Halt:** Ergebnis an Lotse mit Empfehlung zu G1 bis G4 und Startwert «Versatz». P12b beginnt erst danach.

## 3. P12b – Bau

Nachgeführt 2026-11-11 für **S1 und P** (G4 = P, Entscheid des Nutzers 11.11.). Der frühere Umfang für Q (Commits 1–8 mit `QAudioSink`) ist ersetzt; Q bleibt nur Rückfallweg (b1, «Abbruch»).

**Umfang:**

| Thema | Vorgabe |
|---|---|
| Wann der Ton läuft | Nur für Seiten mit `window.obsstudio`, also nur in OBS. Gibt es keine Seite, die aktiv ist, gibt es keinen Ton |
| Seite (`web/overlay.js`) | Wird sie in OBS aktiv: Video von vorne. Wird sie inaktiv: anhalten und zurück auf den Anfang. Ohne `window.obsstudio` bleibt alles wie heute |
| Meldungen der Seite | Start, Lage etwa jede Sekunde, Ende, Sprung beim Wiederholen. Neue Adresse `…/clock` (POST, JSON, eigene Grenze je Seite). Lage aus `requestVideoFrameCallback` (`mediaTime`), wie die Messseite in M2 |
| Ausgabe im Kern | `pw_stream` (libpipewire 0.3) in eigenem Thread (`pw_thread_loop`), ständig offen. Kern bleibt ohne Qt Gui und ohne Qt Multimedia |
| Ausgang je Zweck | Der Kern führt Ausgänge nach Zweck (Antwort 2 des Nutzers vom 09.11.: später Chat-Bot und weitere Soundkarten). P12 baut nur den Zweck «Ton der Videos». Je Zweck ein eigener `pw_stream`, Gerät und Versatz |
| Mischer im Kern | Je Ausgang ein Mischer, nur Logik, ohne Gerät testbar. Der Echtzeit-Abruf von PipeWire holt fertige Proben ohne Sperren und ohne Speicher anzulegen; Dekodieren läuft in einem eigenen Thread mit Vorrat |
| Verzögerung | Nachgeführt 2026-11-12 nach Z3: Der Mischer führt die Lage in Proben (eigener Zähler der übergebenen Proben) und rechnet den hörbaren Zeitpunkt aus Abrufzeit, Lage und Zyklusgrösse `size` (`struct pw_time`), dazu `delay`, `queued` und `buffered`. `delay` war in Z3 an der virtuellen Senke immer 0; ob es an einem echten Ausgang einen Wert hat, bestätigt erst M4. Ändert sich `size` (Quantum-Wechsel), führt er nach |
| Puffer | gewünschte Verzögerung (`node.latency`) mindestens 40 ms (Frage 2) |
| Nachführen | Abweichung unter 20 ms weich mit `swr_set_compensation`; darüber Sprung (Stille einfügen bzw. Proben verwerfen) |
| Fehlender Ausgang | kein Ton und ein Hinweis, kein Ausweichen auf ein anderes Gerät (`target.object` und kein automatisches Umhängen, **nicht geprüft**, b3 prüft es mit einer virtuellen Senke) |
| Stopp | sofort, wenn der Kern Ausgabe aus, verborgene, gelöschte oder geänderte Quelle sieht, oder wenn die SSE-Verbindung der Seite endet. Ohne Meldung nach 3 s ebenfalls Stopp |
| Mehrere Browserquellen auf dasselbe Overlay | Ton einmal je Videoquelle, nach der ersten aktiven Seite |
| Editor | bleibt stumm |

Die Abschnitte b1 bis b6 sind je ein Auftrag an einen Agenten (b5 und b6 dürfen in eine Sitzung). Nach jedem Abschnitt Rückmeldung an Lotse; nach b1 ist es ein **Halt** mit Entscheid.

### b1 – Prototyp mit `pw_stream` und Messung Z3 (2 Commits), danach Halt

**Voraussetzung:** `libpipewire-0.3-dev` ist installiert (Nutzer). Fehlt es: Halt und Meldung an Lotse.

**Schritt 0 (Agent):** Header `/usr/include/pipewire-0.3/pipewire/stream.h` und `…/thread-loop.h` lesen und gegen https://docs.pipewire.org/group__pw__stream.html prüfen: Felder von `struct pw_time` (`now`, `rate`, `delay`, `queued`, `buffered`), Bedeutung von `delay` (Proben bis zum Gerät, Uhr von `now` = `CLOCK_MONOTONIC`?), Eigenschaften `node.latency`, `target.object`, `node.dont-reconnect`. Weicht etwas ab, das die Erfolgsbedingung trägt: Halt und Meldung an Lotse.

**Commits:**
1. `prototypes/ton/`: neues Programm `pw-messen` (eigenes Ziel in `prototypes/ton/CMakeLists.txt`, Qt Core und libpipewire, `QCoreApplication`). Ausgabe über `pw_stream`, Klick und Aufnahme wie M1 (`klickmessung`, `pw-loopback`, virtuelle Senke und Quelle), damit nur die Ausgabe anders ist. Je Klick zusätzlich in der CSV: Schreibzeit der ersten Probe (`steady_clock`), gemeldete Verzögerung (`delay`, `queued`, `buffered`, `rate`), daraus die erwartete hörbare Zeit; Unterläufe (leere Abrufe) zählen. Liste der Ausgänge über die Registry (`media.class` «Audio/Sink»). Dazu `pw-lauf.sh` (wie `nachtlauf.sh`, mit `--kurz`), räumt Knoten und `clock.force-quantum` sicher auf.
2. Messbericht: Abschnitt «Z3» in `docs/archiv/messungen/P12a-bericht.md` mit Datum, Befehlen, Kennzahlen, Rohdaten nur bei Auffälligkeiten.

**Messung** (Agent, Studio-PC, unhörbar, nachts wie M1: `clock.force-quantum` gilt für den ganzen Rechner, Frage 4; als `systemd-run --user` wie der Dienst; keine Last daneben ausser im Lastteil):

| Teil | Inhalt |
|---|---|
| Z3a | Liste der Ausgänge im Dienst |
| Z3b | Ruhe, `node.latency` 40 ms, 120 s |
| Z3c | wie M1c: 40 ms, 180 s, bei 60 / 100 / 140 s `clock.force-quantum` 2048 / 256 / 0 |
| Z3c-Q | Gegenlauf mit `ton-messen` (Q) wie M1c in derselben Nacht, damit beide Wege unter gleichen Bedingungen vorliegen |
| Z3d | nur wenn Frage 1 mit Ja beantwortet: wie M1d, 40 ms, 2 × 5 min mit `scripts/leistung.py --video` in Schleife |

Kenngrösse **«Rest»** je Klick: gemessene Einsatzzeit in der Aufnahme minus erwartete hörbare Zeit aus der gemeldeten Verzögerung. Der Rest enthält den Messweg (`pw-loopback`, Aufnahme) wie in M1.

**Erfolgsbedingung b1** (alle müssen erfüllt sein, Z3b und Z3c, voller Lauf und Probelauf):
- **E1 Sprung:** Bei jedem der drei Wechsel des Quantums ändert sich der Rest um höchstens **10 ms** (Median von 5 Klicks vor gegen 5 Klicks nach dem Wechsel). Das heisst: Entweder springt die hörbare Lage nicht, oder `pw_stream` meldet den Sprung so genau, dass der Kern ihn ausgleicht (Frage 3). Der rohe Sprung (ohne gemeldete Verzögerung) steht zusätzlich im Bericht.
- **E2 Aussetzer:** 0 Unterläufe in Z3b und Z3c, auch beim Wechsel (Q hatte 1).
- **E3 Dienst:** läuft als `systemd-run --user` mit `QCoreApplication`, ohne Qt Gui und Qt Multimedia in der Ausgabe; die Liste der Ausgänge enthält dieselben Geräte wie M1a.
- **E4 Streuung:** p5–p95 des Rests in Ruhe (Z3b) höchstens 15 ms (Q in M1b bei 40 ms: 13,4 ms ohne Pufferinhalt, Messweg eingeschlossen).
- Z3d (falls gemessen): 0 Unterläufe; Streuung nur berichtet, kein Abbruch.

**Abbruch:** Ist E1, E2 oder E3 nicht erfüllt, oder E4 nicht: Halt, kein Einbau. Bericht an Lotse mit Empfehlung, dazu die Wege:
- Q als Rückfall: zuerst Z1 messen (M1c mit Puffer ≥ 1 Quantum). Folge: Qt Gui und Qt Multimedia im Kern, Sprung bleibt für den Kern unsichtbar.
- G1 C (Browserquelle gibt den Ton an OBS): widerspricht dem Entscheid vom 2026-11-02, nur der Nutzer entscheidet.

→ **Halt:** Ergebnis an Lotse. Der Einbau (b2 ff.) beginnt erst nach dem Go des Nutzers.

**Ergebnis b1** (Nachtlauf 2026-11-11, Neuauswertung 2026-11-12; Messbericht `docs/archiv/messungen/P12a-bericht.md`, Abschnitt «Z3», Unterabschnitt «Neuauswertung (12.11.)»):
- Nach der vereinbarten Rechnung (Rest in ms) war E1 nicht erfüllt (−21,3 / +37,3 / −16,0 ms). Grund: `pw_time.delay`, `queued` und `buffered` sind an der virtuellen Senke immer 0; beim Wechsel ändert sich nur `size`, und der Rest verschiebt sich genau um die Änderung des Quantums.
- Entscheid des Nutzers 2026-11-12: E1 neu auswerten, P behalten, `delay` an einem echten Ausgang später mit M4 (RØDECaster) messen.
- Neu ausgewertet (`prototypes/ton/z3_neu_auswerten.py`): E1 in Lesart «Lage in Proben» erfüllt (Änderung 0 Proben bei allen 6 Wechseln, Versatz in allen Z3-Läufen fest 25 024 Proben; Q dagegen +2048 Proben) und in Lesart «Rest + Quantum aus `size`» erfüllt (grösste Änderung 0,068 ms). E2 bis E4 erfüllt.
- Klick 197 (voller Lauf, Wechsel auf 256): vollständig übergeben, Lage in der Aufnahme lückenlos, aber kein Einsatz. Er war im Weg Senke → Aufnahme, als PipeWire den laufenden Zyklus abbrach (Δticks 318 statt 2048). Welcher Knoten und ob hörbar: offen. Folge: Beim Wechsel des Quantums kann ein kurzes Stück Ton fehlen, ohne Lageverschiebung und ohne gemeldeten Unterlauf.
- Offen bis M4: `delay` und der hörbare Zeitpunkt an einem echten Ausgang. b2 und b3 hängen nicht davon ab (Lage in Proben und `size` reichen für den Bau); der Startwert «Versatz» und die Bestätigung von `delay` kommen aus M4.

### b2 – Ton dekodieren und mischen, ohne Gerät (2 Commits)

1. `src/media/audiodecoder.{h,cpp}`: Ton einer Videodatei, float, 48 kHz, Stereo (libavcodec und libswresample), ohne Qt Gui, eigener Thread mit Vorrat. Test `tst_tondecoder` mit `tests/daten/ton/` (MP4/AAC, WebM/Opus, Mono, 44,1 kHz, 5.1, ohne Ton). `libswresample-dev` und `libpipewire-0.3-dev` in `scripts/pakete.txt`.
2. `src/audio/mixer.{h,cpp}`, nur Logik: Quellen auf Probenlage mischen, Lautstärke und «Stumm», Start/Stopp, Wiederholen nach der Länge, die die Seite meldet (Pause beim Wiederholen rund 58,6 ms, M2), Nachführen nach den Meldungen der Seite und nach dem hörbaren Zeitpunkt des Ausgangs, Abruf ohne Sperren (Ring für einen Schreiber und einen Leser). Nachgeführt nach Z3 (2026-11-12): Der Mischer führt die Lage in Proben selbst; den hörbaren Zeitpunkt rechnet er aus Abrufzeit, Lage, `size`, `delay`, `queued` und `buffered`, die der Ausgang je Abruf übergibt (Bedeutung von `delay` erst mit M4 bestätigt). Test `tst_tonmischer` mit gedachter Uhr und gedachten Werten des Ausgangs, ohne Gerät: Wechsel von `size` 1024 → 2048 → 256 → 1024 bei `delay` 0 (wie Z3c, hörbarer Zeitpunkt bleibt in Lesart «Rest + Quantum» innerhalb 10 ms), dazu `delay` ungleich 0 und ein Sprung der Lage um 2048 Proben wie in M1c (Q).

### b3 – Ausgabe über PipeWire im Kern (2 Commits)

1. `src/audio/pwoutput.{h,cpp}`: dünne Schicht über `pw_thread_loop` und `pw_stream`: Liste der Ausgänge (Registry), gewählter Ausgang über `target.object`, fehlender oder verschwundener Ausgang (kein Ausweichen), je Abruf Abrufzeit, `size`, `delay`, `queued`, `buffered` und Lage an den Mischer. Nachgeführt nach Z3 (2026-11-12): Einen abgebrochenen Zyklus (Δticks ungleich voriger Zyklusgrösse, wie beim Quantum-Wechsel in Z3c) zählt die Schicht und schreibt ihn ins Protokoll; er ist kein Fehler (Klick 197: kurzes Stück Ton kann fehlen, Lage bleibt). `delay` wird gelesen und protokolliert, die Rechnung verlässt sich bis M4 nicht auf einen Wert ungleich 0. Schalter `DSS_CORE_AUDIO` (aus = Kern ohne Ton, Stand P11; `dss-core` linkt libpipewire nur mit Schalter). Test `tst_tonausgang`: legt im eigenen Prozess eine virtuelle Senke an (verschwindet mit dem Prozess, **nicht geprüft**), spielt unhörbar, prüft Liste, Wahl, Entfernen der Senke (kein Ton auf einem anderen Gerät) und Wiederkehr.
2. Kern: Ausgang je Zweck in `kern.ini` (Abschnitt je Zweck, zuerst `[ton-video]` mit `ausgang` und `versatz_ms`), Schnittstelle Version 12: `audio_outputs` (Liste der Geräte, nach `hello` und bei Änderung an alle), Felder für Ausgang und Versatz je Zweck in `settings` und `update_settings`, Zustand des Ausgangs. Tests `tst_kerneinstellungen` (`[ton-video]`), `tst_protocol`, `tst_aenderungen`.

### b4 – Seite und Kern verbinden (3 Commits)

**Voraussetzung:** M3 erledigt (Lotse mit dem Nutzer). Kommt in OBS kein «aktiv»-Ereignis: Rückfall `InputActiveStateChanged` über den Kern planen, Halt und Meldung an Lotse.

1. Overlay-Server: Adresse `…/clock` (POST, JSON, Grenzen je Seite, nur mit gültigem Token). Test `tst_overlayserver` (`clock`, Grenzen, falsche Daten).
2. Seite `web/overlay.js`: Verhalten in OBS (von vorne bei «aktiv», anhalten und zurück bei «inaktiv») und Meldungen. Test `tst_browserquelle` mit nachgebildetem `window.obsstudio` und `CustomEvent`: Inhalt der Meldungen, keine Meldungen ohne `obsstudio`. `tst_bildvergleich` muss unverändert bestehen.
3. Kern: Meldungen an den Mischer, Stopp-Regeln (Ausgabe aus, Quelle verborgen/gelöscht/geändert, SSE-Ende, 3 s ohne Meldung). Neu `tst_kernton` (Kern als eigener Prozess mit Ersatzausgang ohne Gerät: Start, Stopp in allen Fällen, Nachführen, Wiederholen).

### b5 – Overlay-Format und Fenster (3 Commits)

1. Overlay-Format 5: `video.volume` (0–100, ganze Zahl) und `video.muted`, neue Quellen 100 und `false` (Antwort 3 des Nutzers). Umwandeln 4 → 5 (`dss-core --format-pruefen`/`--umwandeln`), Tests `tst_datenformat`, `tst_overlaymodel`. K-07 gilt hier nicht: Es ändert sich nichts am Bild, die Seite bleibt stumm.
2. Dock «Eigenschaften», Bereich «Video»: Regler «Lautstärke» 0–100 % und Kästchen «Stumm». Test `tst_fensterbearbeiten`.
3. Dock «Steuerung»: Zeile für den Ton der Videos mit Ausgang, Versatz und Hinweis bei fehlendem Ausgang (Antwort 2 des Nutzers, Aussehen nach Frage 6). Test `tst_fensterbearbeiten`.

Vor Commit 2 erfasst der Agent, wie die bestehenden Docks Zeilen, Felder und Meldungen bauen (z. B. Zeile «OBS» im Dock «Steuerung», `src/app/docks.cpp`, `src/app/theme.cpp`), und folgt dem.

### b6 – Prüfung und Dokumente (2 Commits)

1. `scripts/tonpruefung.py`: virtuelle Senke anlegen, Kern aus dem Entwicklungsordner starten, Meldungen nachbilden, Aufnahme über `pw-loopback`, Abweichung zu den Meldungen und zur gemeldeten Verzögerung auswerten, Senke entfernen. 20 Läufe, unhörbar.
2. Dokumente nachführen: `01`, `02` (Pakete), `03` (E-20, E-21), `04`, `06`, `08`. Dazu der Bericht `docs/archiv/berichte/P12-bericht.md`.

**Prüfstufe «Baustein»** (nach b6, Befehle wie in P11, Ordner `build/pruefung-p12/`; b2 bis b5 je Stufe «Klein»):
- `cmake --build build -j8`
- `ctest --test-dir build`: X11-Sitzung aktiv, 0 übersprungen
- frischer Klon (Bau und `ctest`)
- `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p12/start/startpruefung`
- `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p12/dienst` (Watchdog mit dem Thread von PipeWire)
- `scripts/tonpruefung.py`, 20 Läufe
- **50 Wiederholungen** (Takt, Threads und Video betroffen): `tst_tonmischer`, `tst_tonausgang`, `tst_kernton`, `tst_overlayserver`, `tst_browserquelle`, `tst_bildvergleich`. Ohne Last daneben, nie zwei Fenstertests gleichzeitig.
- Leistung: `scripts/leistung.py` mit 3 Videos mit Ton. Ziel: höchstens 3 Prozentpunkte eines Kerns mehr CPU, 0 Aussetzer in 30 min.
- Bau mit `DSS_CORE_AUDIO=OFF` einmal (neuer Schalter).

## 4. Handtest (Regel 10)

**Prüft Lotse** (nicht während eines Streams, vorher Prozess und Ausgabe von OBS prüfen):
- **H1 Start und Stopp in OBS.** Warum: Nur OBS meldet «aktiv». Testquelle mit «Blitz und Piep» in «test 2». Szene 5× wechseln: Der Kern meldet im Protokoll Start und Stopp, `GetSourceScreenshot` zeigt nach dem Aktivieren den Zähler bei 0. Danach Szene zurück auf «Szene», Testquelle löschen.
- **H2 Versatz** (Recht zur Aufnahme seit 2026-11-09, `CLAUDE.md`). Warum: genaue Zahl statt Hörurteil. Aufnahme 60 s in «test 2», Auswertung wie M4 (30 Blitze). Ist Frage 5 mit Ja beantwortet, ersetzt H2 zugleich M4: erster Lauf mit Versatz 0 ergibt den Startwert, zweiter Lauf mit eingestelltem Versatz. Ziel: Streuung (max − min) ≤ 40 ms, Mittel innerhalb ±20 ms nach eingestelltem Versatz. Aufnahme löschen.
- **H3 Dauerlauf**, nachts, 60 min mit wiederholtem Video in «test 2». Warum: Gang zwischen zwei Rechnern. Ziel: kein Aussetzer, Abweichung im Protokoll immer < 20 ms.
- **H4 Bildschirmfotos** der Zeile für den Ton im Dock «Steuerung» und des Bereichs «Video» in «Eigenschaften» (Ablage nur im Scratchpad).

**Prüft der Nutzer:**
- **N1 Anschluss.** Warum: Kabel und Gerät. USB an den zweiten USB-Anschluss des RØDECaster (Antwort 1), Kanal am RØDECaster einrichten. Der RØDECaster muss eingeschaltet sein (stand nachts nicht in der Geräteliste).
- **N2 Hören.** Warum: Nur der Nutzer hört.
  - Ton sauber, ohne Brummen oder Knacken, am richtigen Regler.
  - Nicht doppelt.
  - Mit einem echten Video wirkt der Ton synchron.
- **N3 Bedienung.** Warum: Bedienung und Gerät.
  - Ausgang im Dock «Steuerung» wählen, Versatz ändern.
  - Lautstärke bzw. «Stumm» an einer Quelle.
  - **Eigener Schritt:** USB-Kabel des Ausgangs ziehen. Erwartet: Hinweis, kein Ton aus einem anderen Gerät. Danach wieder einstecken; der Ton kommt ohne Neustart zurück.

Was Lotse oder der Agent schon geprüft haben, wiederholt der Nutzer nicht.

## 5. Pakete (Regel 5)

- **Neu für P12b (Variante P): `libpipewire-0.3-dev`**, installiert der Nutzer (`sudo` gibt der Nutzer ein), **Voraussetzung für b1**. Begründung: genaue Verzögerung über `pw_stream` (M1c: Sprung +42,67 ms mit Q, für den Kern unsichtbar), Kern ohne Qt Gui und ohne Qt Multimedia. Stand 2026-11-11 (`/var/lib/dpkg/status`): nicht installiert; die Laufzeitbibliothek `libpipewire-0.3-0t64` 1.4.2-1 und `pipewire-bin` 1.4.2-1 sind installiert. Version vor der Installation mit `LANG=C apt-cache policy libpipewire-0.3-dev` lesen; ob es `libspa-0.2-dev` mitzieht, ist nicht geprüft. Kommt mit Version in `scripts/pakete.txt` (b2).
- `libswresample-dev` 7:7.1.5-0+deb13u1 ist installiert und kommt in `scripts/pakete.txt` (b2).
- `qt6-multimedia-dev` 6.8.2-8 bleibt nur für den Prototyp `ton-messen` (Gegenlauf Z3c-Q) und kommt **nicht** in `scripts/pakete.txt`: Der Kern braucht Qt Multimedia mit P nicht.
- Keine weiteren Pakete. Die Installation (`scripts/installieren.sh`) braucht zur Laufzeit nur `libpipewire-0.3-0t64` (installiert).

## 6. Schätzung

| Teil | Agent | Lotse | Nutzer |
|---|---|---|---|
| P12a | erledigt (M1, M2, Z0) | M3 ~1 h mit dem Nutzer, dazu 60 min nachts | M3 dabei ~30 min |
| b1 | 1 Sitzung ~3 h, Messung nachts ~20 min (mit Z3d ~35 min) | Prüfung und Entscheid ~20 min | Paket installieren ~5 min, Go nach b1 |
| b2 | 1 Sitzung ~3–4 h | Prüfung ~15 min | – |
| b3 | 1 Sitzung ~4 h | Prüfung ~15 min | – |
| b4 | 1 Sitzung ~4–5 h | Prüfung ~15 min | – |
| b5 + b6 | 1–2 Sitzungen ~6–7 h, Prüfstufe «Baustein» ~2 h, 50er-Läufe nachts ~2 h | Prüfung ~30 min | – |
| Handtest | – | ~1 h (H1, H2, H4), dazu 60 min nachts (H3) | ~30 min (N1–N3, bei Ja zu Frage 5 auch der Anschluss für M4) |
| **Summe P12b** | **5–6 Sitzungen, ~20–23 h**, dazu Nachtläufe ~2,5 h | **~3,5 h**, dazu 2 h nachts | **~1 h** |

## 7. Fragen an den Nutzer

Fragen 1 bis 4 vor b1 (ein Dialog), Fragen 5 bis 7 vor b4 (zweiter Dialog). Die früheren Fragen 1 bis 4 vom 09.11. sind beantwortet (unten, «Antworten des Nutzers»).

**Vor b1:**
1. **Z3 auch unter Last messen?** Empfehlung: **Ja**, 2 × 5 min mit derselben Last wie M1d (Puffer 40 ms). Grund: Unter Last war die Streuung mit Q etwa dreimal so breit wie in Ruhe (Z0); nur so zeigt sich, ob `pw_stream` sie meldet und ob unter Last Aussetzer kommen. Kostet ~15 min Nachtlauf, für den Nutzer nichts. Gegenargument: Die Last ist mässig (rund 9 % CPU), der ungünstigste Fall bleibt ungemessen.
2. **Puffer im Kern mindestens 40 ms?** Empfehlung: **Ja**, 40 ms fest, keine Einstellung. Grund: Mit Q gab es bei 20 ms unter Last 17 Unterläufe, bei 40 ms keinen. Der feste Teil der Verzögerung wird ohnehin mit dem Versatz ausgeglichen. Gegenargument: 20 ms mehr Verzögerung bis zum Mischpult; kleiner wird er nur, wenn b1 unter Last 0 Unterläufe bei 20 ms zeigt.
3. **Gilt der Sprung als vermieden, wenn `pw_stream` ihn meldet und der Kern ihn ausgleicht?** Empfehlung: **Ja.** Grund: Den Unterlauf beim Wechsel des Quantums verhindert auch P nicht sicher; entscheidend ist, dass der Ton danach wieder zum Bild passt (Rest ≤ 10 ms, Erfolgsbedingung E1). Gegenargument: Ein kurzer Ausgleich kann hörbar sein (weich unter 20 ms, Sprung darüber).
4. **Messung b1 nachts mit Wechsel des Quantums?** Empfehlung: **Ja**, der Agent misst nachts wie in M1 und setzt `clock.force-quantum` danach sicher auf 0 zurück. Grund: Der Wechsel gilt für den ganzen Studio-PC und kann laufenden Ton kurz stören; der Nutzer muss nichts tun.

**Vor b4:**
5. **M4 im Handtest mit dem fertigen Kern statt vorher mit dem Prototyp?** Empfehlung: **Ja**, H2 ersetzt M4. Grund: Der Prototyp `ton-messen` spielt über Q, das verworfen ist; ein Versatz von dort gilt für P nicht. Der Nutzer schliesst den RØDECaster nur einmal an. Gegenargument: Ein Problem mit Brummen oder dem USB-Anschluss zeigt sich erst am Ende von P12b.
6. **Zeile für den Ton im Dock «Steuerung»** (sichtbar, bedienbar). Empfehlung: Zeile «Ton Videos» mit einer Auswahlliste der Ausgänge (Namen wie im System, erster Eintrag «Kein Ausgang») und einem Feld «Versatz» in ms (−500 bis +500, Schritt 1, Startwert 0 bis H2). Fehlt der gewählte Ausgang: Hinweis «Ausgang fehlt» in der Zeile, wie die Zeile «OBS». Ein neuer Kern hat «Kein Ausgang», bis der Nutzer wählt. Grund: kein Ton aus einem falschen Gerät; weitere Zwecke (Chat-Bot) kommen später als eigene Zeilen dazu. Alternative: Ausgang des Systems vorgewählt.
7. **Termin für M3** (Lotse mit dem Nutzer, OBS, ~1 h, dazu 60 min nachts). Empfehlung: **nach b1 und vor b4**, an einem Abend ohne Stream. Grund: b4 baut das Verhalten der Seite in OBS und braucht dafür das Ergebnis von M3 («aktiv»-Ereignis, Zustand beim Laden).

### Fragen vom 2026-11-09 (beantwortet)

1. **Anschluss Studio-PC → RØDECaster** (Gerät). Welches Modell ist es, und wie anschliessen?
   - A) USB an den zweiten USB-Anschluss. Empfohlen, falls vorhanden: digital, kein Brummen, eigener Kanal.
   - B) Klinkenkabel vom Line-Ausgang in einen Eingang.
   - C) Modell nennen, Lotse klärt.
2. **Bereich «Ton» in «Werkzeuge → Einstellungen …»** (bedienbar).
   - Empfehlung: «Audioausgang» (Liste der Ausgänge des Studio-PCs, vorgewählt der Ausgang aus Frage 1) und «Versatz zum Bild» in ms (Startwert aus der Messung). Fehlt der Ausgang: kein Ton und ein Hinweis, kein Ausweichen auf ein anderes Gerät.
   - Alternativen: Ausgang im Dock «Steuerung»; oder Versatz nur fest aus der Messung, ohne Feld.
3. **Lautstärke je Videoquelle** (sichtbar in «Eigenschaften», Bereich «Video»).
   - A) Regler «Lautstärke» 0–100 % und Kästchen «Stumm», neue Quellen 100 % und nicht stumm. Empfohlen: Am Mischpult gibt es nur einen Regler für alle Videos.
   - B) nur «Ton an/aus».
   - C) nichts, Pegel nur am Mischpult.
4. **Rechte für den Messtest** (OBS und Gerät).
   - A) Lotse darf in OBS eine Aufnahme von höchstens 60 s in «test 2» machen, nie während eines Streams, und löscht die Datei nach der Auswertung. Empfohlen: genaue Zahl auf ±½ Bild.
   - B) Der Nutzer macht die Aufnahme selbst (kurzes Skript mit Anleitung).
   - C) keine Aufnahme, nur Hörtest; der Versatz bleibt geschätzt.

## 8. Risiken und `[OFFEN]`

| Risiko oder offene Frage | Gegenmassnahme oder Stand |
|---|---|
| Alle Aussagen zu OBS, obs-browser, obs-websocket, RØDE und Grenzwerten nicht gelesen (Webzugriff gesperrt) | M3 mit Lotse; Webzugriff für den Agenten von b1 und b4 |
| **API von libpipewire nicht gegen die Dokumentation geprüft** (`struct pw_time`, Bedeutung von `delay`, Uhr von `now`, `target.object`, `node.dont-reconnect`); Header fehlen, bis das Paket installiert ist | b1, Schritt 0: Header und Dokumentation lesen, bei Abweichung Halt |
| **b1 scheitert** (Sprung nicht erkannt, Unterlauf beim Wechsel, Streuung zu gross) | Halt vor dem Einbau; Rückfall Q mit Puffer ≥ 1 Quantum (vorher Z1) oder G1 C, Entscheid des Nutzers. Stand 2026-11-12: nach Neuauswertung erfüllt, P bleibt (Entscheid des Nutzers) |
| `pw_time.delay` an der virtuellen Senke immer 0; Wert und Bedeutung an einem echten Ausgang unbekannt | Kern rechnet aus Lage in Proben und `size`, liest `delay` mit; M4 (RØDECaster) misst `delay` und hörbaren Zeitpunkt (Entscheid des Nutzers 12.11.) |
| Beim Quantum-Wechsel fehlt ein kurzes Stück Ton (Klick 197, Z3c), ohne Lageverschiebung und ohne Unterlauf; Knoten und Hörbarkeit offen | Wechsel kommen im Betrieb selten vor (nicht geprüft); `pwoutput` protokolliert abgebrochene Zyklen; Protokoll von H3 darauf prüfen |
| Echtzeit-Abruf von PipeWire: Sperren oder Speicher anlegen im Abruf erzeugen Aussetzer | Mischer mit Ring ohne Sperren, Dekodieren in eigenem Thread; `tst_tonmischer`, `tonpruefung.py`, 50 Wiederholungen, Leistung 30 min |
| Thread von PipeWire im Dienst stört Watchdog oder sauberes Beenden | `dienstpruefung.py`, `startpruefung.py` (20 Starts); Schalter `DSS_CORE_AUDIO` |
| Ausgang fehlt oder wird gezogen: PipeWire hängt den Strom auf das Standardgerät um | `target.object` ohne automatisches Umhängen (nicht geprüft); `tst_tonausgang` mit virtueller Senke, N3 |
| Messweg (`pw-loopback`, Aufnahme) streut mit, sein Anteil ist nicht getrennt (P12a) | Erfolgsbedingung über den Rest, Grenze E4 mit Spielraum zum Wert von Q; Gegenlauf Z3c-Q in derselben Nacht |
| Ob ein Programm am Studio-PC im Betrieb das Quantum ändert, ist nicht geprüft | P meldet die neue Zyklusgrösse `size`, der Kern führt danach nach (E1, Neuauswertung 12.11.); H3 Dauerlauf |
| Unter Last streut die Verzögerung etwa dreimal so breit (Z0, mit Q) | Z3d (Frage 1), Leistung mit 3 Videos |
| Ob «aktiv» kommt und ob es beim Laden einer schon aktiven Quelle gemeldet wird | M3 vor b4; Rückfall `InputActiveStateChanged` bzw. `GetSourceActive` über den Kern |
| Bildrate von OBS (Messgenauigkeit ±½ Bild) | `[OFFEN]`, Lotse liest `GetVideoSettings` in M3 |
| RØDECaster stand nachts nicht in der Geräteliste; Name des Geräts in PipeWire unbekannt | N1 vor H2; Ausgang wird nach Namen gewählt, nie geraten |
| Studio-Modus: Vorschau zeigt das erste Bild angehalten | Annahme, siehe unten; Nutzer kann im Handtest korrigieren |
| Ton und Bild mit verschiedener Länge beim Wiederholen (Pause rund 58,6 ms, M2) | Kern wiederholt nach der Länge des Videos, die die Seite meldet; Test in `tst_tonmischer` |
| Gang zwischen Videouhr (Streaming-PC) und Audiouhr | laufendes Nachführen; M2 (−38,8 ppm am Studio-PC), M3, H3 |
| Bei Ja zu Frage 5 zeigen sich Probleme am Anschluss (Brummen, USB) erst im Handtest | Variante A (USB, digital) gewählt; bei Brummen Trennübertrager (Gerät, Nutzer) |
| Neues Paket `libpipewire-0.3-dev` fehlt | Voraussetzung für b1, Nutzer installiert; ohne Paket Halt |

## Annahmen und Interna-Entscheide

| Entscheid | Begründung |
|---|---|
| «Aktiv» heisst im Programm von OBS; in der Vorschau (Studio-Modus) bleibt das Video angehalten auf dem ersten Bild | Folgt aus der Antwort «Szene wird aktiv → von vorne»; ein Ton nur aus der Vorschau wäre falsch |
| S1: Die Seite führt, der Kern folgt | geht mit verschachtelten Szenen; die Seite kennt den echten Bildstart |
| Kein `restart_when_active` in OBS setzen | Neuladen zeigt kurz ein leeres Overlay; der Kern ändert in OBS bisher nur `url`, `width`, `height` |
| Ton nur für Seiten in OBS (`window.obsstudio`) | Ein anderer Browser im Heimnetz soll keinen Ton auslösen |
| Ein ständig offener Ausgang mit eigenem Mischer | gleichbleibende Verzögerung und sofortiger Start; später auch für den Musikplayer |
| Weiches Nachführen unter 20 ms, Sprung darüber | Sprünge knacken; kleine Gangunterschiede sind über `swr_set_compensation` unhörbar |
| Stopp bei Ende der SSE-Verbindung oder 3 s ohne Meldung | OBS geschlossen oder Netz weg: kein Ton ins Leere |
| Neuer Schalter `DSS_CORE_AUDIO` | riskanter Teil: neue Bibliothek (libpipewire) und Thread im Dienst (Regel 8) |
| P12b beginnt mit Prototyp und Halt (b1) | Entscheid des Nutzers 11.11.: Erst wenn `pw_stream` den Sprung vermeidet, folgt der Einbau |
| b1 misst mit demselben Aufnahmeweg wie M1 (`pw-loopback`, `QAudioSource`) | nur die Ausgabe ändert sich, die Werte sind mit M1c vergleichbar |
| Erfolgsbedingung über den «Rest» (gemessen minus erwartet aus der gemeldeten Verzögerung) | genau das braucht der Kern: Er muss den hörbaren Zeitpunkt kennen, nicht eine feste Verzögerung haben |
| Ausgang je Zweck, zuerst nur «Ton der Videos» (`[ton-video]` in `kern.ini`) | Antwort 2 des Nutzers: weitere Ausgänge später ohne Umbau |
| `pw_thread_loop` statt Abruf im Qt-Ereignisfluss | PipeWire ruft in Echtzeit; der Ereignisfluss des Kerns (Server, OBS, Watchdog) darf ihn nicht bremsen |
| Ersatzausgang ohne Gerät in `tst_kernton`, echte virtuelle Senke nur in `tst_tonausgang` und `tonpruefung.py` | Kern-Tests bleiben unabhängig vom Audiosystem; die Schicht über PipeWire wird trotzdem echt geprüft |
| Qt Multimedia nicht im Kern, nicht in `scripts/pakete.txt` | mit P nicht nötig; zog Qt Gui in den Kern |
| Echter Ausgang in Prüfungen nur unhörbar (Null-Senke), echtes Gerät nur mit dem Nutzer | Auftrag: «Kabel/Gerät nur mit dem Nutzer»; Messungen nachts ohne Ton im Raum |
| P12 geteilt in P12a und P12b | Bau hängt von G1 bis G4 ab; M3 und M4 brauchen Rechte von Lotse und den Nutzer |

## Quellen

- **Lokal gelesen (2026-11-09):**
  - `/var/lib/dpkg/status` (Pakete und Versionen oben)
  - `/usr/share/pipewire/pipewire.conf:43-47`
  - `/usr/include/x86_64-linux-gnu/qt6/QtMultimedia/qaudiosink.h`
  - `/usr/include/x86_64-linux-gnu/libswresample/swresample.h:322-437`
  - Repo: `web/overlay.js`, `src/obs/obsprotocol.h`, `src/obs/obssources.cpp`, `src/media/videoplayer.h`, `src/core/CMakeLists.txt`, `src/media/CMakeLists.txt`, `docs/archiv/berichte/P11-bericht.md`
- **Nicht gelesen (Webzugriff gesperrt, in P12a Schritt 0):**
  - https://github.com/obsproject/obs-browser/blob/master/README.md
  - https://raw.githubusercontent.com/obsproject/obs-browser/master/obs-browser-source.cpp
  - https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md
  - https://doc.qt.io/qt-6.8/qaudiosink.html
  - https://doc.qt.io/qt-6.8/qmediadevices.html
  - https://wicg.github.io/video-rvfc/
  - https://www.itu.int/rec/R-REC-BT.1359
  - https://tech.ebu.ch/publications/r037
  - https://rode.com/en/user-guide/rodecaster-pro-ii
  - https://docs.pipewire.org/
- **Nachführung 2026-11-11, lokal gelesen:**
  - `docs/archiv/messungen/P12a-bericht.md` (Kurzfassung, M1b, M1c, Z0, «G4», «Folgen von Z0 für G4 und P12b», Z1–Z3)
  - `/var/lib/dpkg/status` (`libpipewire-0.3-0t64` 1.4.2-1 und `pipewire-bin` 1.4.2-1 installiert, `libpipewire-0.3-dev` nicht)
  - Repo: `src/core/CMakeLists.txt`, `src/app/docks.h` (Dock «Steuerung»), `prototypes/ton/CMakeLists.txt`, `scripts/pakete.txt`, `docs/08-kern-schnittstelle.md` (Version 11)
- **Nachführung 2026-11-11, nicht gelesen** (kein Webzugriff, b1 Schritt 0):
  - https://docs.pipewire.org/group__pw__stream.html
  - https://docs.pipewire.org/group__pw__thread__loop.html
  - https://docs.pipewire.org/page_man_pipewire-props_7.html (`node.latency`, `target.object`, `node.dont-reconnect`)

---

**Offen für Lotse (Stand 2026-11-11):**
- Fragen 1 bis 4 aus Abschnitt 7 vor b1 an den Nutzer, Fragen 5 bis 7 vor b4.
- `libpipewire-0.3-dev` installiert der Nutzer vor b1.
- Webzugriff für den Agenten von b1 (Dokumentation von PipeWire) und b4 (obs-browser) freigeben, sonst bleiben die Aussagen ungeprüft.

## Antworten des Nutzers (2026-11-09, in der Sitzung)
1. Anschluss: USB an den zweiten USB-Anschluss des RØDECaster (Variante A). Modell und Anschluss im Schritt 0 von P12a bestätigen.
2. Einstellung von Audioausgang und Versatz: **im Dock «Steuerung»**, nicht in «Werkzeuge → Einstellungen». Begründung des Nutzers: Später spielt auch der Chat-Bot weitere Töne aus, auf andere USB-Soundkarten. Darum so planen, dass mehrere Ausgänge (je Zweck/Modul eigener Ausgang) später ohne Umbau dazukommen; P12 baut nur den Ausgang für den Ton der Videos.
3. Lautstärke je Videoquelle: Regler 0–100 % und Kästchen «Stumm», neue Quellen 100 % und nicht stumm.
4. Messtest: Lotse darf in OBS eine Aufnahme von höchstens 60 s in der Szene «test 2» machen, nie während eines Streams, und löscht die Datei nach der Auswertung (in `CLAUDE.md`, «Umgebung», eingetragen).

## Entscheide des Nutzers (2026-11-11, nach dem Messbericht P12a)
- G4 = P (PipeWire direkt), mit Prototyp zuerst: P12b beginnt mit b1 (Z3), der Einbau folgt erst, wenn der Prototyp den Sprung vermeidet.
- Z0 ja (ausgeführt, Messbericht). M2 wird nicht wiederholt. M3 macht Lotse mit dem Nutzer zusammen.
- Nicht entschieden, als Fragen in Abschnitt 7: Z3 unter Last (Frage 1), Puffer mindestens 40 ms (Frage 2).

## Entscheide des Nutzers (2026-11-12, nach dem Messbericht Z3)
- E1 neu auswerten und P behalten; Klick 197 dabei klären (ausgeführt, Abschnitt «b1», «Ergebnis b1»).
- `delay` an einem echten Ausgang später mit M4 (RØDECaster) messen.
