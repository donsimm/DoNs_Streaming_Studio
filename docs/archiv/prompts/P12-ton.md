# P12 – Ton von Videos (Plan)

Plan des Projektleiter-Agenten vom 2026-11-09. Grundlage: Bauplan P12 (E-20, E-21), `docs/01-festlegungen.md` («Ton von Videos», 2026-11-02), Antworten des Nutzers in `docs/archiv/uebergabe-2026-11-08.md` («Antworten Ton (P12)»). Ausführung in Teilen: P12a (Messtest), Halt mit Ergebnis an Lotse, P12b (Bau), Handtest.

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
| G4 | **Q: `QAudioSink`/`QMediaDevices`** | zuerst messen: kein neues Paket |
| G4 | P: PipeWire direkt (`pw_stream`, genaue Verzögerung) | nur, wenn Q die Grenzen aus P12a nicht hält; braucht `libpipewire-0.3-dev` (Nutzer installiert) |

## 2. P12a – Messtest

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

**Umfang (bei Empfehlung S1 und Q):**

| Thema | Vorgabe |
|---|---|
| Wann der Ton läuft | Nur für Seiten mit `window.obsstudio`, also nur in OBS. Gibt es keine Seite, die aktiv ist, gibt es keinen Ton |
| Seite (`web/overlay.js`) | Wird sie in OBS aktiv: Video von vorne. Wird sie inaktiv: anhalten und zurück auf den Anfang. Ohne `window.obsstudio` bleibt alles wie heute |
| Meldungen der Seite | Start, Lage etwa jede Sekunde, Ende, Sprung beim Wiederholen. Neue Adresse `…/clock` (POST, JSON, eigene Grenze je Seite) |
| Mischer im Kern | Ein ständig offener Ausgang mit eigenem Mischer im Kern, eigener Thread |
| Nachführen | Abweichung unter 20 ms weich mit `swr_set_compensation`; darüber Sprung (Stille einfügen bzw. Proben verwerfen) |
| Stopp | sofort, wenn der Kern Ausgabe aus, verborgene, gelöschte oder geänderte Quelle sieht, oder wenn die SSE-Verbindung der Seite endet. Ohne Meldung nach 3 s ebenfalls Stopp |
| Mehrere Browserquellen auf dasselbe Overlay | Ton einmal je Videoquelle, nach der ersten aktiven Seite |
| Editor | bleibt stumm |

**Commits (Regel 8):**
1. `src/media/audiodecoder.{h,cpp}`: Ton einer Videodatei, float, 48 kHz, Stereo (libavcodec und libswresample), ohne Qt Gui. Test `tst_tondecoder` mit `tests/daten/ton/`. `libswresample` und `qt6-multimedia-dev` in `scripts/pakete.txt`.
2. `src/audio/mixer.{h,cpp}`, nur Logik: Quellen auf Probenlage mischen, Lautstärke, Start/Stopp, Wiederholen, Nachführen nach Meldungen. Test `tst_tonmischer` mit gedachter Uhr, ohne Gerät.
3. `src/audio/audiooutput.{h,cpp}`: dünne Schicht über `QAudioSink`/`QMediaDevices` in eigenem Thread; Liste der Ausgänge, gewählter Ausgang, fehlender Ausgang. Schalter `DSS_CORE_AUDIO` (aus = Kern ohne Ton, Stand P11). Test mit Ersatzausgang.
4. Kern: Adresse `…/clock` im Overlay-Server, Anbindung an den Mischer, `kern.ini` `[ton] ausgang`, `[ton] versatz_ms`, Schnittstelle Version 12 (`audio_outputs`, Felder in `settings`).
   - Tests: `tst_overlayserver` (`clock`, Grenzen), `tst_kerneinstellungen` (`[ton]`), neu `tst_kernton` (Kern mit Ersatzausgang: Start, Stopp bei Ausgabe aus / SSE-Ende / 3 s ohne Meldung, Nachführen).
5. Seite: Verhalten in OBS und Meldungen.
   - `tst_browserquelle` mit nachgebildetem `window.obsstudio` und `CustomEvent`: neu von vorne bei «aktiv», Anhalten bei «inaktiv», Inhalt der Meldungen, keine Meldungen ohne `obsstudio`.
   - `tst_bildvergleich` muss unverändert bestehen.
6. Overlay-Format 5: `video.volume` und `video.muted`, je nach Frage 3. Umwandeln 4 → 5 (`dss-core --format-pruefen`/`--umwandeln`), Tests `tst_datenformat`, `tst_overlaymodel`. K-07 gilt hier nicht: Es ändert sich nichts am Bild, die Seite bleibt stumm.
7. Fenster:
   - Dock «Eigenschaften», Bereich «Video»: Lautstärke und «Stumm» (Frage 3);
   - Dialog «Einstellungen», Bereich «Ton»: Ausgang, Versatz, Hinweis bei fehlendem Ausgang (Frage 2);
   - Test `tst_fensterbearbeiten`.
8. Dokumente nachführen: `01`, `02` (Pakete), `03` (E-20, E-21), `04`, `06`, `08`. Dazu der Bericht `docs/archiv/berichte/P12-bericht.md`.

**Prüfstufe «Baustein»** (Befehle wie in P11, Ordner `build/pruefung-p12/`):
- `cmake --build build -j8`
- `ctest --test-dir build`: X11-Sitzung aktiv, 0 übersprungen
- frischer Klon (Bau und `ctest`)
- `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p12/start/startpruefung`
- `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p12/dienst`
- neu `scripts/tonpruefung.py`: Null-Senke anlegen, Kern aus dem Entwicklungsordner starten, Meldungen nachbilden, Überwachung aufnehmen, Abweichung zu den Meldungen auswerten, Senke entfernen. 20 Läufe, unhörbar.
- **50 Wiederholungen** (Takt, Threads und Video betroffen): `tst_tonmischer`, `tst_kernton`, `tst_overlayserver`, `tst_browserquelle`, `tst_bildvergleich`. Ohne Last daneben, nie zwei Fenstertests gleichzeitig.
- Leistung: `scripts/leistung.py` mit 3 Videos mit Ton. Ziel: höchstens 3 Prozentpunkte eines Kerns mehr CPU, 0 Aussetzer in 30 min.

## 4. Handtest (Regel 10)

**Prüft Lotse** (nicht während eines Streams, vorher Prozess und Ausgabe von OBS prüfen):
- **H1 Start und Stopp in OBS.** Warum: Nur OBS meldet «aktiv». Testquelle mit «Blitz und Piep» in «test 2». Szene 5× wechseln: Der Kern meldet im Protokoll Start und Stopp, `GetSourceScreenshot` zeigt nach dem Aktivieren den Zähler bei 0. Danach Szene zurück auf «Szene», Testquelle löschen.
- **H2 Versatz** (nur mit Frage 4 A). Warum: genaue Zahl statt Hörurteil. Aufnahme 60 s, Auswertung wie M4. Ziel: Mittel innerhalb ±20 ms nach eingestelltem Versatz. Aufnahme löschen.
- **H3 Dauerlauf**, nachts, 60 min mit wiederholtem Video in «test 2». Warum: Gang zwischen zwei Rechnern. Ziel: kein Aussetzer, Abweichung im Protokoll immer < 20 ms.
- **H4 Bildschirmfotos** des Bereichs «Ton» im Dialog und des Bereichs «Video» in «Eigenschaften» (Ablage nur im Scratchpad).

**Prüft der Nutzer:**
- **N1 Anschluss.** Warum: Kabel und Gerät. USB bzw. Kabel nach Frage 1, Kanal am RØDECaster einrichten.
- **N2 Hören.** Warum: Nur der Nutzer hört.
  - Ton sauber, ohne Brummen oder Knacken, am richtigen Regler.
  - Nicht doppelt.
  - Mit einem echten Video wirkt der Ton synchron.
- **N3 Bedienung.** Warum: Bedienung und Gerät.
  - Ausgang in «Einstellungen» wählen.
  - Lautstärke bzw. «Stumm» an einer Quelle.
  - **Eigener Schritt:** USB-Kabel des Ausgangs ziehen. Erwartet: Hinweis, kein Ton aus einem anderen Gerät. Danach wieder einstecken.

Was Lotse oder der Agent schon geprüft haben, wiederholt der Nutzer nicht.

## 5. Pakete (Regel 5)

- **Kein neues Paket für P12a und P12b** bei Variante Q:
  - `qt6-multimedia-dev` 6.8.2-8, `libswresample-dev` 7:7.1.5-0+deb13u1 und `pipewire-bin` 1.4.2-1 sind installiert.
  - Die beiden ersten kommen in `scripts/pakete.txt`.
- Nur bei Variante P: `libpipewire-0.3-dev` (Version vor der Installation mit `apt-cache policy` lesen). Begründung: genaue Verzögerung über `pw_stream`, Kern ohne Qt Gui. `sudo` gibt der Nutzer ein.

## 6. Schätzung

| Teil | Agent | Lotse | Nutzer |
|---|---|---|---|
| P12a | 1 Sitzung, ~3–4 h (dazu 60 min Nachtlauf M2) | ~1 h (M3), dazu 60 min nachts | ~30 min (M4: Anschluss, Mischpult, Hören) |
| P12b | 2 Sitzungen à ~4–5 h (Commits 1–4, 5–8), 50er-Läufe nachts ~2 h | Handtest ~1 h, dazu 60 min nachts | ~20 min |

## 7. Fragen an den Nutzer (gebündelt, höchstens 4)

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
| Alle Aussagen zu OBS, obs-browser, obs-websocket, RØDE, Grenzwerten und Qt Multimedia unter Linux nicht gelesen (Webzugriff gesperrt) | P12a, Schritt 0 |
| RØDECaster-Modell und ob es einen zweiten USB-Anschluss hat | `[OFFEN]`, Frage 1 |
| Audiokarten des Studio-PCs | `[OFFEN]` (`/proc/asound/cards` nicht lesbar), liest der Agent in M1 |
| Qt Multimedia im Kern zieht Qt Gui mit; ob `QMediaDevices` ohne `QGuiApplication` und im Dienst geht | M1; sonst Variante P |
| `QAudioSink` meldet keine Verzögerung; ändert ein anderes Programm das Quantum von PipeWire, verschiebt sich der Ton | M1 misst es; Nachführen über die Meldungen der Seite; sonst Variante P |
| Ob «aktiv» beim Laden einer schon aktiven Quelle gemeldet wird | M3; Rückfall `GetSourceActive` über den Kern |
| Bildrate von OBS (Messgenauigkeit ±½ Bild) | `[OFFEN]`, Lotse liest `GetVideoSettings` in M3 |
| Studio-Modus: Vorschau zeigt das erste Bild angehalten | Annahme, siehe unten; Nutzer kann im Handtest korrigieren |
| Ton und Bild mit verschiedener Länge beim Wiederholen | Kern wiederholt nach der Länge des Videos, die die Seite meldet; Test in `tst_tonmischer` |
| Gang zwischen Videouhr (Streaming-PC) und Audiouhr | laufendes Nachführen; M2, M3, H3 |
| Brummschleife bei Variante B | M4 durch den Nutzer; dann Variante A oder Trennübertrager (Gerät, Nutzer) |

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
| Neuer Schalter `DSS_CORE_AUDIO` | riskanter Teil: neue Bibliothek und Thread im Dienst (Regel 8) |
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

---

**Offen für Lotse:**
- Die vier Fragen oben gehen an den Nutzer. Frage 1 und Frage 4 braucht es vor M4, Frage 2 und Frage 3 erst vor P12b.
- Webzugriff für den P12a-Agenten freigeben (Schritt 0), sonst bleiben die Aussagen zu OBS und Geräten ungeprüft.

## Antworten des Nutzers (2026-11-09, in der Sitzung)
1. Anschluss: USB an den zweiten USB-Anschluss des RØDECaster (Variante A). Modell und Anschluss im Schritt 0 von P12a bestätigen.
2. Einstellung von Audioausgang und Versatz: **im Dock «Steuerung»**, nicht in «Werkzeuge → Einstellungen». Begründung des Nutzers: Später spielt auch der Chat-Bot weitere Töne aus, auf andere USB-Soundkarten. Darum so planen, dass mehrere Ausgänge (je Zweck/Modul eigener Ausgang) später ohne Umbau dazukommen; P12 baut nur den Ausgang für den Ton der Videos.
3. Lautstärke je Videoquelle: Regler 0–100 % und Kästchen «Stumm», neue Quellen 100 % und nicht stumm.
4. Messtest: Lotse darf in OBS eine Aufnahme von höchstens 60 s in der Szene «test 2» machen, nie während eines Streams, und löscht die Datei nach der Auswertung (in `CLAUDE.md`, «Umgebung», eingetragen).
