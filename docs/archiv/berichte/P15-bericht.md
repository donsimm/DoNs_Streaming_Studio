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
| Absender | Streaming-PC über Heimnetz-Adresse `403` mit Protokolleintrag; über `127.0.0.1` `200`; vom Studio-PC keine Antwort (Firewall, Regel kommt mit `system-einrichten.sh`, N7) |

### Nicht erledigt, `[OFFEN]`

- `[OFFEN]` **Dauerbewegung ruckelt.** Zielposition alle 50 ms ergibt im Bild Stufen. Ruhig ist `pan_speed`/`tilt_speed`, dabei ist die echte Position aber nicht lesbar (weder `G_CTRL` noch Statusblock). Die Grenze unten (−66°, Schlaf bei −76°) braucht dann einen anderen Schutz (z. B. Lage aus Zeit × Tempo mit Reserve, Tilt weiter über Zielpositionen). Entscheid vor dem nächsten Dienst-Abschnitt.
- `[OFFEN]` **`zoom_continuous` der Tiny 2** meldet 245 bei Bereich −100…100: Wert mit Vorzeichen wird ohne Vorzeichen gelesen (vermutlich −11).
- `[OFFEN]` **Blackmagic-Karte** erscheint in `status` als Kamera (`ptz: false`, keine Werte). Ausblenden (nur UVC-Kameras mit Werten) oder lassen.
- `[OFFEN]` Bildwinkel und KI-Modus sind am Gerät bestätigt und können im Dienst freigeschaltet werden (Bildwinkel `04 01 00/01/02`, KI «Oberkörper» `16 02 02 01`, aus `16 02 00 00` mit Übergang 6).
- `[OFFEN]` Lampe links fiel im WLAN aus (nur Stromtrennung half). Kein Fehler des Dienstes; beobachten.
- Nicht geprüft: L4, L5, N1–N7 (brauchen Studio-Teil d–f bzw. Installation durch den Nutzer), C922 Netzfrequenz 50 Hz setzen und Umstecken (N2, N3), Firewall-Regel von `system-einrichten.sh`.

### Ausserhalb des Studios

Im Kernel-Protokoll des Streaming-PCs seit dem Start (07:15) 1828 korrigierbare PCIe-Fehler (`AER`, `RxErr`, Physical Layer) einer NVMe; mit den Kameras ohne Zusammenhang. Gehört zum System des Streaming-PCs, nicht zu P15.
