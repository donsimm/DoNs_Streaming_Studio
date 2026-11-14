# P12 – Bericht Ton von Videos (E-20, E-21)

Stand: 2026-11-14, nach b6. Plan: `docs/archiv/prompts/P12-ton.md`. Messungen: `docs/archiv/messungen/P12a-bericht.md` (M1, M2, Z0, Z3, M3). Ausgeführt in Teilen: P12a (Messtest), P12b b1 (Prototyp mit `pw_stream`, Halt), b2 bis b6 (Bau), dazu der Rückfall beim Laden einer aktiven Quelle. **Die Prüfstufe «Baustein» und der Handtest stehen aus** (eigene Aufträge); bis hier ist jeder Abschnitt in Stufe «Klein» geprüft.

## Ablauf

| Teil | Inhalt | Ergebnis |
|---|---|---|
| P12a | Testdaten «Blitz und Piep» (`tests/daten/ton/`), Prototyp `prototypes/ton/` mit `QAudioSink`, Nachtlauf M1/M2 (11.11.), Z0 | M2 erfüllt (p95 0,44 ms, −2,3 ms/min). M1: Sprung +42,67 ms beim Wechsel des Quantums auf 2048 → Abbruchbedingung, Entscheid des Nutzers G4 = P (PipeWire direkt) |
| b1 | Prototyp `pw-messen` mit `pw_stream`, Messung Z3 (Nachtlauf 11.11.), Neuauswertung 12.11. | E1 in Lesart «Lage in Proben» erfüllt (Änderung 0 Proben bei allen 6 Wechseln), E2 bis E4 erfüllt. `delay` an der virtuellen Senke immer 0. Entscheid des Nutzers 12.11.: P behalten |
| b2 | Decoder (`src/media/audiodecoder`, `audioring.h`), Mischer (`src/audio/mixer`), ohne Gerät | `tst_tondecoder`, `tst_tonmischer` |
| b3 | Ausgang über PipeWire (`src/audio/pwoutput`), Schalter `DSS_CORE_AUDIO`, `kern.ini` `[ton-video]`, Schnittstelle 12 (`audio_outputs`, `audio_routes`) | `tst_tonausgang` (virtuelle Senke), kein Ausweichen auf ein anderes Gerät |
| b4 Teil 1 | Overlay-Server `…/clock` und `…/events/<page>`, Kern mit Stimmen je Videoquelle, Glätten, Stopp-Regeln, Einstieg, Ersatzausgang | `tst_overlayserver`, `tst_kernton` |
| M3 | Lotse in OBS 32.2.2 (13.11.) | «aktiv» kommt beim Szenenwechsel, nicht beim Laden einer schon aktiven Quelle; Bildrate 30/1 |
| b4 Teil 2 | Seite `web/overlay.js` in OBS: von vorne bei «aktiv», anhalten bei «inaktiv», Meldungen | `tst_browserquelle` («obsPage»), `tst_bildvergleich` unverändert |
| Rückfall | Kern fragt `GetSourceActive` beim Beginn von `…/events/<page>` und schickt `obs_active` | `tst_obsquellen`, `tst_overlayserver`, `tst_browserquelle`; Handtest im echten OBS bestanden (Lotse, 14.11.) |
| b5 | Overlay-Format 5 (`video.volume`, `video.muted`), Schnittstelle 13; Dock «Eigenschaften» (Regler «Lautstärke», «Stumm»); Dock «Steuerung», Zeile «Ton Videos» | Bau ohne Warnungen, `ctest` 49 von 49 (Lotse) |
| b6 | `scripts/tonpruefung.py`, `dss-cli quelle-liste` mit Lautstärke und Stumm, Dokumente, dieser Bericht | unten, «Geprüft» |

## Entscheide des Nutzers

- 2026-11-02: Ton über eine Audiokarte des Studio-PCs und das Mischpult; Browserquelle immer stumm.
- 2026-11-07 (`ton-1`): Der Kern spielt den Ton; Szene aktiv → Video und Ton von vorne; Editor ohne Ton.
- 2026-11-09: USB an den zweiten USB-Anschluss des RØDECaster; Ausgang und Versatz im Dock «Steuerung», je Zweck ein Ausgang; Lautstärke 0–100 % und «Stumm» je Videoquelle; Aufnahme von höchstens 60 s in «test 2» durch Lotse.
- 2026-11-11: G4 = P mit Prototyp und Halt; Z3 auch unter Last; Puffer 40 ms fest; Sprung gilt als gelöst, wenn gemeldet und ausgeglichen; Quantum-Wechsel nachts durch den Agenten.
- 2026-11-12: E1 neu auswerten, P behalten, `delay` am echten Ausgang mit M4; b4 in zwei Teilen; H2 ersetzt M4; Zeile «Ton Videos» (Entscheid 6).
- 2026-11-13: Verborgene Videos laufen nicht weiter (anhalten, zurück auf den Anfang); neue Datei → von vorne.

Die Entscheide stehen auch in `docs/01-festlegungen.md` («Overlay-Daten»).

## Ergebnis je Abschnitt (Kurzfassung)

Einzelheiten mit Messwerten stehen im Plan unter «Ergebnis b1» bis «Ergebnis b6».

- **b1:** Lage in Proben fest (Versatz 25 024 Proben in allen Z3-Läufen), `size` meldet den Wechsel des Quantums; Q (`QAudioSink`) dagegen +2048 Proben. Klick 197: Beim Wechsel auf 256 kann ein kurzes Stück Ton fehlen (Punkt 112).
- **b2:** Weiches Nachführen im Mischer (0,2 %) statt `swr_set_compensation`, weil der Echtzeit-Abruf keinen Speicher anlegen darf.
- **b3:** `pw_time.now` und `pw_stream_get_nsec()` laufen auf `CLOCK_MONOTONIC` wie `steady_clock` (Header und `tst_tonausgang`). Zustände `no_pipewire`, `no_target`, `missing`, `connecting`, `playing`, `error`.
- **b4:** Uhr der Seite über den kleinsten Abstand der letzten 10 s, Glätten bis 4 ms mit einem Achtel; in `tst_kernton` mit Streuung ±3 ms und Laufzeit 0 bis 10 ms 0 Sprünge, 0 weiche Korrekturen.
- **Rückfall:** `videoActive` der Quelle und aller Quellen mit derselben Adresse; die Seite übernimmt `obs_active` nur bei unbekanntem Zustand.
- **b5:** siehe Ablauf; Befunde für b6 sind als offene Punkte 103, 104, 108 und 109 aufgenommen.
- **b6:** Ton durch den ganzen Kern unhörbar geprüft (unten).

## Geändert (b6)

- **Ton-Prüfung:** `scripts/tonpruefung.py` (neu). Virtuelle Senke mit `pw-loopback` (`dss_tonpruefung_ausgabe` → `dss_tonpruefung_aufnahme`), Kern aus `build/` mit eigenem Daten-, Konfig- und Laufzeitordner im Temp-Ordner (`--datenordner`, `--einstellungen`, `--verbindung`, `XDG_CONFIG_HOME`, `XDG_DATA_HOME`), Overlay mit `tests/daten/ton/blitz-piep.mp4` über `dss-cli`, Seite in OBS nachgebildet (`…/events/<page>`, `…/clock`: `start`, `frame` jede Sekunde, `stop`), Aufnahme mit `pw-cat --raw` über stdout, Auswertung je Piep, Aufräumen auch bei Abbruch mit Nachprüfung.
- **dss-cli:** `src/cli/main.cpp` (`quelle-liste` mit den Spalten «Lautstärke» und «Stumm»), Test in `tests/tst_medien.cpp` (`cliHoldAtEnd`).
- **Dokumente:** `docs/01-festlegungen.md` (Entscheide zum Ton, Knoten in PipeWire je Version), `docs/02-umgebung.md` (Pakete `libswresample-dev`, `libpipewire-0.3-dev`, Werkzeuge aus `pipewire-bin`), `docs/03-funktionen-entwurf.md` (E-20, E-21), `docs/04-offene-punkte.md` (Punkte 103 bis 112), `docs/06-overlay-format.md` (Format 5, `video.volume`, `video.muted`, Beispiel), `docs/08-kern-schnittstelle.md` (Version 12 und 13, `audio_outputs`, `audio_routes`, `[ton-video]`, `changes.video.volume`/`muted`, `…/clock`, `…/events/<page>`, `obs_active`, Abschnitt «Ton der Videos»), Plan (`Ergebnis b5`, `Ergebnis b6`), dieser Bericht.

## Geprüft

Stufe «Klein» (Studio-PC, 2026-11-14):

| Befehl | Ergebnis |
|---|---|
| `cmake --build build -j8` | ohne Warnungen |
| `python3 scripts/tonpruefung.py --laeufe 2 --dauer-s 6 --out build/tonpruefung/probe` (Probeläufe vor dem vollen Lauf) | zuerst Abbruch durch die eigene Sicherung (Knoten von `pw-cat` nicht als Aufnahme erkannt), aufgeräumt; dann «nicht bestanden» wegen 2 zusätzlicher Einsätze (Piep genau beim Stopp, siehe Interna); nach beiden Korrekturen bestanden |
| `python3 scripts/tonpruefung.py --out build/tonpruefung/b6-20` (18:31–18:36) | **bestanden**, Werte unten |
| `wpctl status` (danach) | keine Knoten `dss_tonpruefung…`, keine Streams |
| `pgrep -a pw-loopback` (danach) | nichts (Exit-Code 1) |
| `cmake --build build -j8` (nach `dss-cli`) | ohne Warnungen |
| `ctest --test-dir build` (nach `dss-cli`) | 49 von 49 bestanden, keiner übersprungen, 246 s |
| `cmake --build build -j8` und `ctest --test-dir build` (nach den Dokumenten; `tst_overlaystore` liest das Beispiel aus `docs/06-overlay-format.md`) | ohne Warnungen; 49 von 49 bestanden, keiner übersprungen, 246 s |

**Ton-Prüfung, 20 Läufe zu 12 s** (`build/tonpruefung/b6-20/ergebnis.txt`, Last vorher 0,15/0,22/0,15, nichts daneben):

| Grösse | Wert | Grenze |
|---|---|---|
| Meldungen der Seite | 280, alle mit 204 angenommen | alle 204 |
| Piepe ab k = 1 | 120 erwartet, 120 gefunden, 0 fehlen, 0 zusätzlich | alle, keiner zusätzlich |
| Abweichung zu den Meldungen (Einsatz − Start der Seite − 2 s · k, Messweg eingeschlossen) | Mittel −21,02 ms, Median −21,03 ms, p5 −21,07, p95 −20,93, min −21,11, max −20,76 | – |
| Streuung p5–p95 | 0,14 ms | ≤ 15 ms (E4) |
| Streuung max − min | 0,35 ms | ≤ 40 ms (H2) |
| Mittel je Lauf | −21,06 bis −20,84 ms | – |
| Abweichung + `size`/rate (`size` 1024 = 21,33 ms) | Median 0,30 ms, p5 0,26, p95 0,40 | – |
| Gang über 5 min | −0,013 ms/min | – |
| Länge der Piepe | 33,31 ms (alle), Spitze 0,525 | 30 bis 36 ms |
| Kern | Start und Stopp «Seite inaktiv» in 20 von 20 Läufen; «Ton: Lage» 20 Zeilen, Abweichung −0,12 bis 0,02 ms; 0 «Ton: Nachführen», Sprünge 0, weich 0; 0 Abrufe ohne Puffer, 0 abgebrochene Zyklen; `delay` 0, `size` 1024, `queued` 0, `buffered` 0; Ende Exit-Code 0 | – |
| Aufräumen | keine Knoten `dss_tonpruefung…`, `pw-loopback`, `pw-cat` und Kern beendet, Standardausgang und -eingang unverändert, temporärer Ordner gelöscht | – |

Einordnung: Die Abweichung von −21 ms ist genau ein Zyklus von PipeWire (1024 / 48000 = 21,33 ms). Der Kern rechnet den hörbaren Zeitpunkt einen Zyklus nach dem Abruf (Lesart «Rest + Quantum», b1/b2), weil ein Gerät die Proben erst im nächsten Zyklus spielt; die virtuelle Senke gibt sie im selben Zyklus an die Aufnahme weiter. Ohne diesen Zyklus bleiben 0,30 ms Messweg (Pipe von `pw-cat`). Die Streuung des ganzen Wegs Seite → Kern → PipeWire liegt unter 0,4 ms. Was ein echter Ausgang zusätzlich bringt (`delay`, Weg zum Mischpult), zeigt erst H2.

Am Rand: Beim Lauf stand «RØDECaster Pro II Analog Stereo» als Standardausgang in der Liste von PipeWire (`wpctl status`, 18:36); der Ton der Prüfung ging nur in die virtuelle Senke.

**Nicht geprüft** in b6: Prüfstufe «Baustein» (frischer Klon, `startpruefung.py`, `dienstpruefung.py`, 50 Wiederholungen, Leistung, Bau mit `DSS_CORE_AUDIO=OFF`), alles am echten Ausgang und in OBS ausser dem Rückfall.

## Handtest

Kein neuer Handtest aus b6 (Skript, `dss-cli`, Dokumente). Der Handtest von P12 steht im Plan, Abschnitt 4, und folgt nach der Prüfstufe «Baustein». Kurz:

### Prüft Lotse

- **H1 Start und Stopp in OBS** (Szene 5× wechseln, Protokoll und Zähler bei 0). Warum: Nur OBS meldet «aktiv».
- **H2 Versatz** (Aufnahme 60 s in «test 2», 30 Blitze, erster Lauf mit Versatz 0 gibt den Startwert, zweiter mit eingestelltem Versatz). Warum: genaue Zahl statt Hörurteil; ersetzt M4, misst auch `delay` am echten Ausgang.
- **H3 Dauerlauf** 60 min nachts. Warum: Gang zwischen zwei Rechnern, Ruhe des Glättens.
- **H4 Bildschirmfotos** der Zeile «Ton Videos» und des Bereichs «Video». Warum: Aussehen (Punkt 104).

### Prüft der Nutzer

- **N1 Anschluss** (USB an den RØDECaster, Kanal). Warum: Kabel und Gerät.
- **N2 Hören** (sauber, nicht doppelt, synchron, Lautstärke nach Gehör, Punkt 109). Warum: Nur der Nutzer hört.
- **N3 Bedienung** (Ausgang, Versatz, Lautstärke, «Stumm»; eigener Schritt: USB-Kabel ziehen und wieder einstecken). Warum: Bedienung und Gerät.

## Nicht erledigt, `[OFFEN]`

- Prüfstufe «Baustein» (eigener Auftrag, Befehle im Plan, Abschnitt 3, «Prüfstufe «Baustein»»).
- Handtest H1 bis H4, N1 bis N3.
- `[OFFEN]` Nutzer: Ausgang und Versatz auch in «nur Anzeige» bedienbar (Punkt 103).
- Offene Punkte 103 bis 112 in `docs/04-offene-punkte.md`: Regler ohne eigenen Stil, OBS-Start mit Szene im Programm, «Abweichung zuletzt», Neustart von PipeWire, `dss-cli einstellungen` ohne Ausgang und Versatz, Lautstärke linear, `delay` und `versatz_ms` am echten Ausgang, OBS-Fälle (verschachtelte Szenen, «shutdown», 60 min), Klick 197.
- Vor dem Push: Heimnetz-Adresse in `prototypes/ton/LIESMICH.md` bereinigen (aus der Übergabe, nicht Teil von b6).

## Annahmen, Interna-Entscheide (b6)

| Entscheid | Begründung |
|---|---|
| `dss-cli quelle-liste` um «Lautstärke» und «Stumm» ergänzt, eigener Commit | klein und sicher (nur Anzeige, zwei Spalten), im Auftrag erlaubt; eine eigene Funktion (Regel 8) |
| Kern direkt als Prozess, nicht als `systemd-run --user` | Plan b6 verlangt es nicht; so liest das Skript das Protokoll direkt. Der Dienst wird in `dienstpruefung.py` geprüft |
| Overlay über `dss-cli` im Temp-Ordner, Token aus `tokens.json` dort | derselbe Weg wie ein Nutzer ohne Fenster; der Token steht in keiner Datei ausserhalb des Temp-Ordners |
| Aufnahme mit `pw-cat --raw` über stdout statt eines eigenen Programms | kein Bau nötig; die Zeit jedes Blocks beim Eintreffen genügt. Zeit einer Probe = kleinster Wert «Eintreffen − Lage» der Blöcke in ±5 s (unempfindlich gegen Puffer der Pipe und gegen Gang der Uhr von PipeWire) |
| Sicherung: Abbruch bei jeder Verbindung der virtuellen Knoten ausser Kern → Senke und Quelle → `pw-cat`, oder bei geändertem Standardausgang oder -eingang; geprüft vor jedem Lauf | unhörbar und nie ein echtes Gerät. Der Knoten von `pw-cat` trägt keine `application.process.id`; erkannt als Aufnahmestrom «pw-cat» (`Stream/Input/Audio`), der nichts abspielt |
| Stopp der Seite 0,5 s nach der letzten vollen Sekunde | der Kern schreibt etwa 40 ms voraus; ein Piep genau beim Stopp war im Probelauf ganz zu hören und wäre sonst als «zusätzlich» gezählt |
| Piep bei 0 nicht gewertet | er liegt vor der ersten Meldung (die Seite meldet `start` erst nach dem ersten Bild); er ist in 11 von 20 Läufen teilweise zu hören (+2,1 bis +10,6 ms) |
| Grenzen: Streuung p5–p95 ≤ 15 ms (E4 aus b1), max − min ≤ 40 ms (Ziel H2), Länge 30 bis 36 ms (33,3 ms Piep, ohne Lücke) | aus dem Plan übernommen; für den Mittelwert keine Grenze, weil er den Messweg enthält |
| 20 Läufe zu 12 s, 6 gewertete Piepe je Lauf | 120 Einsätze in rund 5 min; jeder Lauf mit eigenem `start` prüft den Einsatz neu |
| Port 32000 bis 32767 von oben, frei geprüft | nie 8080/8081, nicht der Bereich der Tests (24000 bis 31999) |
| Ergebnis unter `build/tonpruefung/<Datum-Zeit>/` | nicht im Repo; Protokoll des Kerns enthält die Adresse im Heimnetz |

## Quellen

- https://docs.pipewire.org/page_man_pw-loopback_1.html, https://docs.pipewire.org/page_man_pw-cat_1.html, https://docs.pipewire.org/page_man_pw-dump_1.html, https://docs.pipewire.org/page_man_pw-cli_1.html (nicht abgerufen, kein Webzugang; Aufrufe wie in `prototypes/ton/pw-lauf.sh`, `--raw` und «-» in b6 auf dem Studio-PC erprobt)
- https://docs.python.org/3/library/time.html#time.monotonic_ns (nicht abgerufen)
- https://html.spec.whatwg.org/multipage/server-sent-events.html (nicht abgerufen)
- Lokal gelesen: `docs/archiv/prompts/P12-ton.md`, `docs/archiv/messungen/P12a-bericht.md`, `tests/tst_kernton.cpp`, `tests/prozesshilfe.h`, `src/core/main.cpp`, `src/core/videoaudio.cpp`, `src/audio/pwoutput.cpp`, `src/common/protocol.cpp`, `src/common/coresettings.cpp`, `src/cli/main.cpp`, `scripts/pakete.txt`, `tests/daten/ton/LIESMICH.md`
- Quellen der früheren Abschnitte: Plan, Abschnitt «Quellen», und Messbericht P12a
