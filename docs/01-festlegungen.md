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
- Funktionsprioritäten (MUSS, SOLL, KANN) werden pro Modul festgelegt, wenn das Modul an der Reihe ist. Der Editor beginnt zuerst. (entschieden)

## Betriebssystem und Technik

- Betriebssystem: entschieden (2026-11-02, vom Nutzer bestätigt): Debian 13 mit Xfce unter X11. Grundlage: Messung P01, alle drei Prototypen starteten dort 20 von 20 Mal sauber. Debian 13 ist von den drei geprüften Systemen (Debian 13, Ubuntu 26.04 LTS, Fedora 44) das einzige mit X11-Sitzung. Andere Systeme wurden nicht untersucht.
- Technik: offen. Zwei Varianten werden gemessen: (A) C++ mit Qt 6, nativ; (B) ein unabhängiger Kern mit Web-Oberfläche. Python mit PyQt6 bleibt nur im Rennen, wenn es die Messungen besteht. Das ist eine Einschätzung von Claude, nicht belegt.
- Zurückgestellt, bis das Netzwerk besteht: T-03 (NDI) und T-04 (Browserquelle in OBS), T-05 (obs-websocket).
