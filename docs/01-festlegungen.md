# 01 – Festlegungen

Stand: 2026-11-17 (Kamerasteuerung, P15; davor Ton von Videos, P12). «Entschieden» heisst: vom Nutzer bestätigt. «Vorschlag» heisst: Vorschlag von Claude, noch nicht bestätigt. Quellen: `docs/05-quellen.md`.

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
- Alle Module teilen sich dieselben Verbindungen (Twitch, OBS) und denselben Ereignisfluss. (Vorschlag; der Ereignisfluss ist seit P13 gebaut, `src/events/`)

## Oberfläche

- Die Oberfläche bleibt wie auf dem Bild des Nutzers (`docs/bilder/oberflaeche.png`): Menüleiste, Kopfbereich mit Modul-Reitern, Verbindungssymbole (Twitch, YouTube, OBS, NDI) und Anzeigen für CPU, RAM und LAN, Leinwand in der Mitte mit Linealen in Pixeln und Schachbrettmuster, fünf Docks unten (Overlays, Quellen, Medien, Eigenschaften, Steuerung; «Medien» kam mit P10b dazu), Statuszeile. (entschieden)
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
- Anbindung an OBS (P10c, Entscheide des Nutzers vom 2026-11-07, F1 bis F12): Der Code hängt nur vom Protokoll ab, nicht vom Betriebssystem oder der Installationsart von OBS und nicht vom Namen der Szenensammlung.
  - F1 Passwort: verschlüsselt durch das System ohne Nachfrage (`systemd-creds --user`, Datei `obs-passwort.cred`), nie im Klartext (Regel 12).
  - F2 «In OBS anlegen»: kleines Fenster mit Name und Szene, aktuelle Szene vorgewählt.
  - F3 Einstellung: Bereich «OBS» im Dialog «Einstellungen», Klick auf den OBS-Knopf öffnet ihn.
  - F4 Knopf: eigene Zeile «OBS» im Dock «Steuerung» mit Zustandstext.
  - F5 Neue Quelle ist sofort sichtbar, wie beim Hinzufügen in OBS.
  - F6 Hat das Overlay schon eine Quelle: Der Knopf wird zu «In weitere Szene einfügen …» und nutzt dieselbe Quelle.
  - F7 Von Hand gelöscht: Hinweis «Quelle in OBS fehlt», Knopf wieder aktiv, nichts passiert von selbst.
  - F8 Overlay im Studio gelöscht: Die Quelle bleibt in OBS und ist leer, das Studio zeigt einen Hinweis und löscht nichts in OBS.
  - F9 OBS-Knopf bei geschlossenem OBS: rot; orange nur beim ersten Versuch nach Start oder Änderung der Einstellungen.
  - F10 Von Hand angelegte Quellen mit genau der richtigen Adresse werden automatisch übernommen.
  - F11 Die Grösse der Quelle in OBS folgt der Auflösung des Overlays; die Lage in der Szene bleibt unberührt.
  - F12 Aktuelle Szene: nur im Tooltip des OBS-Knopfes.

## Twitch (P13)

Entscheide des Nutzers vom 2026-11-09 und 2026-11-15 (Plan `docs/archiv/prompts/P13-twitch.md`, «Entscheide des Nutzers»; Bericht `docs/archiv/berichte/P13-bericht.md`):

- Der Kern ist an Twitch angebunden über EventSub (WebSocket) und die API von Twitch (Helix), nicht über IRC. Die Ereignisse gehen auf den Ereignisfluss des Kerns (Regel 14, `docs/08-kern-schnittstelle.md`, «Ereignisfluss»); sie sind die Auslöser des Aktionssystems (P14). (entschieden)
- Zwei Konten, je mit Anmeldung per Code im Browser (Gerätecode): **Kanal** (lesen, Ereignisse, schreiben) und **Bot** (schreibt in den Chat). Das Bot-Konto ist das bisherige aus der Zeit mit Streamer.bot; ein Bot gleich dem Kanal wird abgewiesen. Der Kanal ist Affiliate. (entschieden)
- Rechte: alles lesen und schreiben, was Twitch erlaubt, auch für Funktionen, die erst später kommen (Kanalpunkte-Effekte, Kategorie per Befehl, Umfrage und Vorhersage, Moderations-Knöpfe), damit später keine neue Anmeldung nötig ist. (entschieden 2026-11-15, Frage 1 B)
- Abzeichen «Chat Bot»: ja. Die App wird vertraulich registriert (mit Client Secret); der Bot sendet mit App-Token. Das Secret gibt der Nutzer im Dialog «Einstellungen» ein, abgelegt wie F1. Das ersetzt «öffentliche App, kein Secret» vom 2026-11-09. (entschieden 2026-11-15, Frage 2 B)
- Zugangsdaten wie F1: Token beider Konten und Client Secret verschlüsselt mit `systemd-creds --user`, ohne Nachfrage, nie im Klartext (Regel 12). Die Client-ID steht in `kern.ini` (`[twitch]`), eine App für beide Konten. Die App registriert der Nutzer selbst mit `scripts/twitch-einrichten.sh`. (entschieden)
- Anzeige: Bereich «Twitch» im Dialog «Einstellungen» (Klick auf den Twitch-Knopf öffnet ihn), Liste «Ereignisse» im Reiter «Chat-Bot», Unterreiter «Live». Die Farbe des Twitch-Knopfes folgt nur dem Kanal. (entschieden)
- Reiter «Chat-Bot» mit Unterreitern nach dem Entwurf (`lotse/entwurf-chatbot`); die fünf Docks des Overlay-Editors sind dort ausgeblendet. (entschieden 2026-11-15)
- Der Bot läuft nur, solange das Studio offen ist (Kern wie heute). (entschieden 2026-11-15)
- Follows kommen als Ereignis direkt von Twitch. «Erste Nachricht im Stream» leitet der Kern selbst ab, Twitch liefert nur «erste Nachricht überhaupt». (entschieden 2026-11-09, Interna)

## Kamerasteuerung (P15)

Entscheide des Nutzers vom 2026-11-15 und 2026-11-17 (Plan `docs/archiv/prompts/P15-kamera.md`, «Entscheide des Nutzers» und «Antworten des Nutzers» 1 bis 5; Checkliste `docs/archiv/prompts/P15-e-checkliste.md`, Abschnitt 4, Entscheide 1 bis 14; Bericht `docs/archiv/berichte/P15-bericht.md`). Schnittstelle des Dienstes: `docs/09-kamera-dienst.md`.

- Geräte am Streaming-PC: OBSBOT Tiny 2 und Logitech C922 Pro Stream über USB (beide UVC), dazu zwei Elgato Key Light Air im Heimnetz (Ein/Aus, Helligkeit, Farbtemperatur). (entschieden 2026-11-15)
- Aufbau: ein kleiner Kamera-Dienst in Python auf dem Streaming-PC (Ordner `kamera-dienst/` im Repo, Benutzerdienst `dss-kamera`, HTTP/JSON auf Port 8093) ist der einzige Weg zu Kameras und Lampen; Studio, Stream Deck und Aktionssystem nutzen dieselbe Schnittstelle. Im Studio spricht der Kern mit dem Dienst, nicht das Fenster. Kamera- und Licht-Presets liegen im Dienst, damit das Stream Deck sie auch ohne Studio abrufen kann. (entschieden 2026-11-15; Ort im Repo, Sprache, Presets im Dienst und Kern als Vermittler: Interna, Plan 2.1, 3 und 7)
- Umfang mindestens wie cameractrls, dazu Schwenken, Neigen und Zoomen der Tiny 2. Ihre Sonderfunktionen (Schlafen/Wecken, HDR, Bildwinkel, KI-Verfolgung) über das offen erforschte Protokoll (Vorlage `cgevans/tiny2`, Bytefolgen selbst geschrieben, kein Code kopiert), kein OBSBOT-SDK. Freigeschaltet ist nur, was am Gerät geprüft ist. (entschieden 2026-11-15)
- Ort der Bedienung: eigener Modul-Reiter «Kameras» neben «Overlays» und «Chat-Bot», kein Dock. (entschieden 2026-11-15, Antwort 1)
- Aufteilung des Reiters: Variante 1B «Alles sichtbar», ohne Unterreiter, drei Spalten: links Steuerung (Steuerkreuz, Zoom, Tempo, Schritt, KI-Verfolgung) und Positionen, Mitte Bildwerte mit Bildlauf, rechts Licht (Lampen, Licht-Presets). Kopfzeile mit Kamera-Auswahl, «Wecken»/«Schlafen» und Zustand des Dienstes. (entschieden 2026-11-15, Antwort 5, Entwurf `lotse/entwurf-kamera`) Der Dialog «Kamera einrichten» öffnet sich über Werkzeuge → «Kamera einrichten …» (vor «Einstellungen …», mit Trennlinie) für die im Reiter gewählte Kamera, auch wenn der Reiter nicht offen ist; vorher Knopf «Einrichten …» in der Kopfzeile. (Wunsch des Nutzers im Gerätetest P15, N3)
- Ein Kamera-Preset («Position») speichert Position und Zoom; beim Speichern das Kästchen «Bildwerte mitspeichern», Standard aus. (entschieden 2026-11-15, Antwort 2)
- Schutz des Dienstes ohne Anmeldung, drei Schichten: lauscht nur auf IPv4 (Adresse im Heimnetz und `127.0.0.1`), antwortet nur erlaubten Absendern (`dienst.ini`, Studio-PC und `127.0.0.1`), Firewall-Regel nur für den Port vom Studio-PC (`docs/02-umgebung.md`, «Streaming-PC»). (entschieden 2026-11-15, Antwort 3)
- Reihenfolge: Schritt 0 und die Abschnitte a bis c (nur der Dienst) parallel zu P13/P14, d bis f nach der Abnahme von P14. Lotse darf am Streaming-PC für Prüfungen Kameras bewegen, wecken und schlafen legen, eine Testinstanz des Dienstes starten und Licht schalten, nie während eines Streams; Werte, die bleiben, nur mit OK des Nutzers in der Sitzung. (entschieden 2026-11-15, Antwort 4)
- Reiter «Kameras» (entschieden 2026-11-17, Entscheide 1 bis 14):
  - Neue Bausteine nur in `src/app` (Seite, Dialoge «Kamera einrichten», «Lampen» und Namensdialog, grosser Richtungsknopf mit 6 neuen Symbolen, Reglerzeile «Regler + Wert fester Breite», `section()` mit Text rechts in der Titelzeile). Leisten mit fünf Knöpfen («Positionen», «Licht-Presets») brechen auf zwei Zeilen um. (1)
  - «Tempo» und «Schritt» im Bereich «Bewegen» ändern die Einstellungen der Kamera, Einheit je Kamera: Tiny 2 in °/s (1 bis 100, Standard 10), C922 in % (Standard 50). (2)
  - Hinweise als Text ohne Kasten in den Farben der Kopfzeile, Marke «•» vor gemerkten Werten, Kamera-Auswahl ohne Farben, keine neuen Theme-Werte. (3)
  - Dialog «Lampen»: «Lampe löschen» statt «Entfernen». (4)
  - KI-Verfolgung bei der Tiny 2 bedienbar, nur «Aus» und «Oberkörper», ohne Tempo; weitere Modi erst nach einem Test am Gerät. Bei der C922 ausgegraut mit «Diese Kamera hat das nicht.». (5)
  - Kein Kästchen «Presets wecken die Kamera»: Abrufen einer Position weckt die Tiny 2 immer; im Reiter steht der Hinweis «weckt die Kamera zuerst». (6)
  - Überall «Schritt» und «Positionen», auch im Dialog. Linke Spalte bestätigt. (7, 8)
  - Dienst nicht erreichbar: Die Auswahl zeigt nur den Namen der Kamera, Lampen und Zoom zeigen «–», bis der Dienst wieder antwortet. (9)
  - «Bewegen»: Hinweis und «Wecken» rechts neben dem Kreuz, der Platz bleibt reserviert; bei «KI-Verfolgung» steht der Hinweis in der Zeile der Auswahl. (10)
  - Gesperrte Regler ohne Wert stehen grau ganz links, daneben «–», nie die Stellung einer anderen Kamera. (11)
  - Dialog «Kamera einrichten»: alle sechs Zahlenfelder so breit wie das breiteste Grenzfeld, Spaltenkopf «Gemerkt», Minuszeichen wie sonst im Projekt. (12)
  - «Verbindung prüfen» im Dialog «Lampen» fragt die Lampe sofort, ohne sie zu schalten, auch unter einer noch nicht gespeicherten Adresse (`POST lights/check`). (13)
  - «Diese Kamera hat das nicht.» bleibt in der Titelzeile der Bereiche. (14)
- Kopfzeile des Reiters (Interna, Entscheide Lotse e4 und f2a): rechts der Zustand des Dienstes in voller Breite; die zweite Zeile reicht über beide Spalten, links der Hinweis «Steuerung, Bild und Licht sind gesperrt …», rechts die Meldung, bei zu wenig Platz am Ende mit «…» gekürzt, der ganze Text im Tooltip.
- Aktionssystem: Schritte «Kamera-Position» (`camera_preset`) und «Licht-Preset» (`light_preset`), je nur mit dem Feld `preset`. Kein Feld `wake` (Abrufen weckt immer, Entscheid 6) und kein Feld `wait` (der Dienst meldet das Ende einer Bewegung nicht, Punkt 130). Im Probelauf rufen sie nichts ab. (Plan 9; Felder: Interna f1)
- Später: Kamera-Fernbedienung per Chat (Entscheid 2026-11-15). Das Stream Deck stellt ein eigener Auftrag im Stream-Deck-Projekt auf die Schnittstelle des Dienstes um.

## Betrieb im Netz

- Zugriff auf den Overlay-Server: im Heimnetz erreichbar, mit Token pro Overlay, ohne Port am Router. (entschieden)
- Der Studio-PC hängt per Kabel am Netz und bekommt am Router eine feste Adresse. (entschieden) Umgesetzt (2026-11-03, Angabe des Nutzers): Kabel `enp3s0`, Adresse `192.168.1.9`, am Router reserviert.
- Overlay-Server (umgesetzt mit P10, 2026-11-04, Entscheide des Nutzers vom 2026-11-03): nur HTTP, nur IPv4 auf der «Adresse im Heimnetz» und `127.0.0.1`, Port aus `kern.ini`; Adresse der Browserquelle `http://<Adresse im Heimnetz>:<Port>/o/<adresse>/<token>/`; alte oder unbekannte Adresse ergibt eine leere, durchsichtige Seite. Einzelheiten: `docs/08-kern-schnittstelle.md`, «Overlay-Server».
- Zielwerte für die Last: 1080p30, bis zu 2 NDI-Ausgänge, bis zu 10 Quellen pro Overlay. (entschieden)
- Spenden und Tip Jar aus dem alten Entwurf sind nicht Teil des Neustarts. (entschieden)
- Funktionsprioritäten (MUSS, SOLL, KANN) werden pro Modul festgelegt, wenn das Modul an der Reihe ist. Der Editor beginnt zuerst. (entschieden) Editor: entschieden (2026-11-02), siehe `docs/03-funktionen-entwurf.md`, E-01 bis E-19.

## Betriebssystem und Technik

- Betriebssystem: entschieden (2026-11-02, vom Nutzer bestätigt): Debian 13 mit Xfce unter X11. Grundlage: Messung P01, alle drei Prototypen starteten dort 20 von 20 Mal sauber. Debian 13 ist von den drei geprüften Systemen (Debian 13, Ubuntu 26.04 LTS, Fedora 44) das einzige mit X11-Sitzung. Andere Systeme wurden nicht untersucht.
- Technik: entschieden (2026-11-02, vom Nutzer bestätigt): C++ mit Qt 6, eigene Leinwand (frühere Variante A). Der Editor zeichnet die Leinwand selbst in C++. Für die Browserquelle in OBS braucht es einen zweiten Zeichner als Webseite. Begründung des Nutzers: Aufwand ist zweitrangig, Kontrolle und Flexibilität zählen. Python/PyQt6 (Variante C) und Qt WebEngine als Editor-Zeichner (Variante B) werden nicht weiterverfolgt. Grundlage: `docs/archiv/messungen/P01-bericht.md`, `docs/archiv/messungen/P02-bericht.md`.
- Bekanntes Risiko dieser Wahl: Die zwei Zeichner können voneinander abweichen (Schrift, Zeilenumbruch, Animationen), sodass der Editor etwas anderes zeigt als der Stream. Das ist eine Einschätzung von Claude, nicht gemessen. Gegenmassnahmen: siehe Abschnitt «Konzept der zwei Zeichner».
- Video mit Alpha in der eigenen Leinwand: eigener Dekodierweg über libavcodec mit `libvpx-vp9` (gemessen in P02, M1). Qt Multimedia 6.8.2 liefert den Alpha nicht.
- Sprache des Kerns (Hintergrundprogramm): entschieden (2026-11-02, vom Nutzer bestätigt): C++, dieselbe Sprache und Werkzeugkette wie das Fenster. Begründung: gemeinsamer Code für Leinwand-Modell, Dekodierung und Zeichnen. Ungeprüft: Was Qt 6.8.2 unter Debian 13 für Webserver und Twitch-Anbindung mitbringt; das klärt der Prompt für den Kern.
- Zurückgestellt, bis das Netzwerk besteht: T-03 (NDI) und T-04 (Browserquelle in OBS), T-05 (obs-websocket). Seit 2026-11-03 besteht das Netzwerk; T-04 ist erledigt (Handtest P10 mit OBS, 2026-11-04), T-05 ist mit P10c gebaut (Prüfung mit dem echten OBS im Handtest P10c), T-03 ist offen.

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

## Overlay-Daten

Entschieden (2026-11-02, vom Nutzer bestätigt):

- Bilder, Videos und Schriftdateien werden beim Hinzufügen als Kopie im Datenordner der Software abgelegt. Das Overlay verweist nur auf die Kopie.
- Drehung wird als ganze Zahl in Zehntelgrad gespeichert (0 bis 3599), angezeigt mit einer Nachkommastelle.
- Die Adresse der Browserquelle wird aus dem Namen des Overlays gebildet (Kleinbuchstaben, Umlaute als ae/oe/ue, übrige Zeichen als «-»), zum Beispiel `ueberblick-start`. Bei gleichem Namen hängt die Software eine Zahl an.
- Beim Umbenennen fragt die Software, ob die Adresse mitgeändert werden soll. Standard «nein», damit OBS weiterläuft.
- Drehrichtung: Ein positiver Winkel dreht im Uhrzeigersinn, um die Mitte der Quelle (entschieden 2026-11-02). So drehen Qt (`QPainter::rotate`) und der Browser (CSS `rotate()`, Leserichtung links nach rechts) von sich aus; keiner der Zeichner rechnet um.
- Adresse: «ß» wird «ss», «æ» wird «ae», «ø» wird «oe», «ł» wird «l» (entschieden 2026-11-02), zum Beispiel «Straße» → `strasse`. Umgesetzt in P05.
- Video: Wiederholen am Ende ist pro Quelle wählbar, Standard «nicht wiederholen» (entschieden 2026-11-02, vom Nutzer korrigiert). Umgesetzt in P05 (Feld `video.loop`, `docs/06-overlay-format.md`).
- Ton von Videos (entschieden 2026-11-02): Der Studio-PC spielt den Ton über eine Audiokarte des Systems ab; von dort geht er über das externe Mischpult zum Streaming-PC. Alle Quellen des Editors nutzen denselben Audioausgang; dieser soll wählbar sein. Später werden mehrere Audiokarten bespielt (andere Module, zum Beispiel Musikplayer). Die Browserquelle in OBS spielt Videos immer stumm ab, sonst käme der Ton doppelt an (entschieden 2026-11-02).
- Risiko (Einschätzung, nicht gemessen): Bild (über das Netz als Browserquelle) und Ton (über das Mischpult) kommen auf verschiedenen Wegen beim Streaming-PC an und können gegeneinander verschoben sein. Prüfbar erst mit T-04. Seit P12b: Der Kern führt den Ton nach den Meldungen der Seite in OBS nach; den festen Versatz gleicht `versatz_ms` aus, gemessen wird er im Handtest H2 (Plan P12, Abschnitt 4).
- Ton von Videos, Umsetzung (P12, Entscheide des Nutzers; Plan `docs/archiv/prompts/P12-ton.md`, Bericht `docs/archiv/berichte/P12-bericht.md`):
  - 2026-11-07 (Bündel `ton-1`): Der Kern spielt den Ton. Wird eine Szene mit Video in OBS aktiv, beginnen Video und Ton von vorne. Im Editor gibt es keinen Ton (Vorhören vielleicht später). Der RØDECaster hängt per USB am Streaming-PC; ein eigener Kanal für den Studio-PC ist möglich.
  - 2026-11-09: Anschluss Studio-PC → RØDECaster über USB an den zweiten USB-Anschluss (Variante A). Ausgang und Versatz im Dock «Steuerung», nicht in «Einstellungen»; je Zweck ein eigener Ausgang, damit später Chat-Bot und Musikplayer eigene Soundkarten bekommen. Lautstärke je Videoquelle: Regler 0–100 % und «Stumm», neue Quellen 100 % und nicht stumm. Lotse darf für Messungen in OBS eine Aufnahme von höchstens 60 s in «test 2» machen.
  - 2026-11-11: Ausgabe über PipeWire direkt (`pw_stream`, G4 = P) statt `QAudioSink`, zuerst als Prototyp mit Halt (b1). Puffer 40 ms fest. Ein Sprung beim Wechsel des Quantums gilt als gelöst, wenn er gemeldet und ausgeglichen wird.
  - 2026-11-12: Lage in Proben und Zyklusgrösse statt `delay` (an der virtuellen Senke immer 0); `delay` am echten Ausgang misst H2. Der Handtest H2 ersetzt M4. Zeile «Ton Videos» im Dock «Steuerung»: Auswahlliste mit erstem Eintrag «Kein Ausgang», Feld «Versatz» −500 bis +500 ms, Hinweis «Ausgang fehlt» wie in der Zeile «OBS»; ein neuer Kern hat «Kein Ausgang».
  - 2026-11-13 (nach M3): Wird eine Quelle in OBS inaktiv, hält das Video an und springt auf den Anfang. Bekommt ein Video eine neue Datei, beginnt es von vorne.
  - Fehlt der gewählte Ausgang, gibt es keinen Ton und einen Hinweis, nie Ton aus einem anderen Gerät (Vorgabe im Plan P12, Abschnitt 3, mit Entscheid 6 vom 2026-11-12 bestätigt).

- Der Token pro Overlay steht nicht in der Overlay-Datei, sondern in einer eigenen Datei `tokens.json`, die nur der Benutzer lesen kann (Dateirechte 0600).
- Reihenfolge: zuerst P04 (Overlay-Modell), dann P05 (Fenster-Gerüst ohne Daten).

## Tests und Abnahme

Entschieden (2026-11-02, vom Nutzer bestätigt): Der Nutzer testet nur, was sinnvoll ist oder nur er ausführen kann. Was Claude Code auf dem Studio-PC schon geprüft hat, wird nicht ein zweites Mal von Hand geprüft. Grund: Anders als im alten Projekt wird direkt auf dem Zielgerät programmiert und getestet. Umgesetzt in `CLAUDE.md` Regel 7 und 10.

## Arbeitsweise mit Claude Code

- Agenten committen nur lokal. Gepusht wird nur nach Freigabe des Nutzers, durch Lotse (entschieden 2026-11-07, ersetzt die Erlaubnis vom 2026-11-02). Gepusht wird per SSH-Schlüssel des Studio-PCs.

## Verbindungsanzeige im Kopfbereich

Entschieden (2026-11-02, vom Nutzer bestätigt): Vier Verbindungen werden angezeigt: Twitch, YouTube (der Nutzer streamt auch auf YouTube), OBS (obs-websocket zum Streaming-PC) und NDI (ob die NDI-Ausgabe läuft). Farben der Knöpfe:

| Farbe | Bedeutung |
|---|---|
| Grün | verbunden bzw. Ausgabe läuft |
| Orange | Verbindungsversuch läuft |
| Rot | eingerichtet, aber nicht verbunden (Fehler) |
| Grau | nicht eingerichtet oder abgeschaltet |

Offen: Was die Software mit YouTube tun soll (Chat, Ereignisse); wird mit dem Chat-Bot-Modul geklärt.

## Aussehen des Fensters

Entschieden (2026-11-02, Wunsch des Nutzers): Das Fenster soll so ähnlich wie möglich wie OBS Studio aussehen (Schrift und Farben). Aufbau und Docks bleiben wie auf `docs/bilder/oberflaeche.png`.

Vorlage ist das Standard-Design von OBS Studio, «Yami» in der Variante «Default» (Datei `frontend/data/themes/Yami.obt` im Repo https://github.com/obsproject/obs-studio, gelesen am 2026-11-02, Commit `65c320b46e1af5c7a555161aea6182cdf7a8c524`). OBS ist wie dieses Projekt eine Qt-Anwendung; das Design ist ein Qt-Stylesheet mit Variablen.

- Schrift: OBS nennt als erste Schrift «Open Sans» (danach Helvetica, Arial, sans-serif). In diesem Repo-Stand liefert OBS keine Schriftdatei mit (nicht gefunden unter `frontend/data`). Für dieses Projekt: Open Sans als Datei laden, nicht auf eine Installation hoffen.
- Grundfarben (Variablen aus `Yami.obt`):

| Rolle in OBS | Variable | Wert |
|---|---|---|
| Fensterhintergrund | `--bg_window` = `--grey7` | `#1D1F26` |
| Hintergrund von Flächen | `--bg_base` = `--grey6` | `#272A33` |
| Hintergrund der Vorschau | `--bg_preview` = `--grey8` | `#13141A` |
| Hauptfarbe (Auswahl, Knöpfe) | `--primary` = `--blue3` | `#284CB8` |
| Hauptfarbe hell | `--primary_light` = `--blue2` | `#476BD7` |
| Text | `--text` = `--white1` | `#FFFFFF` |
| Text gedämpft, ausgegraut | `--text_muted` = `--white5` | `#969696` |
| weitere Grautöne | `--grey1` … `--grey5` | `#5B6273`, `#4E5566`, `#464B59`, `#3C404D`, `#323540` |
| Farben für Zustände (Vorschlag für die Verbindungsknöpfe) | `--green3`, `--yellow2`, `--red3` | `#25A231`, `#E5AF24`, `#C01C37` |

- Lizenz: OBS Studio steht unter GPL Version 2 (Datei `COPYING`). Farbwerte zu übernehmen ist unkritisch. Wird mehr übernommen (Stylesheet-Teile, Symbole), gilt die GPL für diese Teile; solange die Software privat bleibt und nicht weitergegeben wird, entstehen daraus keine Pflichten (Einschätzung, keine Rechtsberatung).

## Kern als Dienst

Entschieden (2026-11-02, vom Nutzer bestätigt):

- Das Fenster startet den Kern (geändert am 2026-11-02, vorher: Start bei der Anmeldung). Beim normalen Schliessen des Fensters wird der Kern mit beendet. Stürzt das Fenster ab, läuft der Kern weiter. Stürzt der Kern ab, startet er von selbst neu, solange er nicht ordentlich beendet wurde. Folge: Ohne offenes Fenster zeigen die Browserquellen in OBS nichts an.
- Solange der Kern läuft, ändert nur der Kern die Overlay-Dateien; `dss-cli` darf dann nur anzeigen.
- Die LAN-Anzeige zeigt automatisch die Netzwerkverbindung der Standardroute (beim Studio-PC das Kabel).

## Overlay-Liste

Entschieden (2026-11-02, vom Nutzer bestätigt):

- Overlays sind in der Liste per Maus frei verschiebbar; die Reihenfolge wird gespeichert.
- Overlays lassen sich in Ordner gruppieren. Eine Ebene: Ordner enthalten nur Overlays, keine Unterordner.
- Die Adresse der Browserquelle hängt nicht vom Ordner ab; Verschieben ändert nichts in OBS.
- Reihenfolge und Ordner speichert der Kern in einer eigenen Datei, nicht in den Overlay-Dateien.
- Umsetzung in P08.

## Bearbeiten im Fenster

Entschieden (2026-11-02, vom Nutzer bestätigt), umgesetzt mit P08:

- Löschen von Overlays, Ordnern und Quellen mit Rückfrage.
- Änderungen wirken sofort (gespeichert und verteilt), ohne Knopf «Übernehmen», wie in OBS.
- Rückgängig und Wiederholen: neue Funktion E-22, SOLL, nach den MUSS-Punkten.
- Be Vietnam Pro liegt als Datei im Repo und ist Standardschrift neuer Textquellen.
- Schriftauswahl: alle installierten Schriften des Studio-PCs, Be Vietnam Pro und eigene Dateien. Die gewählte Schriftdatei wird wie ein Bild in den Datenordner kopiert. Grund: Die Browserquelle zeichnet auf dem Streaming-PC, wo die Schriften des Studio-PCs fehlen.

## Zeichnen im Editor

Entschieden (2026-11-03, vom Nutzer bestätigt), umgesetzt mit P09:

- Bild und Video füllen die Box der Quelle (Breite × Höhe) und werden dafür gestreckt. Eine neu hinzugefügte Bild- oder Videoquelle bekommt die Originalgrösse der Datei (ersetzt Annahme 2 aus P04 für Bild und Video).
- Ziehen mit der Maus wie OBS: an einer Ecke bleibt das Seitenverhältnis, mit Umschalttaste frei; an einer Kante nur Breite oder Höhe. Seit P09b ist es an der Ecke das Verhältnis der Originalgrösse der Datei (wie OBS), bei Text das des Kastens beim Beginn. Zieht man über die Gegenseite hinaus, wird die Quelle in dieser Richtung gespiegelt (wie OBS).
- Drehen mit der Maus über einen Griff oberhalb der gewählten Quelle wie in OBS; mit Umschalttaste rastet der Winkel in 1°-Schritten ein (geändert vom Nutzer im Handtest P09, 2026-11-03; vorher 15°). Ohne Umschalttaste Zehntelgrad.
- Ende eines Videos: ohne Einstellung wird die Quelle leer (durchsichtig). Neues Kästchen «Am Ende anhalten» in den Eigenschaften des Videos: das letzte Bild bleibt stehen. Mit «Wiederholen» beginnt das Video von vorne; «Am Ende anhalten» ist dann ausgegraut. Neues Feld im Modell und in der Overlay-Datei. Gilt gleich für Editor und Browserquelle.
- Im Editor läuft ein Video ab dem Wählen des Overlays. Wann ein Video in der Browserquelle startet, ist seit P10 festgelegt (unten, «Video beim Wechsel des Overlays»).

Entschieden (2026-11-03, vom Nutzer bestätigt), umgesetzt mit P09b:

- **Spiegeln:** Jede Quelle kann waagrecht und senkrecht gespiegelt sein (Felder `flip_horizontal`, `flip_vertical`, Format 3, `docs/06-overlay-format.md`). Der Inhalt wird in seinem Kasten um dessen Mitte gespiegelt, vor der Drehung; Breite und Höhe bleiben positiv.
- **EXIF-Ausrichtung:** Bilder werden so gezeigt, wie ihre EXIF-Ausrichtung es verlangt (wie der Browser mit `image-orientation: from-image`); die Originalgrösse ist die angezeigte Grösse.
- **Zweiter Zeichner (P10, K-07):** Die Browserquelle muss Spiegeln und EXIF-Ausrichtung genauso umsetzen wie der Editor: Reihenfolge Ausrichtung des Bildes → Strecken in den Kasten → Spiegeln um die Mitte → Drehung um die Mitte (CSS etwa `transform: rotate(<Winkel>) scale(-1, 1)`), EXIF bei JPEG und bei PNG (`eXIf` vor `IDAT`). Ob die Chromium-Version der Browserquelle in OBS die EXIF-Ausrichtung von PNG beachtet, ist nicht geprüft (Chromium unterstützt sie seit dem Commit 694d9d3a5cc2bc5fa8b36a5dd11b55d437555ca6 vom 2024-02-05, https://chromium.googlesource.com/chromium/src/+/694d9d3a5cc2bc5fa8b36a5dd11b55d437555ca6); das klärt der Bildvergleich (K-08) bzw. T-04.

Entschieden (2026-11-03, vom Nutzer bestätigt, Punkt 65), umgesetzt mit P10a:

- **Drehung von Videos:** Videos werden so gezeigt, wie die Drehung in ihren Metadaten (Display-Matrix `AV_PKT_DATA_DISPLAYMATRIX`, https://ffmpeg.org/doxygen/7.1/group__lavc__packet__side__data.html; ffprobe zeigt sie als «Display Matrix» mit `rotation`) es verlangt; die Originalgrösse ist die angezeigte Grösse (bei 90° und 270° Breite und Höhe vertauscht). Eine gespiegelte Matrix wird wie die EXIF-Ausrichtungen 2, 4, 5 und 7 umgesetzt. Reihenfolge wie bei Bildern: Drehung aus den Metadaten → Strecken in den Kasten → Spiegeln → Drehung der Quelle. Schalter beim Bau: `DSS_VIDEO_ROTATION`.
- **Farben von Videos (P11, Punkt 79):** Der Editor rechnet YUV in RGB wie die Browserquelle in OBS (OBS massgebend, Prüfung V 2026-11-08): Matrix und Bereich nach der Angabe der Datei; ohne Angabe der Matrix unter 720 Pixeln Höhe BT.601, sonst BT.709, bei VP9 und AV1 immer BT.601; voller Bereich nur mit Angabe. Die Grundfarben werden nicht umgerechnet. Code: `src/media/videocolor.{h,cpp}`, Quellen dort.
- **Zweiter Zeichner (P10):** Die Browserquelle muss die Drehung aus den Metadaten genauso beachten. P10 prüft das mit OBS (Browserquelle) und den Testvideos `tests/daten/video-drehung/drehung-{0,90,180,270}.mp4`: alle vier sehen gleich aus. Ob die Chromium-Version der Browserquelle in OBS die Drehung von Videos beachtet, ist nicht geprüft.

Festgelegt mit P10 (2026-11-04, Teil B 3 des Auftrags; Vorgaben aus Teil B 2):

- **Video beim Wechsel des Overlays:** Ein Video beginnt immer von vorne, wenn seine Quelle wieder gezeigt wird. Im Editor gilt das schon: Beim Wechsel auf ein anderes Overlay werden die Spieler beendet, beim Zurückwechseln neu angelegt, und das Video beginnt bei 0 (geprüft mit `tst_leinwandbild`, Slot `videoRestartsAfterOverlaySwitch`, 2026-11-04). In der Browserquelle beginnt ein Video beim Einblenden der Quelle, beim Laden oder Neuladen der Seite, beim Einschalten der Ausgabe und bei einer anderen Datei von vorne. Begründung: Beide Zeichner verhalten sich gleich, und es gibt keine Stelle, an der der Editor eine Laufzeit merken müsste. Der Wechsel des Overlays im Editor wirkt nicht auf die Browserquelle: Sie spielt unabhängig vom Editor und zeigt ihr Overlay weiter, auch wenn im Editor ein anderes gewählt ist.
- **Video in der Browserquelle:** stumm; Ende wie im Editor (`loop`, `hold_at_end`, sonst leer). Die Drehung aus den Metadaten und die EXIF-Ausrichtung setzt die Seite selbst um, nach Angaben des Kerns. Der Kern liefert die Dateien dafür mit Ausrichtung 1 bzw. Einheitsmatrix aus, damit das Ergebnis nicht von der Chromium-Version abhängt (`docs/08-kern-schnittstelle.md`, «Overlay-Server»).
- **Ausgabe ein/aus (E-24):** Der Zustand steht in der Overlay-Datei (Feld `output`, Format 4, `docs/06-overlay-format.md`), damit er einen Neustart übersteht und Editor und Browserquelle denselben Stand haben. Aus heisst: Die Browserquelle ist leer und durchsichtig, bleibt aber verbunden; der Editor bleibt bearbeitbar.
- **Nicht abspielbare Videos:** Kann Chromium ein Video nicht abspielen (auf dem Studio-PC mit Qt WebEngine 6.8.2: HEVC, MPEG-4 Part 2, ProRes; Bericht P10), bleibt die Quelle leer, und die Seite meldet es dem Kern (Protokoll). Beim Hinzufügen erscheint ein Hinweis, im Dock «Medien» ein Warnzeichen; kein automatisches Umwandeln (entschieden 2026-11-04, umgesetzt mit P10b, Punkt 81 in `docs/archiv/04-erledigte-punkte.md`).

## Einsatz (Version im Stream)

Entschieden (2026-11-03, vom Nutzer bestätigt), umgesetzt mit P10a:

- Getrennt werden die Version **in Arbeit** (Repo, von Claude Code geändert) und die Version **im Einsatz** (installiert, läuft im Stream).
- Eine neue Version kommt nur durch den Nutzer in den Einsatz, mit einem Befehl nach der Abnahme (`scripts/installieren.sh`). Claude Code installiert nie selbst.
- Installationsort: `~/.local/opt/dons-streaming-studio/` im Benutzerordner, ohne `sudo`. Bau dafür im Release-Modus.
- Daten getrennt: Die installierte Version nutzt die echten Daten (`~/.local/share/dons-streaming-studio`). Die Version aus dem Repo nutzt einen eigenen Entwicklungsordner mit eigenem Kern, eigener Verbindung und eigenen Fenster-Einstellungen; beide können gleichzeitig laufen. Beim Installieren werden die echten Dateien bei Bedarf in ein neues Format umgewandelt, vorher mit Sicherungskopie.
- Start der installierten Version über einen Eintrag «DoNs Streaming Studio» im Anwendungsmenü von Xfce, mit dem eigenen Programm-Symbol des Nutzers (Fernseher, aus dem alten Repo `donstv_Studio`, `assets/branding/`, eigenes Werk des Nutzers). Dasselbe Symbol im Fenster und in der Taskleiste.

Umgesetzt mit P10a (2026-11-03), Wahl von Claude Code mit Begründung im Bericht P10a:

- **Erkennen der Version:** Schalter beim Bau `DSS_INSTALLED_EDITION` (Standard aus = in Arbeit), nicht der Ort zur Laufzeit. Ein Bau aus dem Repo nutzt so immer den Entwicklungsordner, auch wenn er von woanders startet, und kann die echten Daten nicht treffen. `scripts/installieren.sh` baut mit `DSS_INSTALLED_EDITION=ON`.

| | Version im Einsatz | Version in Arbeit (Repo) |
|---|---|---|
| Programme | `~/.local/opt/dons-streaming-studio/bin/` | `build/src/…` |
| Daten | `~/.local/share/dons-streaming-studio` | `~/.local/share/dons-streaming-studio-entwicklung` |
| Einheit | `dss-core.service` | `dss-core-entwicklung.service` |
| Verbindung | `$XDG_RUNTIME_DIR/dons-streaming-studio/kern.sock` | `$XDG_RUNTIME_DIR/dons-streaming-studio-entwicklung/kern.sock` |
| Fenster-Einstellungen | `~/.config/dons-streaming-studio/fenster.ini` | `~/.config/dons-streaming-studio-entwicklung/fenster.ini` |
| Einstellungen des Kerns (seit P10-vor) | `~/.config/dons-streaming-studio/kern.ini` | `~/.config/dons-streaming-studio-entwicklung/kern.ini` |
| Port des Overlay-Servers (Standard, in `kern.ini`, wirkt ab P10) | 8080 | 8081 |
| Strom in PipeWire für den Ton der Videos (`node.name`, seit P12b) | `dss_core_ton_video` | `dss_core_entwicklung_ton_video` |
| Fenstertitel | «DoNs Streaming Studio» | «DoNs Streaming Studio – Entwicklung» |
| Symbole, Schriften, Übersetzung | `~/.local/opt/dons-streaming-studio/share/dons-streaming-studio/` | aus dem Repo bzw. `build/` |
| Eigene Symbole des Nutzers | `~/.config/dons-streaming-studio/symbole/` (beide lesen nur) | ebenso |
| Bilder der Leinwand (seit P10-vor) | `~/Bilder/DoNs Streaming Studio/` (Ordner aus `xdg-user-dir PICTURES`), beide Versionen | ebenso |

- `--version` und die Startzeile im Protokoll nennen Version und Commit, zum Beispiel «dss-app 0.1.0 (Einsatz, Commit 85ac702)».
- **Sicherung beim Umwandeln:** `~/.local/share/dons-streaming-studio-sicherungen/<JJJJ-MM-TT_hh-mm-ss>/`, eine vollständige Kopie des Datenordners (`cp -a`). Zurückspielen: `README.md`, Abschnitt «Einsatz».
- **Watchdog (Punkt 41):** `WatchdogSec=10s`, `TimeoutAbortSec=5s` in beiden Einheiten. Hängt der Kern, startet systemd ihn nach höchstens etwa 11 s neu (gemessen mit `kill -STOP`, Bericht P10a).
