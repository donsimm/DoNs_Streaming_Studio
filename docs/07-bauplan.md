# 07 – Bauplan

Stand: 2026-11-03. Reihenfolge der Bausteine (Prompts für Claude Code). Je Baustein ein Prompt unter `prompts/`, eine eigene Claude-Code-Sitzung, ein Bericht unter `docs/berichte/`. Gepflegt vom Chat (Projektplanung), Änderungen nur nach Absprache mit dem Nutzer.

Status: Reihenfolge P06 bis P12 vom Nutzer bestätigt (2026-11-02). Was danach kommt, ist noch offen.

## Erledigt

| Nr. | Baustein | Ergebnis |
|---|---|---|
| P01, P02 | Messprototypen | Grundlage für Betriebssystem und Technik |
| P03 | Projektgerüst, Einrichtungsskript | `dss-core`, `dss-app`, `dss_common`, Tests |
| P04 | Overlay-Modell, Format der Overlay-Datei | Speichern/Laden, Token-Datei, `dss-cli` |
| P05 | Nachträge Modell, Fenster-Gerüst | Fenster nach Bild, ohne Daten |
| P06 | Aussehen wie OBS | Design «Yami», Open Sans, Zahlenfelder mit Pfeilen, Verbindungsknöpfe mit vier Zuständen; abgenommen (2026-11-02), einschliesslich Handtest; Bericht `docs/berichte/P06-bericht.md` |
| P07 | Kern als Dienst, Verbindung Fenster–Kern | abgenommen (2026-11-02), einschliesslich Handtest. Eigene Symbole aus `~/.config/dons-streaming-studio/symbole/`, Punkt 33 behoben; `dss-core` als Benutzerdienst (startet durch das Fenster, nicht bei der Anmeldung), Sperre im Datenordner, Unix-Socket mit JSON pro Zeile (`docs/08-kern-schnittstelle.md`), echte Werte CPU, RAM, LAN; Fenster zeigt Overlays und Quellen (nur anzeigen). Bericht `docs/berichte/P07-bericht.md` |
| P08 | Bearbeiten im Fenster | abgenommen (2026-11-03), einschliesslich Handtest. Änderungen als Nachrichten an den Kern (Schnittstelle Version 2), verzögertes Speichern, Reihenfolge und Ordner in `liste.json`, Docks Overlays, Quellen, Eigenschaften bedienbar (Schalter `DSS_APP_EDITING`), Be Vietnam Pro im Repo, Auswahl der Schriften über fontconfig. Bericht `docs/berichte/P08-bericht.md` |
| P09 | Zeichner im Editor | abgenommen (2026-11-03), einschliesslich Handtest. Kopfbereich fest über die ganze Breite (E-23), Fläche neben Einträgen im Ordner frei (Punkt 48), Qt-Texte deutsch ohne «ß», Auflösung im Dock «Steuerung» (Schnittstelle Version 3), «Am Ende anhalten» (Overlay-Format 2), Originalgrösse neuer Bild- und Videoquellen. Leinwand zeichnet Bild, Text (auch variable Schrift), Video und WebM mit Alpha (libvpx-vp9, eigene Threads), 30 Bilder/s ohne Einbrüche; Wählen, Verschieben, Skalieren, Drehen mit der Maus, Pfeiltasten (Schalter `DSS_APP_RENDERER`, `DSS_APP_CANVAS_EDIT`). Bericht `docs/berichte/P09-bericht.md` |
| P09b | Nacharbeiten Editor | abgenommen (2026-11-03), einschliesslich Handtest. Einrasten 1° beim Drehen, Leistungsanzeige 600 px rechts im Kopfbereich, Seitenverhältnis an der Ecke nach Originalgrösse (Kern schickt `media_sizes`), Spiegeln durch Ziehen und per Befehl (Overlay-Format 3, Schnittstelle Version 4), E-25 Transformieren wie OBS (Schalter `DSS_APP_TRANSFORM`), E-26 Bereiche im Dock «Eigenschaften», EXIF-Ausrichtung in Kern und Editor. Bericht `docs/berichte/P09b-bericht.md` |
| P10a | Nacharbeiten Editor und Einsatz | abgenommen (2026-11-03), einschliesslich Handtest; erste echte Installation durch den Nutzer (Commit `6d83c93`). Verbindungsknöpfe mittig, Logos 19 statt 28 px, Knöpfe Kopieren/Einfügen/Zurücksetzen bei «Transformation», Tastenkürzel wie OBS, Videos ohne Ruckeln (Toleranz 1 ms und Anker, Punkt 66), Drehung aus Video-Metadaten (Punkt 65). Version in Arbeit und im Einsatz getrennt (`DSS_INSTALLED_EDITION`), `scripts/installieren.sh` mit Umwandeln und Sicherung, Menüeintrag mit Programm-Symbol, Watchdog 10 s (Punkt 41); Bericht `docs/berichte/P10a-bericht.md` |
| P10-vor | Vorarbeiten Browserquelle | abgenommen (2026-11-03), einschliesslich Handtest; keine neue echte Installation nötig (folgt mit P10). Übernehmen von Dateien im eigenen Thread des Kerns (Punkt 67, 1 GiB: Fenster bedienbar, 4 GiB: kein Neustart durch den Watchdog), Aufräumen der Reste von `QSaveFile` beim Start (68), `installieren.sh` leert die Programmordner vor dem Kopieren (69), Testmöglichkeit beim Umwandeln (70), Einstellungen des Kerns in `kern.ini` (Port 8080 / 8081, Adresse im Heimnetz, Schnittstelle Version 5, `dss-cli`), Dialog «Werkzeuge → Einstellungen …», «Datei → Leinwand als Bild speichern» (Strg+Umschalt+B, Zeichner in `dss_render`); Bericht `docs/berichte/P10-vor-bericht.md` |
| P10 | Browserquelle | abgenommen (2026-11-04), einschliesslich Handtest mit OBS 33.0.1 auf dem Streaming-PC (T-04) und echter Installation (Commit `c6b3cb1`). Overlay-Server im Kern (nur HTTP, nur IPv4 auf `127.0.0.1` und der Adresse im Heimnetz, Token mit gleichbleibender Laufzeit, leere Seite bei alter Adresse, Meldung «Port belegt» mit dem Programm, Warten auf die Adresse), Seite als zweiter Zeichner (`web/`, Änderungen per Server-Sent Events ohne Neuladen), Ausgabe ein/aus pro Overlay (E-24, Format 4), «Neuer Token» im Fenster und in `dss-cli`, Dock «Steuerung» mit Adresse, «Kopieren» und «Ausgabe: …», Punkte 76 bis 78, Schnittstelle Version 9; Stufe 1 der Prüfung mit Qt WebEngine (`tst_browserquelle`); Bericht `docs/berichte/P10-bericht.md` |

## Geplant

Grundsatz der Reihenfolge: zuerst alles, was die MUSS-Funktionen des Editors (E-01 bis E-12, E-20) braucht, in der Reihenfolge ihrer Abhängigkeiten. Jeder Baustein endet mit etwas, das läuft und geprüft ist.

| Nr. | Baustein | Inhalt | Braucht | Editor-Punkte |
|---|---|---|---|---|
| P10b | Medien | ausgeführt (2026-11-04), Handtest offen. Bericht `docs/berichte/P10b-bericht.md`, Prompt `prompts/P10b-medien.md`. Dock «Medien» (E-28): Liste mit Vorschaubild, Name, Typ, Grösse; ursprüngliche Namen in neuer Datei im Datenordner (zum Beispiel `medien.json`), umbenennbar; Ordner beliebig tief; Hinzufügen; Löschen nur ungenutzter Dateien; Ziehen auf die Leinwand und in die Liste «Quellen»; Lage zwischen «Quellen» und «Eigenschaften». Datei in Übernahme mit Fortschritt in Prozent und Knopf zum Abbrechen (Punkt 73). Nacharbeit: leere «Adresse im Heimnetz» während des Laufs eintragen (Punkt 85). Hinweis beim Hinzufügen und Warnzeichen für Videoformate, die die Browserquelle nicht abspielt (Punkt 81). Eigene Ports für alle Tests und die Startprüfung (Punkt 83). Entscheide in `docs/04-offene-punkte.md`, «Wünsche nach P10a» | P10 | E-28 |
| P10c | Anbindung an OBS | T-05 (obs-websocket: Verbinden, Szenenwechsel erkennen, Wiederverbinden nach OBS-Neustart) und E-29 (Browserquelle direkt in OBS anlegen und nachführen). Passwort von OBS nie im Klartext (Regel 12). Entscheid des Nutzers 2026-11-04 | P10b | E-29 |
| P11 | Bildvergleich der zwei Zeichner | K-08: Test-Overlays, beide Zeichner ohne Fenster, Vergleich der Bilder, Grenze für Unterschiede messen. Editor rechnet Farben von Videos wie Chromium (Punkt 79). WebM mit Drehangabe prüfen, bei Abweichung neutralisieren (Punkt 80) | P10 | (Voraussetzung für K-07) |
| P12 | Ton von Videos | Ton über eine Audiokarte des Studio-PCs, Audioausgang wählbar | P09 | E-20, E-21 |

Danach (Reihenfolge noch offen): T-04 und T-05, sobald das Netzwerk besteht; SOLL-Punkte des Editors (E-06, E-10, E-13 bis E-15, TX-06 bis TX-11); Chat-Bot-Modul; NDI (E-19, T-03).

## Offene Fragen zur Reihenfolge

1. Chat-Bot: laut Festlegung «zuerst» neben dem Editor. Im Vorschlag kommt er nach den MUSS-Punkten des Editors.
2. P10 vor P09 wäre möglich (Browserquelle früher im Stream, Editor zeichnet später). Im Vorschlag kommt der Editor zuerst, damit beim Bau der Browserquelle schon ein Zeichner zum Vergleich da ist.
