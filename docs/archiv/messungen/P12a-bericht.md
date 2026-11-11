# P12a – Messbericht M1 und M2 (Nachtlauf 11.11.)

Stand: 2026-11-11, nachgeführt mit Z0 (Neuauswertung von M1d, Abschnitt «Z0») und den Entscheiden des Nutzers vom 11.11. Plan: `docs/archiv/prompts/P12-ton.md`, Abschnitt 2 («Messungen»). Alle Messwerte stammen vom Studio-PC (Debian 13, Kernel 6.12.113+deb13-amd64, Qt 6.8.2, Qt WebEngine 6.8.2 mit Chromium 122.0.6261.171, PipeWire 1.4.2-1). Prototyp und Skript: `prototypes/ton/` im Stand des Commits «P12a: Skript für die Nachtläufe M1 und M2». Für diesen Bericht wurde nichts gemessen; alle Zahlen stammen aus den Dateien unter `P12a-daten/` und aus den Last-Rohdaten `build/p12a-nacht/2026-11-11-0343/` (nicht im Repo).

**Entscheide des Nutzers (2026-11-11):**
- G4 = **P** (PipeWire direkt). P12b beginnt mit einem Prototyp mit `pw_stream` (Z3).
- Z0 = ja (ausgeführt, siehe «Z0»).
- M2 wird nicht wiederholt (auch nicht mit Fenster ausserhalb des Bildschirms).
- M3 macht Lotse mit dem Nutzer zusammen.

M3 (OBS, Lotse mit dem Nutzer) und M4 (ganzer Weg, Nutzer und Lotse): **[OFFEN]**, stehen aus.

## Kurzfassung

| Messung | Grenze im Plan | Messwert (voller Lauf) | Urteil |
|---|---|---|---|
| M1, Erwartung | Streuung ≤ 5 ms | p5–p95 «ohne Pufferinhalt», Ruhe: 3,4 / 15,5 / 13,4 / 18,3 ms bei Puffer 10 / 20 / 40 / 80 ms; Spanne 31,8 / 56,2 / 27,3 / 52,5 ms | **nicht erfüllt** (nur 10 ms mit p5–p95) |
| M1, Erwartung | 0 Aussetzer ab 20–40 ms Puffer | Ruhe: 0 Unterläufe bei 10 bis 80 ms. Last: 17 Unterläufe bei 20 ms, 0 bei 40 ms | erfüllt ab 40 ms; bei 20 ms unter Last nicht erfüllt |
| M1, Abbruch | `QMediaDevices` oder `QAudioSink` gehen ohne `QGuiApplication` bzw. im Dienst nicht | gehen: `QCoreApplication` in `systemd-run --user`, Geräteliste und alle 7 Klickläufe ohne Fehler | nicht ausgelöst |
| M1, Abbruch | Aussetzer erst ab Puffer > 100 ms | 0 Unterläufe ab 40 ms, in Ruhe ab 10 ms | nicht ausgelöst |
| M1, Abbruch | Verzögerung springt um > 10 ms, wenn sich das Quantum ändert | **+42,67 ms** (2048 Proben) beim Wechsel auf `clock.force-quantum 2048`, gleichzeitig 1 Unterlauf; der Sprung bleibt bis zum Ende des Laufs. Wechsel auf 256 und zurück auf 0 ohne Sprung. Probelauf gleich | **formal ausgelöst** |
| M2, Erwartung | Schwankung p95 ≤ 5 ms | 0,438 ms | erfüllt |
| M2, Erwartung | Gang ≤ 3 ms/min (≈ 50 ppm) | −2,327 ms/min (−38,78 ppm) | erfüllt |
| M2, Abbruch | `requestVideoFrameCallback` fehlt und `currentTime` streut > 10 ms | Callback vorhanden: 107 886 Meldungen, 0 ausgelassene Bilder | nicht ausgelöst |
| M2, Abbruch | p95 > 20 ms | 0,438 ms | nicht ausgelöst |
| M2, Aufbau | Fenster ausserhalb des Bildschirms | Fenster bei 0,27 (auf dem Bildschirm), Seite meldet «visible» | **nicht wie geplant** |

Wichtigste Befunde:

1. **G4:** Die Abbruchbedingung «Sprung > 10 ms beim Quantum-Wechsel» ist formal erfüllt. Nach Plan heisst das: Halt, Variante P vorschlagen. Einordnung und Empfehlung im Abschnitt «G4». Der Nutzer hat am 11.11. P gewählt.
2. **Die Sprünge von etwa +480 ms unter Last sind ein Fehler der Auswertung**, keine echten Sprünge der Verzögerung. Die Auswertung ordnet einen aufgenommenen Klick dem Nachbarklick zu, sobald die Aufnahme 12 000 Proben (250 ms) hinter die Ausgabe zurückgefallen ist. Echt sind dagegen: 17 Unterläufe von `QAudioSink` bei 20 ms Puffer unter Last und ein Probenverlust auf der Aufnahmeseite (1,92 s in 300 s bei 20 ms, 0,38 s bei 40 ms). Die alten Werte für Median, p95 und «fehlend» von M1d sind damit nicht brauchbar. **Z0** (Zuordnung nach der Zeit) liefert gültige Werte: ohne Pufferinhalt Median 35,1 / 39,1 ms, p95 55,8 / 59,9 ms, Spanne 67,7 / 59,3 ms bei 20 / 40 ms Puffer, kein Klick fehlt, kein Sprung und kein Gang der Verzögerung über 300 s.
3. **M2** erfüllt beide Grenzen. Das Fenster lag aber auf dem Bildschirm, nicht ausserhalb.
4. Der RØDECaster stand in der Nacht nicht in der Geräteliste; Standardausgang war «Internes Audio Digital Stereo (IEC958)».

## Aufbau

Befehle (ausgeführt vom Nacht-Agenten, Zeiten und Exit-Code aus `nachtlauf.log`):

```
bash ~/DoNs_Streaming_Studio/prototypes/ton/nachtlauf.sh --kurz   # 2026-11-11 03:38:09–03:43:01, Exit-Code 0
bash ~/DoNs_Streaming_Studio/prototypes/ton/nachtlauf.sh          # 2026-11-11 03:43:53–05:06:00, Exit-Code 0
```

| Teil | Inhalt (voller Lauf) | Daten |
|---|---|---|
| M1a | Geräteliste im Benutzerdienst | `m1-liste.txt` |
| M1b | Ruhe, Puffer 10 / 20 / 40 / 80 ms, je 120 s | `m1-ruhe-*.csv`, `…-zusammenfassung.txt` |
| M1c | Puffer 40 ms, 180 s; bei 60 / 100 / 140 s `clock.force-quantum` 2048 / 256 / 0 | `m1-quantum*` |
| M1d | Puffer 20 und 40 ms, je 300 s; daneben `scripts/leistung.py --video` in Schleife | `m1-last-*`, Rohdaten der Last in `build/p12a-nacht/` |
| M2 | Messseite in Qt WebEngine, 3600 s, `blitz-piep.webm` mit Wiederholen, Prototyp führt den Ton nach | `m2-*` |

- **Weg (unhörbar):** `QAudioSink` in `ton-messen` → virtuelle Senke `dss_p12a_ausgabe` → `pw-loopback` → virtuelle Quelle `dss_p12a_aufnahme` → `QAudioSource` im selben Prozess. Klick 1 ms lang, alle 500 ms (24 000 Proben), 48 kHz, float, Stereo. Abweichung vom Plan (siehe `prototypes/ton/LIESMICH.md`): `pw-loopback` statt Überwachung einer Null-Senke. Jede gemessene Verzögerung enthält deshalb auch `pw-loopback` und die Aufnahme.
- **Vorher und nachher** (beide Läufe): `clock.force-quantum 0`, Standardausgang `alsa_output.pci-0000_00_1b.0.iec958-stereo`. Aufräumen geprüft: keine `dss_p12a`-Knoten übrig, keine «ABWEICHUNG».
- **Grössen** (`prototypes/ton/klickmessung.cpp:314-382`):
  - «Verzögerung»: vom Abruf durch `QAudioSink`, der die erste Probe des Klicks liefert, bis zum Einsatz in der Aufnahme.
  - «ohne geschätzten Pufferinhalt»: Verzögerung minus Schätzung, wie lange der Klick noch im Puffer von `QAudioSink` wartet (Puffergrösse − angeforderte Bytes + Lage im Abruf). Den abgezogenen Teil kennt ein Mischer im Kern selbst.
  - «Sprung der Lage»: Änderung von (Probenlage in der Aufnahme − Probenlage in der Ausgabe) um mehr als 2 Proben zwischen zwei gefundenen Klicks.
  - Zuordnung: Ein Einsatz in der Aufnahme gilt als Klick Nr. `round((Lage − Lage des ersten Einsatzes) / 24 000)` (`klickmessung.cpp:333-348`). Diese Regel erklärt Befund 2.

### Welche Grösse für die Streuung

Verwendet: **p5–p95 der Verzögerung «ohne geschätzten Pufferinhalt»**, daneben die Spanne (max − min).

- «Ohne Pufferinhalt», weil der Puffer von `QAudioSink` und die Lage des Klicks im Abruf dem Kern bekannt sind. Die Nachführung rechnet sie selbst ein (`prototypes/ton/folgen.cpp:58-60`). Streuung, die den Ton gegen das Bild verschiebt, ist nur der Rest. Die rohe Verzögerung enthält zusätzlich die Lage des Klicks im Abrufblock von 42,67 ms und streut deshalb schon durch die Messart um rund 40 ms.
- p5–p95, weil einzelne Ausreisser die Spanne bestimmen (Puffer 10 ms: p95 18,5 ms, max 46,5 ms).
- Die Spanne steht trotzdem dabei, weil der Plan für M4 die Streuung als max − min festlegt.
- Grenze der Messung: Die Schätzung des Pufferinhalts ist ein Modell. Der Messweg (`pw-loopback`, Zeitstempel der Aufnahme) streut mit. Sein Anteil ist nicht bestimmt **[OFFEN]**, die Werte sind also eine obere Grenze für die Ausgabe allein.

Mit beiden Grössen ist «Streuung ≤ 5 ms» bei 20, 40 und 80 ms nicht erfüllt. Die Streuung ist kein Abbruchkriterium.

## M1: Werte

### M1a: Geräte im Dienst

`ton-messen --liste` als `systemd-run --user`: «QCoreApplication (ohne QGuiApplication), Qt 6.8.2». Ausgänge: HDMI der Grafikkarte, «Internes Audio Digital Stereo (IEC958)» [Standard], `dss_p12a_ausgabe`. Eingänge: «Internes Audio Analog Stereo» [Standard], `dss_p12a_aufnahme`. Alle melden float 48 kHz Stereo als unterstützt. Ein RØDECaster ist nicht dabei.

### M1b: Ruhe

| Puffer gewünscht / erhalten | 10 / 7,67 ms | 20 / 15,00 ms | 40 / 30,00 ms | 80 / 60,00 ms |
|---|---|---|---|---|
| Klicks geschrieben / gefunden | 240 / 240 | 241 / 241 | 241 / 240 ¹ | 241 / 241 |
| Startverzögerung (`start()` bis Klick 0) | 67,4 ms | 28,3 ms | 50,1 ms | 55,5 ms |
| Verzögerung roh, Median / p5–p95 (Breite) | 37,5 / 18,9–56,6 (37,7) | 35,4 / 14,3–58,1 (43,8) | 63,6 / 43,2–83,8 (40,6) | 83,5 / 63,2–103,9 (40,7) |
| **ohne Pufferinhalt**, Median / p5–p95 (Breite) | 16,6 / 15,1–18,5 (**3,4**) | 16,5 / 6,3–21,9 (**15,5**) | 42,4 / 35,7–49,1 (**13,4**) | 47,9 / 39,6–57,9 (**18,3**) |
| Spanne roh / ohne Pufferinhalt | 45,1 / 31,8 | 92,5 / 56,2 | 65,9 / 27,3 | 80,9 / 52,5 |
| Unterläufe (`QAudioSink`) | 0 | 0 | 0 | 0 |
| Sprünge der Lage | 1 (−2,67 ms) | 4 (je −2,67 ms) | 0 | 0 |
| Abrufe: Länge Median / Abstand Median | 42,667 / 39,1 ms ² | 42,667 / 42,5 ms | 42,667 / 45,3 ms | 42,667 / 40,2 ms |

¹ Der fehlende Klick ist Klick 240, geschrieben bei 119,98 s, kurz vor dem Stopp bei 120 s (`m1-ruhe-40.csv:242`). Kein Aussetzer.
² Bei 10 ms gibt es auch leere Abrufe (Länge p5 = 0 ms, Abstand p5 = 3,1 ms).

Befund: `QAudioSink` erhält rund drei Viertel des gewünschten Puffers und holt unabhängig davon immer Blöcke von 2048 Proben (42,67 ms) ab. Was das für den Mischer im Kern bedeutet, ist nicht geprüft **[OFFEN]**.

### M1c: Wechsel des Quantums

Puffer 40 ms gewünscht, 30 ms erhalten; 361 von 361 Klicks gefunden.

| Zeit im Lauf | Ereignis (`m1-quantum-zeiten.txt`, `m1-quantum.csv`) | Lage-Differenz | Verzögerung ohne Pufferinhalt (Stichproben) |
|---|---|---|---|
| 0 bis 58,86 s | `clock.force-quantum 0` (Standard) | 25 344 Proben | Klick 115–117: 49,2 / 51,7 / 53,5 ms |
| 58,861 s | Wechsel auf **2048**; Unterlauf bei 58,862 s («leer» für 10,1 ms) | **27 392 (+2048 = +42,67 ms)** ab Klick 118 | Klick 119 / 120: 103,0 / 104,6 ms |
| 98,948 s | Wechsel auf 256 | unverändert 27 392 | Klick 256: 105,1 ms |
| 138,963 s | zurück auf 0 | unverändert 27 392 bis Klick 359 | Klick 355: 99,4 ms |

- Der Sprung ist auch in der Zeit sichtbar (rund +50 ms ohne Pufferinhalt). Er bleibt nach der Rückkehr auf 256 und 0 bis zum Ende des Laufs stehen.
- Probelauf: Wechsel auf 2048 bei 13,853 s, Unterlauf bei 13,855 s, Sprung +2048 Proben bei Klick 28, sonst kein Sprung. Gleiches Bild.
- Graph bei 10 s, also vor dem Wechsel (`m1-quantum-pw-dump.json`, nicht im Repo, siehe «Interna-Entscheide»): `clock.rate` 48000, `clock.quantum` 1024, `clock.min-quantum` 32, `clock.max-quantum` 2048, `clock.force-quantum` 0. Ausgabestrom von `ton-messen`: `node.latency` 480/48000 (10 ms), `pulse.attr.tlength` 11 520 Bytes (30 ms). Aufnahmestrom: `node.latency` 2400/48000 (50 ms), `pulse.attr.fragsize` 19 200 Bytes. Welches Quantum vor dem Wechsel tatsächlich lief, steht nicht in den Daten (nicht geprüft).

Folgerungen (nicht geprüft):
- Der Puffer von `QAudioSink` (30 ms) ist kleiner als ein Quantum von 2048 Proben (42,67 ms). Beim Wechsel reicht er für einen Zyklus nicht. Es kommt zum Unterlauf, und der Ton läuft danach um genau ein Quantum später.
- Ob der Sprung mit 80 ms Puffer (60 ms erhalten, also mehr als ein Quantum) ausbleibt, ist nicht gemessen.
- Welcher Teil des Messwegs die 2048 Proben aufnimmt (Ausgabe, `pw-loopback` oder Aufnahme), ist nicht getrennt. Der gleichzeitige Unterlauf von `QAudioSink` spricht für die Ausgabe.

### M1d: Last

Last: `scripts/leistung.py --video` (2 Videos H.264 1080p30 im Editor, `dss-app` und `dss-core`) in Schleife, keine Ersatzlast. Jeder Lauf: «started» = Start von `dss-app` (nur ganze Sekunden), Messfenster von +5 bis +65 s, Ende der App etwa +75 s (`--messdauer 75`), Pause bis zum nächsten Lauf etwa 1 s (`scripts/leistung.py:43-45, 119-142`).

| Lauf | started | CPU gesamt Mittel (min–max) | `dss-app` (% eines Kerns) | GPU Mittel (min–max) | Leinwand Bilder/s, Einbrüche > 50 ms |
|---|---|---|---|---|---|
| last-1 | 03:55:05 | 8,7 % (7,0–18,0) | 56,0 | 16,2 % (3–23) | 30,02 / 0 |
| last-2 | 03:56:21 | 10,2 % (9,2–12,9) | 63,2 | 19,9 % (13–21) | 30,0 / 0 |
| last-3 | 03:57:37 | 8,6 % (7,4–9,9) | 53,0 | 22,3 % (12–24) | 30,0 / 0 |
| last-4 | 03:58:53 | 9,2 % (8,0–11,2) | 62,0 | 15,6 % (15–16) | 30,0 / 0 |
| last-5 | 04:00:09 | 10,5 % (9,0–16,4) | 66,5 | 20,9 % (3–24) | 30,0 / 0 |
| last-6 | 04:01:25 | 8,9 % (7,7–11,7) | 53,9 | 19,9 % (13–21) | 30,0 / 0 |
| last-7 | 04:02:41 | 10,1 % (9,0–11,2) | 65,0 | 20,1 % (13–21) | 30,0 / 0 |
| last-8 | 04:03:57 | 8,0 % (7,0–9,2) | 52,8 | 15,0 % (14–15) | 30,0 / 0 |
| last-9 | nicht protokolliert | – | – | – | abgebrochen mit «Last beendet» 04:05:20, keine Daten |

Die Last war mässig: rund 8 bis 10,5 % der 8 Threads, `dss-core` 0,0 bis 0,1 % eines Kerns.

**«Last (1 min)» im Protokoll ist nicht brauchbar.** Beleg: Um 04:00:16 stand dort 0,02, nach 5 min Dauerlast. In derselben Zeit (last-1 bis last-5) lag die CPU nach `mpstat` im Mittel bei 8,6 bis 10,5 % von 8 Threads, also bei rund 0,7 bis 0,8 belegten Threads, und `dss-app` allein bei 53 bis 66,5 % eines Kerns. Der erste Wert (0,01 um 03:55:14) wurde nur 10 s nach dem Start der Last gelesen und kann sie noch nicht enthalten. Warum `/proc/loadavg` so niedrig blieb, ist nicht geprüft.

| | Puffer 20 ms (15 erhalten) | Puffer 40 ms (30 erhalten) |
|---|---|---|
| Beginn (Wanduhr) | 03:55:16,0 | 04:00:17,8 |
| Unterläufe `QAudioSink` | **17** (Zustand «leer» je 0,04 bis 6,1 ms) | **0** |
| Sprünge der Lage | 400, davon 4 gross: +478,67 / +484,00 / +484,00 / +497,33 ms | 65, davon 1 gross: +494,67 ms |
| Lage-Differenz Klick 0 → letzter Klick | 26 624 → −65 536 Proben: **1920,0 ms** Verlust | 26 624 → 8448 Proben: **378,7 ms** Verlust |
| alte Auswertung: fehlend / fremd | 4 / 4 | 1 / 1 |
| alte Auswertung: Verzögerung Median / p95 (unbrauchbar) | 1041,3 / 2036,0 ms | 80,8 / 579,3 ms |
| Verzögerung roh nach richtiger Zuordnung (Stichproben, siehe unten) | 23,7 bis 82,4 ms | 37,6 / 50,4 ms |
| **Z0** (nach der Zeit): ohne Pufferinhalt Median / p95 / Spanne | 35,1 / 55,8 / 67,7 ms | 39,1 / 59,9 / 59,3 ms |

Gültige Werte für M1d: Abschnitt «Z0» unten.

#### Die grossen Sprünge sind ein Fehler der Auswertung

Belege, alle aus `m1-last-20.csv` und `m1-last-40.csv`:

1. **Jeder grosse Sprung ist genau ein Klickabstand (24 000 Proben) minus einen der üblichen kleinen Schritte:** 22 976 = 24 000 − 1024; 23 232 = 24 000 − 768 (zweimal); 23 872 = 24 000 − 128; 23 744 = 24 000 − 256.
2. **Er kommt genau dann, wenn die Lage-Differenz unter die Hälfte des Klickabstands fällt.** In beiden Läufen beträgt sie bei Klick 0 26 624 Proben, die Grenze liegt also bei 26 624 − 12 000 = 14 624. Bei 20 ms vor dem ersten Sprung (Klick 132) 15 488, danach (richtig gezählt) 14 464. Bei 40 ms vor dem Sprung (Klick 357) 14 848, danach 14 592. Ab dort rundet die Zuordnung auf den vorigen Klick.
3. **Anzahl grosser Sprünge = fehlende = fremde Klicks** (4 / 4 / 4 und 1 / 1 / 1). Die «fehlenden» sind die letzten geschriebenen Klicks (bei 20 ms die Klicks 596–599, bei 40 ms Klick 599), also keine Lücken mitten im Ton.
4. **Ordnet man dem nächsten Klick zu, liegt die Verzögerung im üblichen Bereich** (roh, Aufnahmezeit minus Schreibzeit des richtigen Klicks):
   - 20 ms: 64,9 / 49,5 / 53,7 / 61,5 / 23,7 / 82,4 / 59,0 ms (eingetragen als Klick 133, 254, 255, 440, 441, 559, 595);
   - 40 ms: 50,4 / 37,6 ms (eingetragen als Klick 358 und 598).
   - Zum Vergleich in Ruhe bei 20 ms: roh 5,1 bis 97,6 ms.

Echt sind:
- **Probenverlust auf der Aufnahmeseite.** Die Lage-Differenz fällt in Schritten von 128 bis 1024 Proben (Summe 1,92 s bzw. 0,38 s), während die Verzögerung in der Zeit gleich bleibt. Hätte die Ausgabe Proben verloren, kämen die Klicks entsprechend früher an, und die Verzögerung wäre um bis zu 1,9 s gefallen. Folgerung: Die Proben fehlen zwischen der virtuellen Senke und `QAudioSource` (`pw-loopback` oder Aufnahme). Welcher Teil, ist nicht geprüft **[OFFEN]**. In Ruhe gibt es diese Schritte kaum (0 bis 4 je Lauf).
- **17 Unterläufe** von `QAudioSink` bei 20 ms unter Last. Wie lang die Lücke im Ton jeweils war, ist nicht gemessen.

Folge: Streuung und Aussetzer unter Last sind aus den vorhandenen Zusammenfassungen nicht ablesbar. Die CSV-Dateien enthalten alle Zeiten. Die Auswertung mit Zuordnung nach der Zeit ist in Z0 nachgeholt (Abschnitt «Z0» unten).

#### Abgleich mit den Läufen `last-*`

Zeit: `epoch_ms` aus der CSV, umgerechnet in MEZ (UTC+1), abgeglichen an `m1-quantum-zeiten.txt` (1794365583047 ↔ Protokoll 03:53:03). Kriterium «fällt zusammen»: höchstens 5 s nach «started» oder höchstens 5 s vor oder nach dem Ende der App (+75 s). «started» hat nur ganze Sekunden, die Abstände sind deshalb auf ±1 s genau.

| Sprung (Puffer, eingetragen bei Klick) | Wanduhr (zwischen zwei Klicks) | Lauf | Abstand zu «started» | Ergebnis |
|---|---|---|---|---|
| +478,67 ms (20 ms, Klick 133) | 03:56:22,0–22,5 | last-2 | +0 bis +2 s | **fällt zusammen** (Übergang last-1 → last-2) |
| +484,00 ms (20 ms, Klick 255) | 03:57:23,1–23,6 | last-2 | +61 bis +63 s | fällt nicht zusammen (im Messfenster) |
| +484,00 ms (20 ms, Klick 441) | 03:58:56,1–56,6 | last-4 | +2 bis +4 s | **fällt zusammen** (Übergang last-3 → last-4) |
| +497,33 ms (20 ms, Klick 558) | 03:59:54,6–55,1 | last-4 | +61 bis +62 s | fällt nicht zusammen (im Messfenster) |
| +494,67 ms (40 ms, Klick 358) | 04:03:16,3–16,9 | last-7 | +34 bis +36 s | fällt nicht zusammen (im Messfenster) |

- CPU (`mpstat`, je 1 s) bei den drei Sprüngen im Messfenster:
  - 03:57:22–24: 10,87 / 10,15 / 9,69 %
  - 03:59:54–56: 8,49 / 8,64 / 9,43 %
  - 04:03:16–17: 10,51 / 8,84 %
  - Keine Spitze gegenüber dem Mittel des Laufs. Für die beiden Sprünge am Übergang gibt es keine `mpstat`-Werte (Aufwärmzeit).
- Wegen Befund 2 sagt der Zeitpunkt eines grossen Sprungs nur, wann der aufgelaufene Probenverlust die Grenze von 12 000 Proben überschritt, nicht wann etwas geschah.

**Unterläufe** (20 ms, alle 17; Abstand zu «started» des jeweiligen Laufs):

| Lauf | Anzahl | Abstand zu «started» |
|---|---|---|
| last-1 | 0 | – (03:55:16 bis 03:56:21 kein Unterlauf) |
| last-2 | 7 | +8, +20, +23, +26, +34, +60, +67 s |
| last-3 | 2 | +31, +47 s |
| last-4 | 5 | +13, +18, +30, +37, +42 s |
| last-5 | 3 | +1, +3, +5 s (Übergang last-4 → last-5; Lauf M1d endet bei 04:00:16) |

Ergebnis: **3 von 17 Unterläufen fallen zusammen** (Übergang last-4 → last-5), **14 fallen nicht zusammen**. Sie treten während der laufenden Last auf, nicht gehäuft am Start oder Ende eines Laufs. Bei 40 ms unter derselben Last gab es keinen Unterlauf.

#### Z0: M1d nach der Zeit neu ausgewertet

Ohne neue Messung, nur aus `m1-last-20.csv` und `m1-last-40.csv` (Entscheid des Nutzers 11.11.). Skript: `prototypes/ton/neu_auswerten.py`, Ausgaben `P12a-daten/<Lauf>/m1-last-20-neu.txt`, `m1-last-40-neu.txt` und `m1-gegenprobe-neu.txt`.

**Zuordnung:** Ein Einsatz gehört zu dem Klick, dessen Sendezeit (`geschrieben_ms`) plus Bezugsverzögerung am nächsten an der Zeit des Einsatzes (`aufnahme_ms`) liegt. Bezugsverzögerung = Median der rohen Verzögerung von Klick 0 bis 9 (68,3 ms bei 20 ms, 66,2 ms bei 40 ms). Fenster ± 250 ms, also der halbe Klickabstand: breiter darf es nicht sein, sonst passt ein Einsatz zu zwei Klicks. Ausgenutzt wurde davon −61,7 bis +32,1 ms (20 ms) bzw. −52,9 bis +41,0 ms (40 ms); jeder Einsatz bleibt mindestens 188 ms vom Rand. 0 Einsätze ausserhalb, 0 Klicks doppelt.

**Grenze der Daten:** Einsätze, die die alte Auswertung als «fremd» verworfen hat, stehen nicht in der CSV, nur ihre Anzahl in der Zusammenfassung. Die Klicks dazu bleiben ohne Zeit. Das Skript zeigt, dass es genau die Klicks an den Stellen sind, an denen die alte Zuordnung um einen weiteren Klick verrutscht (20 ms: Klick 133, 256, 443, 561; 40 ms: Klick 358), und dass ihre Zahl gleich der Zahl der fremden Einsätze ist (4 bzw. 1). Diese Klicks kamen also in der Aufnahme an; ihre Verzögerung fehlt in den Kennzahlen (n = 596 bzw. 599).

| Voller Lauf, unter Last | Puffer 20 ms (15 erhalten) | Puffer 40 ms (30 erhalten) | zum Vergleich Ruhe 20 / 40 ms (M1b) |
|---|---|---|---|
| Klicks geschrieben / mit Zeit / verworfen (s. o.) / **fehlend** | 600 / 596 / 4 / **0** | 600 / 599 / 1 / **0** | – |
| **ohne Pufferinhalt**: Median / p95 | **35,1 / 55,8 ms** | **39,1 / 59,9 ms** | 16,5 / 21,9 ms; 42,4 / 49,1 ms |
| ohne Pufferinhalt: min / max, **Spanne** | 5,5 / 73,2 ms, **67,7 ms** | 10,7 / 70,0 ms, **59,3 ms** | 56,2 ms; 27,3 ms |
| ohne Pufferinhalt: p5–p95 (Breite) | 10,0–55,8 (**45,7 ms**) | 19,2–59,9 (**40,7 ms**) | 15,5 ms; 13,4 ms |
| roh: Median / p95 / Spanne | 54,9 / 85,6 / 93,8 ms | 60,4 / 89,9 / 93,8 ms | 35,4 / – / 92,5 ms; 63,6 / – / 65,9 ms |
| **Aussetzer** (Unterläufe `QAudioSink`) | **17**, «leer» 0,04 bis 6,1 ms (Median 2,1 ms) | **0** | 0; 0 |
| Sprünge der Lage > 2 Proben / grösster | 400 / 1024 Proben (21,3 ms), keiner > 12 000 | 65 / 1024 Proben, keiner > 12 000 | – |
| Probenverlust der Aufnahme | 92 160 Proben = 1920,0 ms | 18 176 Proben = 378,7 ms | – |

**Verlauf je 60 s** (ohne Pufferinhalt, nach Sendezeit; Spalten Median / p95 / Spanne, Verlust in Proben, Unterläufe):

| Abschnitt | 20 ms | Verlust / Unterläufe | 40 ms | Verlust / Unterläufe |
|---|---|---|---|---|
| 0–60 s | 37,5 / 55,8 / 66,5 | 10 368 / 0 | 41,5 / 59,3 / 57,2 | 5632 / 0 |
| 60–120 s | 32,5 / 53,8 / 50,7 | 22 016 / 5 | 47,2 / 60,2 / 58,1 | 2048 / 0 |
| 120–180 s | 36,2 / 56,3 / 50,2 | 18 176 / 3 | 35,4 / 59,1 / 53,3 | 4352 / 0 |
| 180–240 s | 35,6 / 53,8 / 54,9 | 16 128 / 3 | 47,4 / 61,8 / 55,7 | 5120 / 0 |
| 240–300 s | 34,5 / 57,0 / 54,2 | 25 472 / 6 | 32,5 / 45,6 / 35,4 | 1024 / 0 |

Befunde aus Z0:
- **Kein Sprung und kein Gang der Verzögerung in der Zeit.** Der Median je 60 s bleibt bei 32,5 bis 37,5 ms (20 ms) bzw. 32,5 bis 47,4 ms (40 ms), obwohl die Aufnahme 1,92 s bzw. 0,38 s an Proben verliert. Das bestätigt Befund 2: Die Proben fehlen auf der Aufnahmeseite, die Ausgabe läuft in der Zeit gleichmässig weiter.
- **Kein Klick fehlt**, auch nicht im Lauf mit den 17 Unterläufen («leer» höchstens 6,1 ms). Ob die Unterläufe hörbare Aussetzer erzeugen, misst die Klickmessung nicht (nicht geprüft).
- **Um die Unterläufe** ändert sich der Median von je 3 Klicks davor und danach in beide Richtungen (z. B. 9,2 → 53,7 ms, 26,3 → 11,2 ms; Liste in `m1-last-20-neu.txt`). Bei einer Breite p5–p95 von 45,7 ms ist das nicht von der Streuung zu trennen. Ein bleibender Versatz nach einem Unterlauf ist im Verlauf je 60 s nicht zu sehen.
- **Streuung unter Last etwa dreimal so breit wie in Ruhe** (p5–p95 45,7 / 40,7 ms gegen 15,5 / 13,4 ms). Welcher Teil davon aus dem Messweg kommt (Zeitstempel der Aufnahme, die unter Last selbst Proben verliert), ist nicht getrennt **[OFFEN]**, wie schon für die Ruhe. Die Werte sind eine obere Grenze für die Ausgabe allein.
- Die alte Regel «richtige Nummer = eingetragene Nummer plus Anzahl bisheriger grosser Sprünge» (siehe «Interna-Entscheide») ist bestätigt: Die Zuordnung verrutscht genau bei den alten grossen Sprüngen (alte Nummern 133, 255, 441, 558 bei 20 ms und 358 bei 40 ms, richtig 134, 257, 444, 562 und 359).

**Gegenprobe** (`m1-gegenprobe-neu.txt`, beide Läufe): Toleranz 0,0015 ms, weil die CSV jede Zeit mit 3 Nachkommastellen speichert und das Skript die Differenzen neu rechnet.
- Ruhe 10 / 20 / 40 / 80 ms (M1b) und Quantum (M1c), voller Lauf und Probelauf: 0 Einsätze anders zugeordnet, n gleich, alle Kennzahlen (min, p5, Median, p95, max) roh und ohne Pufferinhalt gleich wie in der alten Zusammenfassung (grösste Abweichung 0,0010 ms).
- M1d vor dem ersten Verrutschen (Klick 0–132 bei 20 ms, 0–357 bei 40 ms): n gleich (133 / 358), Kennzahlen gleich (grösste Abweichung 0,0010 ms).
- M1d im Probelauf (30 s, kein grosser Sprung): ganzer Lauf gleich.
- M1d ganzer voller Lauf: verschieden (463 bzw. 241 Einsätze anders zugeordnet). Das ist die erwartete Korrektur: Ab dem ersten Verrutschen zählt die alte Auswertung den Versatz von 1 bis 4 Klicks (500 bis 2000 ms) in die Verzögerung.
- Hinweis zur Bezeichnung: Der Auftrag nannte die Ruhe-Messungen «M1a». In diesem Bericht heissen sie M1b (M1a ist die Geräteliste). Geprüft wurden die Ruhe-Messungen.
- Der Probelauf von M1d bei 20 ms hat 1 Klick ohne Einsatz (Klick 60, geschrieben bei 30,008 s, also nach dem Ende der Messdauer von 30 s; `stop()` leert den Puffer). Die alte Auswertung zählt ihn ebenfalls als «fehlend».

### Probelauf (`--kurz`) zum Vergleich

| | Probelauf (15 s / 45 s / 30 s) | voller Lauf (120 s / 180 s / 300 s) |
|---|---|---|
| Ruhe 10 / 20 / 40 / 80: Unterläufe | 0 / 0 / 0 / 0 | 0 / 0 / 0 / 0 |
| Ruhe, p5–p95 ohne Pufferinhalt | 6,1 / 8,6 / 13,3 / 18,4 ms | 3,4 / 15,5 / 13,4 / 18,3 ms |
| Quantum: Sprung beim Wechsel auf 2048 | +42,67 ms, 1 Unterlauf | +42,67 ms, 1 Unterlauf |
| Last 20 / 40: Unterläufe | 0 / 0 | 17 / 0 |
| Last 20 / 40: Sprünge der Lage | 35 / 2 (kein grosser) | 400 / 65 |

## M2: Uhr der Seite in Qt WebEngine

Server als Benutzerdienst, Seite mit `seite-zeigen` (`--lage -1400,0`), 3600 s, `blitz-piep.webm` (60 s) mit Wiederholen. Der Prototyp spielte den Ton nach den Meldungen auf `dss_p12a_ausgabe` (Puffer 40 ms). Aufgenommen und ausgewertet wurde der Ton in M2 nicht.

| | voller Lauf | Probelauf |
|---|---|---|
| Meldungen «bild» | 107 886 in 3600,0 s (29,97 je s) | 1799 in 59,9 s (30,01 je s) |
| Abschnitte (Start, Wiederholung) / ausgelassene Bilder | 60 / 0 | 1 / 0 |
| **Eintreffen im Prototyp gegen `mediaTime`: Gang** | **−38,78 ppm = −2,327 ms/min** | −433,86 ppm = −26,032 ms/min |
| Eintreffen: Schwankung Median / **p95** / max | 0,287 / **0,438** / 33,515 ms | 3,328 / 7,827 / 35,432 ms |
| `performance.now()` beim Senden: Gang / p95 | −38,76 ppm / 0,416 ms | −432,59 ppm / 7,817 ms |
| `presentationTime`: Gang / p95 | −38,08 ppm / 0,418 ms | −39,33 ppm / 0,402 ms |
| `expectedDisplayTime`: Gang / p95 | −37,17 ppm / 0,437 ms | −37,97 ppm / 0,419 ms |
| Weg Seite → Prototyp über dem kürzesten, p95 / max | 0,504 / 9,467 ms | 0,377 / 1,429 ms |
| Pause beim Wiederholen, Median (p5–p95; min–max) | 58,59 ms (58,56–58,62; 25,21–75,24), n = 59 | – |
| Nachführen: Einträge / Sprünge > 20 ms | 28 181 / 86 | 487 / 1 |
| Nachführen: \|Abweichung Ton − Soll\| Median / p95 / max | 3,833 / 7,314 / 22,913 ms | 3,898 / 7,455 / 15,972 ms |

- **Fenster:** Das Protokoll meldet «Seite geladen: ja, Fenster bei 0,27 1290x754» (`nachtlauf.log:646`, im Probelauf gleich), und die Seite meldete `"sichtbarkeit":"visible"` (`m2-ausgabe.txt:4`). Das Fenster lag also auf dem Bildschirm. Gleicher Befund wie in P02: Ohne `Qt::X11BypassWindowManagerHint` schiebt xfwm4 das Fenster auf 0,27 zurück (`P02-bericht.md`, «Sichtbarkeitsfälle»), und `seite-zeigen.cpp:49-54` setzt diesen Hinweis nicht. Der Name `m2-ausserhalb` und der Text «(Fenster ausserhalb des Bildschirms)» sind feste Texte in `nachtlauf.sh:297, 303`. Die Ersatzregel des Skripts (neu bei 0,0, wenn weniger als 300 Meldungen kommen) prüft nur die Anzahl, nicht die Lage. M2 hat also den Fall «sichtbar» gemessen. Ob der Bildschirm während der 60 min gesperrt wurde, ist nicht geprüft. Die Seite meldete durchgehend Bilder ohne Auslassung.
- **Gang:** −38,78 ppm liegt unter der Grenze von 50 ppm (78 % davon). Gemessen ist die Medienzeit von Chromium gegen die Systemuhr desselben Rechners. Die Ursache des Gangs ist nicht geprüft. Für den Betrieb zählt der Gang zwischen dem Streaming-PC und dem Studio-PC (M3).
- **Nachführen:** Die Abweichung stammt aus der eigenen Rechnung des Prototyps (Pufferschätzung und fester Gerätewert, `folgen.cpp:57-60`), nicht aus einer Aufnahme. Ob die 86 Sprünge > 20 ms alle an den 59 Wiederholungen liegen, ist nicht geprüft.
- **Pause beim Wiederholen:** Das Video steht bei jedem Neubeginn rund 58,6 ms (knapp 2 Bilder bei 30 fps). Für P12b wichtig: Der Kern wiederholt nach der Länge, die die Seite meldet.

### Probelauf −433 ppm, voller Lauf −38 ppm

- Im Probelauf stimmen `presentationTime` (−39,33 ppm) und `expectedDisplayTime` (−37,97 ppm) mit dem vollen Lauf überein. Die Medienzeit lief gegen die Systemuhr also in beiden Läufen gleich.
- Abweichend sind nur die Zeit, zu der die Seite meldet (`performance.now()` beim Senden, −432,59 ppm), und das Eintreffen (−433,86 ppm). Bei beiden ist die Schwankung hoch (p95 7,8 ms, Median 3,3 ms). Der Weg Seite → Prototyp ist dabei kurz (p95 0,377 ms).
- Die Abweichung entsteht also in der Seite, zwischen der Darstellung eines Bildes und dem Aufruf des Callbacks, nicht im Netz und nicht in der Medienzeit.
- Über 59,9 s ergeben −433,86 ppm rund 26 ms. Eine solche Störung kippt die Gerade über 60 s stark, über 3600 s kaum. Das passt zur Erklärung «Einschwingen in den ersten Sekunden».
- Ob die Abweichungen im Probelauf tatsächlich am Anfang liegen, ist nicht nach Zeitfenstern geprüft **[OFFEN]**. Die Rohdaten (`2026-11-11-0338-kurz/m2-meldungen.csv.gz`) erlauben die Prüfung. Gegen eine reine Störung am Anfang spricht der hohe Median der Schwankung (3,3 ms): Mehr als die Hälfte aller Meldungen weicht über 3 ms ab.

## G4: Einordnung (entschieden: P, Nutzer 2026-11-11)

**Formal:** Die Abbruchbedingung von M1 «Verzögerung springt um > 10 ms, wenn sich das Quantum von PipeWire ändert» ist erfüllt: +42,67 ms im vollen Lauf und im Probelauf. Nach dem Plan folgt: Halt, Variante P vorschlagen. Die übrigen Abbruchbedingungen von M1 und M2 sind nicht ausgelöst.

**Einschränkungen des Befunds:**
- Er tritt nur beim Wechsel auf das erzwungene Maximum 2048 (`clock.max-quantum`) auf. Der Wechsel auf 256 und zurück auf 0 ergab keinen Sprung.
- Gemessen ist er mit einem Puffer, der kleiner als dieses Quantum ist (30 ms < 42,67 ms).
- Der Messweg enthält `pw-loopback`.
- Ob ein anderes Programm am Studio-PC das Quantum im Betrieb so hoch setzt, ist nicht geprüft.

**Was der Befund für Q heisst:** `QAudioSink` meldet keine Verzögerung des Geräts. Die Nachführung rechnet den hörbaren Zeitpunkt aus dem eigenen Puffer und einem festen Gerätewert (`folgen.cpp:58-60`). Ein Sprung hinter `QAudioSink` bleibt für den Kern also unsichtbar und stünde als fester Versatz von 42,67 ms bis zum Neustart des Ausgangs. Dass er erst mit einem Neustart verschwindet, ist nicht geprüft. Gemessen ist nur, dass er bis zum Ende des Laufs (120 s nach dem Wechsel) blieb. Das liegt ausserhalb des Ziels ±20 ms. Die Gegenmassnahme im Plan («Nachführen über die Meldungen der Seite», Risiko-Tabelle) deckt das nicht ab: Die Seite meldet nur die Lage des Bildes.

**Was P bringen soll:** Laut Plan liefert `pw_stream` die genaue Verzögerung. Ein Sprung wäre damit erkennbar und auszugleichen. Den Unterlauf selbst verhindert P nicht. Nicht geprüft, die Dokumentation von PipeWire wurde nicht gelesen.

**Empfehlung: Variante P.** Begründung:
1. Die vorab vereinbarte Abbruchbedingung ist erfüllt.
2. Q kann Änderungen der Verzögerung grundsätzlich nicht sehen. Ein grösserer Puffer (Zusatzmessung Z1) könnte nur diesen einen Auslöser entschärfen.
3. Mit P bleibt Qt Gui aus dem Kern (Plan, «Befunde»: Qt Multimedia zieht Qt Gui mit).

Gegenargumente:
- Neues Paket `libpipewire-0.3-dev` (Nutzer installiert) und mehr eigener Code.
- Der Vorteil von P ist noch nicht gemessen. P12b sollte deshalb mit einem kleinen Prototyp beginnen, der M1c mit `pw_stream` wiederholt (Z3).

Wer Q ohne neues Paket bevorzugt, sollte vor dem Entscheid Z1 messen.

**Entscheid des Nutzers (2026-11-11): P.** P12b beginnt mit einem Prototyp mit `pw_stream` (Z3).

### Folgen von Z0 für G4 und P12b

Z0 ändert an der Empfehlung P nichts:
- Der Grund für P ist der Sprung beim Wechsel des Quantums (M1c). Z0 betrifft nur M1d und berührt ihn nicht.
- Z0 nimmt die scheinbaren Sprünge von rund 480 ms unter Last weg. Sie waren kein Argument für P; unter Last gibt es weder Sprung noch Gang der Verzögerung in der Zeit. Ein zusätzliches Argument für oder gegen P entsteht daraus nicht.
- Unter Last wird die Streuung (ohne Pufferinhalt) etwa dreimal so breit wie in Ruhe (p5–p95 rund 41 bis 46 ms). Mit Q sieht der Kern davon nichts. Ob die von `pw_stream` gemeldete Verzögerung diese Streuung abbildet, ist offen. Vorschlag für Z3 in P12b: den Prototyp mit `pw_stream` auch unter derselben Last messen, nicht nur M1c (nicht entschieden).
- Bei 20 ms Puffer gab es unter mässiger Last 17 Unterläufe, bei 40 ms keinen. Vorschlag für P12b, unabhängig von Q oder P (nicht entschieden): Puffer nicht unter 40 ms gewünscht (30 ms erhalten) ansetzen, bis Z2 oder der Prototyp mit `pw_stream` etwas anderes zeigt.

### Zusatzmessungen, die die Frage klären könnten

| Nr. | Was | Klärt |
|---|---|---|
| Z0 | **ausgeführt 2026-11-11** (Auswertung, keine Messung): M1d aus den vorhandenen CSV-Dateien neu ausgewertet, Zuordnung nach der Zeit statt nach der Probenlage. Ergebnis im Abschnitt «Z0» | echte Streuung und Aussetzer unter Last, Verlauf der Verzögerung |
| Z1 | M1c mit Puffer 40 und 80 ms, je 180 s; Wechsel 0 → 1024 → 2048 → 0; wirksames Quantum dabei mit `pw-top` lesen | Sprung bei Puffer ≥ 1 Quantum? Sprung auch beim Wechsel auf das Standard-Quantum 1024? |
| Z2 | M1d mit Puffer 40 und 80 ms je 10 min unter derselben Last | Unterläufe und Probenverlust der Aufnahme bei grösserem Puffer |
| Z3 | nur bei P: Prototyp mit `pw_stream`, M1c wiederholen, gemeldete Verzögerung gegen die Klickmessung | ob P den Sprung erkennt und meldet |

## Weitere Befunde

- **Kein RØDECaster in der Nacht:** `m1-liste.txt` enthält nur HDMI, «Internes Audio Digital Stereo (IEC958)» (Standard) und die Messknoten. Laut Übergabe war am 09.11. «RØDECaster Pro II Analog Stereo» der Standardausgang. Für M4 muss der RØDECaster angeschlossen und eingeschaltet sein.
- `processedUSecs − elapsedUSecs` liegt bei allen Klickläufen zwischen −18,2 und −102,0 ms (−102,0 ms bei Last 20 ms).
- In allen Klickläufen meldet Qt einmal «QIODevice::seek (QIODevice): Cannot call seek on a sequential device». Folgen auf die Messung sind nicht erkennbar; die Ursache ist nicht geprüft.

## Geändert

| Datei | Art |
|---|---|
| `docs/archiv/messungen/P12a-bericht.md` | neu (dieser Bericht) |
| `docs/archiv/messungen/P12a-daten/2026-11-11-0343/` | neu: voller Lauf, ohne `m1-quantum-pw-dump.json` |
| `docs/archiv/messungen/P12a-daten/2026-11-11-0338-kurz/` | neu: Probelauf, ohne `m1-quantum-pw-dump.json` |
| `prototypes/ton/neu_auswerten.py` | neu (Z0): Zuordnung nach der Zeit, Kennzahlen, Verlauf je 60 s, Gegenprobe; nur Python-Standardbibliothek |
| `P12a-daten/<beide Läufe>/m1-last-20-neu.txt`, `m1-last-40-neu.txt`, `m1-gegenprobe-neu.txt` | neu (Z0): Ausgaben des Skripts |
| dieser Bericht | nachgeführt (Z0): Entscheide des Nutzers 11.11., Abschnitte «Z0» und «Folgen von Z0 für G4 und P12b», offene Punkte |

## Geprüft

- Stufe: keine Bau- oder Testprüfung, nur ein Bericht und Messdaten. Nichts gemessen. Für den Messbericht kein Skript gestartet und kein Code geändert; für Z0 ein Auswertungsskript (siehe unten).
- 2026-11-11: `git status --porcelain --ignored --untracked-files=all`: Alle 48 Dateien in `P12a-daten/` sind unversioniert und nicht durch `.gitignore` ausgeschlossen.
- 2026-11-11: Suche (ripgrep) in Bericht und Datenordnern nach `192.168`, `ts.net` und `@`:
  - `192.168` und `ts.net`: keine Treffer.
  - `@`: nur in den beiden `m1-quantum-pw-dump.json` (je 12 Treffer, E-Mail-Adressen der Autoren von PipeWire-Modulen). Diese Dateien sind nicht im Commit.
- Die Rechenschritte in diesem Bericht (Differenzen, Umrechnung der Zeiten, Zuordnung) sind von Hand aus den genannten Zeilen der Dateien gerechnet, ohne Skript. Ausnahme: Abschnitt «Z0».
- Z0, 2026-11-11: `python3 -I prototypes/ton/neu_auswerten.py docs/archiv/messungen/P12a-daten/2026-11-11-0343 docs/archiv/messungen/P12a-daten/2026-11-11-0338-kurz` (Python 3 von Debian 13): Exit-Code 0, keine Fehlermeldung, «Gegenprobe bestanden» für beide Läufe. Summe des Verlusts je 60 s gleich dem Verlust des ganzen Laufs (92 160 bzw. 18 176 Proben). Keine Messung, nichts abgespielt, kein Dienst gestartet.

## Handtest

Kein Handtest nötig für diesen Commit. M3 und M4 aus dem Plan stehen aus (siehe unten).

## Nicht erledigt, `[OFFEN]`

- **[OFFEN]** M3 (Lotse mit dem Nutzer zusammen, Entscheid 11.11., OBS): `window.obsstudio`, Ereignisse «aktiv»/«sichtbar», Gang über 60 min gegen den Studio-PC, Bildrate (`GetVideoSettings`).
- **[OFFEN]** M4 (Nutzer und Lotse): ganzer Weg über den RØDECaster, Versatz und Streuung über 30 Blitze, Hörtest. Voraussetzung: RØDECaster angeschlossen (in der Nacht nicht in der Geräteliste).
- Erledigt: Entscheid G4, vom Nutzer am 11.11. entschieden: P.
- Erledigt: M1d neu auswerten (Z0), 11.11., Abschnitt «Z0». Gültige Werte für Median, p95, Spanne, «fehlend» und Aussetzer unter Last.
- **[OFFEN]** Verzögerung der 4 bzw. 1 Klicks, deren Einsatz die alte Auswertung verworfen hat: steht nicht in der CSV, nur mit neuer Messung oder geänderter Auswertung im Prototyp zu holen. Für die Kennzahlen unerheblich (n = 596 bzw. 599).
- **[OFFEN]** Ursache des Probenverlusts auf der Aufnahmeseite unter Last (`pw-loopback` oder `QAudioSource`).
- **[OFFEN]** Ursache der 17 Unterläufe bei 20 ms unter mässiger Last (rund 9 % CPU).
- **[OFFEN]** Anteil des Messwegs an der Streuung in Ruhe und unter Last (Z0: unter Last etwa dreimal so breit).
- **[OFFEN]** Abrufblöcke von 42,67 ms unabhängig vom Puffer: Bedeutung für den Mischer.
- M2 mit Fenster ausserhalb des Bildschirms ist nicht gemessen (Fenster lag bei 0,27). Entscheid des Nutzers 11.11.: M2 wird nicht wiederholt.
- **[OFFEN]** Erklärung «Einschwingen» für den Probelauf nicht nach Zeitfenstern geprüft.
- **[OFFEN]** Warum `/proc/loadavg` unter Last 0,02 zeigte.

## Annahmen, Interna-Entscheide

| Entscheid | Begründung |
|---|---|
| Streuung als p5–p95 «ohne geschätzten Pufferinhalt», Spanne daneben | siehe «Welche Grösse für die Streuung» |
| Zeitumrechnung `epoch_ms` → MEZ (UTC+1) | abgeglichen: `m1-quantum-zeiten.txt` 1794365583047 entspricht dem Protokolleintrag 03:53:03 |
| «Fällt zusammen» = höchstens 5 s nach «started» oder höchstens 5 s um das App-Ende (+75 s) | «started» hat nur ganze Sekunden; der Übergang zwischen zwei Läufen (App-Ende, Start des nächsten) dauert etwa 1 s |
| Richtige Zuordnung nach einem grossen Sprung = eingetragene Nummer plus Anzahl bisheriger grosser Sprünge | folgt aus der Rundungsregel in `klickmessung.cpp:339` und den Belegen 1 bis 3; mit Z0 bestätigt |
| Z0: Zuordnung zum Klick mit der nächsten erwarteten Zeit (Sendezeit + Bezugsverzögerung), Fenster ± 250 ms | halber Klickabstand: grösstes Fenster ohne Doppeldeutigkeit; genutzt wurden höchstens 62 ms, Rand nie erreicht |
| Z0: Bezugsverzögerung = Median der rohen Verzögerung von Klick 0 bis 9 | dort wandert der Versatz um höchstens 640 Proben (Grenze 12 000), die alte Zuordnung ist also richtig; Median statt Mittel gegen Ausreisser |
| Z0: Zuordnung nach roher Verzögerung (Sendezeit), nicht nach «ohne Pufferinhalt» | so verlangt («Sendezeit gegen Zeit des Einsatzes»); bei ± 250 ms Fenster und 94 ms Spanne macht die Wahl keinen Unterschied |
| Z0: Toleranz der Gegenprobe 0,0015 ms | CSV speichert jede Zeit mit 3 Nachkommastellen, das Skript rechnet die Differenz neu |
| Z0: Verlauf in Abschnitten von 60 s nach Sendezeit; Verlust je Abschnitt gegen den letzten Klick des vorigen Abschnitts | Auftrag («z. B. je 60 s»); so ergibt die Summe den Verlust des ganzen Laufs |
| Z0: Ausgaben auch für den Probelauf | gleiche Rechnung, dient als weitere Gegenprobe (dort kein grosser Sprung) |
| Z0: Bezeichner im Skript auf Englisch, Texte und Kommentare auf Deutsch | Regel 13 |
| `m1-quantum-pw-dump.json` (beide Läufe) nicht im Commit, unversioniert im Datenordner gelassen | enthält Benutzer- und Rechnernamen sowie Namen und E-Mail-Adressen Dritter (Autoren der PipeWire-Module). Der Auftrag schliesst Personennamen und Adressen im Commit aus. Die benötigten Werte stehen im Bericht |
| Rohdaten von M1 und M2 (CSV, `m2-meldungen.csv.gz`) im Commit | Auffälligkeiten (Entscheid Lotse 09.11.: Rohdaten von M2 nur bei Auffälligkeiten): Z0 und die Prüfung «Einschwingen» brauchen sie |
| Last-Rohdaten `build/p12a-nacht/` nicht im Repo | Auftrag; im Bericht zusammengefasst |

## Quellen

Lokal gelesen (2026-11-11):
- `docs/archiv/prompts/P12-ton.md` (Abschnitt 2, «Messungen», Risiken)
- `docs/archiv/messungen/P02-bericht.md` (Sichtbarkeitsfälle, Lage 0,27)
- `prototypes/ton/klickmessung.cpp:314-382`, `prototypes/ton/folgen.cpp:57-60`, `prototypes/ton/seite-zeigen.cpp:46-58`, `prototypes/ton/nachtlauf.sh`, `prototypes/ton/LIESMICH.md`
- `scripts/leistung.py:43-45, 119-142`
- `docs/archiv/messungen/P12a-daten/2026-11-11-0343/*` und `…/2026-11-11-0338-kurz/*`
- `build/p12a-nacht/2026-11-11-0343/leistung.log`, `last-1/` bis `last-8/` (`mpstat.txt`, `ergebnis.json`)
- Z0: `prototypes/ton/klickmessung.cpp:314-401` (Spalten der CSV, Zuordnung, Zustände), `prototypes/ton/auswertung.cpp:7-26` (Rechnung der Kennzahlen, im Skript nachgebaut)

Im Code genannt, für diesen Bericht **nicht abgerufen** (kein Webzugriff in diesem Schritt):
- https://doc.qt.io/qt-6.8/qaudiosink.html
- https://doc.qt.io/qt-6.8/qaudiosource.html
- https://doc.qt.io/qt-6.8/qmediadevices.html
- https://wicg.github.io/video-rvfc/
- https://docs.pipewire.org/page_man_pw-loopback_1.html
- https://docs.pipewire.org/page_man_pw-metadata_1.html
- https://docs.pipewire.org/ (`pw_stream`, Verzögerung; Variante P)
