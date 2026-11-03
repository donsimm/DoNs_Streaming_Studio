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

## Geplant

Grundsatz der Reihenfolge: zuerst alles, was die MUSS-Funktionen des Editors (E-01 bis E-12, E-20) braucht, in der Reihenfolge ihrer Abhängigkeiten. Jeder Baustein endet mit etwas, das läuft und geprüft ist.

| Nr. | Baustein | Inhalt | Braucht | Editor-Punkte |
|---|---|---|---|---|
| P10 | Browserquelle | Dazu: Schalter «Ausgabe ein/aus» pro Overlay in der Liste (E-24); fester Installationsort ausserhalb des Repos mit Installationsskript (Punkt 42), Überwachung eines hängenden Kerns (Punkt 41). Webserver im Kern, Seite pro Overlay mit Token, zweiter Zeichner als Webseite, Aktualisierung ohne Neuladen, Videos stumm, Adresse kopieren im Dock Steuerung | P09 | E-12 |
| P11 | Bildvergleich der zwei Zeichner | K-08: Test-Overlays, beide Zeichner ohne Fenster, Vergleich der Bilder, Grenze für Unterschiede messen | P10 | (Voraussetzung für K-07) |
| P12 | Ton von Videos | Ton über eine Audiokarte des Studio-PCs, Audioausgang wählbar | P09 | E-20, E-21 |

Danach (Reihenfolge noch offen): T-04 und T-05, sobald das Netzwerk besteht; SOLL-Punkte des Editors (E-06, E-10, E-13 bis E-15, TX-06 bis TX-11); Chat-Bot-Modul; NDI (E-19, T-03).

## Offene Fragen zur Reihenfolge

1. Chat-Bot: laut Festlegung «zuerst» neben dem Editor. Im Vorschlag kommt er nach den MUSS-Punkten des Editors.
2. P10 vor P09 wäre möglich (Browserquelle früher im Stream, Editor zeichnet später). Im Vorschlag kommt der Editor zuerst, damit beim Bau der Browserquelle schon ein Zeichner zum Vergleich da ist.
