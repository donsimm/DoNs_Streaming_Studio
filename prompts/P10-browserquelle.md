# P10 – Browserquelle

**Sitzung:** Öffne eine neue Claude-Code-Sitzung auf dem Studio-PC, in einem Terminal innerhalb der Xfce-Sitzung, im Ordner des Repos. Führe zuerst `git pull` aus. Dieser Prompt wird in einer eigenen Sitzung ausgeführt.

Lies zuerst `CLAUDE.md`, alle Dokumente unter `docs/` (besonders `docs/01-festlegungen.md` Abschnitte «Betrieb im Netz», «Konzept der zwei Zeichner» (K-01 bis K-08), «Zeichnen im Editor», «Einsatz (Version im Stream)», `docs/02-umgebung.md` Abschnitt «Netzwerk», `docs/03-funktionen-entwurf.md` E-12 und E-24, `docs/04-offene-punkte.md` Abschnitt «Entschieden für P10» und Punkte 53, 76, 77, 78, `docs/06-overlay-format.md`, `docs/07-bauplan.md`, `docs/08-kern-schnittstelle.md`) und die Berichte `docs/berichte/P10a-bericht.md` und `docs/berichte/P10-vor-bericht.md`.

## Ausgangslage

P10-vor ist abgenommen (2026-11-03): Übernehmen von Dateien im eigenen Thread, `kern.ini` mit Port (8080 im Einsatz, 8081 in Arbeit) und «Adresse im Heimnetz», Dialog «Einstellungen», Bild der Leinwand (`dss_render`). Das Netzwerk zum Streaming-PC besteht (Studio-PC `192.168.1.9`, Streaming-PC `192.168.1.116`). Jetzt kommt der Hauptweg der Ausgabe: die Browserquelle (E-12) mit dem zweiten Zeichner.

## Teil A: Overlay-Server im Kern (Entscheide des Nutzers 2026-11-03)

1. **Server** im Kern, nur HTTP im Heimnetz. Er hört **nur auf IPv4** zu: auf der «Adresse im Heimnetz» aus `kern.ini` und auf `127.0.0.1`, auf dem Port aus `kern.ini`. Nicht auf IPv6 (beide PCs haben öffentliche IPv6-Adressen; ob der Router eingehende IPv6-Verbindungen sperrt, ist nicht geprüft). Technik wählen und begründen (zum Beispiel Qt HttpServer, Qt WebSockets oder eigener Server auf `QTcpServer`); ein neues Paket nur nach Rückfrage beim Nutzer (Regel 5). Schalter beim Bau.
2. **Port belegt:** Kann der Server den Port nicht öffnen, meldet der Kern das deutlich im Fenster (nicht still ohne Server weiterlaufen). Die Meldung nennt, wenn der Kern es herausfinden kann, das Programm, das den Port belegt (zum Beispiel «vermutlich DoNs Streaming Studio im Einsatz»; Punkt 77). Ändert der Nutzer den Port, startet der Server neu.
3. **Adresse fehlt:** Hat der PC die «Adresse im Heimnetz» beim Start nicht (zum Beispiel Kabel nicht eingesteckt), meldet der Kern das im Fenster, hört bis dahin nur auf `127.0.0.1` zu und startet den Server auf der Adresse von selbst, sobald sie da ist.
4. **Adresse:** `http://<Adresse im Heimnetz>:<Port>/o/<adresse>/<token>/`. Alles, was die Seite nachlädt (Bilder, Videos, Schriften, Verbindung für Änderungen), liegt unter diesem Pfad. Der Token wird mit gleichbleibender Laufzeit verglichen (Quelle nennen). Der Kern schreibt die Adresse nie mit Token ins Protokoll (Regel 12); im Protokoll steht höchstens die lesbare Adresse.
5. **Alte oder unbekannte Adresse** (neue Adresse beim Umbenennen, neuer Token, gelöschtes Overlay, falscher Token): sofort ungültig. Der Server antwortet mit einer **leeren, durchsichtigen Seite**, nicht mit einer Fehlerseite (sonst wäre in OBS ein Kasten mit Text im Stream sichtbar). Der Kern protokolliert «unbekannte Adresse» ohne Token, und das Fenster zeigt einen Hinweis, dass eine Browserquelle eine ungültige Adresse benutzt.
6. **Änderungen ohne Neuladen:** Jede Änderung am Overlay erscheint in der Browserquelle ohne Neuladen der Seite (K-02: der Kern schickt das ganze Overlay neu). Technik wählen und begründen. Bricht die Verbindung ab (zum Beispiel Neustart des Kerns), verbindet sich die Seite von selbst wieder; bis dahin bleibt das letzte Bild stehen.
7. **Ausgabe ein/aus (E-24):** Schalter pro Overlay links neben dem Eintrag in der Liste des Docks «Overlays» (an der Stelle aus Punkt 48). Aus = die Browserquelle zeigt nichts (leer, durchsichtig), bleibt aber verbunden; der Editor bleibt bearbeitbar. Wo der Zustand gespeichert wird, wählen und begründen; ändert sich dadurch das Format der Overlay-Datei, gelten die Regeln zum Umwandeln aus P10a (`docs/06-overlay-format.md`, `dataformat`).
8. **Neuer Token:** Knopf «Neuer Token» im Dock «Steuerung» neben «Kopieren», mit Rückfrage «Die alte Adresse funktioniert danach nicht mehr. In OBS muss die neue Adresse eingetragen werden. Fortfahren?». Ebenso mit `dss-cli`.

## Teil B: Zweiter Zeichner als Webseite (K-01 bis K-08)

1. Eine Seite pro Overlay, in der Grösse des Overlays, Hintergrund durchsichtig. Sie zeichnet aus dem Overlay, das der Kern schickt, dasselbe wie der Zeichner des Editors (K-07). Ohne Bibliotheken aus dem Netz; alles liefert der Kern.
2. Vorgaben, alle wie im Editor:
   - Bild und Video füllen den Kasten (gestreckt).
   - Reihenfolge: Ausrichtung aus EXIF bzw. Drehung aus den Video-Metadaten → Strecken in den Kasten → Spiegeln um die Mitte → Drehung um die Mitte (`docs/01-festlegungen.md`, «Zeichnen im Editor»).
   - EXIF bei JPEG und PNG; Drehung von Videos aus der Display-Matrix. Ob Chromium das selbst tut oder die Seite es anhand von Angaben des Kerns tun muss, prüfen und so umsetzen, dass das Ergebnis nicht von der Chromium-Version abhängt.
   - Text: dieselbe Schriftdatei wie im Editor (K-04), Zeilen wie im Editor, an der Box abgeschnitten, Ausrichtung waagrecht.
   - Video: stumm; startet **immer von vorne** beim Einblenden der Quelle, beim Laden oder Neuladen der Seite und beim Einschalten der Ausgabe; spielt unabhängig vom Editor. Ende wie im Editor (`loop`, `hold_at_end`, sonst leer).
   - Sichtbar ja/nein, Zeichenreihenfolge, Lage und Grösse in ganzen Pixeln.
3. **Verhalten beim Wechsel des Overlays (Anker aus P10a):** Prüfe, was ein Video im Editor tut, wenn man auf ein anderes Overlay wechselt und zurück (weiterlaufen oder neu beginnen). Lege es für beide Zeichner gleich fest, begründe es und halte es in `docs/01-festlegungen.md` fest.
4. Welche Videoformate Chromium abspielt (WebM VP9 mit Alpha, MP4 H.264), prüfen und im Bericht nennen. Was die Seite nicht abspielen kann, zeigt sie leer und meldet es dem Kern (Protokoll); `[OFFEN]` im Bericht.

## Teil C: Fenster

1. Dock «Steuerung»: Feld «Browserquelle» mit der Adresse des gewählten Overlays (mit Token), Knopf «Kopieren» (Zwischenablage), Knopf «Neuer Token» (Teil A 8), Anzeige «Ausgabe: …» mit dem Zustand. Ohne gewähltes Overlay ausgegraut.
2. Meldungen aus Teil A (Port belegt, Adresse fehlt, ungültige Adresse benutzt) gut sichtbar im Fenster.

## Teil D: Nacharbeiten aus P10-vor (Entscheide des Nutzers 2026-11-03)

1. **Punkt 78:** Der Kern nimmt als «Adresse im Heimnetz» nur eine Adresse an, die eine Netzwerkkarte dieses PCs gerade hat; sonst Meldung im Dialog mit den Adressen dieses PCs (zum Beispiel «192.168.1.1 gehört nicht zu diesem PC. Adressen dieses PCs: 192.168.1.9.»).
2. **Punkt 76:** Abgewiesene Einstellungen erscheinen nur im Dialog, nicht in der Statuszeile.
3. **Punkt 77:** Bei ungültiger `kern.ini` bietet der Dialog den Knopf «Auf Standardwerte zurücksetzen»; der Kern benennt die alte Datei in `kern.ini.ungueltig-<JJJJ-MM-TT_hh-mm-ss>` um, schreibt eine neue mit Standardwerten und trägt die Adresse im Heimnetz wieder selbst ein.

## Tests

- Ohne Fenster: Prüfung des Tokens, alte und unbekannte Adressen (leere Seite, kein Token im Protokoll), Adresse nur von diesem PC (Punkt 78), `kern.ini` zurücksetzen (Punkt 77), Port belegt (Meldung), Server auf IPv4 und nicht auf IPv6 (mit `ss -ltn` oder gleichwertig belegen).
- **Stufe 1 der Prüfung (Entscheid des Nutzers):** ein Testprogramm auf Basis von Qt WebEngine (schon installiert, `docs/02-umgebung.md`) lädt die Seite über `http://192.168.1.9:<Port>/…`, also über die echte Netzwerkkarte, und greift das Bild mit Durchsichtigkeit ab (Weg aus P02, M2). Geprüft werden: Bild gegen das Bild der Leinwand (`dss_render`) mit den Testdaten `tests/daten/exif/` und `tests/daten/video-drehung/`, Text, Spiegeln, Drehung; Änderung im Editor erscheint ohne Neuladen (Zeit messen); Ausgabe aus und wieder ein; neuer Token; Wiederverbinden nach Neustart des Kerns. Abweichungen zwischen den Zeichnern mit Zahlen nennen (die feste Grenze kommt mit P11).
- Last: ein Overlay mit 10 Quellen, darunter 2 Videos, 1080p30: CPU und Speicher von Kern und Testprogramm messen.
- Ob eine Firewall auf dem Studio-PC den Port sperrt, prüfen und nennen (nichts ändern ohne Rückfrage).
- `tst_medien` und `tst_fensterbearbeiten` je 50 Mal; Startprüfung `--runs 20`; `ctest` auch in einem frischen Klon.

## Nicht Teil dieses Auftrags

- NDI (T-03), obs-websocket (T-05), Dock «Medien» (P10b), automatischer Bildvergleich mit fester Grenze (P11), Ton (P12)
- Installation in den echten Ort `~/.local/opt/dons-streaming-studio/` (macht der Nutzer im Handtest)
- Neue Pakete nur nach Rückfrage beim Nutzer, mit Begründung, **bevor** du etwas installierst.

## Abnahme

1. Bau ohne Warnungen mit `-Wall -Wextra`, alle Schalter einzeln abschaltbar; Release-Bau ohne Warnungen.
2. `ctest --test-dir build` besteht, auch in einem frischen Klon; `tst_medien` und `tst_fensterbearbeiten` je 50 Mal.
3. Startprüfung 20 von 20 je Fall sauber.
4. Echte Daten und Installation unverändert (Prüfsummen vorher und nachher), nichts installiert (Regel 18).
5. Dokumente nachgeführt, besonders `docs/08-kern-schnittstelle.md` (Server, Adressen, Nachrichten der Seite) und `docs/01-festlegungen.md`.
6. Handtest durch den Nutzer (Regel 10): höchstens 5 Prüfungen, **jede ein eigener Abschnitt**. Befehle auf dem Studio-PC beginnen mit `cd ~/DoNs_Streaming_Studio &&`; Befehle auf dem Streaming-PC ausdrücklich so kennzeichnen. Inhalt:
   - Version aus dem Repo (Port 8081) mit einem Test-Overlay (Bild mit EXIF, gedrehtes Video, Text, gespiegelte Quelle): Browserquelle in **OBS auf dem Streaming-PC** anlegen (Adresse aus «Kopieren», Weg der Adresse zum Streaming-PC beschreiben), Aussehen gegen die Leinwand vergleichen, auch mit «Bildschirmfoto (Quelle)» in OBS und dem Bild der Leinwand (Strg+Umschalt+B); ob OBS beim Bildschirmfoto die Durchsichtigkeit behält, festhalten.
   - Änderung im Editor erscheint in OBS ohne Neuladen; Ausgabe aus und ein; Video beginnt beim Einblenden von vorne.
   - Neuer Token: alte Adresse in OBS leer und durchsichtig, Hinweis im Fenster; neue Adresse eintragen.
   - **Echte Installation** mit `scripts/installieren.sh` (das darf nur der Nutzer), danach die installierte Version (Port 8080) mit den echten Daten in OBS.
   - Aufräumen der Entwicklungsdaten und der Browserquellen in OBS, die nur zum Test angelegt wurden.

## Bericht

Schreibe den Bericht nach `docs/berichte/P10-bericht.md` nach dem Muster der bisherigen Berichte und Regel 15. Trage erledigte Punkte in `docs/04-offene-punkte.md` ein (E-12, E-24, T-04 vorbereitet, Punkte 76 bis 78) und setze P10 in `docs/07-bauplan.md` unter «Erledigt» («ausgeführt, Handtest offen»).

Ein Commit pro Funktion, dazu Tests und Bericht. Prüfe vor dem Push mit `git status` und `git show --stat`, dass nichts fehlt und nichts aus `~/.config`, `~/.local/share`, `~/.local/opt` oder `~/Bilder` im Repo gelandet ist. Führe am Ende selbst `git pull --rebase origin main` und `git push origin main` aus (Regel 17).
