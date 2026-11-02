# 03 – Funktionen je Modul (Entwurf)

Stand: 2026-11-02. Dieser Entwurf fasst zusammen, was der Nutzer genannt hat (du), was auf dem Bild der Oberfläche zu sehen ist (Bild) und was im alten Repo `donstv_Studio` stand (altes Repo, Funktionsliste `docs/01-funktionen.md`). Alles ist zu bestätigen und noch nicht priorisiert.

## Editor (Hauptfokus)

- Overlays anlegen, umbenennen, kopieren, löschen (Bild). Auflösung pro Overlay einstellbar (du, altes Repo).
- Quellen: Text (komplett anpassbar), Bild, Video, WebM, GIF (du). Hinzufügen, entfernen, Reihenfolge ändern, ein- und ausblenden (Bild).
- Quellen verschieben, skalieren, drehen, mit Zahleneingabe für X, Y, Breite, Höhe, Drehung. Lage und Grösse in ganzen Pixeln (Bild, altes Repo).
- Hilfen: Lineale, Einrasten, Raster, Hilfslinien, Ausrichten, Zoom «Einpassen», Anzeige der Zeigerposition (Bild).
- Schrift: Be Vietnam Pro als Standard, andere Schriften wählbar, eigene Schriftdateien (altes Repo).
- Ausgabe: Browserquelle mit kopierbarer URL, Live-Aktualisierung in OBS ohne Neuladen (Bild, altes Repo). NDI später, höchstens 2 Ausgänge (du).
- Vorschau mit einstellbarer Bildrate und Verkleinerung (Bild).
- Animationen pro Quelle, zum Beispiel Schweben, Atmen, Schreibmaschine (altes Repo).
- Spezialquellen, einzeln hinzufügbar: Chat-Box, Alert-Box, Ereignisliste, Zielbalken (altes Repo).
- Quellen an die aktive OBS-Szene koppeln (altes Repo, nicht im Bild).

## Kopfbereich und Allgemeines

- Modul-Reiter, Verbindungsanzeige für Twitch, YouTube, OBS und NDI, Leistungsanzeige für CPU, RAM und LAN (Bild).
- Einstellungen: dunkles und helles Design (altes Repo).
- Konfiguration speichern, laden, exportieren, importieren (altes Repo).

## Chat-Bot (nochmals zu prüfen)

- Zuerst: Twitch-Anmeldung, Chat ansehen, Nachrichten senden, Commands verwalten und ausführen (Name, Befehl, Berechtigung, Cooldown, Antwort), Protokoll (altes Repo).
- Danach: Actions, Queues, Variablen, Timer, Trigger (Streamer.bot-Stil) und Import einer früheren Streamer.bot-Installation (altes Repo; der Nutzer hatte den Import als wichtig bezeichnet).
- Chat-Spiele mit Anzeige im Overlay (du). Ablauf: Chat-Ereignis, Spiellogik, Anzeige per Browserquelle.

## Spätere Module (nur Ideen)

- Live-Modul: Funktionen für die Zeit, in der der Nutzer live ist. Inhalt wird später geklärt.
- Zuschauer-Minispiele, zum Beispiel «Pflanze giessen» und «Katze streicheln» (altes Repo).
- Musikplayer: Audio über die Standard-Audiokarte und ein externes Mischpult. Die Audiokarte ist in der Software wählbar. Audio per NDI ist als Option vorgesehen.
