# P12 – Ton von Videos (Plan)

Plan des Projektleiter-Agenten vom 2026-11-09, nachgeführt am 2026-11-11 für G4 = P (Abschnitte 3 bis 8) und am 2026-11-12 für b4 Teil 1 (Entscheide vor b4, «Ergebnis b4 Teil 1»), am 2026-11-14 für den Rückfall beim Laden einer aktiven Quelle (Abschnitt b4, «Ergebnis Rückfall …») sowie für b5 und b6 («Ergebnis b5», «Ergebnis b6»; Bericht `docs/archiv/berichte/P12-bericht.md`). Grundlage: Bauplan P12 (E-20, E-21), `docs/01-festlegungen.md` («Ton von Videos», 2026-11-02), Antworten des Nutzers in `docs/archiv/uebergabe-2026-11-08.md` («Antworten Ton (P12)») und unten («Antworten des Nutzers»), Messbericht `docs/archiv/messungen/P12a-bericht.md` (M1, M2, Z0). Ausführung in Teilen: P12a (Messtest), Halt mit Ergebnis an Lotse, P12b (Prototyp mit `pw_stream`, Halt, dann Bau in Abschnitten), Handtest.

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

**Stand 2026-11-11:** M1 und M2 gemessen (Nachtlauf 11.11.), M1d nach der Zeit neu ausgewertet (Z0), alles im Messbericht `docs/archiv/messungen/P12a-bericht.md`. M2 wird nicht wiederholt. M3 macht Lotse mit dem Nutzer zusammen, vor Commit 2 von b4 (Entscheid 12.11.: b4 Teil 1 ohne M3). M4 entfällt: H2 ersetzt M4 (Frage 5, Entscheid 12.11.).

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

**Ergebnis b2** (2026-11-12, Stufe «Klein»: Bau, `ctest` 47 von 47, `tst_tondecoder` und `tst_tonmischer` je 20 von 20 einzeln):
- `src/media/audiodecoder.{h,cpp}` und `src/media/audioring.h` (eigenes Ziel `dss_audiodecoder`, Kern und Fenster binden libswresample noch nicht). Der Ring trägt Durchgänge mit Marke und Nummer; der Decoder öffnet die Datei je Durchgang neu (kein Suchen), schneidet nach `setLoopLength` und beginnt mit `loop` den nächsten Durchgang.
- `src/audio/mixer.{h,cpp}` (Ziel `dss_audio`, ohne Qt). Schnittstelle für b3: `render(OutputTiming, out, frames)` mit `callNs`, `sinceNowNs`, `size`, `delay`, `rateNum`/`rateDenom`, `queued`, `buffered`; hörbarer Zeitpunkt nach `Mixer::audibleNs` (Lesart «Rest + Quantum»). Für b4: `start(id, pass, PageClock)`, `report`, `stop`, `prepare(id, pass)` (Vorrat nach `restart()` ohne Start), `setLength`, `setVolume`, `setExtraDelayNs` (Versatz).
- Abweichung vom Plan: Weiches Nachführen nicht mit `swr_set_compensation`, sondern im Mischer (0,2 % schneller bzw. langsamer, Interpolation mit 4 Punkten nur in dieser Zeit). Grund: Der Echtzeit-Abruf darf keinen Speicher anlegen; im Decoder-Thread wirkte der Ausgleich erst nach dem Vorrat und verwischte die Lage der Proben.
- Gemeldete Sprünge des Ausgangs (Wechsel von `size`, `delay`, `queued`) gleicht der Mischer sofort mit einem Sprung aus, nicht weich.

### b3 – Ausgabe über PipeWire im Kern (2 Commits)

1. `src/audio/pwoutput.{h,cpp}`: dünne Schicht über `pw_thread_loop` und `pw_stream`: Liste der Ausgänge (Registry), gewählter Ausgang über `target.object`, fehlender oder verschwundener Ausgang (kein Ausweichen), je Abruf Abrufzeit, `size`, `delay`, `queued`, `buffered` und Lage an den Mischer. Nachgeführt nach Z3 (2026-11-12): Einen abgebrochenen Zyklus (Δticks ungleich voriger Zyklusgrösse, wie beim Quantum-Wechsel in Z3c) zählt die Schicht und schreibt ihn ins Protokoll; er ist kein Fehler (Klick 197: kurzes Stück Ton kann fehlen, Lage bleibt). `delay` wird gelesen und protokolliert, die Rechnung verlässt sich bis M4 nicht auf einen Wert ungleich 0. Schalter `DSS_CORE_AUDIO` (aus = Kern ohne Ton, Stand P11; `dss-core` linkt libpipewire nur mit Schalter). Test `tst_tonausgang`: legt im eigenen Prozess eine virtuelle Senke an (verschwindet mit dem Prozess, **nicht geprüft**), spielt unhörbar, prüft Liste, Wahl, Entfernen der Senke (kein Ton auf einem anderen Gerät) und Wiederkehr.
2. Kern: Ausgang je Zweck in `kern.ini` (Abschnitt je Zweck, zuerst `[ton-video]` mit `ausgang` und `versatz_ms`), Schnittstelle Version 12: `audio_outputs` (Liste der Geräte, nach `hello` und bei Änderung an alle), Felder für Ausgang und Versatz je Zweck in `settings` und `update_settings`, Zustand des Ausgangs. Tests `tst_kerneinstellungen` (`[ton-video]`), `tst_protocol`, `tst_aenderungen`.

**Ergebnis b3** (2026-11-12, Stufe «Klein»: Bau ohne Warnungen, `ctest` 48 von 48 nach jedem Commit; `tst_tonausgang`, `tst_kerneinstellungen`, `tst_protocol`, `tst_aenderungen` und `tst_kernverbindung` je 20 von 20 einzeln):
- **Uhr (offene Frage aus b2):** `pw_time.now` und `pw_stream_get_nsec()` laufen auf `CLOCK_MONOTONIC`, wie `std::chrono::steady_clock` unter Linux. Belege: `/usr/include/spa-0.2/spa/node/io.h:139-141` (`spa_io_clock.nsec` «against monotonic clock (CLOCK_MONOTONIC)») und `/usr/include/pipewire-0.3/pipewire/stream.h:594-596` (`pw_stream_get_nsec` vergleichbar mit `pw_time.now`). Geprüft in `tst_tonausgang`: `steady_clock` liegt zwischen zwei Ablesungen von `CLOCK_MONOTONIC`, und in jedem Abruf liegt `pw_stream_get_nsec()` zwischen zwei Ablesungen von `steady_clock` (`Stats::clockOutside` immer 0). Der Mischer darf `callNs` (steady) und `sinceNowNs` also ohne Umrechnung verbinden.
- `src/audio/pwoutput.{h,cpp}` (Ziel `dss_pwoutput`, nur mit `DSS_CORE_AUDIO`, Standard an). Ein Strom je Zweck mit `target.object`, `node.dont-reconnect`, `node.dont-fallback`, `node.dont-move` und `node.latency` 1920/48000. Der Strom wird nur angelegt, wenn der gewählte Knoten in der Registry steht. Verschwindet der Knoten, wird der Strom entfernt (Zustand `missing`), und kommt er wieder, wird der Strom neu angelegt. Zusätzlich prüft die Schicht die Verbindungen des Stroms: Hängt er an einem fremden Knoten, gibt er Stille aus, wird entfernt und bleibt im Zustand `error`, bis der Ausgang neu gewählt wird. Bis die Verbindung bestätigt ist, gibt er Stille aus. Zustände: `no_pipewire`, `no_target`, `missing`, `connecting`, `playing`, `error`.
- Gemessen in `tst_tonausgang` (virtuelle Senke, unhörbar): `size` 1024, `delay`, `queued` und `buffered` 0, `rate` 1/48000, `sinceNowNs` höchstens etwa 0,1 ms. Abgebrochene Zyklen, Abrufe ohne Puffer und Wechsel von `delay` und `size` schreibt die Schicht einmal je Sekunde ins Protokoll.
- Kern: `kern.ini` mit `[ton-video]` (`ausgang` = `node.name`, leer = kein Ausgang; `versatz_ms` von −500 bis +500, Standard 0). Schnittstelle Version 12:
  - `settings` mit `audio_routes` (`{"video": {"output", "offset_ms"}}`), `audio_offset_min_ms` und `audio_offset_max_ms`.
  - `update_settings` mit `audio_routes` (je Zweck `output` und/oder `offset_ms`).
  - `audio_outputs` mit `enabled`, `devices` (`name`, `description`, `default`) und `routes` (je Zweck `state`, `detail`). Der Kern schickt es nach `hello` und bei jeder Änderung an alle.
  - `CoreClient` führt `audioOutputs()`.
  - Der Mischer läuft in b3 ohne Quellen (Stille).
- Abweichungen vom Plan:
  - Zusätzliche Tests: `tst_tonausgang` prüft auch eine fremde Verbindung (Link von aussen auf eine zweite Senke) und «kein Ausgang». `tst_kernverbindung` prüft `audio_outputs` nach `hello`, einen fehlenden Ausgang über `update_settings` (Zustand `missing`), Abweisungen und das Lesen nach einem Neustart. Dabei wählt der Test nur einen Ausgang, den es nicht gibt, und legt keinen Strom an.
  - Zwei Versionsprüfungen sind von 11 auf 12 nachgeführt (`tst_protocol`, `tst_kernverbindung`).
  - Kennung des Zwecks in der Schnittstelle `video`, Abschnitt in `kern.ini` `[ton-video]`.
  - Nach einem Fehler des Stroms versucht die Schicht es nach 5 s neu. Ausgänge, die erscheinen oder verschwinden, stehen im Protokoll des Kerns («Ton: …»).
  - `docs/08` wird erst in b6 nachgeführt (wie geplant).
- Offen für b4:
  - Fällt PipeWire weg (Neustart von PipeWire), bleibt der Zustand `no_pipewire` bis zum Neustart des Kerns. Es gibt keine neue Verbindung, **nicht geprüft**. Entscheiden, ob b4 das braucht.
  - `tst_kernton` (geplant in b4) mit Ersatzausgang. `tst_kernverbindung` startet jetzt Kerne mit Verbindung zu PipeWire, aber ohne Strom.
  - Bedeutung und Vorzeichen von `versatz_ms`: Angenommen ist «Weg hinter dem Ausgang, der Kern spielt so viel früher» (`Mixer::setExtraDelayNs`). Der Bereich ±500 ms ist eine Annahme nach Frage 6. Bestätigung mit H2/M4.
  - `delay` an einem echten Ausgang (M4).
  - `dss-cli einstellungen` zeigt Ausgang und Versatz noch nicht.
  - Ältere Programme lesen eine `kern.ini` mit `[ton-video]` nicht («unbekannter Abschnitt»). Das betrifft nur das Zurückgehen auf eine Version vor P12b.

### b4 – Seite und Kern verbinden (3 Commits)

**Voraussetzung:** M3 erledigt (Lotse mit dem Nutzer). Kommt in OBS kein «aktiv»-Ereignis: Rückfall `InputActiveStateChanged` über den Kern planen, Halt und Meldung an Lotse. **Entscheid des Nutzers 2026-11-12:** b4 in zwei Teilen. Teil 1 ohne M3: Commit 1 und Commit 3, das Format der Meldungen so, dass Commit 2 es nur noch senden muss. Teil 2 nach M3: Commit 2. **M3 erledigt** (Lotse, 2026-11-13, Messbericht P12a, «M3 (2026-11-13)»): «aktiv» kommt beim Szenenwechsel, aber nicht beim Laden einer schon aktiven Quelle.

1. Overlay-Server: Adresse `…/clock` (POST, JSON, Grenzen je Seite, nur mit gültigem Token). Test `tst_overlayserver` (`clock`, Grenzen, falsche Daten). **Erledigt (Teil 1).**
2. Seite `web/overlay.js`: Verhalten in OBS (von vorne bei «aktiv», anhalten und zurück bei «inaktiv») und Meldungen. Test `tst_browserquelle` mit nachgebildetem `window.obsstudio` und `CustomEvent`: Inhalt der Meldungen, keine Meldungen ohne `obsstudio`. `tst_bildvergleich` muss unverändert bestehen. **Erledigt (Teil 2)**, Vorgaben unten in «Für Commit 2», Ergebnis in «Ergebnis b4 Teil 2».
3. Kern: Meldungen an den Mischer, Stopp-Regeln (Ausgabe aus, Quelle verborgen/gelöscht/geändert, SSE-Ende, 3 s ohne Meldung). Neu `tst_kernton` (Kern als eigener Prozess mit Ersatzausgang ohne Gerät: Start, Stopp in allen Fällen, Nachführen, Wiederholen). **Erledigt (Teil 1).**

**Ergebnis b4 Teil 1** (2026-11-12, ohne M3, Stufe «Klein»: Bau ohne Warnungen; `ctest` 48 von 48 nach Commit 1 und 49 von 49 nach Commit 3; `tst_overlayserver` 20 von 20 nach Commit 1; `tst_kernton`, `tst_tonausgang` und `tst_kernverbindung` je 20 von 20 nach Commit 3):

- **Commit 1, Overlay-Server** (`src/server/clockreport.{h,cpp}`, `overlayserver.{h,cpp}`):
  - `POST …/clock`, Inhalt JSON (Format Version 1, ein Objekt je Meldung und Video, höchstens 2048 Byte):

    | Feld | Inhalt | Pflicht |
    |---|---|---|
    | `v` | 1 | immer |
    | `page` | Kennung der Seite, beim Laden zufällig, `[a-z0-9]{8,32}` | immer |
    | `seq` | laufende Nummer je Seite (ganze Zahl ≥ 0); ältere und doppelte verwirft der Kern | immer |
    | `event` | `start` (erstes Bild nach dem Start von vorne), `frame` (Lage), `loop` (erstes Bild nach dem Wiederholen), `end` (Ende ohne Wiederholen), `stop` (Seite inaktiv) | immer |
    | `run` | Lauf der Seite, zählt bei jedem «aktiv» hoch | immer |
    | `now_ms` | `performance.now()` beim Absenden | immer |
    | `source` | Kennung der Videoquelle | ausser `stop` (ohne `source`: alle Videos der Seite) |
    | `file` | `media/…` wie die Seite es spielt | `start`, `frame`, `loop` |
    | `loop` | Durchgang seit dem Start (0 = erster) | `start`, `frame`, `loop` |
    | `media_s` | `mediaTime` des Bildes (requestVideoFrameCallback) | `start`, `frame`, `loop` |
    | `display_ms` | `expectedDisplayTime` desselben Bildes | `start`, `frame`, `loop` |
    | `duration_s` | Länge des Videos (`duration`); fehlt oder 0 = unbekannt | freiwillig |

  - Antworten: 204 angenommen; 400 falsche Daten (auch Quelle, die keine Videoquelle dieses Overlays ist); 413 zu gross; 429 über der Grenze; 404 falscher Token (wie alle Adressen); 405 andere Methode.
  - Grenze je Seite: 40 Meldungen am Stück, danach 20 je Sekunde; höchstens 64 Seiten gleichzeitig (ruhende fallen nach 60 s heraus). Abweisungen höchstens alle 10 s als eine Zeile ins Protokoll, ohne Token.
  - Neu `…/events/<page>`: dieselben Ereignisse wie `…/events`, dazu meldet der Server dem Kern Beginn und Ende (für «SSE-Ende»). `…/events` ohne Kennung bleibt gültig (Seiten von heute).
  - Der Server setzt je Meldung das Eintreffen in `steady_clock` (für den Abgleich der Uhr der Seite).
- **Commit 3, Kern** (`src/core/videoaudio.{h,cpp}`, `src/audio/pageclock.{h,cpp}`, `src/audio/audiooutput.{h,cpp}`, `src/audio/nulloutput.{h,cpp}`, `coreservice`, `main.cpp`):
  - Je Videoquelle eine Stimme (Stelle im Mischer, Decoder mit Vorrat). Vorbereitet, sobald eine Seite des Overlays über `…/events/<page>` verbunden ist; so beginnt der Ton beim Start ohne Warten auf den Decoder.
  - Ablauf beim Stopp wie offen aus b3: `Mixer::stop` → `AudioDecoder::restart` → `Mixer::prepare`; der nächste Start spielt diesen Vorrat (`Mixer::start` mit dem neuen Durchgang). Gebaut und in `tst_kernton` mehrfach durchlaufen.
  - Glätten (offen aus b3): Die Zeit der Seite wird mit dem kleinsten Abstand «Eintreffen − `now_ms`» der letzten 10 s auf `steady_clock` abgebildet (wie im Prototyp, M2), danach die Lage geglättet: Weicht sie um höchstens 4 ms von der Fortschreibung ab, zählt ein Achtel; mehr gilt sofort. Gemessen in `tst_kernton` (Ersatzausgang, 2026-11-12): Streuung der Lage ±3 ms und Laufzeit 0 bis 10 ms über 4 s ergeben 0 Sprünge, 0 weiche Korrekturen, Abweichung zuletzt −0,78 ms. Ohne Streuung: 5 ms Versatz führt weich nach, 45 ms mit einem Sprung.
  - Wiederholen: Länge aus `duration_s` an Mischer und Decoder. `tst_kernton`: gemeldete Länge 1 s, Pause 58,6 ms, nach 3,4 s 3 Wiederholungen, 0 Sprünge, 0 weich, gespielt 3,23 s.
  - Stopp-Regeln und Gründe im Protokoll («Ton: Stopp «Quelle» in «Overlay»: Grund (gespielt, Wiederholungen, Sprünge, weich, Abweichung zuletzt)»): «Seite inaktiv» (`stop`), «Ende des Videos» (`end`), «Ausgabe aus» (Ausgabe des Overlays, E-24), «Quelle verborgen», «Quelle gelöscht», «Overlay gelöscht», «Quelle geändert» (andere Datei oder Wiederholen umgeschaltet), «Seite getrennt» (Ende von `…/events/<page>`), «3 s ohne Meldung» (Prüfung alle 100 ms; im Test nach 3,0 bis 3,1 s). Alle in `tst_kernton` geprüft.
  - Einstieg: Kommt `frame` oder `loop` für ein Video, das nicht spielt, steigt der Ton an der gemeldeten Lage ein («Ton: Einstieg …»): nach 3 s ohne Meldung, nach dem Wiederverbinden der Seite, wenn die Quelle wieder sichtbar oder die Ausgabe wieder an ist, nach einer geänderten Quelle und nach einem Neustart des Kerns. Nicht nach `end` oder `stop` desselben Laufs und nicht, solange die Ereignisse der Seite beendet sind.
  - Mehrere Seiten auf dasselbe Overlay: Die Seite, die zuerst startet, führt; Meldungen anderer Seiten für dieselbe Quelle zählen erst, wenn sie stoppt (`tst_kernton`, «firstPageLeads»).
  - Protokoll: «Ton: Start», «Ton: Einstieg», «Ton: Stopp», «Ton: Nachführen» (höchstens einmal je Sekunde, wenn der Mischer nachgeführt hat; Sprünge ohne den Einsatz beim Start) und alle 10 s «Ton: Lage …» mit Abweichung, Wiederholungen und Pause (für H3).
  - Ersatzausgang ohne Gerät (`NullOutput`): ruft den Mischer alle 10 ms mit 480 Rahmen ab, wie ein Gerät mit fester Uhr, ohne PipeWire. Der Kern nimmt ihn mit der Testoption `--ersatzausgang` (Gerät `dss_ersatzausgang`). Dafür haben `PwOutput` und `NullOutput` die gemeinsame Schnittstelle `AudioOutput` (Zustände und `AudioDevice` dorthin verschoben, Verhalten von `PwOutput` unverändert).
  - Beim Beenden beendet `CoreService::stopAudio()` die Decoder, solange Mischer und Ausgang noch leben.
- **Warum der Kern nur 2 Ausgänge sieht (M1a: 5):** Die 5 aus M1a und Z3a sind 3 Ausgänge und 2 Eingänge (`docs/archiv/messungen/P12a-daten/2026-11-11-0343/m1-liste.txt`, `P12b-daten/2026-11-11-2304/z3a-liste.txt`). Unter den Ausgängen war die vorübergehende Null-Senke `dss_p12a_ausgabe` der Messung, unter den Eingängen `dss_p12a_aufnahme`; beide wurden nach der Messung entfernt. Es bleiben 2 echte Ausgänge (`media.class` «Audio/Sink»): HDMI der Grafikkarte und «Internes Audio Digital Stereo (IEC958)». Der Kern listet nur «Audio/Sink», also 2. Kein Fehler, nichts am System geändert. Die aktuelle Liste wurde in dieser Sitzung nicht neu gelesen (`wpctl` war nicht erlaubt); der RØDECaster ist erst mit N1 zu erwarten.
- **Abweichungen vom Plan:**
  - `…/events/<page>` ist neu (Plan nannte nur `…/clock`): Ohne Kennung in den Ereignissen kann der Kern das Ende der Verbindung keiner Seite zuordnen.
  - «Ausgabe aus» ist als Ausgabe des Overlays (E-24) umgesetzt. Ein fehlender oder gezogener Ausgang (Gerät) stoppt die Stimmen nicht; kommt er wieder, führt der Mischer mit einem Sprung nach (gemeldeter Sprung des Ausgangs, b2).
  - «Quelle geändert» heisst andere Datei oder Wiederholen umgeschaltet. Lage, Grösse, Drehung und Name ändern am Ton nichts.
  - Einstieg mitten im Video ist dazugekommen (siehe oben). Ohne ihn bliebe der Ton nach 3 s ohne Meldung oder nach einem Neustart des Kerns bis zum nächsten «aktiv» stumm.
  - Die Seite darf öfter als jede Sekunde melden (Grenze 20 je Sekunde); der Test meldet viermal je Sekunde.
- **Für Commit 2 (`web/overlay.js`, nach M3):** Nur mit `window.obsstudio`. Beim Laden eine Kennung `page` wählen und die Ereignisse über `…/events/<page>` öffnen. Bei «aktiv»: `run` + 1, alle Videos von vorne; je Video beim ersten Bild (`requestVideoFrameCallback`) `start`, danach etwa jede Sekunde `frame`, beim ersten Bild nach dem Wiederholen `loop` mit `loop` + 1, bei `ended` ohne Wiederholen `end`; immer mit `duration_s`, sobald bekannt. Bei «inaktiv»: anhalten, zurück auf den Anfang, `stop` ohne `source`. Jede Meldung mit `seq` + 1 und `now_ms`, gesendet mit `fetch(…/clock, {method: 'POST', body, keepalive: true})` und `Content-Type: text/plain` (keine Vorabfrage). Commit 2 entscheidet ausserdem: Laufen verborgene Videos weiter (der Einstieg nach dem Zeigen setzt voraus, dass die Seite weiter meldet)? Startet die Seite ein Video mit neuer Datei von vorne? **Beide Fragen hat der Nutzer am 2026-11-13 entschieden** (siehe «Entscheide des Nutzers (2026-11-13, nach M3)»).
- **`[OFFEN]` nach M3** (Annahmen zu OBS), Stand nach M3 (2026-11-13):
  - `obsSourceActiveChanged` kommt in OBS 32.2.2 (obs-browser 2.26.9): **ja**, bei jedem Wechsel ins Programm und hinaus.
  - Beim Laden einer schon aktiven Quelle: **kein «aktiv»** (`GetSourceActive` meldete `videoActive: true`). Rückfall über den Kern nötig, **gebaut 2026-11-14** (siehe «Ergebnis Rückfall beim Laden einer aktiven Quelle»).
  - CEF in OBS liefert `expectedDisplayTime` und `mediaTime`: **ja**, Schwankung p95 0,39 ms gegen die Medienzeit (M2: 0,44 ms). Aber nur rund 20 Aufrufe je Sekunde bei 30 Bildern/s.
  - Streuung des Eintreffens am Studio-PC p95 24,9 ms, davon p95 24,6 ms schon beim Senden in der Seite; Weg über dem kürzesten p95 4,0 ms. Ob das Glätten (10 s, 4 ms, ein Achtel) damit ruhig bleibt: **nicht geprüft** (H3).
  - Gang gegen die Medienzeit am Studio-PC −5,2 ms/min über rund 3 min; **über 60 min nicht gemessen**.
  - Verhalten mit «shutdown»: **nicht geprüft**.
- **Weiter offen:**
  - aus b3, in Teil 1 nicht bearbeitet: Neustart von PipeWire (Zustand `no_pipewire` bis zum Neustart des Kerns), `dss-cli einstellungen` ohne Ausgang und Versatz, `delay` an einem echten Ausgang und Vorzeichen von `versatz_ms` (H2).
  - «Abweichung zuletzt» in der Zeile «Ton: Stopp …» ist kurz nach einem Start oder Einstieg nicht aussagekräftig: Der Mischer rechnet sie im ersten Abruf gegen die alte Lage (im Test bis −945 ms, 11 ms nach einem Einstieg). Für H3 zählen die Zeilen «Ton: Lage …». Zu entscheiden bis b6: im Mischer beim Start auf 0 setzen oder in der Zeile weglassen.
  - `docs/08-kern-schnittstelle.md` («Overlay-Server», `…/clock`, `…/events/<page>`) wird in b6 nachgeführt, wie geplant.

**Ergebnis b4 Teil 2** (2026-11-13, nach M3, Stufe «Klein»: Bau ohne Warnungen; `ctest` 49 von 49, `tst_bildvergleich` unverändert grün; `tst_browserquelle` und `tst_overlayserver` je 20 von 20):

- **Commit 2, Seite** (`web/overlay.js`), nur mit `window.obsstudio`:
  - Beim Laden Kennung `page` aus 12 Zeichen `[a-z0-9]` (`crypto.getRandomValues`), Ereignisse über `events/<page>`. Meldungen mit `fetch('clock', {method: 'POST', keepalive: true})`, `Content-Type: text/plain`, `seq` ab 0, `now_ms` beim Absenden.
  - Bis zum ersten `obsSourceActiveChanged` ist der Zustand unbekannt: Videos laufen wie ohne OBS, **keine Meldungen**. Grund: M3 zeigt, dass beim Laden einer schon aktiven Quelle kein «aktiv» kommt; die Seite weiss dann nicht, ob sie im Programm ist, und ein Ton aus einer verborgenen Quelle wäre falsch.
  - «aktiv»: `run` + 1, alle Videos anhalten, zurück auf den Anfang, sichtbar, abspielen. Gezählt wird erst nach `seeked`: Im Test kam sonst als «start» noch ein Bild von vor dem Zurückspulen (Lage 1,27 s statt 0). Dann je Video `start` beim ersten Bild, `frame`, sobald `expectedDisplayTime` 1000 ms nach der letzten Meldung des Videos liegt, `loop` (Durchgang + 1), wenn `mediaTime` kleiner ist als beim vorigen Bild, `end` bei `ended` ohne Wiederholen. `duration_s`, sobald bekannt, auch bei `end`.
  - «inaktiv»: alle Videos anhalten, zurück auf den Anfang (erstes Bild bleibt stehen), `stop` ohne `source`. Videos, die in dieser Zeit neu dazukommen (Quelle eingeblendet, Ausgabe ein, neue Datei), warten am Anfang. Derselbe Zustand zweimal hintereinander ändert nichts.
  - Neue Datei, «Wiederholen» oder «Am Ende anhalten» geändert: neues Element wie bisher, von vorne, mit `start` und Durchgang 0.
  - Ohne `window.obsstudio`: unverändert (`events` ohne Kennung, keine Meldungen).
- **Overlay-Server:** `…/events/<page>` mit ungültiger Adresse bekommt jetzt «gone» wie `…/events`. In Teil 1 kam 404; die Seite in OBS hätte nach einem neuen Token oder Umbenennen nie neu geladen. Test in `tst_overlayserver` («oldAddressesBecomeInvalid») und `tst_browserquelle`.
- **Test** `tst_browserquelle`, neu «obsPage»: eigenes Overlay mit `tests/daten/ton/blitz-piep.mp4` (60 s, ohne Wiederholen) und `drehung-0.mp4` (10 Bilder, wiederholt), zweite Ansicht mit `window.obsstudio` (über `QWebEngineScript` vor den Skripten der Seite) und `CustomEvent('obsSourceActiveChanged')`. Geprüft: ohne Ereignis keine Meldungen; Inhalt und Folge (`v`, `page`, `seq` ohne Lücke, `run`, `loop`, `file`, `media_s` < 0,2 s bei `start` und `loop`, Abstand der `frame` 1000 bis 1500 ms, `duration_s`), `keepalive`, `text/plain`, Antwort 204 des Kerns; «inaktiv»: `stop` ohne `source`, Videos angehalten bei 0, danach keine Meldung; wieder «aktiv»: Lauf 2 mit `start` bei 0; neue Datei (in der Seite nachgebildet, weil der Kern die Datei eines Videos heute nicht ändert): `start` mit der neuen Datei, danach `end`; keine abgewiesene Meldung im Protokoll des Kerns; die Ansicht ohne `window.obsstudio` hat nichts gemeldet; neuer Token: die Seite mit `events/<page>` lädt neu und ist leer. Gemessen im Test (Qt WebEngine, Chromium 122, 2026-11-13): `frame` nach 1029,5 und 1033,2 ms, Lage + 0,967 und + 1,033 s; in 2,6 s 12 Meldungen (langes Video 3, kurzes 9 mit 8 Wiederholungen).
- **Rund 20 Bildaufrufe je Sekunde in OBS (M3):** für die Meldungen ohne Bedeutung. `frame` braucht einen Aufruf je Sekunde; jede Meldung trägt `mediaTime` und `expectedDisplayTime` desselben Bildes. Fällt das erste Bild nach dem Start oder Wiederholen aus, meldet die Seite das nächste mit seiner Lage (`media_s` dann bis 1 Bild grösser), der Kern setzt den Ton an diese Lage.
- **Rückfall beim Laden einer schon aktiven Quelle: nicht gebaut, `[OFFEN]`** (Stand 13.11.; gebaut am 14.11., siehe «Ergebnis Rückfall beim Laden einer aktiven Quelle»). Im Kern fehlt alles dafür: Er fragt `GetSourceActive` nicht, abonniert `InputActiveStateChanged` nicht (eigenes Bit, hohes Datenaufkommen) und hat kein Ereignis an die Seite. Folge heute: Nach dem Neuladen einer Quelle, die im Programm ist, läuft das Video ohne Ton bis zum nächsten Wechsel aus dem Programm und zurück. Ob OBS beim Start mit der Szene im Programm «aktiv» meldet, ist nicht geprüft. Vorschlag (eigener Schritt, nicht in b5): Der Kern fragt beim Beginn von `…/events/<page>` mit `GetSourceActive` die Browserquelle des Overlays ab (Name aus `src/obs/obssources`) und schickt der Seite ein Ereignis (z. B. «obs_active» mit `active`); die Seite behandelt es wie «aktiv»/«inaktiv», solange ihr Zustand unbekannt ist. Mehrere Browserquellen mit derselben Adresse und verschachtelte Szenen sind dabei zu klären.
- **Einmal beobachtet:** Im dritten Testlauf während der Entwicklung (schon mit `seeked`, noch mit `QTRY_VERIFY_WITH_TIMEOUT` als Warten) war 3 s nach dem zweiten «aktiv» noch eine Meldung ohne Antwort; nach Angabe von QtTest kam die Antwort zwischen 3 und 6 s. Ursache nicht geklärt (langsame Antwort des Kerns oder bei jeder Abfrage eine gerade neue Meldung, nicht unterschieden); danach 1 Lauf, 20 von 20 Läufe und `ctest` ohne Fehler. Der Test gibt in diesem Fall die offenen Meldungen und die Ton-Zeilen des Kerns aus.
- **Für b5 offen:** nichts aus b4, das b5 aufhält (die Seite bleibt stumm, `volume`/`muted` betreffen nur den Kern). Weiter offen für später: Rückfall oben (erledigt 14.11., Abschnitt unten); Handtest der Seite im echten OBS (Lotse, Szene «test 2»: Protokoll des Kerns «Ton: Start»/«Ton: Stopp» beim Wechsel, `stop` beim Wechsel hinaus); Ursache der rund 20 Aufrufe je Sekunde; Gang über 60 min; «Abweichung zuletzt» (bis b6); `docs/08-kern-schnittstelle.md` (b6).

**Ergebnis Rückfall beim Laden einer aktiven Quelle** (2026-11-14, Auftrag von Lotse nach M3, Stufe «Klein»: Bau ohne Warnungen; `ctest` 49 von 49; `tst_browserquelle`, `tst_overlayserver` und `tst_obsquellen` je 20 von 20 mit `ctest --repeat until-fail:20`):

- **Ablauf:** Beginnt `…/events/<page>` (Signal `pageOpened`, auch beim Wiederverbinden der Seite mit derselben Kennung), fragt der Kern `ObsSources::queryActive` (neu, `src/obs/obssources`). Kennt er die Antwort, schickt er über `OverlayServer::sendPageEvent` nur an diese Seite das Ereignis `obs_active` mit `{"active": true|false}`. `web/overlay.js` behandelt es wie «aktiv»/«inaktiv» (`setActive`), aber nur, solange der eigene Zustand unbekannt ist (`clock.active === null`). Ein Ereignis aus OBS (`obsSourceActiveChanged`) hat damit immer Vorrang, auch wenn es vor der Antwort des Kerns kommt.
- **Abfrage:** `GetSourceActive` mit `sourceUuid`, ausgewertet wird `videoActive` (im Programm), nicht `videoShowing` (auch Vorschau). Das passt zur Annahme «aktiv heisst im Programm» (unten). Kein Abo von `InputActiveStateChanged`: Eine Abfrage beim Beginn der Ereignisse genügt, danach meldet OBS der Seite jeden Wechsel selbst (M3).
- **Klärung 1, mehrere Browserquellen mit derselben Adresse:** Gefragt werden die zugeordnete Quelle und alle anderen Browserquellen mit genau derselben Adresse aus dem letzten Abgleich (wie die Übernahme nach F10). **Aktiv, wenn eine davon aktiv ist.** Inaktiv nur, wenn alle antworten und keine aktiv ist. Die Seite selbst kann nicht sagen, zu welcher der Quellen sie gehört; ist eine andere Kopie im Programm, spielt der Ton trotzdem nur einmal je Videoquelle (Regel «die erste Seite führt», b4 Teil 1).
- **Klärung 2, verschachtelte Szenen:** `videoActive` gilt für die Quelle als Ganzes, also auch, wenn sie über eine verschachtelte Szene ins Programm kommt. Laut Protokoll von obs-websocket, im echten OBS mit verschachtelter Szene **nicht geprüft**.
- **Klärung 3, OBS nicht verbunden:** kein Ereignis, Verhalten wie zuvor (Zustand unbekannt, Video läuft ohne Meldungen, kein Ton). Dasselbe, wenn der erste Abgleich mit OBS noch fehlt, es keine Browserquelle mit dieser Adresse gibt oder OBS zu einer Quelle nicht antwortet und keine aktiv ist. Protokoll des Kerns je Seite eine Zeile:
  - «OBS: Seite <Kennung> von «<Overlay>»: Quelle im Programm (n Quellen gefragt), obs_active geschickt» bzw. «… nicht im Programm …»;
  - «OBS: Seite <Kennung> von «<Overlay>»: Zustand im Programm unbekannt (OBS nicht verbunden | Quellen in OBS noch nicht abgeglichen | keine Browserquelle in OBS | OBS hat zu x von n Quellen nicht geantwortet), kein obs_active».
- **Tests:**
  - `tst_obsquellen` «queryActive» (Attrappe `tests/obstestserver.h` mit `GetSourceActive`): nicht verbunden, inaktiv, aktiv, zweite Quelle mit derselben Adresse (2 gefragt, aktiv, wenn die Kopie aktiv ist, Zuordnung bleibt), keine Antwort, ohne Quelle (keine Anfrage).
  - `tst_overlayserver` «pageEventOnlyToThatPage»: nur die Seite mit der Kennung bekommt `obs_active`, nicht eine zweite Seite und nicht `…/events` ohne Kennung.
  - `tst_browserquelle` «obsActiveFallback» (Kern als Prozess, Quelle über den Kern in der Attrappe angelegt): Quelle aktiv → Seite ohne Ereignis aus OBS «aktiv», Lauf 1 mit zwei `start` bei Lage < 0,2 s; danach gilt «inaktiv» aus OBS wie gewohnt. Quelle inaktiv → «inaktiv», Videos bei 0 angehalten, nur `stop`. Antwort 1,5 s verzögert und vorher «aktiv» aus OBS → `obs_active` (inaktiv) wird übergangen, Videos laufen, kein `stop`. «obsPage»: ohne OBS kein `obs_active`, Zustand bleibt unbekannt.
- **Abweichung vom Vorschlag:** keine. Zusätzlich: Wartezeit im Test mit Ereignisschleife (`waitForCoreLog`), weil die Attrappe im selben Prozess läuft.
- **`[OFFEN]`:**
  - Startet OBS mit der Szene im Programm, lädt die Browserquelle womöglich, bevor OBS mit dem Kern verbunden ist. Dann kommt «OBS nicht verbunden», die Seite bleibt bis zum nächsten Wechsel stumm. Ob das so geschieht, ist **nicht geprüft**. Möglicher Ausbau, nicht gebaut: beim Verbinden mit OBS die offenen Seiten mit unbekanntem Zustand noch einmal fragen.
  - Verhalten im echten OBS (Handtest durch Lotse, Szene «test 2») und mit verschachtelter Szene: **nicht geprüft**.
  - `docs/08-kern-schnittstelle.md` (`obs_active` in «Overlay-Server») wird mit b6 nachgeführt, wie `…/clock`.

### b5 – Overlay-Format und Fenster (3 Commits)

1. Overlay-Format 5: `video.volume` (0–100, ganze Zahl) und `video.muted`, neue Quellen 100 und `false` (Antwort 3 des Nutzers). Umwandeln 4 → 5 (`dss-core --format-pruefen`/`--umwandeln`), Tests `tst_datenformat`, `tst_overlaymodel`. K-07 gilt hier nicht: Es ändert sich nichts am Bild, die Seite bleibt stumm.
2. Dock «Eigenschaften», Bereich «Video»: Regler «Lautstärke» 0–100 % und Kästchen «Stumm». Test `tst_fensterbearbeiten`.
3. Dock «Steuerung»: Zeile für den Ton der Videos mit Ausgang, Versatz und Hinweis bei fehlendem Ausgang (Antwort 2 des Nutzers, Aussehen nach Entscheid 6 vom 2026-11-12: Zeile «Ton Videos», Auswahlliste der Ausgänge mit erstem Eintrag «Kein Ausgang», Feld «Versatz» in ms von −500 bis +500, Schritt 1, Start 0, Hinweis «Ausgang fehlt» wie in der Zeile «OBS», ein neuer Kern hat «Kein Ausgang»). Test `tst_fensterbearbeiten`.

Vor Commit 2 erfasst der Agent, wie die bestehenden Docks Zeilen, Felder und Meldungen bauen (z. B. Zeile «OBS» im Dock «Steuerung», `src/app/docks.cpp`, `src/app/theme.cpp`), und folgt dem.

**Ergebnis b5** (2026-11-14, Agent; Lotse geprüft: Bau ohne Warnungen, `ctest` 49 von 49; nach der Übergabe):

- **Commit 1:** Overlay-Format 5 mit `video.volume` und `video.muted`, Schnittstelle 13 (`changes.video.volume`/`muted`), Umwandeln 4 → 5.
- **Commit 2:** Dock «Eigenschaften», Bereich «Video»: Regler «Lautstärke» 0–100 % und Kästchen «Stumm».
- **Commit 3:** Dock «Steuerung», Zeile «Ton Videos» mit Ausgang («Kein Ausgang» zuerst), Versatz −500 bis +500 ms und Zustand («Ausgang fehlt»).
- **Für b6 und später** (aufgenommen in `docs/04-offene-punkte.md`, Punkte 103 bis 109): `[OFFEN]` Nutzer, Ausgang und Versatz sind auch in «nur Anzeige» bedienbar (wie die Einstellungen; Punkt 103); Regler ohne eigenen Stil im Thema (104); `dss-cli` zeigt Lautstärke, Ausgang und Versatz nicht (Lautstärke und Stumm in b6 ergänzt, Ausgang und Versatz Punkt 108); Lautstärke linear, nach Gehör nicht geprüft (109). Handtest des Nutzers (Regler, Stumm, Ausgang, «Ausgang fehlt», Versatz) nach der Installation des Teststands.

### b6 – Prüfung und Dokumente (2 Commits)

1. `scripts/tonpruefung.py`: virtuelle Senke anlegen, Kern aus dem Entwicklungsordner starten, Meldungen nachbilden, Aufnahme über `pw-loopback`, Abweichung zu den Meldungen und zur gemeldeten Verzögerung auswerten, Senke entfernen. 20 Läufe, unhörbar.
2. Dokumente nachführen: `01`, `02` (Pakete), `03` (E-20, E-21), `04`, `06`, `08`. Dazu der Bericht `docs/archiv/berichte/P12-bericht.md`.

**Ergebnis b6** (2026-11-14, Agent, Stufe «Klein»: Bau ohne Warnungen, `ctest` 49 von 49, keiner übersprungen; Einzelheiten im Bericht P12):

- **`scripts/tonpruefung.py`:** `pw-loopback` mit Senke `dss_tonpruefung_ausgabe` und Quelle `dss_tonpruefung_aufnahme`; Kern aus `build/` mit Daten-, Konfig- und Laufzeitordner im Temp-Ordner, Ausgang über `[ton-video] ausgang=` in der eigenen `kern.ini`; Overlay mit `blitz-piep.mp4` über `dss-cli`; Seite nachgebildet (`…/events/<page>`, `…/clock` mit `start`, `frame` jede Sekunde, `stop` 0,5 s nach der letzten vollen Sekunde); Aufnahme mit `pw-cat --raw` über stdout. Sicherung vor jedem Lauf: Abbruch, wenn ein virtueller Knoten anders verbunden ist als Kern → Senke und Quelle → `pw-cat` oder wenn sich Standardausgang oder -eingang ändern. Aufräumen auch bei Abbruch, danach nachgeprüft. Exit-Code 0 bestanden, 1 nicht bestanden, 2 Abbruch, 3 Aufräumen unvollständig.
- **Lauf** `python3 scripts/tonpruefung.py --out build/tonpruefung/b6-20` (20 Läufe zu 12 s, 18:31–18:36): bestanden. 280 Meldungen mit 204; 120 von 120 Piepen ab k = 1, keiner zusätzlich; Abweichung zu den Meldungen Median −21,03 ms, p5 −21,07, p95 −20,93, min −21,11, max −20,76 (Streuung p5–p95 0,14 ms, max − min 0,35 ms); Abweichung + `size`/rate (21,33 ms) Median 0,30 ms; Gang −0,013 ms/min; Piepe 33,31 ms. Kern: 20 Starts und 20 Stopps, 0 «Nachführen», 0 Abrufe ohne Puffer, 0 abgebrochene Zyklen, `delay` 0, Exit-Code 0. Danach `wpctl status` ohne `dss_tonpruefung…`, `pgrep -a pw-loopback` ohne Treffer.
- **Lesart:** −21 ms ist ein Zyklus von PipeWire. Der Kern rechnet den hörbaren Zeitpunkt einen Zyklus nach dem Abruf (Gerät), die virtuelle Senke gibt die Proben im selben Zyklus weiter. Am echten Ausgang misst das H2.
- **`dss-cli quelle-liste`:** neue Spalten «Lautstärke» und «Stumm» (eigener Commit, Test `tst_medien` «cliHoldAtEnd»).
- **Dokumente:** `docs/01`, `02`, `03`, `04` (Punkte 103 bis 112), `06` (Format 5), `08` (Version 12 und 13, `…/clock`, `obs_active`, Abschnitt «Ton der Videos»), Bericht P12.
- **Offen:** Handtest (Abschnitt 4). Leistung mit 3 Videos mit Ton (unten) `[OFFEN]`.
- **Prüfstufe «Baustein»** gelaufen 2026-11-14, Ergebnis im Bericht P12, Abschnitt «Prüfstufe Baustein (2026-11-14)»: Bau, `ctest` 49 von 49, frischer Klon 49 von 49, Startprüfung je 20 von 20, Dienstprüfung bestanden, Ton-Prüfung bestanden (Streuung p5–p95 0,14 ms), 50 Wiederholungen je 50 von 50, Bau mit `DSS_CORE_AUDIO=OFF` ohne Warnung. Leistung nicht geprüft: `scripts/leistung.py` misst nur `dss-app` und spielt keinen Ton; braucht eine Erweiterung (Entscheid Lotse).

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

**Vor b4** (Fragen 5 und 6 beantwortet am 2026-11-12, siehe «Entscheide des Nutzers (2026-11-12, vor b4)»):
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
| Ob «aktiv» kommt und ob es beim Laden einer schon aktiven Quelle gemeldet wird | M3 (2026-11-13): kommt beim Wechsel, nicht beim Laden einer aktiven Quelle. Rückfall `GetSourceActive` über den Kern gebaut 2026-11-14 («Ergebnis Rückfall beim Laden einer aktiven Quelle»); im echten OBS nicht geprüft, OBS-Start mit Szene im Programm `[OFFEN]` |
| Bildrate von OBS (Messgenauigkeit ±½ Bild) | M3 (2026-11-13): 30/1, die Testdaten bleiben |
| RØDECaster stand nachts nicht in der Geräteliste; Name des Geräts in PipeWire unbekannt | N1 vor H2; Ausgang wird nach Namen gewählt, nie geraten |
| Studio-Modus: Vorschau zeigt das erste Bild angehalten | Annahme, siehe unten; Nutzer kann im Handtest korrigieren |
| Ton und Bild mit verschiedener Länge beim Wiederholen (Pause rund 58,6 ms, M2) | Kern wiederholt nach der Länge des Videos, die die Seite meldet; Test in `tst_tonmischer` |
| Gang zwischen Videouhr (Streaming-PC) und Audiouhr | laufendes Nachführen; M2 (−38,8 ppm am Studio-PC), M3 (−86,8 ppm gegen das Eintreffen am Studio-PC, rund 3 min), H3 |
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
| b4: Uhr der Seite über den kleinsten Abstand der letzten 10 s, Glätten bis 4 ms mit einem Achtel (Agent, 2026-11-12) | Laufzeit im Netz nur nach oben gestreut; Glätten nimmt die Streuung heraus und folgt einem Gang von 40 ppm mit etwa 0,3 ms Rückstand; echte Sprünge (Video hängt) gelten sofort |
| b4: Grenze je Seite 40 am Stück, 20 je Sekunde, 64 Seiten (Agent) | eine Seite mit mehreren Videos meldet je Video etwa einmal je Sekunde; Schutz gegen eine fehlerhafte Seite im Heimnetz |
| b4: Kennung der Seite auch in `…/events/<page>` (Agent) | Stopp bei «SSE-Ende» braucht die Zuordnung Verbindung–Seite |
| b4: Einstieg mitten im Video (Agent) | Ton kommt nach 3 s ohne Meldung, Wiederverbinden oder Neustart des Kerns ohne neues «aktiv» zurück |
| b4: Ersatzausgang über die Testoption `--ersatzausgang` und die Schnittstelle `AudioOutput` (Agent) | `tst_kernton` prüft den echten Kern als Prozess ohne PipeWire; `PwOutput` bleibt unverändert |
| b4 Teil 2: Zustand «unbekannt» bis zum ersten Ereignis aus OBS, ohne Meldungen (Agent, 2026-11-13) | M3: beim Laden einer aktiven Quelle kein «aktiv»; lieber stumm als Ton aus einer verborgenen Quelle. Das Bild bleibt wie heute (Video läuft) |
| b4 Teil 2: `frame` nach der Zeit des Bildes (`expectedDisplayTime`, 1000 ms seit der letzten Meldung des Videos), auch `loop` setzt die Zeit zurück (Agent) | eine Lagemeldung je Sekunde und Video genügt dem Kern; kurze wiederholte Videos bleiben unter der Grenze von 20 je Sekunde |
| b4 Teil 2: erst nach `seeked` zählen (Agent) | im Test kam sonst ein Bild von vor dem Zurückspulen als «start» |
| b4 Teil 2: «inaktiv» lässt das erste Bild sichtbar stehen (Agent) | wie die Annahme zur Vorschau oben; im Programm ist die Quelle dann ohnehin nicht zu sehen |
| b4 Teil 2: Fehler «404 statt gone» bei `…/events/<page>` im selben Commit wie die Seite (Agent) | ohne die Korrektur lädt die neue Seite nach neuem Token nicht neu; dieselbe Funktion, eine Zeile |
| Rückfall (14.11.): Abfrage bei jedem Beginn von `…/events/<page>`, auch beim Wiederverbinden (Agent) | einfach und billig (eine Anfrage je Quelle); die Seite übergeht die Antwort, wenn sie ihren Zustand schon kennt |
| Rückfall: `videoActive`, nicht `videoShowing` (Agent) | «aktiv» heisst im Programm (Annahme oben); eine Quelle nur in der Vorschau bleibt inaktiv |
| Rückfall: mehrere Quellen mit derselben Adresse, aktiv, wenn eine aktiv ist (Agent, Vorschlag Lotse) | die Seite kennt ihre Quelle in OBS nicht; lieber Ton aus einer Kopie im Programm als gar keiner |
| Rückfall: unbekannt statt «inaktiv», wenn OBS zu einer Quelle nicht antwortet und keine aktiv ist (Agent) | ohne vollständige Antwort ist «inaktiv» nicht belegt; die Seite bleibt wie vor dem Rückfall |
| Rückfall: Ereignis in Commit 2 zusammen mit Seite und Server (Agent, Regel 8) | eine Funktion: Server, Kern und Seite tragen nur zusammen; die Abfrage in `ObsSources` ist ein eigener Commit |

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
- **Nachführung 2026-11-12 (b4 Teil 1), lokal gelesen:** `prototypes/ton/messseite.html`, `folgen.{h,cpp}` und `meldeserver.cpp` (Form der Meldungen, Abbilden der Uhr), `docs/archiv/messungen/P12a-daten/2026-11-11-0343/m1-liste.txt`, `docs/archiv/messungen/P12b-daten/2026-11-11-2304/z3a-liste.txt`, `docs/archiv/messungen/P12a-bericht.md` (M1a, Z3). Nicht abgerufen (kein Webzugriff): https://wicg.github.io/video-rvfc/, https://github.com/obsproject/obs-browser/blob/master/README.md.
- **Nachführung 2026-11-13 (b4 Teil 2), lokal gelesen:** `build/p12a-m3/m3-zusammenfassung.txt`, `src/server/clockreport.cpp`, `src/server/overlayserver.cpp` (`handleInvalid`, `handleOverlay`, `handleClock`), `src/core/videoaudio.cpp`, `prototypes/ton/messseite.html` (Name und Form der Ereignisse von obs-browser). Nicht abgerufen (kein Webzugriff): https://github.com/obsproject/obs-browser/blob/master/README.md, https://wicg.github.io/video-rvfc/, https://fetch.spec.whatwg.org/, https://w3c.github.io/webcrypto/#Crypto-method-getRandomValues, https://doc.qt.io/qt-6.8/qwebenginescript.html.
- **Nachführung 2026-11-14 (Rückfall), lokal gelesen:** `src/obs/obssources.{h,cpp}`, `src/obs/obsconnection.h`, `src/server/overlayserver.{h,cpp}`, `src/core/coreservice.{h,cpp}`, `web/overlay.js`, `tests/obstestserver.h`, `docs/archiv/messungen/P12a-bericht.md` (M3: `videoActive: true` beim Neuladen). Nicht abgerufen (kein Webzugriff): https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md (`GetSourceActive`: Felder `sourceName`/`sourceUuid`, Antwort `videoActive`, `videoShowing`; Feldnamen nach M3 und dem Protokoll, nicht gegen die aktuelle Fassung geprüft), https://github.com/obsproject/obs-browser/blob/master/README.md, https://html.spec.whatwg.org/multipage/server-sent-events.html (benannte Ereignisse, `addEventListener`).
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

## Entscheide des Nutzers (2026-11-12, vor b4)
- b4 jetzt ohne M3: nur Commit 1 (`…/clock`) und Commit 3 (Kern, `tst_kernton`). Commit 2 (`web/overlay.js`, Verhalten in OBS) folgt nach M3. Das Format der Meldungen wird so festgelegt, dass Commit 2 es nur noch senden muss; Annahmen zu OBS sind `[OFFEN] nach M3` (ausgeführt, «Ergebnis b4 Teil 1»).
- Frage 5: Ja, H2 ersetzt M4. Der RØDECaster wird nur einmal angeschlossen, im Handtest mit dem fertigen Kern.
- Frage 6: Zeile «Ton Videos» im Dock «Steuerung» mit Auswahlliste der Ausgänge (erster Eintrag «Kein Ausgang»), Feld «Versatz» in ms (−500 bis +500, Schritt 1, Start 0), Hinweis «Ausgang fehlt» wie in der Zeile «OBS», ein neuer Kern hat «Kein Ausgang». Gebaut in b5.
- Frage 7 (Termin M3): im Auftrag an den Agenten kein Termin genannt; M3 liegt jetzt vor Commit 2 von b4.

## Entscheide des Nutzers (2026-11-13, nach M3)
- Verborgene Videos laufen nicht weiter: bei «inaktiv» anhalten, zurück auf den Anfang, `stop` (ausgeführt, «Ergebnis b4 Teil 2»).
- Bekommt ein Video eine neue Datei, startet es von vorne, mit `start` (ausgeführt, «Ergebnis b4 Teil 2»).
