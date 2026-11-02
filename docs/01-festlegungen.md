# 01 – Festlegungen

Stand: 2026-11-02. «Entschieden» heisst: vom Nutzer bestätigt. «Vorschlag» heisst: Vorschlag von Claude, noch nicht bestätigt. Quellen: `docs/05-quellen.md`.

## Ziel

- Private Software, nur für den Stream des Nutzers. Kein Verkauf, keine Veröffentlichung. (entschieden)
- Sie mischt, nimmt nicht auf und streamt nicht selbst wie OBS. Sie erzeugt pro Overlay eine Leinwand und gibt sie aus. (entschieden)
- Es ist ein Neustart. Code und Entscheide des alten Repos `donstv_Studio` gelten nicht automatisch. (entschieden)
- Programmiert wird sie von Claude Code, direkt auf dem Studio-PC. (entschieden)

## Module

| Modul | Stand | Aufgabe |
|---|---|---|
| Editor | zuerst, Hauptfokus | Leinwand pro Overlay, Quellen bearbeiten, Ausgabe |
| Chat-Bot | zuerst | Chat, Commands, später Streamer.bot-Funktionen. Umfang und Import werden beim Chat-Bot-Modul geklärt; die Daten der alten Installation hat der Nutzer vollständig. |
| Live-Modul | später | Inhalt wird später geklärt |
| Zuschauer-Minispiele | später (denkbar) | Chat-Spiele mit Overlay-Anzeige |
| Musikplayer | später (denkbar) | Audio über die Standard-Audiokarte und ein externes Mischpult, die Audiokarte muss in der Software wählbar sein. Audio per NDI zum Streaming-PC ist als Option vorgesehen (laut obs-ndi möglich, nicht geprüft). |

- Module sind fest eingebaut. Jedes Modul bekommt dieselbe Schnittstelle zum Ereignisfluss, damit sich später ein weiteres Modul ohne Umbau ergänzen lässt. Es gibt kein Plugin-System, auch nicht für Dritte. (entschieden)
- Alle Module teilen sich dieselben Verbindungen (Twitch, OBS) und denselben Ereignisfluss. (Vorschlag)

## Oberfläche

- Die Oberfläche bleibt wie auf dem Bild des Nutzers: Menüleiste, Kopfbereich mit Modul-Reitern, Verbindungssymbole (Twitch, YouTube, OBS, NDI) und Anzeigen für CPU, RAM und LAN, Leinwand in der Mitte mit Linealen in Pixeln und Schachbrettmuster, vier Docks unten (Overlays, Quellen, Eigenschaften, Steuerung), Statuszeile. (entschieden)
- Die Software besteht aus zwei Teilen: dem Fenster zum Bedienen und einem Hintergrundprogramm (dem Kern), das Twitch, OBS und die Overlays dauerhaft betreibt. Stürzt das Fenster ab, laufen die Overlays im Stream weiter. Das Fenster enthält keine Logik. Ob der Aufbau zur Technikwahl passt, zeigen die Messungen T-01 bis T-03. (entschieden)

## Editor

- Pro Overlay eine eigene, anpassbare Leinwand. Mehrere Overlays, Auflösung pro Overlay verschieden.
- Quellen: Text (komplett anpassbar), Bilder, Videos, WebM-Dateien. Einfügen und manipulieren.
- Ausgabe heute: Browserquelle (Hauptweg). Ausgabe später: NDI, höchstens 2 Ausgänge.
- Läuft auf dem Studio-PC, sonst läuft dort nichts anderes.
- WebM mit Alpha (aus Quellen abgeleitet, nicht selbst gemessen): Chromium zeigt den Alpha-Kanal von VP9-WebM an. FFmpegs eingebauter VP9-Decoder verwirft ihn, nur libvpx liefert ihn. Das betrifft die Technikwahl, falls Frames ohne Chromium dekodiert werden.

## OBS

- Kein OBS-Plugin. Die Software läuft eigenständig auf dem Studio-PC. OBS holt die Overlays als Browserquelle (später per NDI) und wird per obs-websocket gesteuert. (entschieden: Plugins nicht für den Kern; Vorschlag: obs-websocket für die Steuerung)
- Ein Plugin wäre nur für eine kleine Hilfsfunktion auf der OBS-Seite denkbar, die weder Browserquelle, NDI noch obs-websocket leisten. Aktuell nicht nötig.

## Betrieb im Netz

- Zugriff auf den Overlay-Server: im Heimnetz erreichbar, mit Token pro Overlay, ohne Port am Router. (entschieden)
- Der Studio-PC hängt per Kabel am Netz und bekommt am Router eine feste Adresse. (entschieden)
- Zielwerte für die Last: 1080p30, bis zu 2 NDI-Ausgänge, bis zu 10 Quellen pro Overlay. (entschieden)
- Spenden und Tip Jar aus dem alten Entwurf sind nicht Teil des Neustarts. (entschieden)
- Funktionsprioritäten (MUSS, SOLL, KANN) werden pro Modul festgelegt, wenn das Modul an der Reihe ist. Der Editor beginnt zuerst. (entschieden) Editor: entschieden (2026-11-02), siehe `docs/03-funktionen-entwurf.md`, E-01 bis E-19.

## Betriebssystem und Technik

- Betriebssystem: entschieden (2026-11-02, vom Nutzer bestätigt): Debian 13 mit Xfce unter X11. Grundlage: Messung P01, alle drei Prototypen starteten dort 20 von 20 Mal sauber. Debian 13 ist von den drei geprüften Systemen (Debian 13, Ubuntu 26.04 LTS, Fedora 44) das einzige mit X11-Sitzung. Andere Systeme wurden nicht untersucht.
- Technik: entschieden (2026-11-02, vom Nutzer bestätigt): C++ mit Qt 6, eigene Leinwand (frühere Variante A). Der Editor zeichnet die Leinwand selbst in C++. Für die Browserquelle in OBS braucht es einen zweiten Zeichner als Webseite. Begründung des Nutzers: Aufwand ist zweitrangig, Kontrolle und Flexibilität zählen. Python/PyQt6 (Variante C) und Qt WebEngine als Editor-Zeichner (Variante B) werden nicht weiterverfolgt. Grundlage: `docs/messungen/P01-bericht.md`, `docs/messungen/P02-bericht.md`.
- Bekanntes Risiko dieser Wahl: Die zwei Zeichner können voneinander abweichen (Schrift, Zeilenumbruch, Animationen), sodass der Editor etwas anderes zeigt als der Stream. Das ist eine Einschätzung von Claude, nicht gemessen. Gegenmassnahmen: siehe Abschnitt «Konzept der zwei Zeichner».
- Video mit Alpha in der eigenen Leinwand: eigener Dekodierweg über libavcodec mit `libvpx-vp9` (gemessen in P02, M1). Qt Multimedia 6.8.2 liefert den Alpha nicht.
- Sprache des Kerns (Hintergrundprogramm): entschieden (2026-11-02, vom Nutzer bestätigt): C++, dieselbe Sprache und Werkzeugkette wie das Fenster. Begründung: gemeinsamer Code für Leinwand-Modell, Dekodierung und Zeichnen. Ungeprüft: Was Qt 6.8.2 unter Debian 13 für Webserver und Twitch-Anbindung mitbringt; das klärt der Prompt für den Kern.
- Zurückgestellt, bis das Netzwerk besteht: T-03 (NDI) und T-04 (Browserquelle in OBS), T-05 (obs-websocket).

## Konzept der zwei Zeichner

Entschieden (2026-11-02, vom Nutzer bestätigt). Grundidee: Der Kern ist die einzige Stelle, die weiss, wie ein Overlay aussieht. Beide Zeichner (Editor und Browserquelle) stellen dar, was der Kern vorgibt.

| Nr. | Regel |
|---|---|
| K-01 | Eine Datei pro Overlay im Format JSON. Inhalt: Auflösung, Liste der Quellen in Zeichenreihenfolge, pro Quelle Nummer, Art, X, Y, Breite, Höhe (ganze Pixel), Drehung, sichtbar ja/nein, Eigenschaften der Art. Dazu eine Versionsnummer des Formats. |
| K-02 | Der Kern verteilt die Datei. Editor und Browserquelle bekommen sie vom Kern. Bei jeder Änderung schickt der Kern das ganze Overlay neu. |
| K-03 | Dateien (Bilder, Videos, Schriften) liegen im Kern und werden von beiden Zeichnern von dort geholt. |
| K-04 | Text zeichnen beide Zeichner selbst, aus derselben Schriftdatei (E-11). Entscheid des Nutzers gegen den Vorschlag «Text als Bild vom Kern»: schärfer beim Vergrössern, höheres Risiko für Abweichungen. |
| K-05 | Bild, Video, WebM stellen beide Zeichner selbst dar (Editor über libavcodec mit libvpx). Verglichen wird ein festes Einzelbild, nicht der Ablauf. |
| K-06 | Animationen (E-14) stehen als Formel mit Zeit in der Datei. Beide Zeichner rechnen dieselbe Formel. |
| K-07 | Eine neue Eigenschaft kommt nur dazu, wenn beide Zeichner sie können und der Bildvergleich besteht. |
| K-08 | Automatischer Bildvergleich: Beide Zeichner zeichnen dasselbe Test-Overlay ohne Fenster (Webseite in Chromium), die Bilder werden verglichen. Erlaubt sind nur kleine Unterschiede an Kanten; die Grenze wird im Bau-Prompt festgelegt und gemessen. |

Nicht geprüft: Ob Chromium auf dem Studio-PC gleich zeichnet wie die Browserquelle in OBS auf dem Streaming-PC (zeigt erst T-04).

Zu K-04, entschieden (2026-11-02, vom Nutzer bestätigt): Den automatischen Zeilenumbruch (TX-11) rechnet nur der Kern und schickt die fertigen Zeilen; die Zeichner brechen nie selbst um.
