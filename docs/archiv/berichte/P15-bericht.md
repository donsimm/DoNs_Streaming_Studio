# P15 – Bericht (Kamerasteuerung, Abschnitte a bis f)

Stand 2026-11-17: Schritt 0 und alle Abschnitte a bis f gebaut, Stufe «Klein» je Abschnitt bestanden, Prüfstufe «Baustein» bestanden, mit Lastprobe (Lauf durch Lotse, Abschnitt «Prüfstufe Baustein» unten). **Offen:** Gerätetest mit Lotse und Nutzer (Liste unten), Installation des Dienstes durch den Nutzer (N7). Plan `docs/archiv/prompts/P15-kamera.md`, Checkliste des Reiters `docs/archiv/prompts/P15-e-checkliste.md`.

## Gesamtübersicht

| Abschnitt | Inhalt | Stand |
|---|---|---|
| Schritt 0 | Lotse am Gerät (2026-11-15): Konstanten, Statusblock, Wecken/Schlafen, 4,5 s nach dem Wecken, Schwelle zum Einschlafen −76° (Grenze −66°), Python 3.14.8, Port 8093 frei, Firewall ohne offenen Bereich | erledigt, Ergebnis im Plan |
| a1–a3 | Dienst: Grundgerüst (`dienst.ini`, Sperre, HTTP mit Absenderprüfung, `GET status`, Attrappe), V4L2 und Geräte (`by-id`, Ein-/Ausstecken), Bildwerte mit gemerkten Werten | gebaut, am Gerät geprüft (Prüfung a) |
| b1–b7 | Dienst: OBSBOT (Statusblock, Wecken, Schlafen, HDR), Bewegen mit Schutzregeln und Totmann, Kamera-Presets; b4/b5 Dauerbewegung der Tiny 2 über Geschwindigkeit mit gemessener Tempokurve; b6 Zone aus `ip route get`; b7 Bildwinkel, «KI aus» vor Bewegung, Geräte ohne Werte ausgeblendet | gebaut, am Gerät geprüft (Prüfung b, b5, b7) |
| c1–c4 | Dienst: Licht und Licht-Presets, Ereignisse (SSE) und Export, `docs/09-kamera-dienst.md`, Installation (`installieren.sh`, `system-einrichten.sh`, Einheit), Einstellungen je Kamera | gebaut, am Gerät geprüft (Prüfung c); Firewall-Regel vom Nutzer eingerichtet 2026-11-16 |
| d1–d3 | Studio: Bibliothek `dss_camera` (`CameraServiceClient`), Kern-Anbindung (`kern.ini [kamera-dienst]`, Schnittstelle Version 19, Ereignisfluss, `dss-cli kamera-…`), Dialog «Einstellungen», Bereich «Kamera-Dienst» | gebaut, nur gegen die Attrappe |
| e1–e4 | Studio: Checkliste und Ansichtslogik `cameraview` (e1), KI-Verfolgung «Aus»/«Oberkörper» (e1b), Reiter «Kameras» links mit Dialog «Kamera einrichten» (e2), Mitte «Bildwerte», rechts «Licht», Dialog «Lampen» (e3), Nacharbeit nach der UI-Prüfung mit Entscheiden 9 bis 14 und `POST lights/check` (e4) | gebaut, nur gegen die Attrappe; UI-Prüfung nach e2/e3 durch einen getrennten Prüfer |
| f1 | Schritte «Kamera-Position» (`camera_preset`) und «Licht-Preset» (`light_preset`) im Aktionssystem | gebaut, nur gegen die Attrappe |
| f2a | Reihenfolge der schreibenden Anfragen je Lampe und Kamera im Kamera-Client (Punkt 129), Kopfzeile «Kameras» mit zweiter Zeile über beide Spalten | gebaut, nur gegen die Attrappe |
| f2b | Dokumente `01` bis `09` nachgeführt, diese Übersicht | erledigt (nur Doku) |
| Prüfstufe «Baustein» | Plan 12; dazu Fix der Attrappe (Punkt 135), erwartete Zeilen in `scripts/startpruefung.py`, Lastprobe `kd_last` | bestanden; `kd_last` bestanden (Lauf durch Lotse) |

### Offene Punkte

- In `docs/04-offene-punkte.md` aufgenommen (Entscheid Lotse 2026-11-17: aufnehmen, nicht bauen): **130** Warten, bis die Bewegung fertig ist; **131** `stop` kann `move` überholen; **132** «Kamera» als Ganzes hält beim Wecken der Tiny 2 andere Kameras auf; **133** Dauerbewegung während eines laufenden Ziels (b4); **134** weitere KI-Modi erst nach Test am Gerät; **136** `zoom_continuous` meldet 245 (b6); **137** Schlüssel der Bildwerte nicht an den echten Kameras geprüft.
- Punkt 129 erledigt in f2a, Punkt 135 (`kd_presets` scheiterte einmal) in der Prüfstufe «Baustein» (Archiv `docs/archiv/04-erledigte-punkte.md`). Neu aus der Prüfstufe: **138** `tst_fensterchatbot::chatEmotesLoading` scheiterte einmal (nicht P15).
- `[OFFEN]` Lampe links fiel im WLAN aus (Prüfung c); kein Fehler des Dienstes, beobachten.
- `[OFFEN]` Die Rechte von Lotse am Streaming-PC für P15 (Plan, Frage 4 A: «wird in `CLAUDE.md`, «Umgebung», eingetragen») stehen nicht in `CLAUDE.md`. Nicht Teil von f2b (nur `docs/`), Entscheid Lotse.
- `[OFFEN]` Nicht als Fenstertest abgedeckt: Fehler einmal je Halten (e3, nur ohne Fenster), Weg über den Kern-Socket mit langsamer Lampe (f2a, geprüft ist der Client).
- Reste ohne Freigabe zum Löschen: `build/bilder-e4` (leer, e4), `build/scratch-f1-teil.txt` (f1).

### Prüfstufe Baustein (2026-11-17)

Stand `lotse/p15` nach f2b, dazu die drei Änderungen unten. Studio-PC, X11-Sitzung aktiv (`loginctl show-session 1`: `Type=x11`, `Active=yes`, `LockedHint=no` vor jedem Fensterlauf; `wiederholen.sh` prüft vor jedem Lauf). Kein anderer Agent, keine Bauten oder Tests daneben, die Läufe nacheinander, nie zwei Fenstertests gleichzeitig; Last am Ende 0,14/0,34/0,32. Rohdaten unter `build/pruefung-p15/` (nicht im Repo).

| Stufe | Befehl | Ergebnis |
|---|---|---|
| Bau | `scripts/bauen.sh -j 8` (vor und nach den Änderungen) | «Bau ok (0 Warnungen)» |
| Tests | `scripts/testen.sh` (ohne Filter), vor den Änderungen | **88 von 88**, keiner übersprungen, 461 s |
| Tests | dasselbe nach den Änderungen | **87 von 88**, 459 s: `tst_fensterchatbot` (`chatEmotesLoading`, Zeitgrenze 5000 ms) einmal gescheitert, Wiederholung `scripts/testen.sh -R tst_fensterchatbot` **1 von 1**, 26 s. Nicht P15, Punkt 138 |
| Tests `kd_*` | `scripts/testen.sh -R ^kd_` nach dem Fix | **10 von 10**, 63 s |
| Frischer Klon | `/var/tmp/dss-klon-p15` (Stand e9a44dd, von Lotse angelegt): `cmake -S … -B …/build`, `cmake --build …/build -j8`, `ctest --test-dir …/build` | Bau ohne Fehler und ohne Warnung; **88 von 88**, keiner übersprungen, 484 s |
| Startprüfung | `python3 scripts/startpruefung.py --runs 20 --out build/pruefung-p15/start/startpruefung` (11:52–11:54) | je **20 von 20** sauber: `core-sigterm` (Start Median 47,7 ms, Ende 8,1 ms), `core-sigint` (47,2 / 7,8 ms), `app-sigterm` (478,0 / 32,8 ms), `app-kern-sigterm` (465,8 / 48,7 ms); keine Restprozesse, keine unerwarteten Zeilen. Erster Lauf (11:48) «nicht sauber», siehe unten |
| Dienstprüfung | `python3 scripts/dienstpruefung.py --kills 5 --out build/pruefung-p15/dienst` | **bestanden**. Fenster startet den Kern in 0,520 s (verbunden 0,580 s); Neustart nach `kill -9` 1,136 bis 1,175 s, Fenster wieder verbunden 1,906 bis 2,979 s |
| 50 Wiederholungen | `scripts/wiederholen.sh build <Test> 50 <absoluter Ordner>`, nacheinander 11:57–13:26 | `kd_presets` vor dem Fix **48 von 50** (Lauf 7 und 9, Punkt 135), nach dem Fix **100 von 100** (2 × 50, Ende 12:22:42 und 12:26:27); je **50 von 50**: `kd_bewegen` (vor dem Fix 12:01:01, nach dem Fix 13:12:33), `kd_obsbot` (12:05:30, 13:17:03), `kd_ereignisse` (12:14:39, 13:26:14), `tst_kameradienst` (12:37:42), `tst_kernkamera` (12:42:45), `tst_fensterkameras` (13:01:07) |
| Lastprobe | `ctest --test-dir build -C Lastprobe -R '^kd_last$' -V` | bestanden, 2026-11-17, Lauf durch Lotse ohne Last daneben, 64 s: 600 Anfragen in 60 s, kein 5xx, 11-mal Ein-/Ausstecken. Status 409 nur für ausgesteckte Kameras (controls 39, move 18, recall 35). Ohne Wartezeit nach dem Einstecken 538 Anfragen, max 6,4 ms; in der Wartezeit gesendet 62, max 2,91 s, alle vor dem Ende der Wartezeit beantwortet (max −98 ms danach). Medianwerte 1,3–4,0 ms je Art. Totmann 3-mal gemeldet, keine Verletzung. Tiny 2: 0 Neustarts, 0 verlorene Bewegungen. CPU des Dienstes in % eines Kerns: Mittel 2,6, Median 2,0, p95 5,0, max 5,0. Lampen: 0 Fehler. Nebenbei eine `ResourceWarning` (offene stderr-Pipe im Test), ohne Einfluss |

**Fix Punkt 135 (Attrappe).** Mit 50 Wiederholungen nachgestellt: 2 von 50 gescheitert, beide in `test_presets_over_http` beim Lesen der Gerätedatei (`FakeDeviceFolder.values` → `JSONDecodeError: Expecting value: line 1 column 1`). Ursache: `Path.write_text` leert die Datei zuerst und schreibt dann; der Dienst schreibt die Knotendatei bei jedem Wert neu, der Test liest sie gleichzeitig. Ein Fehler der Attrappe, nicht des Dienstes (am Gerät gibt es keine Datei). `kamera-dienst/dss_camera/fake.py`: neue Funktion `_write_node` schreibt in eine Hilfsdatei `.<knoten>.<pid>.<thread>.tmp` im selben Ordner und tauscht sie mit `os.replace` (rename(2), atomar); alle fünf Schreibstellen der Knotendateien nutzen sie. Die Gerätesuche liest `dev/` nicht (nur `by-id`), die Hilfsdatei wird also nie als Gerät gesehen. Danach 100 von 100 (bei 4 % Fehlerrate wären 0 von 100 mit ≈ 1,7 % Wahrscheinlichkeit Zufall).

**Startprüfung: erwartete Zeilen nachgeführt.** Der erste Lauf meldete alle 80 Starts «nicht sauber», nur wegen unerwarteter Info-Zeilen; Exit-Code, Startzeile, Ende und Restprozesse waren in allen 80 in Ordnung. Die Zeilen: im Kern «Aktionen: …» und «Ton Chat-Bot: Ton (ton-aktionen): …» (P14, jeder Start), «Overlay «Chat-Bot» angelegt» (P14, nur erster Start im leeren Datenordner), im Fenster mit Kern «Emotes geladen: …» (P13, sobald die Listen innerhalb der Laufzeit geladen sind). P14 hatte die Stufe «Baustein» nicht laufen lassen (P14-Bericht, «Nicht geprüft»). `scripts/startpruefung.py`: die festen Zeilen in `CORE_EXPECTED`, die übrigen in der neuen Liste `OPTIONAL` (dürfen vorkommen, müssen nicht). Der zweite Lauf war sauber.

**Lastprobe (neu, `kamera-dienst/tests/test_last.py`, ctest-Eintrag `kd_last` nur mit `-C Lastprobe`).** Der Dienst läuft als eigener Prozess mit Attrappen-Ordner (Tiny 2, C922) und zwei Attrappen-Lampen. 60 s lang alle 100 ms eine Anfrage in eigenem Faden, reihum: `move` der Tiny 2 (Pan und Tilt, Tempo 50) in den ersten 2,5 s jeder 5 s, sonst `GET status`; Bildwerte (abwechselnd `PUT controls` C922 Helligkeit und `GET controls` Tiny 2); Positionsabruf (`recall` je eines Kamera-Presets der Tiny 2 und der C922); Licht (`PUT lights/links`, `PUT lights/all` umschalten, Licht-Preset abrufen). Ab 6 s alle 5 s Ein- oder Ausstecken, abwechselnd Tiny 2 und C922; die Tiny 2 wird so während einer Dauerbewegung ausgesteckt. Prüft: jede Anfrage beantwortet, kein 5xx; keine Antwort über 3 s; null «Neustarts» der Tiny-2-Attrappe; Totmann: Ist die Tiny 2 eingesteckt und läuft kein `move`, steht ihr Tempo spätestens 0,5 s + 0,35 s nach der letzten Antwort auf `move` auf 0 (Abfrage alle 50 ms), und der Dienst meldet mindestens einmal «(Totmann)»; Exit-Code 0. Ausgabe: Anzahl und Status je Art, Antwortzeiten (Median, p95, max), CPU des Dienstes in % eines Kerns (Mittel, Median, p95, max, aus `utime + stime` in `/proc/<pid>/stat`). Ohne `DSS_LASTPROBE=1` besteht der Testfall sofort (ohne `skipTest`, wie `chatLoad` in P13). **Ausnahme von der 3-s-Grenze:** Nach dem Einstecken wartet der Dienst absichtlich, bis die Kamera 3 s da ist (Plan 2.3 Regel 7, `devices.SETTLE_S`, Abfrage alle `POLL_INTERVAL_S` = 1 s). Anfragen an diese Kamera, die vorher gesendet werden, dürfen darum bis 3 s nach dem Ende dieser Wartezeit brauchen; die Ausgabe nennt sie getrennt. Alle anderen Anfragen haben die feste Grenze von 3 s.

**Dienste:** `dss-core.service` war beim ersten Blick (nach dem `ctest` im Klon) `inactive` und ist es am Ende auch; den Zustand vor dem ersten Lauf hat der Agent nicht festgehalten. Die Einheit hat kein `[Install]` und läuft nur, solange ein Fenster sie gestartet hat; keine Prüfung hält sie an (die Dienstprüfung nutzt die eigene Einheit `dss-core-pruefung.service` und entfernt sie wieder). Am Ende kein Prozess `dss-*` und kein Kamera-Dienst aus den Tests.

**Beobachtung:** Der Start des Fensters dauert jetzt im Median 478 ms (`app-sigterm`), in P13 waren es 243 ms. Kein Fehler und keine Grenze überschritten; Ursache nicht untersucht.

### Handtest-Liste für den Gerätetest

Nie während eines Streams (vorher Stream und Aufnahme prüfen). Am Gerät schon erledigt (2026-11-16, nicht wiederholen): `status` und `controls` beider Kameras, Wecken/Schlafen 10×, Preset aus dem Schlaf, Dauerbewegung hält bei −66°, Totmann (332 ms), Tempokurven und ruhiges Anhalten (b5), Bildwinkel und «KI aus» vor Bewegung (b7), Licht-Presets 10× mit Begrenzung (L2), Absender und Firewall-Regel vom Studio-PC (L3).

**Prüft Lotse**

1. **Bilder des Reiters.** `DSS_TEST_BILDER=<ordner> scripts/testen.sh -R '^tst_fensterkameras$'`, dann ansehen: `kameras-e3.png` (Kreuz oben in «Bewegen», Platz rechts unter «Schritt», «KI-Verfolgung» in einer Zeile, Breite rechts, Marke «•»), `kameras-e3-weg.png` (Auswahl nur mit Namen, Zoom und Lampen «–», Hinweis links in der zweiten Zeile, Meldung rechts ganz lesbar), `kameras-e3-c922.png` (Regler ohne Wert ganz links), `kameras-e3-einrichten.png` (sechs Zahlenfelder gleich breit, «Gemerkt»), `kameras-e3-lampen.png` (Meldungszeile), `schritt-kamera.png` (Aktion «DSS-Test Kamera» mit beiden Schritten, Kurzinhalte «Weit» und «Hell», Abschnitt «Kamera und Licht»). Grund: im Bau nicht angesehen.
2. **Dienst am Gerät, Rest.** KI über `PUT cameras/<tiny2>/controls {"obsbot_ai": 1}` (Kamera folgt), `0` (steht, nach ≈ 5 s weiter 0), nicht gemerkt (`remembered: false`, nicht in `kameras.json`, nicht im Preset «mit Bildwerten»), im Schlaf `409 camera_asleep` (e1b); Bewegung direkt nach «KI aus» während des Übergangs 6 (b7, nicht geprüft); Dienst während einer Fahrt beenden hält an (b4); `system-einrichten.sh --trocken <studio-pc>` zeigt die Zone der Kabelkarte (b6); Schlüssel der Bildwerte mit `dss-cli kamera-aufruf GET cameras/<id>/controls` (Punkt 137); `zoom_continuous` der Tiny 2 (Punkt 136). Grund: nur am Gerät.
3. **Studio gegen den Dienst (L4).** Kern und Fenster aus `build/` mit eigenem `XDG_CONFIG_HOME` gegen den Dienst: Kreuz (Klick = Schritt, Halten fährt fliessend und hält beim Loslassen, Pfeiltasten mit Fokus, Richtung von Pfeil und Bild); KI «Oberkörper» im Reiter, danach ein Schritt → Anzeige «Aus», Kamera steht; Bildwerte (C922 Helligkeit ziehen, Weissabgleich von Hand, «Zurücksetzen» eines Bereichs; Tiny 2 Bildwinkel); `dss-cli kamera-preset <id>`; Bild der Kameraquelle in «test 2» vorher/nachher über obs-websocket, Testquelle danach löschen, Szene zurück auf «Szene». Grund: echter Weg Fenster → Kern → Dienst → Kamera, Bild nur in OBS.
4. **Key Lights im Reiter.** Dialog «Lampen» mit den echten Adressen; «Verbindung prüfen» mit echter Adresse vor OK und mit falscher Adresse, die Lampe schaltet dabei nicht; Helligkeit und Farbtemperatur ziehen, «Alle ein»/«Alle aus», Licht-Preset speichern und abrufen, danach Zustand wie vorher. Grund: echte Lampen im Netz.
5. **Aktion mit beiden Schritten.** Aus der Version in Arbeit eine Aktion mit «Kamera-Position» und zwei «Licht-Preset» direkt nacheinander echt auslösen (nicht Probelauf), Tiny 2 im Schlaf; im Verlauf die Meldungen, am Ende gilt das zweite Licht-Preset. Danach einmal mit gelöschtem Preset (Fehler im Verlauf, Lauf geht weiter). Grund: echte Geräte, Wecken bis 10 s, Reihenfolge (f2a).
6. **Neustart des Dienstes (L5).** Eigener Schritt: `systemctl --user restart dss-kamera` am Streaming-PC (erst nach N7). Presets, Lampen und gemerkte Werte unverändert; das Studio verbindet sich von selbst neu. Grund: Speichern und Wiederverbinden im echten Betrieb.

**Prüft der Nutzer**

1. **Installation und Netzregel (N7).** Eigene Schritte: `~/DoNs_Streaming_Studio/kamera-dienst/installieren.sh <streaming-pc>` auf dem Studio-PC; am Streaming-PC `~/.config/dss-kamera/dienst.ini` anlegen (`kamera-dienst/LIESMICH.md`) und `systemctl --user restart dss-kamera`. Die Firewall-Regel steht schon (2026-11-16). Prüfen: Dienst läuft nach der Anmeldung von selbst; ein drittes Gerät im Heimnetz (Handy, Browser auf `http://<streaming-pc>:8093/api/v1/status`) bekommt keine Antwort. Grund: Installation macht nur der Nutzer, drittes Gerät.
2. **Steuerkreuz und Zoom (N1).** Klick und Halten in alle Richtungen, Zoom, «Tempo», «Schritt»; passt die Richtung der Pfeile (sonst «Richtung umkehren», der Standard wird hier festgelegt); Positionen anlegen, abrufen, umbenennen, löschen. Grund: Bedienung und Gefühl.
3. **Aussehen des Reiters.** Mitte und rechte Spalte (Breite rechts, Lage von «Diese Kamera hat das nicht.», Grösse der Liste «Licht-Presets»), «Bewegen» mit Hinweis und «Wecken» neben dem Kreuz, Kopfzeile bei «Dienst weg», soweit nicht schon mit den Entscheiden 9 bis 14 gesehen. Grund: nur im Fenster zu beurteilen.
4. **Bildwerte (N2).** C922 Netzfrequenz 50 Hz (Flackern weg?), Belichtung, Fokus, Weissabgleich; Tiny 2 HDR, Bildwinkel, KI-Verfolgung; Bild in OBS beurteilen. Grund: Aussehen im Stream.
5. **Umstecken (N3).** Eigener Schritt: beide Kameras bei laufendem OBS (ohne Stream) ab- und wieder anstecken. Nach wenigen Sekunden stimmen die gemerkten Werte wieder (C922 50 Hz); Lotse liest danach das Protokoll des Dienstes und `journalctl -k`. Grund: nur der Nutzer kann umstecken.
6. **Schlafen und Wecken (N4).** «Schlafen» und «Wecken» im Reiter, LED und Bild in OBS; Position aus dem Schlaf abrufen. Grund: LED und Kopf sieht nur der Nutzer.
7. **Licht (N5).** Eigene Licht-Presets «Stream» und «Abend» anlegen und abrufen; Regler «Helligkeit» einer Lampe schnell ziehen und loslassen, die Lampe steht danach auf dem letzten Wert (f2a). Grund: Licht im Raum.
8. **Aktionssystem (N6).** Eine eigene Aktion mit «Kamera-Position» und «Licht-Preset» anlegen und echt auslösen (Test-Knopf mit «echt ausführen»), auch die Texte im Menü «Schritt hinzufügen» und den Hinweis ohne Dienst ansehen. Grund: Bedienung des Editors.

Stream Deck: nicht Teil dieses Gerätetests (eigener Auftrag im Stream-Deck-Projekt).

## Prüfung am Gerät a bis c (2026-11-16, 07:20–07:50, Lotse, Nutzer am Streaming-PC)

**Aufbau:** `kamera-dienst/installieren.sh <streaming-pc> --ziel ~/dss-kamera-test/opt --datenordner ~/dss-kamera-test/daten --ohne-einheit` (Exit 0, «Kamera-Dienst 0.1.0, Commit a94e73a»). Testinstanz per SSH mit `XDG_CONFIG_HOME` und `XDG_DATA_HOME` im Testordner, erst ohne `dienst.ini` (nur `127.0.0.1`), für L3 mit Test-`dienst.ini`. Kein installierter Dienst, keine Einheit. Kein Stream, keine Aufnahme (`GetStreamStatus`, `GetRecordStatus`). Kernel-Protokoll (`journalctl -k`) über die ganze Zeit: keine Meldung zu USB, UVC oder Video, kein `EPROTO`, keine Abmeldung.

### Prüfung a: Status und Bildwerte

| Was | Ergebnis |
|---|---|
| `GET status` | Tiny 2, C922 und die Blackmagic-Karte (`ptz: false`) gefunden; Tiny 2 `asleep` |
| `GET controls` | C922 17 Werte, Tiny 2 26 Werte (mit Gruppe «OBSBOT»), Antwort 0,05–0,15 s |
| C922 Netzfrequenz | steht auf 60 Hz (`power_line_frequency 2`); erwartet, in der Testinstanz ist nichts gemerkt |
| Firewall (lesen) | Zone wie in Schritt 0, Port des Dienstes nicht offen |

### Prüfung b: Tiny 2 (L1)

| Was | Ergebnis |
|---|---|
| Wecken/Schlafen 10× | immer `200`; Wecken 1,52–1,76 s, Schlafen 0,25 s; danach jedes Mal Zustand 3. Nutzer: Kopf und LED jedes Mal richtig |
| Preset aus dem Schlaf | `recall` weckt und fährt an, Antwort nach 6,3 s (mit 4,5 s Wartezeit); Position 10° wie gespeichert, kein Neustart (Nutzer) |
| Dauerbewegung nach unten 10 s | 50 × `200`, hält bei −66,0°, bleibt wach |
| Totmann | steht 332 ms nach der letzten Anfrage (Grenze 500 ms) |
| Mit Videostrom (Plan 10, Nr. 15) | Testquelle «DSS Kameratest» in «test 2»: HDR aus/an sichtbar (Nutzer), 3 Schritte hin und zurück sichtbar, Quelle aus/ein ohne Neustart, Zustand dabei immer 1 |
| Bildwinkel mit Videostrom | `04 01 01/02/00`: Status `0x11` folgt (1, 2, 0), Zoom-Byte 5 % und 15 %, HDR bleibt; Nutzer: drei Stufen sichtbar, am Ende wie vorher. **Bestätigt** |
| KI mit Videostrom | `16 02 02 01` → 2/1, Kamera folgt (Nutzer); `16 02 00 00` → kurz 6/1, nach ~5 s 0/0, Kamera steht (Nutzer). Der Wert 6 aus Schritt 0 ist ein Übergang. **Bestätigt** |
| Dauerbewegung im Bild (Nr. 16) | Dienst (Zielposition alle 50 ms, Tempo 20 und 60): **ruckelig** (Nutzer). Direkt: 1°-Schritte alle 50 ms und alle 100 ms ebenfalls unruhiger; `pan_speed` ±40 **am ruhigsten** (Nutzer) |
| Position bei `pan_speed` | `G_CTRL` Pan bleibt während und nach der Bewegung beim letzten gesetzten Wert (Treiber-Zwischenspeicher); der 60-Byte-Statusblock ändert sich durch die Bewegung in keinem Byte |

Testquelle danach gelöscht (`RemoveInput`), Programm zurück auf «Szene», Tiny 2 in der Mitte und im Schlaf.

### Prüfung c: Licht (L2) und Absender (L3)

| Was | Ergebnis |
|---|---|
| Stand vorher | beide an, 24, 220 Mired |
| Lampe links | antwortete zu Beginn, dann nicht mehr (auch kein Ping); Dienst meldet sie richtig `reachable: false`. Nach Stromtrennung durch den Nutzer wieder da (aus, 25/214), der Dienst fand sie ohne Neustart. Lampe rechts: erste Anfrage nach Ruhe > 3 s, danach 0,03 s |
| Presets «Test hell» (80/160) und «Test warm» (15/330) | je 10× abgerufen, ohne Fehler; Antwort Median 181 und 167 ms, höchstens 204 ms; beide Lampen sichtbar gewechselt (Nutzer) |
| Begrenzung | 150/100 → 100/143, 1/400 → 3/344 |
| Danach | beide an, 24, 220 Mired (wie vorher, Nutzer), Test-Presets gelöscht |
| Absender | Streaming-PC über Heimnetz-Adresse `403` mit Protokolleintrag; über `127.0.0.1` `200`; vom Studio-PC zuerst keine Antwort (Firewall) |
| Firewall (Teil von N7) | Nutzer: `sudo …/system-einrichten.sh --zone FedoraWorkstation <studio-pc>` (Trockenlauf vorher gleich), «success» zweimal. Danach vom Studio-PC `200` in 3 ms, Streaming-PC über Heimnetz weiter `403`; `--list-rich-rules`: neue Regel neben 4455 und ssh. Drittes Gerät nicht geprüft |

### Nicht erledigt, `[OFFEN]`

- Erledigt in b4: ~~`[OFFEN]` **Dauerbewegung ruckelt.**~~ Zielposition alle 50 ms ergibt im Bild Stufen. Ruhig ist `pan_speed`/`tilt_speed`, dabei ist die echte Position aber nicht lesbar (weder `G_CTRL` noch Statusblock). Die Grenze unten (−66°, Schlaf bei −76°) braucht dann einen anderen Schutz (z. B. Lage aus Zeit × Tempo mit Reserve, Tilt weiter über Zielpositionen). Entscheid vor dem nächsten Dienst-Abschnitt.
- `[OFFEN]` **`zoom_continuous` der Tiny 2** meldet 245 bei Bereich −100…100: Wert mit Vorzeichen wird ohne Vorzeichen gelesen (vermutlich −11). Befund b6: Der Dienst liest schon mit Vorzeichen (siehe «b6»), die 245 kommen nach dem Code so vom Treiber (nicht geprüft). Am Gerät mit `v4l2-ctl -d <tiny2> -C zoom_continuous` prüfen.
- Erledigt in b7: ~~`[OFFEN]` **Blackmagic-Karte** erscheint in `status` als Kamera (`ptz: false`, keine Werte). Ausblenden (nur UVC-Kameras mit Werten) oder lassen.~~
- Erledigt in b7: ~~`[OFFEN]` Bildwinkel und KI-Modus sind am Gerät bestätigt und können im Dienst freigeschaltet werden (Bildwinkel `04 01 00/01/02`, KI «Oberkörper» `16 02 02 01`, aus `16 02 00 00` mit Übergang 6).~~ KI einschalten: entschieden vom Nutzer 2026-11-17 («Aus» und «Oberkörper»), umgesetzt in e1b.
- Erledigt in b6: ~~`[OFFEN]` **`system-einrichten.sh` ohne `--zone` bricht ab:**~~ Am Streaming-PC sind zwei Zonen aktiv (`FedoraWorkstation` auf der Kabelkarte, `trusted` auf `tailscale0`). Besser die Zone der Schnittstelle wählen, über die der Studio-PC erreicht wird (`ip route get <studio-pc>`), und `LIESMICH.md` nachführen.
- `[OFFEN]` Lampe links fiel im WLAN aus (nur Stromtrennung half). Kein Fehler des Dienstes; beobachten.
- Nicht geprüft: L4, L5, N1–N6 (brauchen Studio-Teil d–f), N7 ausser der Firewall-Regel (Installation durch den Nutzer, drittes Gerät), C922 Netzfrequenz 50 Hz setzen und Umstecken (N2, N3).

### Ausserhalb des Studios

Im Kernel-Protokoll des Streaming-PCs seit dem Start (07:15) 1828 korrigierbare PCIe-Fehler (`AER`, `RxErr`, Physical Layer) einer NVMe; mit den Kameras ohne Zusammenhang. Gehört zum System des Streaming-PCs, nicht zu P15.

## b4: Dauerbewegung über Geschwindigkeit (2026-11-16)

- **Geändert:** `kamera-dienst/dss_camera/motion.py` (Tiny 2: `move` setzt `pan_speed`/`tilt_speed`, Lage aus Tempo × Zeit, Halt vor der Grenze, Halt mit Korrektur des Treiberwerts, Standardtempo je Modell), `obsbot.py` (Messkurve Tempo → °/s), `v4l2.py` (Kennungen), `controls.py` (Tempo nicht in `controls`, Tempo 0 beim Erscheinen), `fake.py` (Attrappe mit echter Lage über die Zeit, Tempo, absolute Ziele mit 62 °/s), Tests `kd_bewegen`, `kd_einstellungen`, `kd_werte`, `docs/09-kamera-dienst.md`, `kamera-dienst/LIESMICH.md`.
- **Geprüft (2026-11-16):** `scripts/testen.sh -R '^kd_'` alle grün (Attrappe). Am Gerät nicht geprüft.
- **Handtest, prüft Lotse:** Dauerbewegung Tiny 2 im OBS-Bild fliessend (Pan, Tilt, schräg, Standard 10 °/s); nach dem Loslassen kein sichtbarer Sprung über ≈ 2°; `tilt -1` mit Tempo 100 hält bei −66° ohne Schlaf; Pan an den Grenzen ±130° ohne Anschlag; Dienst während der Fahrt beenden (`systemctl --user stop`) hält an. Grund: Bewegung und Bild nur am Gerät sichtbar.
- **Interna:** Lage nach der Messkurve statt «Wert = °/s» (Pan 10 → 12,0 °/s, sonst Korrektursprung ≈ 20 %); Tempo nur bei Änderung senden, aber jeden Schritt Kamera und Zustand prüfen (Abbruch nach Regel 6 wie bisher); nach Abbruch sofort Halt mit Korrektur, misslingt er, beim Ende nochmals nur Tempo 0; Halt auch bei `step`, `home`, Presets und beim Beenden des Dienstes; Tempo-Werte aus `controls` ausgeblendet (sonst über `PUT controls` setzbar).
- `[OFFEN]` Startet eine Dauerbewegung, während ein absolutes Ziel (`step`, Preset) noch fährt, beginnt die Schätzung beim Ziel statt bei der echten Lage.
- **Quellen:** Messwerte am Gerät (Lotse, 2026-11-16, über das OBS-Bild); https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/ext-ctrls-camera.html (`V4L2_CID_PAN_SPEED`, `V4L2_CID_TILT_SPEED`, nicht abgerufen).

## b5: Tempokurven nachgemessen (2026-11-16)

Nach b4 springt das Bild der Tiny 2 beim Loslassen um 3–6° (wächst mit der Fahrzeit): Die alte Kurve setzte das Tempo zu hoch an (Pan 10 → 12,0 statt 10,87 °/s), die geschätzte Lage lief der echten voraus, die Korrektur nach dem Halt fuhr sichtbar nach.

Messung durch Lotse am 2026-11-16 über das OBS-Bild: Fahrt mit `…_speed v` für 1 s und für 4 s, dann `…_speed 0`; Tempo = (Lage 4 s − Lage 1 s) / 3 s, Versatz = Rest bei 1 s.

| Achse | Wert | °/s | Bemerkung |
|---|---|---|---|
| Pan | 5 | 5,32 | Versatz 0,04–0,2 s, streut |
| Pan | 10 | 10,87 | |
| Pan | 20 | 22,78 | |
| Pan | 30 | – | ungültig (Anschlag ≈ 130°) |
| Pan | 40 | 42,6 | aus der ersten Messung, nicht wiederholt |
| Pan | 80 | 84,8 | aus der ersten Messung, nicht wiederholt |
| Tilt | 5 | 4,58 | Versatz +0,05 s |
| Tilt | 10 | 9,96 | Versatz +0,02 s |
| Tilt | 20 | ≈ 20,5 | nur die 1-s-Fahrt gültig (4 s über den Kippbereich) |
| Tilt | 40 | – | ungültig (Bild-Tracking verloren) |

- **Geändert:** `kamera-dienst/dss_camera/obsbot.py` (`PAN_SPEED_CURVE`, `TILT_SPEED_CURVE`, Kommentar), `kamera-dienst/tests/test_bewegen.py` (erwartete Lagen aus den Konstanten statt fester Zahlen, neuer Test `test_speed_curve`). `fake.py` und `motion.py` nutzen die Konstanten schon, unverändert.
- **Geprüft (2026-11-16):** `scripts/bauen.sh` und `scripts/testen.sh -R '^kd_'`, Ergebnis in der Rückmeldung an Lotse. Am Gerät nicht geprüft.
- **Korrektur nach dem Halt (Befund, nicht geändert):** Im Code gibt es keine Schwelle von 2° (die «≈ 2°» aus b4 sind nur das Prüfkriterium im Handtest). Beim normalen Halt sendet `Motion._advance_speed` Tempo 0 und danach immer den Treiberwert absolut auf die geschätzte Lage, gerundet aufs Raster und auf die Grenzen begrenzt (`Axis.raw`), egal wie gross die Abweichung zur echten Lage ist. Wird die Grenze im nächsten Schritt überschritten, geht der Wert absolut auf die Grenze. Nur nach einem Abbruch beim zweiten Versuch (Ende der Bewegung) gibt es keine Korrektur, nur Tempo 0.
- **Interna:** Kein Anfahrversatz in der Schätzung (streut ±0,1 s, Vorzeichen nicht stabil). Über dem letzten Messpunkt gilt weiter die Steigung des letzten Abschnitts (Tilt über 20).
- **Sprung nach dem Loslassen (erledigt, Prüfung am Gerät 2026-11-16, Lotse und Nutzer):** Das OBS-Bild läuft ≈ 0,5 s hinter der Kamera (Anfahrt im Bild 0,55–0,58 s nach dem ersten `move`, letzte Bewegung 0,47–0,54 s nach `stop`). Die frühere Messung las «vor der Korrektur» 0,25–0,45 s nach dem Halt ab, also noch während der Fahrt im Bild; die 3–6° waren grösstenteils ein Fehler der Messung. Aus dem Bildverlauf (`p15_b4.py zeit pan|tilt 2`, Tempo 10): letzter Schritt Pan 1,5°/1,7° statt 1,2°, Tilt 1,9° statt ≈ 0,95°, also echte Korrektur ≈ +0,3–0,5° (Pan) und ≈ −1° (Tilt). Der Nutzer sah bei Pan 2 s, Tilt 2 s und Pan 4 s ein ruhiges Anhalten. `p15_b4.py start|unten|rand` bestanden: Tilt Tempo 100 hält bei −66° (Zustand wach), Pan Tempo 100 hält bei 130°.
- **Quellen:** Messwerte am Gerät (Lotse, 2026-11-16, über das OBS-Bild).

## b6: kleine Punkte (2026-11-16)

Zwei kleine Punkte in einem Commit (Regel 8): beide aus «Nicht erledigt», je eine Stelle, ohne Änderung an einer Schnittstelle.

- **`zoom_continuous` (nicht behoben, Befund):** `v4l2_control.value` und die Grenzen in `v4l2_queryctrl` sind in `dss_camera/v4l2.py` schon `ctypes.c_int32`, wie `__s32` in `<linux/videodev2.h>` des Studio-PCs (`kd_v4l2` vergleicht Grösse und Lage mit dem Header). `get_ctrl` gibt den Wert unverändert weiter, `GET controls` ebenso. Eine −11 vom Treiber käme als −11 an; die 245 stammen damit vom Treiber oder vom Gerät. Neu: Test `test_signed_values` in `tests/test_v4l2.py` (−11 und Minimum −100 kommen negativ zurück). Ursache am Gerät klären, `[OFFEN]` oben.
- **`system-einrichten.sh`:** Ohne `--zone` nimmt das Skript die Zone der Schnittstelle aus `ip route get <studio-pc>` (`dev`), über `firewall-cmd --get-zone-of-interface`, und meldet sie. Gibt es keine Route oder hat die Schnittstelle keine Zone, gilt wie bisher die einzige aktive Zone, sonst Abbruch mit Meldung. `--zone` hat Vorrang. Im Trockenlauf wird die Zone ebenso ermittelt, wenn `firewall-cmd` vorhanden ist, sonst bleibt `<aktive-zone>`. Kopf des Skripts und `LIESMICH.md` nachgeführt. Neu: Test `test_dry_run_zone_from_route` in `tests/test_installieren.py` (Ersatz für `ip` und `firewall-cmd`, zwei aktive Zonen).
- **Geprüft (2026-11-16):** `scripts/bauen.sh` (0 Warnungen), `scripts/testen.sh -R '^kd_'` 10 von 10, `bash -n kamera-dienst/system-einrichten.sh`. Am Streaming-PC nicht geprüft (Lauf mit sudo und echter Route).
- **Handtest, prüft Lotse:** `…/system-einrichten.sh --trocken <studio-pc>` (ohne sudo) am Streaming-PC zeigt die Zone der Kabelkarte (oder `<aktive-zone>`, falls `firewall-cmd` ohne sudo nicht antwortet), und `v4l2-ctl -C zoom_continuous` an der Tiny 2. Grund: Route, Zonen und Treiber gibt es nur dort.
- **Interna:** Rückfall auf «einzige aktive Zone», damit Rechner mit einer Zone ohne Bindung der Schnittstelle (Standardzone) weiter gehen; im Trockenlauf Route nur, wenn `firewall-cmd` vorhanden ist (Studio-PC ohne firewalld: Platzhalter wie bisher).
- **Quellen (nicht abgerufen):** https://firewalld.org/documentation/man-pages/firewall-cmd.html (`--get-zone-of-interface`), https://man7.org/linux/man-pages/man8/ip-route.8.html (`ip route get`), https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-g-ctrl.html (`__s32 value`, steht schon in `v4l2.py`).

## b7: Bildwinkel, KI aus, Geräte ohne Werte (2026-11-16)

Drei Punkte aus «Nicht erledigt», entschieden vom Nutzer am 2026-11-16, in einem Commit (Regel 8): Alle ändern die Liste der Kameras und Werte (`status`, `GET controls`) in derselben Version der Schnittstelle.

- **KI aus vor Bewegung:** `obsbot.AI_OFF_UNLOCKED = True`. Der Dienst sendet `16 02 00 00` (Selektor 0x06) einmal je Bewegung (Start einer Dauerbewegung, Schritt, Ziel, Grundstellung, Preset), nicht je Takt, und wartet nicht auf den Status 0/0 (Übergang 6 ≈ 5 s). Einschalten bietet der Dienst nicht an.
- **Bildwinkel:** neuer Wert `obsbot_fov` in der Gruppe «OBSBOT», «Bildwinkel», Typ `menu` (0 «Weit», 1 «Mittel», 2 «Eng»), Wert aus Status-Byte 0x11, Befehl `04 01 xx`, Setzen mit Rücklesen (wie HDR, höchstens 1 s), nur im Zustand «wach», gemerkt und beim Erscheinen neu gesetzt, `null` = «Weit» und vergessen. Kommt wie HDR in Presets «mit Bildwerten» mit.
- **Geräte ohne Werte:** Ein abgefragtes Gerät ohne einstellbaren Wert (kein V4L2-Wert eines bekannten Typs, der weder abgeschaltet noch nur lesbar ist; nicht die Tiny 2) fehlt in `status` (und damit im Ereignis `status`), auch ausgesteckt. Einmal je Gerät im Protokoll: «Gerät <id> (<modell>) ohne einstellbare Werte, ausgeblendet.» Allgemein, nicht über den Namen. Nicht abfragbare Geräte bleiben sichtbar (mit Fehler).
- **Geändert:** `kamera-dienst/dss_camera/obsbot.py` (Freischaltung, Bildwinkel, gemeinsames Setzen mit Rücklesen, Docstring), `controls.py` (Setzen und Neusetzen über `obsbot.set_value`, Meldung im Schlaf mit Namen des Werts), `devices.py` (`Camera.hidden`, `adjustable`, Filter in `status`, Protokoll), `v4l2.py` (`V4L2_CTRL_FLAG_READ_ONLY`), `motion.py` (Docstring), `fake.py` (Attrappe: Bildwinkel im Status 0x11, KI-Modus `ai`), Tests `tests/test_obsbot.py` (`test_fov`, `test_status_block`, `test_locked_functions`), `tests/test_bewegen.py` (`test_ai_off_before_move`), `tests/test_v4l2.py` (`test_device_without_values_hidden`), `tests/test_presets.py`, `tests/kamera_ioctl_werte.cpp` (neue Konstante gegen den Header), `docs/09-kamera-dienst.md`, `kamera-dienst/LIESMICH.md`.
- **Geprüft (2026-11-16):** `scripts/bauen.sh` (Bau ok, 0 Warnungen), `scripts/testen.sh -R '^kd_'` 10 von 10 (Attrappe, Agent und Lotse). **Am Gerät (Lotse, 2026-11-16, Testinstanz):** `GET status` ohne Blackmagic-Karte, C922 und Tiny 2 da, Meldung einmal im Protokoll; `obsbot_fov` 0/1/2 zurückgelesen 0/1/2, im OBS-Bild weit/mittel/eng bestätigt; Bewegung mit «KI aus» davor: Anfahrt im Bild nach 0,40–0,53 s (vorher ohne KI-Befehl 0,55–0,58 s, `p15_b4.py zeit pan|tilt 2`), also keine Verzögerung. Nicht geprüft: Bewegung, während die KI-Verfolgung vorher aktiv war (Übergang 6).
- **Handtest, prüft Lotse:**
  - Bewegung direkt nach «KI aus»: Tiny 2 mit KI «Oberkörper» (`16 02 02 01`), dann `move`/`step` über den Dienst; folgt die Kamera dem Befehl sofort, oder verwirft sie ihn während des Übergangs (Status 6)? Grund: nur am Gerät sichtbar, **nicht geprüft**.
  - Bildwinkel über `PUT controls {"obsbot_fov": 0/1/2}`: Bild weit, mittel, eng im OBS-Bild; Zuordnung bestätigen. Grund: Annahme aus dem Zoom-Byte.
  - `GET status` am Streaming-PC: Blackmagic-Karte fehlt, C922 und Tiny 2 da, Meldung im Protokoll einmal. Grund: Werte der echten Karte nur dort lesbar.
- **Annahmen, Interna-Entscheide:** 00 = weit, 01 = mittel, 02 = eng (am Gerät bestätigt). Standard «Weit» ist eine Annahme (Werkseinstellung nicht bekannt). Unbekannter Wert in Byte 0x11 erscheint als Menüeintrag «Unbekannt (n)». Ausgeblendete Geräte bleiben intern bekannt (`GET cameras/{cam}/…` mit ihrer Kennung antwortet weiter), nur die Liste blendet sie aus. Prüfung «einstellbar» in `devices.py` statt über `controls.supported`, weil `controls` `devices` importiert; Tempo-Werte zählen dort mit, kommen aber nur an der Tiny 2 vor. Eine Anfrage `GET cameras` gibt es nicht, die Liste steht in `GET status`.
- **Quellen:** Messwerte am Gerät (Prüfung b, Zeilen «Bildwinkel mit Videostrom» und «KI mit Videostrom», 2026-11-16); https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/vidioc-queryctrl.html (`V4L2_CTRL_FLAG_READ_ONLY` 0x0004, nicht abgerufen; Wert durch `kd_v4l2` gegen den Header des Studio-PCs geprüft).

## e1b: KI-Verfolgung einschalten (2026-11-17)

Entscheid 5 des Nutzers vom 2026-11-17 (`docs/archiv/prompts/P15-e-checkliste.md`, Abschnitt 4): Die KI-Verfolgung ist bei der Tiny 2 bedienbar, nur «Aus» und «Oberkörper», ohne Tempo. Ein Commit (Regel 8): Dienst, Ansichtslogik und Checkliste betreffen denselben neuen Wert.

- **Geändert:**
  - Dienst: `kamera-dienst/dss_camera/obsbot.py` (Wert `obsbot_ai` in der Gruppe «OBSBOT», «KI-Verfolgung», Typ `menu` 0 «Aus», 1 «Oberkörper»; Status 0x18/0x1c; `set_ai` mit Rücklesen höchstens 1 s; `REMEMBERED` getrennt von `WRITABLE`), `controls.py` (KI nicht merken, trotzdem Ereignis `controls`; Neusetzen nur gemerkter Werte), `presets.py` (KI nicht in Presets «mit Bildwerten»), `fake.py` (Attrappe: KI-Modus im Status, «aus» mit Übergang 6/1 und nach 5 s 0/0).
  - Tests: `kamera-dienst/tests/test_obsbot.py` (`test_ai` neu, `test_status_block`, `test_locked_functions`), `test_bewegen.py` (Übergang 6 nach «KI aus»), `test_presets.py` (Kommentar).
  - Ansichtslogik: `src/common/cameraview.{h,cpp}` (`cameraControlView` mit `controls`: `aiEnabled`, `aiValue`, `aiMenu`, `aiHint`; `cameraAiCall`), `tests/tst_kameraansicht.cpp`.
  - Doku: `docs/09-kamera-dienst.md`, `kamera-dienst/LIESMICH.md`, Checkliste `docs/archiv/prompts/P15-e-checkliste.md` (Abschnitt 4 «Entscheide des Nutzers», Abschnitt 1, Baum, Zeilen zur KI und zu den übrigen Entscheiden), dieser Bericht.
- **Geprüft (2026-11-17, Studio-PC, Stufe «Klein»):** `scripts/bauen.sh` Bau ok, 0 Warnungen; `scripts/testen.sh -R '^kd_'` 10 von 10; `scripts/testen.sh -R '^tst_kameraansicht$'` 1 von 1; `scripts/testen.sh` 87 von 87 (424 s, X11-Sitzung aktiv, nicht gesperrt). Nur gegen die Attrappe; am Gerät **nicht geprüft**.
- **Handtest, prüft Lotse** (Tiny 2 wach, nie während eines Streams; Grund: nur am Gerät und im Bild sichtbar):
  - **Einschalten:** `PUT cameras/<tiny2>/controls {"obsbot_ai": 1}` → Antwort `obsbot_ai` = 1; im OBS-Bild folgt die Kamera dem Oberkörper.
  - **Ausschalten:** `PUT … {"obsbot_ai": 0}` → Antwort sofort 0 (Übergang 6 gilt als «Aus»); die Kamera steht; nach ≈ 5 s `GET controls` weiter 0.
  - **Nicht gemerkt:** danach `GET controls` mit `remembered: false` für `obsbot_ai` und `kameras.json` der Testinstanz ohne `obsbot_ai`; ein Preset «mit Bildwerten» ohne `obsbot_ai`.
  - **Schlaf:** im Schlaf `PUT … {"obsbot_ai": 1}` → `409 camera_asleep`.
- **Handtest, prüft der Nutzer:** kein Handtest nötig (die Bedienung im Reiter kommt mit e2).
- **Nicht erledigt, `[OFFEN]`:**
  - Das Ausschalten vor einer Bewegung (`ai_off_before_move`) meldet kein Ereignis `controls`; eine offene Ansicht zeigt danach noch «Oberkörper», bis die Werte neu gelesen werden. Für e2 zu entscheiden (z. B. Werte nach einer Bewegung neu lesen oder `on_change` nach `tracking_off`, dann aber bei jeder Bewegung).
  - Die Zeile «Vor dem Bau von e2: Antworten einholen» aus dem Auftrag gibt es in der Checkliste und im Repo nicht (Suche nach «Antworten einholen» und «Vor dem Bau von e2» ohne Treffer); abgedeckt durch Abschnitt 4 «Entscheide des Nutzers».
- **Annahmen, Interna-Entscheide:**
  - Byte 0x18 = Modus, 0x1c = Untermodus: aus dem Plan («Ergebnis Schritt 0», Recherche `tiny2`, Schritt 0 Zeile 12: `16 02 02 00` → 2/0, `16 02 02 01` → 2/1); die Paare 2/1, 6/1, 0/0 sind mit Videostrom bestätigt.
  - Übergang 6 wird als «Aus» angezeigt: Nach dem Befehl «aus» folgt die Kamera laut Prüfung b nicht mehr; als «Unbekannt» würde die Auswahl nach jeder Bewegung 5 s springen. Rücklesen nach «Aus» kehrt darum sofort zurück.
  - Modus 0 gilt mit jedem Untermodus als «Aus». Andere Paare (z. B. 2/0 aus einem anderen Programm) erscheinen als zusätzlicher Eintrag «Unbekannt (m/u)», Wert 256 · m + u (ausserhalb der Auswahl, wie beim Bildwinkel).
  - Nicht gemerkt, nicht neu gesetzt, nicht in Presets: Das weicht absichtlich von `obsbot_fov` ab, weil die Verfolgung ein Zustand ist (Auftrag). Dafür trennt `obsbot.REMEMBERED` (HDR, Bildwinkel) die gemerkten von den schreibbaren Werten; ein Setzen ohne Merken löst trotzdem das Ereignis `controls` aus.
  - Ansicht: `aiHint` «Nur bei wacher Kamera» (derselbe Text wie der Tooltip der OBSBOT-Bildwerte) bei schlafender, fehlender oder unbekannter Tiny 2; ohne gelesene Werte (`controls` leer) nicht bedienbar und ohne Hinweis. `cameraControlView` bekommt die Werte als vierten Parameter mit Standard `{}`, damit bestehende Aufrufe gleich bleiben.
  - Attrappe: Start 2/1 (Verfolgung an, Annahme wie bisher); «aus» bei 0 oder 6 ändert nichts.
- **Quellen:** Messwerte am Gerät (Prüfung b, Zeile «KI mit Videostrom», 2026-11-16); Plan `docs/archiv/prompts/P15-kamera.md` («Ergebnis Schritt 0», Zeile 12); https://github.com/cgevans/tiny2 (Statusbytes 0x18/0x1c, über den Plan, nicht neu abgerufen).

## e2: Reiter «Kameras» mit Kopfzeile, linker Spalte und «Kamera einrichten» (2026-11-17)

Ein Commit (Regel 8): Kern-Client, Ansichtslogik, Reiter und Dialoge gehören zum selben Baustein «Reiter mit linker Spalte»; ohne `callCamera` ist der Reiter nicht bedienbar.

- **Geändert:**
  - Kern-Client: `src/common/coreclient.{h,cpp}` (`callCamera(CameraCall)` vergibt `requestId`, Signal `cameraResultReceived(call, result)`, offene Anfragen fallen bei der Trennung weg).
  - Ansichtslogik: `src/common/cameraview.{h,cpp}` (`cameraPresetGetCall`, `…RenameCall`, `…DeleteCall`, `cameraConfigPutCall`, `cameraResultError`, `cameraSpeedSuffix`, `cameraConfigRange`, `cameraConfigChanges`, `cameraRememberedRows`; `CameraData::applyResult` für `GET camera-presets/<id>`, `cameraPresetsWithoutDetails`, `noteCallSent`), Test `tests/tst_kameraansicht.cpp` (5 Fälle neu).
  - Reiter: `src/app/camerapage.{h,cpp}` neu, `headerbar.{h,cpp}` (dritter Modul-Reiter «Kameras»), `mainwindow.{h,cpp}` (Seite in `m_modules`, Statuszeile), `theme.cpp` (Zustand und Hinweise, Listen, Bildlauf links).
  - Dialoge: `src/app/camerasetupdialog.{h,cpp}`, `src/app/presetnamedialog.{h,cpp}` neu.
  - Symbole: `src/app/symbole/nach-links.svg`, `nach-rechts.svg`, `nach-links-oben.svg`, `nach-rechts-oben.svg`, `nach-links-unten.svg`, `nach-rechts-unten.svg` (gedrehte Fassungen von `nach-oben.svg`).
  - Test `tests/tst_fensterkameras.cpp` neu, `tests/CMakeLists.txt`.
- **Geprüft (2026-11-17, Studio-PC, Stufe «Klein»; X11-Sitzung 1 aktiv, nicht gesperrt, `loginctl show-session 1`):** `scripts/bauen.sh` Bau ok, 0 Warnungen; `scripts/testen.sh -R '^tst_kameraansicht$'` 1 von 1; `scripts/testen.sh -R '^tst_fensterkameras$'` 1 von 1 (9 s); `scripts/testen.sh` 88 von 88 (436 s). Danach UI-Prüfung (ui-consistency:verifying, ein eigener Prüfer, nur Code gelesen, nicht gerendert, ohne Kalibrierung mit gepflanztem Fehler); übernommen: «Neuer Name» beim Umbenennen, Tooltips an den einzeiligen Hinweisen, Namen für Screenreader an den vier Grenzfeldern, Projekttext «Keine Verbindung zum Kern, Änderung nicht geschickt», Abstände 8 statt 16/24. Danach `scripts/bauen.sh` 0 Warnungen, `scripts/testen.sh -R '^(tst_fensterkameras|tst_kameraansicht|bildschirmfoto)$'` 3 von 3. Nur gegen die Attrappe; am Gerät **nicht geprüft**. Bild `kameras-e2.png` **nicht selbst angesehen** (der Lauf mit `DSS_TEST_BILDER` ist Lotse vorbehalten).
- **Handtest, prüft Lotse** (Tiny 2 und C922 am Streaming-PC, nie während eines Streams):
  - **Bild:** `kameras-e2.png` mit `DSS_TEST_BILDER` ansehen (Kopfzeile, Kreuz, Leisten in zwei Zeilen).
  - **Kreuz am Gerät:** Klick = ein Schritt, Halten fährt fliessend und hält beim Loslassen; Pfeiltasten bei Fokus auf dem Kreuz; Richtung von Pfeil und Bild (Grund: nur im OBS-Bild sichtbar).
  - **KI:** «Oberkörper» wählen, folgt; danach ein Schritt → Anzeige «Aus», Kamera steht.
- **Handtest, prüft der Nutzer:** Aussehen und Bedienung des Reiters (Grösse des Kreuzes, Lage von «Tempo»/«Schritt», Breite der linken Spalte), weil das nur im Fenster zu beurteilen ist.
- **Nicht erledigt, `[OFFEN]`:**
  - «Presets wecken die Kamera»: Der Dienst hat dafür kein Feld (`recall` weckt die Tiny 2 immer, `auto_wake` betrifft nur Bewegen und ist Standard aus). Das Kästchen steht angekreuzt und nicht bedienbar da. Zu entscheiden: Feld im Dienst, auf `auto_wake` umdeuten (anderer Text) oder weglassen.
  - Ein Fehler des Dienstes bei der Fahrt (z. B. `409` während Halten) steht bei jeder Wiederholung neu in der Statuszeile (gleicher Text).
  - Aus der UI-Prüfung, sichtbar, darum für den Nutzer: «Schritt» (Reiter) und «Schrittweite» (Dialog) für denselben Wert; «Presets wecken die Kamera» und «auch aus Positionen» im Dialog, im Reiter heisst es «Positionen»; Spaltenkopf «gemerkt» klein (wie im Entwurf); die zweite Zeile des Zustands ist nur bei Fehler rot, sonst grau; im Dialog stehen Name, Grenzen und «Bewegen» in getrennten Layouts (Fluchten nur im Bild prüfbar).
  - Checkliste nachführen: Baum «links» jetzt mit Bildlaufbereich; [OFFEN] «Grenzen im Dialog» mit «Standard» als Sonderwert umgesetzt; `aiHint` ohne eigene Farbe (immer grau, `CameraControlView` hat keinen Zustand dafür).
- **Annahmen, Interna-Entscheide:**
  - Ganze Presets nach dem Start: einzeln mit `GET camera-presets/<id>` für jedes Preset ohne Kamera, je Verbindung einmal (nicht `GET export`: das liefert die Dateien des Dienstes, deren Format die Schnittstelle nicht zusichert). Eine Antwort ersetzt nur ein bekanntes Preset; ein inzwischen gelöschtes kommt nicht zurück.
  - `ai_off_before_move` (Entscheid Lotse): `CameraData::noteCallSent` setzt `obsbot_ai` nach `move`, `step`, `home` einer Kamera und `recall` eines Presets dieser Kamera auf 0; ohne gelesene Einstellungen gilt der Standard (an).
  - Grenzen im Dialog: «Standard» als Sonderwert des Zahlenfelds (`setSpecialValueText`, kleinster Wert = untere Grenze − 1 °) schickt `null`. OK schickt nur geänderte Felder und schliesst erst nach `200`; sonst Meldung im Dialog. «Vergessen» wirkt sofort (`PUT …/controls {key: null}`), nicht erst mit OK.
  - Liste «Gemerkte Werte»: Name aus `label` des Dienstes (z. B. «Farbtemperatur»), Anzeige von Wert und Standard wie in der Mitte.
  - «Tempo» und «Schritt» in «Bewegen» sind auch bei schlafender Kamera bedienbar (Einstellungen, `setupEnabled`), nur bei fehlendem Bereich in `ranges` grau. Jede Änderung schickt sofort `PUT …/config`.
  - Linke Spalte in einem Bildlaufbereich (wie `#propertiesScroll`): Ohne ihn stieg die Mindesthöhe des Fensters auf 1139 px, `bildschirmfoto` scheiterte (erwartet 1280x800).
  - Fehler einer Anfrage ohne eigene Behandlung (nicht `GET`): Statuszeile «Kamera-Dienst: <Meldung>». Speichern, Überschreiben, Umbenennen: «Gespeichert: <Name>», Löschen: «Gelöscht: <Name>» (`savedStatusText`, `deletedStatusText`).
  - Grosser Richtungsknopf: Seite 3 × Schrifthöhe, Symbol 1,5 × Schrifthöhe. Zoom «−»/«+» ohne Symbol (Ersatztext), «Mitte» als Text.
  - Hinweis über den Spalten (Dienst weg) in `@stateError@`, Hinweis «Kamera schläft» in `@stateConnecting@`, übrige Hinweise `@textMuted@`; keine neuen Theme-Werte.
  - Test: Der Kamera-Dienst kommt über `kern.ini` (`[kamera-dienst]`) wie in `tst_kernkamera`, nicht über den Dialog «Einstellungen»; eigener Kern, darum kein Abschalten am Ende nötig. Die Tiny 2 der Attrappe hat keine Helligkeit, darum prüft «Vergessen» den Bildwinkel.
  - Neue Texte: «Nach links oben» … «Nach rechts unten», «Nach oben», «Nach unten», «Nach links», «Nach rechts», «Mitte», «Zoom», «Zoom heraus», «Zoom hinein», «Modus», «Position speichern», «Position umbenennen», «Position «X» löschen?», «Gespeichert werden Position und Zoom der Kamera «X». Mit Bildwerten …» (Entwurf), Dialog: «Schwenken», «Neigen», «von», «bis», «Standard», «Schwenken umkehren», «Neigen umkehren», «Schrittweite», «Gemerkte Werte», «Wert», «gemerkt», «Standard des Geräts», «Vergessen», Hinweise aus dem Entwurf Seite 5, «Keine Verbindung zum Kern».
- **Quellen:** `docs/09-kamera-dienst.md` (Bewegen, Einstellungen, Kamera-Presets, Ereignisse); `kamera-dienst/dss_camera/presets.py` (Antwort `{"preset": …}`), `motion.py` (`ranges`, `DEFAULTS`), `fake.py` (Kameras der Attrappe); Entwurf `lotse/entwurf-kamera` (`1b-alles-sichtbar.html`, `5-dialoge.html`); https://doc.qt.io/qt-6/qabstractspinbox.html#specialValueText-prop (Sonderwert «Standard»).

## e3: Mitte «Bildwerte», rechts «Licht», Dialog «Lampen», Nacharbeit e2 (2026-11-17)

Ein Commit (Auftrag): Mitte, Licht, Dialog und Nacharbeit ändern dieselbe Seite und dieselbe Ansichtslogik.

- **Geändert:**
  - Ansichtslogik `src/common/cameraview.{h,cpp}`: Anfragen `lightPresetGetCall`, `…CreateCall`, `…OverwriteCall`, `…RenameCall`, `…DeleteCall`, `lightCreateCall` (`POST lights`), `lightConfigCall` (`PUT lights/<id>/config`), `lightDeleteCall` (`DELETE lights/<id>`), `cameraStatusGetCall`, `cameraExportGetCall`; `cameraSliderText`, `cameraCheckValue` (bool nur bei Typ «bool»), `LightPanelView`/`lightPanelView`, Dialog «Lampen»: `LightEntry`, `lightEntriesFromExport`, `lightsDialogCalls`, `lightsDialogProblem`, `lightCheckResult`; `CameraData::lightPresetsWithoutDetails` und `applyResult` für `GET light-presets/<id>`; `CameraHold::serial`/`reportFailure` (Entscheid Lotse). Test `tests/tst_kameraansicht.cpp` (6 Fälle neu).
  - Mitte: `src/app/camerapage.{h,cpp}` (`createImagePanel`, `refreshImage`, `showControlMenu`, Bündeln über `CameraBundler` mit eigenem `QTimer`, `flushBundled` beim Loslassen und Kamerawechsel; Hilfe `sliderRow` «Regler + Wert fester Breite»; Hilfe `scrollColumn` auch für links).
  - Rechts: `camerapage.{h,cpp}` (`createLightPanel`, `createLightPresetsSection`, `rebuildLights`, `refreshLights`, Licht-Presets speichern/umbenennen/löschen, `openLights`); `src/app/panels.{h,cpp}` (`section()` mit neuem Parameter `QLabel **titleText`, Entscheid 1); `src/app/theme.cpp` (`#propertySectionText`, `#lightsCheckResult`, `#cameraRememberedMark`, Bildlauf Mitte und rechts, Listen `#cameraLightPresets`, `#lightsList`).
  - Dialog «Lampen»: `src/app/lightsdialog.{h,cpp}` neu, `src/app/CMakeLists.txt`.
  - Nacharbeit e2: `src/app/camerasetupdialog.{h,cpp}` (Kästchen «Presets wecken die Kamera» entfernt, «Schritt»); Halten meldet einen Fehler einmal (`camerapage.cpp`, `sendHoldCall`); Checkliste `docs/archiv/prompts/P15-e-checkliste.md` (Entscheide 6–8, Entscheid Lotse, Baum, «Standard», KI-Hinweis grau, Dialog «Lampen»).
  - Test `tests/tst_fensterkameras.cpp` (Fälle `imageValues`, `lightsDialog`, `lights`, `lightPresets`, Bilder e3, Nacharbeit im Dialog), `tests/kameradiensthilfe.h` (`FakeLights`: Attrappen-Lampen `FakeLight` in eigenem Python-Prozess), `tests/CMakeLists.txt` (Kommentar).
- **Geprüft (2026-11-17, Studio-PC, Stufe «Klein»; X11-Sitzung 1 aktiv, nicht gesperrt, `loginctl show-session 1`):** `scripts/bauen.sh` Bau ok, 0 Warnungen; `scripts/testen.sh -R '^tst_kameraansicht$'` 1 von 1; `scripts/testen.sh -R '^tst_fensterkameras$'` 1 von 1 (16 s; zwei Läufe davor scheiterten an Fehlern im Test: Klick in die Mitte eines breiten Kästchens trifft es nicht, Zählung schloss `GET …/controls` ein); `scripts/testen.sh -R '^bildschirmfoto$'` 1 von 1; `scripts/testen.sh` 88 von 88 (445 s). Mindestgrösse des Fensters ≤ 1280x800 mit drei Lampen geprüft (`picture`). Nur gegen die Attrappe (Kameras und `FakeLight`); am Gerät und an den Key Lights **nicht geprüft**. Bilder `kameras-e3*.png` **nicht erzeugt und nicht angesehen**: Der Aufruf mit `DSS_TEST_BILDER` ist in dieser Sitzung nicht freigegeben. UI-Prüfung (`ui-consistency:verifying`) **nicht gemacht**, laut Auftrag durch einen getrennten Prüfer.
- **Handtest, prüft Lotse** (nie während eines Streams):
  - **Bilder:** `DSS_TEST_BILDER=<ordner> scripts/testen.sh -R '^tst_fensterkameras$'`, dann `kameras-e3.png`, `-c922.png`, `-weg.png`, `-lampen.png`, `-einrichten.png` ansehen (Breite der rechten Spalte, Marke «•», Text rechts in den Titelzeilen, Leisten in zwei Zeilen). Grund: im Bau nicht angesehen.
  - **Bildwerte am Gerät:** C922 Helligkeit ziehen (Bild ändert fliessend), Weissabgleich von Hand, «Zurücksetzen» des Bereichs; Tiny 2 «OBSBOT» Bildwinkel. Grund: nur im OBS-Bild sichtbar; die Schlüssel ausser denen der Attrappe sind nicht am Gerät geprüft (Checkliste 5).
  - **Key Lights:** Dialog «Lampen» mit den echten Adressen, «Verbindung prüfen», Helligkeit und Farbtemperatur ziehen, «Alle aus»/«Alle ein», Licht-Preset speichern und abrufen. Grund: echte Lampen im Netz.
- **Handtest, prüft der Nutzer:** Aussehen und Bedienung von Mitte und rechter Spalte (Breite rechts, Lage des Hinweises «Diese Kamera hat das nicht.» in der Titelzeile, Grösse der Liste «Licht-Presets»), weil das nur im Fenster zu beurteilen ist.
- **Nicht erledigt, `[OFFEN]`:**
  - «Verbindung prüfen» liest `GET status`: Der Dienst fragt die Lampen alle 5 s ab, `reachable` ist der Stand dieser Abfrage, kein frischer Versuch. Eine Anfrage «Lampe jetzt lesen» oder «Adresse prüfen» gibt es im Dienst nicht (`docs/09`, «Lampen»; `PUT lights/<id>` braucht Werte und würde schalten). Darum prüfbar nur mit gespeicherter Adresse («Prüfbar nach OK»). Zu entscheiden: Anfrage im Dienst ergänzen oder so lassen.
  - Adressen der Lampen nur über `GET export` (Datei `licht.json`); `GET status` hat kein `host`. Der Dienst sichert das Format der Dateien nicht als Schnittstelle zu (Begründung in e2). Vorschlag: `host` in `GET status` oder `GET lights`.
  - Aus e2 erledigt: «Presets wecken die Kamera» entfällt, «Schritt» auch im Dialog (Entscheide 6 und 7), Fehler beim Halten einmal je Halten, Checkliste nachgeführt.
  - Kein Fenstertest für «Fehler einmal je Halten»: Die Attrappe lehnt eine Fahrt nicht reproduzierbar ab (eine schlafende Tiny 2 sperrt das Kreuz schon im Fenster). Geprüft nur ohne Fenster (`holdFailureOncePerHold`).
- **Annahmen, Interna-Entscheide:**
  - Hinweis eines Bereichs ohne Werte («Diese Kamera hat das nicht.») rechts in der Titelzeile über den neuen Text-Parameter von `section()`, nicht als eigene Zeile unter der Linie wie im Entwurf 4 D: So bleibt die Anordnung in jedem Zustand gleich (Regel 9), ohne sechs leere reservierte Zeilen. Sichtbar, darum im Handtest des Nutzers.
  - Zeilen der Mitte: Spalte 0 Marke «•» (Platz immer reserviert, `setRetainSizeWhenHidden`), 1 Name, 2 Regler bzw. Auswahl (dehnbar), 3 Wert. Kästchen über Spalten 1–3 wie «Wiederholen» im Dock «Eigenschaften». Wertanzeige mit fester Breite für «−00000», «0000 K», «100 %».
  - Ein Regler, den der Nutzer gerade zieht, wird von Ereignissen nicht verstellt (`isSliderDown`); die Anzeige folgt sofort dem Regler. Auswahl und Kästchen schicken sofort (`PUT …/controls`), Regler gebündelt (höchstens alle 100 ms je Pfad, beim Loslassen und beim Kamerawechsel alles Offene).
  - Kästchen schicken `true/false` nur bei Werten vom Typ «bool», sonst `1/0` (`controls.py` im Dienst lehnt sonst ab).
  - Kontextmenü «Zurücksetzen» an Beschriftung und Bedienelement, nur bei gemerkten, bedienbaren Werten; schickt `{key: null}`.
  - Lampen: Bereiche entstehen nur neu, wenn sich Kennungen oder Namen ändern und der Dialog «Lampen» zu ist; der Dialog setzt beim Schliessen `m_lightsDialog` zurück und löst den Neubau aus. Neue Lampen aus einem Ereignis bei offenem Dialog erscheinen erst danach. Die Spalten von «Licht-Presets» (Name, je Lampe) entstehen mit den Bereichen neu.
  - Farbtemperatur: Regler 2900–7000 K, Anzeige und Wert auf 50 K gerundet, an den Dienst in Mired (`lightMiredFromKelvin`). Helligkeit 3–100 %.
  - Ganze Licht-Presets nach dem Start einzeln mit `GET light-presets/<id>` wie die Kamera-Presets (e2).
  - Dialog «Lampen»: Änderungen gelten mit OK, Anfragen nacheinander (löschen, ändern, anlegen); lehnt der Dienst eine ab, bleibt der Dialog offen mit der Meldung und liest den Stand neu. Neue Lampe beginnt als «Neue Lampe», Name gewählt. «Lampe löschen» ohne Rückfrage, weil «Abbrechen» es zurücknimmt. Zustand in der Tabelle als Text ohne Farbe.
  - «Aktuelles Licht speichern …» mit `PresetNameDialog` ohne Kästchen, Fenstertitel «Licht speichern» (Checkliste «Rahmen»).
  - Freigaben rechts (`lightPanelView`): «Alle ein/aus» und Speichern nur mit mindestens einer Lampe; Abrufen, Umbenennen, Löschen mit gewähltem Preset; ohne Dienst alles grau.
  - Rechte Spalte mit Bildlauf wie links, damit viele Lampen das Fenster nicht über 1280x800 strecken.
  - Fehler einmal je Halten: `CameraHold::serial` zählt jedes Drücken; eine späte Antwort auf ein früheres Halten bleibt still, wenn ein neueres schon gemeldet hat.
  - Test: `FakeLights` startet `FakeLight` aus `fake.py` in einem Python-Prozess (zwei Lampen auf 127.0.0.1), eine dritte Lampe zeigt auf einen freien, geschlossenen Port («nicht erreichbar»).
  - Neue Texte: «Gemerkt: Der Dienst setzt den Wert, sobald die Kamera erscheint.» (Tooltip der Marke), «<Bereich> zurücksetzen», «Zurücksetzen», «Alle ein», «Alle aus», «Lampen …», «Ein», «Helligkeit», «Farbtemperatur», «Licht-Presets», «Aktuelles Licht speichern …», «Licht speichern», «Gespeichert wird der Stand aller Lampen. Eine Lampe, die nicht antwortet, bleibt beim Abrufen unverändert.», «Licht-Preset umbenennen», «Licht-Preset «X» löschen?»; Dialog: «Lampen», «Name», «Adresse», «Zustand», «Hinzufügen», «Lampe löschen», «Verbindung prüfen», «Prüfe …», «✔ Lampe antwortet», «Lampe antwortet nicht», «Lampe nicht gefunden», «Prüfbar nach OK», «Neue Lampe», «Adresse oder Name, wahlweise mit «:Port»», «Jede Lampe braucht einen Namen.», «Lampe «X»: Adresse fehlt.», Hinweis aus dem Entwurf Seite 5.
- **Quellen:** `docs/09-kamera-dienst.md` («Lampen», «Licht-Presets», «Ereignisse»); `kamera-dienst/dss_camera/lights.py` (`create`, `config`, `delete`, `check_values`, Abfrage alle 5 s), `export.py` (`files`/`licht.json`), `controls.py` (Typ «bool»), `fake.py` (`FakeLight`, Bildwerte der Attrappe); Entwurf `lotse/entwurf-kamera` (`1b-alles-sichtbar.html`, `4-zustaende.html` D und F, `5-dialoge.html`); Qt 6: https://doc.qt.io/qt-6/qabstractslider.html#sliderDown-prop, https://doc.qt.io/qt-6/qwidget.html#customContextMenuRequested, https://doc.qt.io/qt-6/qsizepolicy.html#setRetainSizeWhenHidden (nicht neu abgerufen, kein Netz ausser 127.0.0.1).

## e4: Nacharbeit Reiter «Kameras» nach der UI-Prüfung, Lampe sofort prüfen (2026-11-17)

Ein Commit (Auftrag): Entscheide 9–14 des Nutzers und die Entscheide Lotse betreffen dieselbe Seite, dieselbe Ansichtslogik und die Anfrage `lights/check`, die nur der Dialog «Lampen» braucht.

- **Geändert:**
  - Dienst: `kamera-dienst/dss_camera/lights.py` (`Lights.check` für `POST lights/check`, `host` je Lampe in `status`), `http_api.py` (Weg), `fake.py` (`host` bei den Lampen der Attrappe ohne Ordner); Tests `kamera-dienst/tests/test_licht.py` (`test_check` mit gespeicherter und neuer Adresse, `hang_s`, geschlossenem Port, ungültigen Feldern; über HTTP), `test_api.py`; `kamera-dienst/LIESMICH.md` (Abschnitt «Lampen»), `docs/09-kamera-dienst.md`.
  - Kern: `src/common/cameraprotocol.cpp` (`POST lights/check` in der festen Liste), Test `tests/tst_kernkamera.cpp` (29 Anfragen), `docs/08-kern-schnittstelle.md`.
  - Ansichtslogik `src/common/cameraview.{h,cpp}`: `cameraChoiceText(…, serviceUsable)`, `CameraControlView::zoomText`, `lightRows` ohne Dienst «–», `lightCheckCall`, `lightCheckResult(check)`, `lightEntriesFromStatus` (ersetzt `lightEntriesFromExport`, `cameraStatusGetCall`, `cameraExportGetCall`), `CameraBundler::flush` mit 100 ms je Pfad. Test `tests/tst_kameraansicht.cpp`.
  - Reiter: `src/app/camerapage.cpp` (Kopfzeile als Raster mit zwei Zeilen, Hinweis «… gesperrt …» links unten, Hinweis und «Wecken» rechts neben dem Kreuz, «KI-Verfolgung» in einer Zeile, Hinweise als `#hintLabel`, Regler ohne Wert ganz links, Auswahl ohne Zustand ohne Dienst, Zugänglichkeitsnamen der Lampenregler, `flushBundled` mit Takt); `src/app/panels.{h,cpp}` (`setStyleState`); `src/app/theme.cpp`.
  - Dialoge: `src/app/lightsdialog.{h,cpp}` (Adressen aus `status`, sofort prüfen mit `lights/check`, Meldung drei Zeilen), `src/app/camerasetupdialog.cpp` («Tempo», «Schritt» so breit wie die Grenzen, «Gemerkt», Meldung drei Zeilen).
  - Test `tests/tst_fensterkameras.cpp`; Checkliste `docs/archiv/prompts/P15-e-checkliste.md` (Entscheide 9–14, Entscheide Lotse e4, Baum, Hinweise, Dialog «Lampen»); `docs/04-offene-punkte.md` (Punkt 129).
- **Geprüft (2026-11-17, Studio-PC, Stufe «Klein»; X11-Sitzung 1 aktiv, nicht gesperrt, `loginctl show-session 1`):** `scripts/bauen.sh` Bau ok, 0 Warnungen; `scripts/testen.sh -R '^kd_'` 10 von 10 (vor und nach der Änderung an `flush`); `-R '^tst_kameraansicht$'` 1 von 1; `-R '^tst_fensterkameras$'` 1 von 1, danach fünf Läufe einzeln je 1 von 1 (16 s); `-R '^bildschirmfoto$'` 1 von 1; `scripts/testen.sh` 88 von 88 (447 s). Vor der Änderung an `flush` scheiterte `tst_fensterkameras::lights` in 2 von 4 Läufen (einmal einzeln, einmal im ganzen Lauf; dort auch `kd_presets::test_presets_over_http` einmal: leere Gerätedatei der Attrappe gelesen, `JSONDecodeError`, im Lauf davor und danach grün, nicht untersucht). `scripts/wiederholen.sh` ist in dieser Sitzung nicht freigegeben, 50 Läufe darum **nicht gemacht**. Bilder **nicht erzeugt und nicht angesehen** (Auftrag: Lotse; der Aufruf mit `DSS_TEST_BILDER` ist hier nicht freigegeben). Ein leerer Ordner `build/bilder-e4` blieb liegen (Löschen nicht freigegeben). Nur gegen die Attrappe; an Kameras und Key Lights **nicht geprüft**.
- **Handtest, prüft Lotse** (nie während eines Streams):
  - **Bilder:** `DSS_TEST_BILDER=<ordner> scripts/testen.sh -R '^tst_fensterkameras$'` (Dateinamen wie e3). In `kameras-e3.png`: Kreuz oben in «Bewegen», Platz rechts unter «Schritt», «KI-Verfolgung» in einer Zeile, keine leere Zeile über den Spalten. In `kameras-e3-weg.png`: Auswahl nur mit Namen, Zoom und Lampen «–», Hinweis rot links unten in der Kopfzeile, Zustand rechts ganz lesbar. `kameras-e3-c922.png`/Tiny 2: Regler ohne Wert ganz links. `kameras-e3-einrichten.png`: sechs Zahlenfelder gleich breit, «Gemerkt». `kameras-e3-lampen.png`: Meldungszeile. Grund: im Bau nicht angesehen; ob der Hinweis «Kamera nicht gefunden. Sie erscheint wieder, sobald sie eingesteckt ist.» in zwei Zeilen neben «Wecken» passt, hängt von der Breite der linken Spalte ab.
  - **Key Lights:** «Verbindung prüfen» mit der echten Adresse vor OK und mit einer falschen Adresse; die Lampe darf dabei nicht schalten. Grund: echte Lampen im Netz.
- **Handtest, prüft der Nutzer:** Aussehen von «Bewegen» (Hinweis und «Wecken» rechts neben dem Kreuz) und der Kopfzeile bei «Dienst weg», weil das nur im Fenster zu beurteilen ist.
- **Nicht erledigt, `[OFFEN]`:**
  - Punkt 129 (`docs/04-offene-punkte.md`): Zwei Anfragen an dieselbe Lampe können im Dienst überholen. `flush` hält jetzt 100 ms ein; ganz ausgeschlossen ist es erst, wenn das Fenster je Pfad auf die Antwort wartet oder der Dienst die Reihenfolge festhält.
  - Minuszeichen in den Grenzfeldern (Entscheid 12, «wie sonst im Projekt»): Die Zahlenfelder des Projekts mit negativen Werten (X und Y im Dock «Eigenschaften», `docks.cpp:192–193`; Ton-Versatz, `docks.cpp:407`) sind `QSpinBox` ohne eigene Anzeige und schreiben das Minus von Qt (ASCII). Das «−» steht im Projekt nur in Anzeigetexten (`degreeText` in `cameraview.cpp`). Darum unverändert. Soll das Minus in allen Feldern «−» sein, wäre das eine Änderung über mehrere Seiten.
  - Aus e3 erledigt: «Verbindung prüfen» fragt sofort (Entscheid 13), Adressen aus `status` (Entscheid Lotse).
  - Bericht des Prüfers: «Fokusreihenfolge», «Halten der Pfeiltasten» und die Zeilen von «OBSBOT» unter dem Bildrand nicht neu geprüft.
- **Annahmen, Interna-Entscheide:**
  - Kopfzeile: Raster mit zwei Zeilen; rechts der Zustand (oben fett, unten Meldung), beide mit ihrer vollen Breite (nie abgeschnitten, Test `serviceGone` mit langer Meldung und Mindestbreite ≤ 1280); links oben Auswahl und Knöpfe, links unten der Hinweis «… gesperrt …» (`#hintLabel`, rot über `state`, der Rest der Breite, sonst abgeschnitten mit ganzem Text im Tooltip). Die Kopfzeile hatte schon die Höhe für zwei Zeilen; die Zeile über den Spalten (≈ 30 px) entfällt.
  - «Dienst weg» (Entscheid 9): Auswahl, Lampen und Zoom werden aus der Logik ohne Dienst «–» bzw. nur Name; bei einer fehlenden Kamera ist der Zoom ebenfalls «–» (Entwurf 4 B). Die Lampenbereiche bleiben (Anordnung fest), Zustand «–» grau, «Ein» aus, Regler ganz links.
  - «Bewegen» (Entscheid 10): Hinweis zwei Zeilen hoch mit Umbruch, «Wecken» rechts daneben (Platz in Breite und Höhe immer reserviert). Kreuz oben ausgerichtet. «KI-Verfolgung»: Modus, Auswahl, Hinweis in einer Zeile.
  - Regler ohne Wert (Entscheid 11): Bereich 0–0, damit der Griff links steht und kein Wert einer anderen Kamera übrig bleibt.
  - Dialog «Kamera einrichten» (Entscheid 12): Alle sechs Zahlenfelder bekommen die Breite des breitesten Grenzfelds (`sizeHint`, aus der Schrift, nach dem Laden der Bereiche); «Tempo» und «Schritt» im Raster mit Dehnung rechts wie «Grenzen».
  - `lights/check` (Entscheid 13): Antwort immer `200` mit `reachable`; ohne Antwort die Meldung des Dienstes mit Grund («Lampe unter «…» antwortet nicht (Zeitüberschreitung).»). Gehört die Adresse einer gespeicherten Lampe, wartet die Prüfung auf deren Sperre. Der Stand in `status` bleibt unverändert. Der Dialog prüft die Adresse im Feld (auch ungespeichert); eine laufende Prüfung gilt nicht mehr, wenn die Wahl oder die Adresse wechselt. Der Text «Prüfbar nach OK» entfällt.
  - Dialog «Lampen» liest Adressen aus `host` in `status` statt aus `GET export`. Lehnt der Dienst eine Änderung ab, lädt der Dialog den Stand sofort und noch einmal mit dem nächsten abweichenden Stand (Ereignis `lights`).
  - Theme: `#cameraMoveHint`, `#cameraAiHint`, `#cameraRecallHint`, `#cameraBlockedHint` entfallen; die Hinweise heissen `hintLabel` wie in der Familie, gelb und rot über `#hintLabel[state="warning"|"error"]`. Tests finden sie über die Bereiche `cameraMovePanel`, `cameraAiPanel`, `cameraPresetsPanel`, `cameraHeader`.
  - `setStyleState` als gemeinsame Hilfe in `panels.{h,cpp}` statt je einer Kopie in `camerapage.cpp` und `lightsdialog.cpp` (zwei Nutzer, `headerbar.cpp` bleibt inline).
  - `CameraBundler::flush`: hält wie `put` und `tick` 100 ms je Pfad ein (der Vertrag in `cameraview.h` sagte das schon); der Rest geht mit dem Takt. Grund: zwei PUT im selben Moment überholten sich im Dienst (siehe Punkt 129).
  - Zugänglichkeitsnamen «Helligkeit <Lampe>», «Farbtemperatur <Lampe>».
  - Neue Texte: «Lampe unter «X» antwortet nicht (Grund).» (vom Dienst), «Keine Verbindung zum Kern» (Prüfen ohne Kern), Spaltenkopf «Gemerkt».
- **Quellen:** `docs/09-kamera-dienst.md` («Status», «Lampen»); `kamera-dienst/dss_camera/lights.py` (`ElgatoClient`, Zeitgrenze 1 s, ein neuer Versuch); Entwurf `lotse/entwurf-kamera` (`4-zustaende.html` B und E, `5-dialoge.html`); `settingsdialog.cpp:174` (`#settingsProblem`, drei Zeilen); `docks.cpp:43–47`, `:192–193`, `:407` (Zahlenfelder mit negativen Werten); Python `threading.Lock` (Reihenfolge der Wartenden nicht festgelegt): https://docs.python.org/3/library/threading.html#lock-objects (nicht abgerufen, kein Netz ausser 127.0.0.1).

### Für den nächsten Abschnitt (f1: Schritte `camera_preset`, `light_preset`)
- Anfragen gibt es: `cameraPresetRecallCall(id)`, `lightPresetRecallCall(id)` (`cameraview.h`), beide im Kern erlaubt.
- Kennungen und Namen beider Preset-Arten liefert `camera_service` (`status`: `camera_presets`, `light_presets`); damit die Auswahl im Schritt füllen, ohne ganze Presets zu lesen.
- Licht-Presets antworten auch bei einer Lampe ohne Antwort mit 200 und einem Eintrag in `errors`: `errors` auswerten, nicht nur den Status.
- Laut Plan laufen beide Schritte im Probelauf nicht (Risiken), wie die Schritte zu Twitch und OBS.
- Abrufen weckt die Tiny 2 immer (Entscheid 6), kein Feld «wecken» im Schritt.
- Fehler des Dienstes: `cameraResultError(result)`; Antwort als `camera_result`.
- Gelöschtes Preset: `404 not_found`; für die Prüfmeldung am Feld die Liste aus `camera_service`.
- Fenstertest nach `tst_fensterkameras` (Dienst in der Attrappe, `FakeLights` aus `kameradiensthilfe.h`).
- Neu seit e4: Lampen tragen `host` in `status`; `lights/check` liest ohne zu schalten (für f1 nicht nötig). Punkt 129: mehrere Anfragen an dieselbe Lampe kurz hintereinander können überholen; ein Schritt, der zwei Licht-Presets direkt nacheinander abruft, ist davon betroffen.

## f1: Schritte «Kamera-Position» (`camera_preset`) und «Licht-Preset» (`light_preset`) (2026-11-17)

Ein Commit (Auftrag).

- **Geändert:**
  - Typen und Ablauf (`dss_actions`): `src/actions/steptypes.{h,cpp}` (zwei Typen, Feld `preset`, anlegbar, ohne Nummer bei Streamer.bot), `src/actions/actionservices.h` (`PresetKind`, Schnittstelle `CameraControl`, `ActionServices::camera`), `src/actions/actionrun.cpp` (Abruf, Probelauf, «keine Position gewählt», «nicht verfügbar»).
  - Kern: neu `src/core/cameraactions.{h,cpp}` (`CameraActions` über den Kamera-Client, feste Liste `checkCameraCall`, Fehler und `errors` der Lampen), `src/core/actionservice.{h,cpp}` (`Options::camera`), `src/core/main.cpp`, `src/core/CMakeLists.txt` (nur mit `DSS_CORE_CAMERA`).
  - Fensterlogik: `src/common/actionview.{h,cpp}` (`ActionViewNames` mit Presets und `cameraUnavailable`, Kurzinhalt, Menü-Abschnitt «Kamera und Licht»), `src/common/actionfields.{h,cpp}` (Abschnitte, Auswahl `camera_preset`/`light_preset`, `setCameraNames`, `fieldInactiveReason`, `fieldNotice`).
  - Fenster: `src/app/actionfieldspanel.{h,cpp}` (Grund aus der Logik ausgegraut mit Tooltip, Hinweis unter dem Abschnitt), `src/app/mainwindow.{h,cpp}` (Namen aus `camera_service`, Neuaufbau nur bei anderen Presets oder anderem Zustand).
  - Tests: `tst_aktionsablauf` (`cameraSteps`, Probelauf), `tst_kernaktionen` (`cameraSteps` gegen den Dienst in der Attrappe mit zwei Attrappen-Lampen), `tst_aktionsfelder` (`cameraChoices`), `tst_aktionsformat` (22 Typen), `tst_aktionsansicht` (Menü, Kurzinhalt), `tst_fensterkameras` (`actionSteps` nach `serviceGone`), `tests/CMakeLists.txt`, `tests/daten/aktionen/vollstaendig.json` (beide Typen am Ende der Aktion «Alle Auslöser»).
- **Geprüft (2026-11-17, Studio-PC, Stufe «Klein»; X11-Sitzung 1 aktiv, nicht gesperrt, `loginctl show-session 1`):** `scripts/bauen.sh` Bau ok, 0 Warnungen; `scripts/testen.sh -R '^(tst_aktionsablauf|tst_aktionsfelder|tst_aktionsformat|tst_aktionsansicht)$'` 4 von 4; `scripts/testen.sh --voll tst_kernaktionen` 14 von 14 Testfällen (`cameraSteps` grün, 17,7 s); `scripts/testen.sh --voll tst_fensterkameras` 15 bestanden, `picture` übersprungen (ohne `DSS_TEST_BILDER`); `scripts/testen.sh` 88 von 88 (454,75 s), nichts übersprungen. Bild `schritt-kamera.png` **nicht erzeugt** (Auftrag: Lotse). Nur gegen die Attrappe; an Kameras und Key Lights **nicht geprüft**. Stumm-Regel mit diesen Schritten **nicht eigens geprüft** (keine neue Logik, siehe unten).
- **Handtest, prüft Lotse:**
  - **Bild:** `DSS_TEST_BILDER=<ordner> scripts/testen.sh -R '^tst_fensterkameras$'`, Datei `schritt-kamera.png`: Reiter «Chat-Bot», «Aktionen», Aktion «DSS-Test Kamera» mit beiden Schritten in der Mitte (Kurzinhalte «Weit» und «Hell»), rechts «Schritt «Kamera-Position»» mit Auswahl «Weit». Grund: im Bau nicht angesehen.
  - **Kamera und Lampen:** Aktion mit «Kamera-Position» und «Licht-Preset» im echten Lauf (nicht Probelauf) aus der Version in Arbeit, Tiny 2 im Schlaf; danach im Verlauf die Meldungen. Grund: echte Geräte, Wecken bis 10 s.
- **Handtest, prüft der Nutzer:** kein Handtest nötig über das Bild hinaus; ob Abschnitt «Kamera und Licht» im Menü «Schritt hinzufügen» und der Hinweistext passen, sieht Lotse im Bild.
- **Nicht erledigt, `[OFFEN]`:**
  - Feld `wait` weggelassen: `POST camera-presets/<id>/recall` antwortet, sobald das Ziel gesetzt ist; der Dienst meldet das Ende der Bewegung nicht (`kamera-dienst/dss_camera/motion.py`, `_goto`). Dafür bräuchte der Dienst eine Rückmeldung (Position lesen, bis sie steht).
  - «Ausgegraut mit Hinweis wie andere Schritte mit fehlender Verbindung (Twitch, OBS)»: Ein solches Muster gibt es im Panel noch nicht (Twitch-Schritte sind ohne Verbindung bedienbar und melden erst im Lauf, OBS-Schritte sind nicht anlegbar). Umgesetzt mit den vorhandenen Mitteln: ausgegraut wie «weich» (Tooltip mit Grund) und derselbe Grund in der Prüfmeldungszeile unter dem Abschnitt.
  - Befund in `tst_fensterkameras::serviceGone` (aus e4, nicht von f1): Mit der langen Meldung braucht die Seite «Kameras» 1294 px Mindestbreite (`page()->minimumSizeHint()`), das Fenster meldet aber 1084, weil sein Layout nicht neu gerechnet wird; nach einem Wechsel in einen anderen Reiter und zurück meldet es 1302 (> 1280), die Prüfung scheitert dann. Darum steht `actionSteps` nach `serviceGone`. Die Kopfzeile sollte bei langen Meldungen nicht über 1280 gehen; nicht angefasst (ausserhalb des Auftrags).
  - Punkt 129 nicht angefasst (f2).
  - Die Datei `build/scratch-f1-teil.txt` (versehentlich angelegt, ohne Inhalt von Belang) blieb liegen; Löschen nicht freigegeben.
  - `ui-consistency:verifying` nicht gelaufen (nicht im Auftrag); die Felder folgen dem bestehenden Muster des Panels.
- **Annahmen, Interna-Entscheide:**
  - Schritt ohne Nummer bei Streamer.bot (`sbType` 0), Teil P14-1 (läuft und ist anlegbar), eigener Menü-Abschnitt «Kamera und Licht» nach «Overlay und Ton».
  - Fehler im Lauf: Dienst aus («Kamera-Dienst ist aus.»), ohne Antwort (Meldung des Clients), Status ≥ 400 mit der Meldung des Dienstes (`cameraResultError`), Licht mit `errors` bei 200 als Fehler mit allen Meldungen (die anderen Lampen sind gesetzt). Der Lauf geht immer weiter und endet wie bei anderen Schritten als «Fehler» mit der ersten Meldung. Kern ohne `DSS_CORE_CAMERA`: «nicht verfügbar».
  - Protokoll des Kerns ohne Inhalt: «Aktionen: Kamera-Dienst: <Methode> <Pfad>: <Status> <Code>» bzw. «… n Meldungen».
  - Wartezeit: die des Kamera-Clients (15 s je Anfrage, wegen Wecken der Tiny 2); der Lauf wartet so lange auf den Schritt.
  - Probelauf: «Probelauf: würde Position «<Name>» abrufen» bzw. «… Licht-Preset «<Name>» …», Name aus dem letzten Stand des Dienstes im Kern, sonst die Kennung.
  - Stumm-Regel: keine neue Logik nötig. Ein stummer Kern startet keine Läufe aus Ereignissen, Befehlen und Zeitnachrichten und weist echte Testläufe ab (`ActionService`, Plan P14 3.4); Probeläufe rufen nichts ab.
  - Auswahl im Fenster: Einträge nach Name, «Keine» vorne; gelöschte Kennung als «<Kennung> (fehlt)» mit Hinweis «Position «x» gibt es im Kamera-Dienst nicht mehr.»; ohne verbundenen Dienst Feld ausgegraut, Kennung ohne «(fehlt)», Grund «Kamera-Dienst nicht verbunden: Auswahl erst möglich, wenn er antwortet.» als Tooltip und unter dem Abschnitt. Kurzinhalt in der Mitte: Name in Guillemets, sonst die Kennung.
  - Das Fenster baut die Aktion nur neu auf, wenn sich Presets oder Verbindungszustand ändern (`camera_service` kommt auch beim Bewegen).
- **Quellen:** `docs/09-kamera-dienst.md` («Kamera-Presets», «Licht-Presets», «Fehlercodes»); `kamera-dienst/dss_camera/presets.py` (`recall`, `not_found`), `lights.py` (`recall`, `errors`), `motion.py` (`goto`, `_goto`); Plan `docs/archiv/prompts/P15-kamera.md` Abschnitt 9 und 11 f; Checkliste `docs/archiv/prompts/P15-e-checkliste.md` Abschnitt 4 (Entscheide 6 und 7).

### Für den nächsten Abschnitt (f2: Dokumente, Bericht, Punkt 129)
- `docs/03` braucht die Schritte «Kamera-Position» und «Licht-Preset» im neuen Abschnitt «Kameras und Licht» (Felder, Probelauf, Fehler im Verlauf, kein `wake`, kein `wait`).
- `docs/08`: keine neue Nachricht; die Schritte laufen im Kern über den Kamera-Client (`CameraActions`), nicht über `camera_call`.
- `docs/04`: neuer Punkt «Warten, bis die Bewegung fertig ist» (Dienst müsste das Ende melden); Befund Mindestbreite der Kopfzeile «Kameras» bei langer Meldung (1294 px, siehe f1 `[OFFEN]`).
- `docs/05`: keine neue externe Quelle aus f1.
- Punkt 129 betrifft jetzt auch Aktionen: Zwei Schritte «Licht-Preset» direkt nacheinander gehen ohne Pause an den Dienst.
- `tests/daten/aktionen/vollstaendig.json` enthält beide Typen; neue Typen dort ergänzen, sonst scheitert `tst_aktionsformat::typeTables`.
- Prüfung f: Stufe «Baustein» (frischer Klon, `startpruefung.py`, `dienstpruefung.py`).

## f2a: Reihenfolge der Anfragen je Lampe und Kamera (Punkt 129), Mindestbreite Reiter «Kameras» (2026-11-17)

Ein Commit für beide Teile (Regel 8, Auftrag): Beide betreffen den Weg Fenster/Kern → Kamera-Dienst bei derselben Version 19 der Schnittstelle, ohne neue Nachricht.

- **Geändert:**
  - Reihenfolge im Kern: neu `src/camera/cameracallqueue.{h,cpp}` (`CallQueue`, `callTarget`, nur Qt Core), `src/camera/cameraserviceclient.{h,cpp}` (`request` reiht ein; `startReady`, `send`, `finishCall`), `src/camera/CMakeLists.txt`. `CoreService::handleCameraCall` und `CameraActions` unverändert, sie nutzen denselben Client.
  - Kopfzeile «Kameras»: `src/app/camerapage.cpp` (zweite Zeile mit Hinweis und Meldung über beide Spalten, `ElidedLabel` für die Meldung).
  - Tests: `tst_kameradienst` (`callQueue` ohne Netz, `lightOrder` gegen den Dienst mit langsamer Attrappen-Lampe), `tests/kameradiensthilfe.h` (`FakeLights::start(count, firstHangS)`, `puts()`), `tst_fensterkameras` (`showModule`; `actionSteps` hält den Dienst selbst an und steht jetzt vor `serviceGone`; `serviceGone` prüft die Breite nach Reiterwechsel und bei 1280 px, startet den Dienst am Ende neu).
  - Doku: Punkt 129 von `docs/04-offene-punkte.md` nach `docs/archiv/04-erledigte-punkte.md`; `docs/08-kern-schnittstelle.md`, `camera_call`, Absatz «Reihenfolge».
- **Geprüft (2026-11-17, Studio-PC, Stufe «Klein»; X11-Sitzung 1 aktiv, nicht gesperrt, `loginctl show-session 1 -p Active -p LockedHint` vor den Fenstertests):**
  - `scripts/bauen.sh`: Bau ok, 0 Warnungen.
  - `scripts/testen.sh --voll tst_fensterkameras`: 15 bestanden, `picture` übersprungen (ohne `DSS_TEST_BILDER`). Mindestbreite mit langer Meldung nach Reiterwechsel: Seite 1076 px, Fenster 1084 px (vorher 1294 und 1302 px, f1).
  - `scripts/testen.sh -R "tst_kameraansicht|tst_kernkamera|tst_kernaktionen|tst_kameradienst|bildschirmfoto|kd_"`: 16 von 16 (106,36 s).
  - `scripts/testen.sh -R "^tst_kameradienst$"` 20-mal einzeln nacheinander: 20 von 20 (13,2 bis 13,5 s je Lauf). `scripts/wiederholen.sh` war nicht freigegeben.
  - Gegenprobe: Mit allen Anfragen frei (Ziele abgeschaltet, danach zurückgesetzt) scheitern `callQueue` und `lightOrder` (die ersetzte Anfrage bekam die Antwort von 60 statt 70 %).
  - `scripts/testen.sh`: 88 von 88 (464,20 s), nichts übersprungen.
  - **Nicht geprüft:** echte Key Lights und Kameras; Stufe «Baustein» (frischer Klon, `startpruefung.py`, `dienstpruefung.py`) nicht verlangt.
- **Handtest, prüft Lotse:**
  - **Bild Kopfzeile ohne Dienst:** `DSS_TEST_BILDER=<ordner> scripts/testen.sh -R '^tst_fensterkameras$'`, Datei `kameras-e3-weg.png`: Hinweis «Steuerung, Bild und Licht sind gesperrt …» links, Meldung rechts ganz lesbar, rechtsbündig unter «Kamera-Dienst nicht erreichbar:». Grund: Die zweite Zeile ist anders aufgeteilt (über beide Spalten); im Bau nur vermessen, nicht angesehen.
- **Handtest, prüft der Nutzer:**
  - **Lampe am Regler:** Im Reiter «Kameras» den Regler «Helligkeit» einer echten Key Light schnell ziehen und loslassen; die Lampe steht danach auf dem letzten Wert, kein Fehler. Grund: echtes Gerät, Laufzeit der Lampe im Heimnetz. Kann in den Gerätetest von P15 mit Lotse.
- **Nicht erledigt, `[OFFEN]`:**
  - `stop` kann wie bisher ein vorher geschicktes `move` überholen; dann fährt die Kamera bis zum Totmann (500 ms) weiter. Eine Reihe für `move` würde beim Halten stauen (siehe unten).
  - «Kamera» als Ganzes: Ein Abruf eines Kamera-Presets, der die Tiny 2 weckt (bis 10 s), hält so lange auch Bildwerte anderer Kameras zurück (sie kommen danach, zusammengefasst).
  - Der Weg über den Kern (Socket, `camera_call`) ist mit langsamer Lampe nicht eigens getestet; geprüft ist der Client, den der Kern nutzt (`tst_kernkamera`, `tst_kernaktionen` grün).
- **Annahmen, Interna-Entscheide:**
  - Ort: im Kamera-Client (`dss_camera`), nicht in `CoreService`: Fenster, `dss-cli` und Aktionen gehen alle über `CameraServiceClient::request` und sind so gleich geschützt. Die Logik (`CallQueue`) kennt kein Netz und ist ohne Fenster getestet (Regel 9).
  - Ziele: eine Lampe (`lights/<id>`, `…/config`, `DELETE`); «Licht» als Ganzes (`lights/all`, `POST lights`, `light-presets/…`) wartet auf jede Lampe und jede Lampe darauf, weil ein Licht-Preset mehrere Lampen schaltet; eine Kamera (`cameras/<id>/…`); «Kamera» als Ganzes (`camera-presets/…`), weil nur der Dienst weiss, welche Kamera ein Preset fährt. Lesende `GET` und `POST lights/check` (liest nur) sind frei.
  - `move` und `stop` frei: Beim Halten kommt alle 200 ms ein `move`; die Antwort kann bis 15 s dauern (Wecken). In einer Reihe ohne Zusammenfassen (Auftrag: Bewegungen nie zusammenfassen) würden sich die `move` stauen und `stop` käme erst nach allen. Der Totmann des Dienstes begrenzt eine Überholung auf 500 ms.
  - Zusammenfassen nur bei `PUT`, gleichem Pfad, gleichen Feldern (auch verschachtelt, z. B. `motion` in `config`) und ohne `"toggle"`; nur mit der letzten wartenden Anfrage an dasselbe Ziel (steht z. B. `lights/all` dazwischen, bleibt die Reihenfolge). Die ersetzte Anfrage bekommt dieselbe Antwort wie die neue statt einer eigenen Antwort «ersetzt»: kein neues Feld in `camera_result`, Fenster und Verlauf sehen den Stand nach dem letzten Wert.
  - Zeitgrenze: die bestehende von 15 s je Anfrage (`setTransferTimeout`); danach geht die nächste los. Wartet eine Anfrage, während der Dienst abgeschaltet wird, antwortet sie mit «Kamera-Dienst ist abgeschaltet.».
  - Kopfzeile: kein Zeilenumbruch, weil die zweite Zeile genau eine Textzeile hoch ist (feste Höhe, Regel 9) und ein Umbruch eine dritte Zeile oder ein anderes Bild der Kopfzeile bräuchte. Stattdessen nutzt die Meldung die ganze Breite unter Auswahl und Knöpfen (bei 1280 px ganz lesbar, auch die lange Meldung aus dem Test); erst wenn auch das nicht reicht, endet sie mit «…», der Anfang bleibt, der ganze Text steht im Tooltip (wie bisher gesetzt). `ElidedLabel` ändert nur das Zeichnen, nicht das Layout.
  - Tests: Reihenfolge in `tst_kameradienst` mit dem Client direkt (kein Socket nötig, derselbe Client wie im Kern). Testfälle in `tst_fensterkameras` hinterlassen den Dienst laufend und den Reiter «Kameras»; `actionSteps` steht absichtlich vor `serviceGone`, damit der frühere Fehlschlag nach einem Reiterwechsel abgedeckt ist.
- **Quellen:** `docs/09-kamera-dienst.md` («Anfragen», «Bewegen» mit Totmann 500 ms, «Lampen», «Licht-Presets»); `kamera-dienst/dss_camera/lights.py` (`_apply`, Sperre je Lampe), `kamera-dienst/dss_camera/fake.py` (`FakeLight.hang_s`, `puts`); Qt 6.8: https://doc.qt.io/qt-6.8/qfontmetrics.html#elidedText, https://doc.qt.io/qt-6.8/qstyle.html#drawItemText, https://doc.qt.io/qt-6.8/qwidget.html#minimumSizeHint-prop, https://doc.qt.io/qt-6.8/qnetworkrequest.html#setTransferTimeout.

### Für den nächsten Abschnitt (f2b: Doku 01–09)
- `docs/08` hat den Absatz «Reihenfolge» unter `camera_call` schon (f2a); Version bleibt 19.
- `docs/09`: Hinweis, dass das Studio Schreibanfragen je Lampe/Kamera nacheinander schickt (der Dienst selbst ordnet nicht); `move`/`stop` frei.
- `docs/03`: Schritte «Kamera-Position» und «Licht-Preset» (aus f1) und das Verhalten zweier Licht-Presets direkt nacheinander (in Reihenfolge).
- `docs/04`: neue Punkte aus f1 (Warten auf Ende der Bewegung) und aus f2a `[OFFEN]` (`stop` überholt `move`, «Kamera» als Ganzes beim Wecken) nach Entscheid aufnehmen.
- `docs/01`/`docs/07`: Stand P15 f nachführen; Kopfzeile «Kameras» zweite Zeile über beide Spalten.
- Prüfung f: Stufe «Baustein» steht aus.

## f2b: Dokumente 01 bis 09, Gesamtübersicht (2026-11-17)

Ein Commit, nur Doku (Auftrag).

- **Geändert:**
  - `docs/01-festlegungen.md`: neuer Abschnitt «Kamerasteuerung (P15)» (Entscheide 2026-11-15 und Antworten 1 bis 5, Entscheide 1 bis 14 vom 2026-11-17, Kopfzeile, Schritte ohne `wake` und `wait`).
  - `docs/02-umgebung.md`: Streaming-PC mit Firewall-Regel für 8093, zweiter Zone, neuer Teil «Kamera-Dienst (P15)» (Python 3.14.8, Kameras, Lampen, Dienst, Port, Regel, cameractrls).
  - `docs/03-funktionen-entwurf.md`: neuer Abschnitt «Kameras und Licht» mit den Schritten «Kamera-Position» und «Licht-Preset» (Felder, Probelauf, Fehler im Verlauf, kein `wake`, kein `wait`, zwei Licht-Presets nacheinander).
  - `docs/04-offene-punkte.md`: Punkte 130 bis 137.
  - `docs/05-quellen.md`: Abschnitt «Kameras und Licht (P15)».
  - `docs/06-overlay-format.md`: Schritte `camera_preset`/`light_preset` in `aktionen.json`, Abschnitt «Kamera-Dienst» (Dateien auf dem Streaming-PC).
  - `docs/07-bauplan.md`: Stand P15 (a bis f gebaut, Reiter statt Dock, offen: Baustein, Gerätetest, N7).
  - `docs/08-kern-schnittstelle.md`: Stand, Probelauf mit Kamera und Licht, Absatz zu den Schritten (`CameraActions`, keine neue Nachricht, Version 19).
  - `docs/09-kamera-dienst.md`: Absatz «Reihenfolge» unter «Anfragen», Einschränkung «Ende einer Bewegung».
  - Dieser Bericht: Titel, Gesamtübersicht, offene Punkte, Prüfstufe, Handtest-Liste.
- **Geprüft (2026-11-17, Stufe «Klein», nur Doku):** `scripts/bauen.sh` Bau ok, 0 Warnungen; `git diff --stat` nur die zehn Dateien unter `docs/`; Suche nach «ß» in `docs/` ohne neuen Treffer (nur Stellen, die die Regel «ß» → «ss» beschreiben). `ctest` nicht gelaufen (kein Code geändert).
- **Handtest:** kein Handtest nötig (nur Doku); die Liste für den Gerätetest steht oben.
- **Annahmen, Interna-Entscheide:**
  - Neben den fünf Punkten aus dem Entscheid Lotse kamen drei offene Befunde aus den Berichten nach `04` (133 aus b4, 136 aus b6, 137 aus der Checkliste e), damit sie nicht nur im Bericht stehen (Regel 1).
  - Ob der Dienst inzwischen installiert ist, steht in keinem Bericht; `docs/02` sagt darum «nicht geprüft».
  - Die Dateien des Dienstes stehen in `06` als eigener Abschnitt, obwohl sie auf dem Streaming-PC liegen (Auftrag: «06 (Dateien des Dienstes)»); massgebend bleibt `09`.
- **Quellen:** Plan `docs/archiv/prompts/P15-kamera.md`, Checkliste `docs/archiv/prompts/P15-e-checkliste.md` (Abschnitte 4 und 5), dieser Bericht (a bis f2a), `kamera-dienst/LIESMICH.md`, `kamera-dienst/dss_camera/presets.py` (Kopf), `motion.py` (`_goto`), `src/actions/steptypes.cpp`, `src/actions/actionrun.cpp` (Texte des Probelaufs), `src/core/cameraactions.cpp`.
