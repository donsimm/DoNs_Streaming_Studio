# P15 – Bericht (Kamera-Dienst, Abschnitte a bis c)

Stand: Dienst a1 bis c4 gebaut und mit der Attrappe geprüft (`ctest -R '^kd_'` 10/10, `kd_bewegen` 20×). Studio-Teil d bis f folgt nach P14.

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
