---
topic: P14 Emotes, Emotes von Twitch, BTTV, 7TV und FFZ als Bilder im Bereich «Chat» des Unterreiters «Live» (Reiter «Chat-Bot»)
reference: src/app/chatbotpage.cpp (Bereich «Chat», ChatRowDelegate)
written: 2026-11-16 (Stand 4923161, Branch lotse/p14-emotes-plan; setzt auf P14 e3 auf)
---

# P14 Emotes: Plan

Grundlage: Checkliste `docs/archiv/prompts/P14-emotes-checkliste.md` (Abschnitte 1–6), Recherche `docs/archiv/recherchen/2026-11-16-emotes.md` (im Folgenden «Recherche» mit Abschnittsnummer), Plan `docs/archiv/prompts/P14-chatbot.md` (Zeile «Unterreiter «Live»»). Entscheid des Nutzers (2026-11-16): Emotes aus Twitch, BTTV, 7TV und FFZ im Chat «Live», gleich nach P14 als eigener Abschnitt; die neue Version wird erst installiert, wenn die Emotes fertig sind. Die Checkliste ist eine Momentaufnahme vom 2026-11-16. Gilt der Code etwas anderes, gilt der Code, und der Agent meldet die Abweichung.

Status eines Abschnitts: offen `[ ]`, erledigt `[x]`, `parked — <Grund>` bleibt offen mit Grund auf der Zeile.

## Für jeden Abschnitt

- Ein frischer Agent, **genau ein lokaler Commit** auf dem Bau-Branch der Emotes (oder einem Worktree-Branch, wenn parallel), etwa 1–2 h. Kein Push.
- Lesen: `CLAUDE.md`, `.claude/HANDOFF.md`, diesen Abschnitt, die Recherche nur in den genannten Abschnitten, die genannten Dateien gezielt (Grep, Ausschnitte).
- Prüfstufe «Klein»: `scripts/bauen.sh` (0 Warnungen), dann `scripts/testen.sh -R '<Tests des Abschnitts>'`. Fenstertests nie gleichzeitig; die X11-Sitzung muss aktiv und nicht gesperrt sein.
- Logik nur in `dss_common` (`src/common`), ohne Qt Widgets und ohne Qt Gui, mit Test ohne Fenster (Regel 9). Dekodieren und Zeichnen dürfen Qt Gui nutzen (Regel 9), Test ohne Fenster. Kein Layout-Wechsel in Ereignis-Behandlern.
- **Kein Test in `ctest` geht ins Netz.** Listen und Bilder kommen aus `tests/daten/emotes/` über einen lokalen HTTP-Stub (Regel 7). Kein Test schreibt in `~/.cache`; der Zwischenspeicher liegt im Test in einem `QTemporaryDir`.
- **Keine fremden Emote-Bilder ins Repo** (das Repo ist öffentlich; FFZ verbietet «redistributing», Recherche 4). Testbilder macht der Agent selbst.
- Rückmeldung an Lotse nach der Kurzvorlage in `CLAUDE.md`, mit allen neuen sichtbaren Texten im Wortlaut.

## Decided

Noch keine Entscheide zu den Fragen unten. Aus P14 gilt weiter: `settingsdialog.cpp` bleibt unverändert; Knöpfe und Rückfragen heissen «… löschen»; Prüfmeldungen und Texte kommen aus der Logik (P14-e3-Checkliste, Abschnitte 7 und 8).

## Wer lädt: das Fenster (Interna-Entscheid)

- **Das Fenster lädt** Listen und Bilder, hält den Zwischenspeicher und dekodiert. Gründe: Die Bilder braucht nur die Anzeige; der Kern hat kein Qt Gui und müsste Bilder sonst über die Verbindung schicken; jede Nachricht dafür wäre eine neue Version. Das Fenster kennt die Kanal-ID schon (`twitch_status`, `TwitchStatusInfo::channel.userId`, `src/common/protocol.h:511–522`).
- **Der Kern ändert nur eine Kleinigkeit** (em-1): Er übernimmt `emote.format` aus EventSub als `emote_format` in jedes Fragment (`src/twitch/eventsubprotocol.cpp:176–188`). Heute geht das Feld verloren, und Twitch-Emotes könnten nur statisch erscheinen.
- **Keine neue Version der Schnittstelle.** Ein Fenster der Version 18 liest das zusätzliche Feld im Fragment nicht und weist nichts ab; ein neues Fenster mit einem alten Kern nimmt ohne `emote_format` das statische Bild. Vorbild: «Seit P14 c1a (Version unverändert, nur neue Typen)» in `docs/08-kern-schnittstelle.md`, Abschnitt «Ereignisfluss». Die nächste freie Nummer bleibt **19** (`protocol.h:59`). Ob die Prüfung der Ereignisse im Kern auch die Felder **innerhalb** eines Fragments prüft, prüft em-1 (`docs/08`: «fehlendes oder zusätzliches Feld» gilt laut Text für die Felder des Ereignisses).
- **Zwischenspeicher:** nur der HTTP-Zwischenspeicher `QNetworkDiskCache` für Listen und Bilder aller vier Anbieter, keine eigene Bildsammlung (vorsichtige Variante, Recherche 4 und 7; Frage 4). Ort `~/.cache/<appDirName>/emotes` (`QStandardPaths::GenericCacheLocation`, getrennt je Version wie `configDir`), nicht der Datenordner (Regel 18 bleibt unberührt). Grösse höchstens 100 MiB (`setMaximumCacheSize`; Qt-Standard 50 MB, Recherche 7). Im Speicher höchstens 64 MiB dekodierte Bilder (`QCache` mit Kosten in Bytes).
- **Grenzen gegen zu grosse Antworten und Bilder** (Interna, Recherche 8 «Sicherheit»): Liste höchstens 8 MiB (7TV, grosser Kanal: rund 2,4 MB, Recherche 3), höchstens 2000 Emotes je Liste; Bild höchstens 2 MiB, höchstens 512 px je Seite (Grösse vor dem Dekodieren über `QImageReader::size()`, dazu `QImageReader::setAllocationLimit`), höchstens 256 Bilder je Animation; Bilddauer ≤ 10 ms gilt als 100 ms (wie Chatterino, Recherche 6). Zeitgrenze je Anfrage 15 s (`QNetworkRequest::setTransferTimeout`). Was darüber liegt, bleibt Text, eine Zeile ins Protokoll.
- **Nur die vier Anbieter:** Anfragen und Weiterleitungen nur an `api.betterttv.net`, `cdn.betterttv.net`, `7tv.io`, `cdn.7tv.app`, `api.frankerfacez.com`, `cdn.frankerfacez.com`, `static-cdn.jtvnw.net`, nur `https`, nur GET, ohne Twitch-Token, ohne Chat-Inhalte. Keine 7TV-«presences», kein BTTV-`broadcastMe`, kein Helix (Recherche 8).

## Ohne Netz, ohne WebP

- **Ohne Netz:** Listen zuerst aus dem Netz (`PreferNetwork`); scheitert das, aus dem Zwischenspeicher (`AlwaysCache`). Bilder `PreferCache`. Fehlt beides, bleibt das Wort Text. Statuszeile: «Emotes: Ohne Netz, Listen vom letzten Mal» bzw. «Emotes: Ohne Netz und ohne gespeicherte Listen».
- **Ohne WebP-Plugin** (heute am Studio-PC so, Checkliste Abschnitt 1): einmal beim Start `QImageReader::supportedImageFormats().contains("webp")`. Fehlt es, lädt das Fenster BTTV und 7TV weder Listen noch Bilder (keine nutzlosen Abrufe), FFZ nur statisch, Twitch normal (PNG/GIF über qtbase). Die Wörter bleiben Text. Statuszeile bei jedem Laden: «Emotes: WebP fehlt (Paket qt6-image-formats-plugins), BTTV und 7TV bleiben Text», dazu einmal `qWarning` ins Protokoll.
- **Paket:** `qt6-image-formats-plugins` kommt in `scripts/pakete.txt` (em-5, Version gemessen mit `dpkg-query`), damit `scripts/einrichten.sh` es prüft und mit `sudo apt install` installiert (der Nutzer bestätigt). Laut Recherche 6: Version 6.8.2-4 in Debian 13, enthält `libqwebp.so` (dazu tiff, tga, wbmp, icns, mng). **Kein** `kimageformat6-plugins` (AVIF), 7TV liefert WebP.

## Abschnitte, Übersicht

| Name | Inhalt | Braucht | Schätzung |
|---|---|---|---|
| em-0 | Lotse: Freigaben holen, Paket, Prüfung kalibrieren (kein Commit) | – | 0,5 h |
| em-1 | Kern: `emote_format` in den Fragmenten | – | 1 h |
| em-2 | Logik: Listen von BTTV, 7TV, FFZ lesen, Bild-Adressen | em-0 (Testdaten) | 2 h |
| em-3 | Logik: Erkennung, Abschnitte in `ChatRow`, Echo, Statustexte | em-2 | 2 h |
| em-4 | Logik: Laden mit Zwischenspeicher, Grenzen, nur vier Anbieter | em-2 | 2 h |
| em-5 | Bilder dekodieren, gemeinsamer Takt, Paket in `pakete.txt` | em-0 (Paket) | 1,5 h |
| em-6 | Fenster: Delegat zeichnet Emotes | em-3, em-5 | 2 h |
| em-7 | Fenster: Kontextmenü, Einstellung, Verdrahtung, Statuszeile | em-1, em-4, em-6 | 1,5 h |
| em-8 | Lotse: Handtest, Plan schliessen | alle | – |

Summe Agenten etwa 12 h (etwa 3 Sitzungen).

**Parallel in getrennten Worktrees** (nie zwei Fenstertests gleichzeitig):
- Spur Kern: em-1 (`src/twitch`, `src/events/eventtypes.h`, `docs/08`).
- Spur Logik: em-2, danach em-3 und em-4 parallel (beide fügen nur Zeilen in `src/common/CMakeLists.txt` an; em-4 dazu `tests/CMakeLists.txt`; Lotse führt diese Zeilen beim Zusammenführen von Hand zusammen).
- Spur Bilder: em-5 (`src/app/emoteimages.*`, `src/app/CMakeLists.txt`, `tests/CMakeLists.txt`, `scripts/pakete.txt`) parallel zur Logik, sobald das Paket installiert ist.
- Fenster em-6 und em-7 nacheinander (beide `chatbotpage.cpp`).

## Aufgaben

- [ ] **em-0 Vor dem Bau (Lotse, kein Commit).**
  - **Freigabe des Nutzers für Netzabrufe** (in der Sitzung, gebündelt mit den Fragen): einmal GET der globalen Listen (BTTV `3/cached/emotes/global`, 7TV `v3/emote-sets/global`, FFZ `v1/set/global`) und der Listen des eigenen Kanals (BTTV `3/cached/users/twitch/<ID>`, 7TV `v3/users/twitch/<ID>`, FFZ `v1/room/id/<ID>`), dazu je CDN ein `curl -I` auf ein Bild eines globalen Emotes (Cache-Header, Recherche 7: nicht bestätigt). Die JSON-Antworten kommen nach `tests/daten/emotes/`; Kanal-ID, Benutzernamen, Avatar-Adressen und `actor_id` ersetzt durch neutrale Werte (`100000001`, `testkanal`), sonst unverändert. Die Header kommen nur in die Rückmeldung von em-4. Ohne Freigabe: em-2 baut die Testdaten von Hand aus den Feldern der Recherche und meldet das als `[OFFEN]` (Regel 7).
  - **Paket:** Der Nutzer installiert `qt6-image-formats-plugins` (`sudo apt install qt6-image-formats-plugins`, sudo gibt er selbst ein). Ohne Paket bleibt em-5 `parked — Paket fehlt`.
  - **Prüfung kalibrieren** für die Art «Liste mit selbst gezeichneten Zeilen» wie in e3-0: Kopie von `src/app/chatbotpage.cpp` ausserhalb des Arbeitsordners, darin eine Pflanzung (zum Beispiel `setUniformRowHeights(false)` an `#chatBotChat` oder eine `QColor` als Literal im Delegaten statt eines Theme-Werts), ein getrennter Agent mit `ui-consistency:verifying`, der Checkliste (Abschnitt 3) und nur dem Hinweis «weicht irgendwo ab». Wird die Abweichung nicht gemeldet: anhalten. Wer pflanzt, prüft nicht; der kalibrierte Agent prüft danach keine Seite. Lotse behält einen Prüfer für em-6 und em-7.

### Kern

- [ ] **em-1 `emote_format` in den Fragmenten** (`src/twitch/eventsubprotocol.cpp`, Kommentar `src/events/eventtypes.h:70`, `docs/08-kern-schnittstelle.md` Abschnitt «Ereignisfluss»).
  - Je Fragment zusätzlich `emote_format`: Liste der Texte aus `emote.format` (`static`, `animated`), bei Fragmenten ohne Emote `null`. Quelle: https://dev.twitch.tv/docs/eventsub/reference/chat (Recherche 1).
  - Prüfen und im Bericht nennen: ob die Prüfung der Ereignisse (`eventtypes.h`, Prüfer im Kern) Felder innerhalb eines Fragments prüft; ob ein Test oder `dss-cli ereignisse` die Fragmente genau vergleicht (Suche `emote_id` in `tests/`: `tst_twitchprotokoll`, `tst_kernaktionen`, `tst_fensterchatbot`).
  - `docs/08`: eine Zeile «Seit P14 Emotes (Version unverändert): Fragmente in `twitch.chat_message` und `twitch.first_message` tragen `emote_format` …».
  - Testdaten: ein neues Fragment mit `"format": ["static", "animated"]` in einer Kopie nach dem Muster von `tests/daten/twitch/chat_message.json` (neutrale IDs).
  - Tests: `tst_twitchprotokoll` (Format statisch, animiert, ohne Emote `null`). `scripts/testen.sh -R '^(tst_twitchprotokoll|tst_kerntwitch|tst_kernaktionen)$'`.
  - Handtest: L2.

### Logik

- [ ] **em-2 Listen und Bild-Adressen** (neu `src/common/emotes.{h,cpp}`, `src/common/CMakeLists.txt`, neu `tests/tst_emotes.cpp`, `tests/CMakeLists.txt`, `tests/daten/emotes/*.json`).
  - `enum class EmoteProvider { Twitch, Bttv, SevenTv, Ffz }`; `struct Emote { EmoteProvider provider; QString id; QString name; bool animated; bool zeroWidth; int width; int height; }` (Grösse in der kleinsten Stufe, falls bekannt).
  - Lesen je Antwort, robust gegen fehlende Felder und falsche Typen, Fehlertext auf Deutsch ohne Inhalt der Antwort:
    - BTTV global (Array; `modifier: true` auslassen; Zero-Width nur über die feste Liste `SoSnowy`, `IceCold`, `SantaHat`, `TopHat`, `ReinDeer`, `CandyCane`, `cvMask`, `cvHazmat`, Recherche 2) und Kanal (`channelEmotes` und `sharedEmotes`, nie Zero-Width).
    - 7TV Set und Benutzer (`emote_set.emotes[]`, Name aus `name` im Set, Zero-Width wenn `(flags & 1) != 0`, `host.url` mit `https:` davor, nur Dateien mit `format` `WEBP`; Recherche 3). Der Zugriff auf v3 steht in einer eigenen Funktion (v4 später, Recherche 3 «Risiko»).
    - FFZ global (nur Sets aus `default_sets`) und Raum (`room.set` bzw. `sets`); `modifier: true` und `hidden: true` auslassen (Recherche 4).
  - Grenzen: höchstens 2000 Emotes je Liste (der Rest fällt weg, Meldung), Antworten über 8 MiB werden nicht gelesen.
  - Adressen: `QUrl emoteImageUrl(const Emote &, bool animate, bool webp)`: Twitch `https://static-cdn.jtvnw.net/emoticons/v2/{id}/{static|animated}/dark/1.0`; BTTV `https://cdn.betterttv.net/emote/{id}/1x.webp` bzw. statisch `…/{id}/static/1x.webp`; 7TV `https://cdn.7tv.app/emote/{id}/1x.webp` bzw. `1x_static.webp`; FFZ `https://cdn.frankerfacez.com/emote/{id}/1` bzw. animiert `…/animated/1`. Ohne WebP: BTTV und 7TV leer, FFZ nur statisch. Adressen der Listen: `QUrl emoteListUrl(provider, std::optional<QString> channelId)`.
  - Tests: `tst_emotes` mit den Dateien aus `tests/daten/emotes/` (Anzahl, Namen, Zero-Width je Anbieter, Modifier und `hidden` ausgelassen, `default_sets`, nur WEBP bei 7TV, kaputtes JSON, falscher Typ, Grenze 2000, Adressen mit und ohne WebP, animiert und statisch). `scripts/testen.sh -R '^tst_emotes$'`.
  - Handtest: keiner allein (über L2).

- [ ] **em-3 Erkennung, Abschnitte, Echo, Statustexte** (`src/common/emotes.{h,cpp}`, `src/common/chatbotlive.{h,cpp}`, `src/common/windowdata.h`, `tests/tst_emotes.cpp`, `tests/tst_chatbotlive.cpp`).
  - `class EmoteIndex`: aus den sechs Listen; Vorrang beim selben Namen FFZ-Kanal, BTTV-Kanal, 7TV-Kanal, FFZ-global, BTTV-global, 7TV-global (Recherche 5). Vergleich exakt, Gross- und Kleinschreibung zählt.
  - `struct ChatFragment { QString type; QString text; QString emoteId; QStringList formats; }`; `struct ChatSegment { QString text; std::optional<Emote> emote; }`. `QList<ChatSegment> chatSegments(const QList<ChatFragment> &, const EmoteIndex &)`: Fragmente `emote` werden Twitch-Emotes (Position aus EventSub, keine Textsuche; animiert, wenn `formats` `animated` enthält). Im übrigen Text an `' '` trennen, Leerzeichen bleiben Text, jedes Wort exakt im Index suchen. `cheermote` und `mention` bleiben Text. Zero-Width nur hinter einem Emote (sonst normal).
  - `ChatRow` bekommt `fragments` (aus `data.fragments`, bei `twitch.chat_sent` ein Fragment `text`) und `segments`. `chatRow(event, twitch, const EmoteIndex *)`; ohne Index ein Abschnitt Text wie heute.
  - `ChatRows::setEmotes(const EmoteIndex &)` rechnet die Abschnitte aller Zeilen neu (danach baut das Fenster die Liste neu, `resetChat`).
  - **Echo der eigenen Nachricht:** Kommt `twitch.chat_message` mit der `message_id` einer Zeile aus `twitch.chat_sent`, ersetzt es deren Fragmente, Abschnitte und Chatfarbe; keine zweite Zeile. `RowsAppend` bekommt `int changed = -1` (Stelle der geänderten Zeile); `EventRows` setzt es nie. Ob das Echo kommt, ist nicht belegt (`chatbotlive.h:105–106`); ohne Echo bleiben Twitch-Emotes in eigenen Nachrichten Text, BTTV/7TV/FFZ werden über den Namen erkannt.
  - Statustexte (Wortlaut Checkliste Abschnitt 4, «Neue Texte»): `QString emoteStatusText(const EmoteLoadState &)` mit Anzahl je Anbieter, nicht erreichbaren Anbietern, ohne Netz, ohne WebP, kein Kanal; `QString emoteReloadWaitText(int seconds)`; `QString emoteToolTip(const Emote &)` → «<Name> (BTTV)».
  - Tests: `tst_emotes` (Vorrang mit demselben Namen in allen sechs Listen, Gross-/Kleinschreibung, Wort mit Satzzeichen bleibt Text, mehrere Leerzeichen, Zero-Width vorne und hinter Text, Twitch-Fragment animiert/statisch, Statustexte, Tooltip); `tst_chatbotlive` (Abschnitte aus `tests/daten/twitch/chat_message.json`, `setEmotes` ändert bestehende Zeilen, Echo ersetzt statt anzuhängen, `changed`, ohne Index wie heute). `scripts/testen.sh -R '^(tst_emotes|tst_chatbotlive)$'`.
  - Handtest: H1, L2.

- [ ] **em-4 Laden mit Zwischenspeicher** (neu `src/common/emoteloader.{h,cpp}`, `src/common/configpaths.{h,cpp}`, `src/common/CMakeLists.txt`, neu `tests/tst_emoteladen.cpp`, `tests/CMakeLists.txt`).
  - `QString emoteCacheDir(Edition)` in `configpaths`: `~/.cache/<appDirName>/emotes` (`GenericCacheLocation`, Quelle https://doc.qt.io/qt-6.8/qstandardpaths.html).
  - `class EmoteLoader : QObject` mit eigenem `QNetworkAccessManager` und `QNetworkDiskCache` (Ordner und Endpunkte im Konstruktor, `struct EmoteEndpoints` mit den echten Adressen als Standard; Tests geben `http://127.0.0.1:<port>` an). `load(channelId, bool webp)`: die sechs Listen (ohne WebP ohne BTTV und 7TV), danach Signal `indexChanged(EmoteIndex)` und `stateChanged(EmoteLoadState)`. `image(const Emote &, bool animate)`: Bild-Bytes, Signal `imageLoaded(key, QByteArray)`; dieselbe Adresse nur einmal gleichzeitig.
  - Regeln wie oben unter «Wer lädt» und «Ohne Netz»: nur GET, nur die Hosts der Endpunkte (auch bei Weiterleitung, `QNetworkRequest::UserVerifiedRedirectPolicy` und Prüfung in `redirected`), Grössen- und Zeitgrenzen, `PreferNetwork` mit Rückfall `AlwaysCache` für Listen, `PreferCache` für Bilder, `User-Agent` fest ohne Rechnernamen. Neu laden höchstens alle 30 s (`reloadAllowedIn()` für «Emotes erst in 30 s wieder ladbar»; Uhr im Test einspeisbar). Ohne Kanal-ID nur die globalen Listen.
  - Cache-Header aus em-0 im Bericht nennen; senden die CDNs `no-store`, steht das als `[OFFEN]` im Bericht (dann lädt jeder Start neu).
  - Tests: `tst_emoteladen` mit lokalem HTTP-Stub (`QTcpServer`, liefert Dateien aus `tests/daten/emotes/`, zählt Anfragen und Methoden): sechs Listen, nur GET; ohne WebP keine Anfrage an BTTV/7TV; Antwort zu gross → Text und Meldung; Weiterleitung auf fremden Host abgewiesen; Stub aus → Listen aus dem Zwischenspeicher, Meldung «ohne Netz»; zweites Bild aus dem Zwischenspeicher ohne Anfrage; Sperre 30 s. Zwischenspeicher im `QTemporaryDir`. `scripts/testen.sh -R '^(tst_emoteladen|tst_configpaths)$'`.
  - Handtest: L3, H2.

### Bilder

- [ ] **em-5 Dekodieren, gemeinsamer Takt, Paket** (neu `src/app/emoteimages.{h,cpp}` nur mit Qt Gui, `src/app/CMakeLists.txt`, neu `tests/tst_emotebilder.cpp`, `tests/CMakeLists.txt`, `tests/daten/emotes/bilder/`, `scripts/pakete.txt`).
  - Voraussetzung: `qt6-image-formats-plugins` installiert (em-0). Version mit `dpkg-query -W -f='${Version}' qt6-image-formats-plugins` messen und mit Begründung in `scripts/pakete.txt` eintragen (Muster der Kommentare dort, Regel 5).
  - `class EmoteImages`: `bool webpAvailable()` (einmal `QImageReader::supportedImageFormats()`); `add(key, QByteArray)` dekodiert mit `QBuffer`, `QImageReader::setDecideFormatFromContent(true)`, Grenzen (2 MiB, 512 px, 256 Bilder, `setAllocationLimit`), alle Bilder vorab mit `nextImageDelay()` (≤ 10 ms → 100 ms), auf die Emote-Höhe (Frage 2) mit `Qt::SmoothTransformation` skaliert, als `QImage`/`QPixmap` im `QCache` (64 MiB). `const QPixmap *frame(key, qint64 elapsedMs, bool animate)`: Bild zum gemeinsamen Zeitpunkt (eine Uhr für alle, Recherche 6). `bool hasAnimated() const`. Kein `QMovie`.
  - Testbilder selbst gemacht, nie von einem CDN: PNG und statisches WebP mit `QImageWriter`; GIF mit zwei Bildern von Hand (kleines GIF89a, der Agent wählt das Werkzeug und nennt es; ImageMagick, `img2webp` und `gif2webp` fehlen am Studio-PC, geprüft 2026-11-16). Ein animiertes WebP lässt sich ohne Werkzeug nicht herstellen: Prüfung nur im Handtest (L2), `[OFFEN]` im Bericht.
  - Tests: `tst_emotebilder` ohne Fenster (PNG, GIF mit zwei Bildern und Dauer, `frame` nach Zeit, Animation aus → erstes Bild, WebP statisch, zu gross abgewiesen, kaputte Bytes, Speichergrenze, `webpAvailable`). `scripts/testen.sh -R '^tst_emotebilder$'`.
  - Handtest: L2, L4.

### Fenster

Jede Fenster-Aufgabe trägt ihre Checkliste: eine Momentaufnahme vom 2026-11-16 (Stand 4923161) mit Referenz `src/app/chatbotpage.cpp` (Bereich «Chat», `ChatRowDelegate`). Die Art heisst «Liste mit selbst gezeichneten Zeilen» (1 Mitglied mit eigenem Zeichnen von 10 Listen). Ablauf jeder Fenster-Aufgabe:
1. Checkliste vor dem Schreiben neu lesen, nicht aus dem Gedächtnis arbeiten.
2. Seite als prüfbereit melden. Lotse schickt sie an den Prüfer dieser Art (ein Agent mit `ui-consistency:verifying`, ab em-6 für beide Aufgaben derselbe, nie der Schreibende oder der Kalibrierte) und gibt dessen Befund zurück. Der Agent behebt ihn im selben Commit (`--amend`, nur lokal).
3. Status an Lotse: `done` nach bestandener Prüfung oder `parked — <Grund>`.

- [ ] **em-6 Delegat zeichnet Emotes** (`src/app/chatbotpage.{h,cpp}`).
  - `ChatBotPage::setEmoteImages(const EmoteImages *)`, `emoteImagesChanged()` (nur `m_chat->viewport()->update()`), `setAnimateEmotes(bool)`. `chatItem` legt die Abschnitte in `kChatSegmentsRole`; `appendChat` beachtet `RowsAppend::changed` (Rollen der Zeile ersetzen, keine neue Zeile).
  - Ein `QTimer` je Seite als gemeinsamer Takt (30 ms, Uhr `QElapsedTimer`), läuft nur, wenn «Live» sichtbar ist, «Emotes animieren» an ist und `hasAnimated()`.
  - Checkliste:
    - Liste bleibt `QTreeWidget` `#chatBotChat` mit `setUniformRowHeights(true)`, `setHeaderHidden(true)`, `NoFocus`, Zeit `digit * 10`; keine zweite Liste. Entschieden: Referenz; feste Zeilenhöhe 10 von 10 Listen in 7 Dateien.
    - Zeichnen nur im vorhandenen `ChatRowDelegate` (Abzeichen, Name, Abschnitte); kein `QLabel`, kein `QMovie`, kein Widget je Emote; Farben nur aus dem Theme und der Chatfarbe. Entschieden: Referenz.
    - Text der Zelle bleibt die ganze Nachricht (Suche, Tooltip, Tests); Abschnitte als weitere Rolle. Entschieden: Referenz.
    - Feste Emote-Höhe nach Frage 2 (Empfehlung 28 px); Zeilenhöhe aus `sizeHint` des Delegaten, für jede Zeile gleich; Breite nach Seitenverhältnis, höchstens 4-mal die Höhe; Abstand 4 px wie zwischen den Abzeichen. Entschieden: Frage 2; Abstand Referenz (`chatbotpage.cpp:134`).
    - Zero-Width über das vorherige Emote gelegt, beide mittig, Breite die grössere; ohne Emote davor normal. Entschieden: Auftrag, Recherche 5.
    - Ohne Bild das Wort als Text in `m_theme.text` wie heute, kein Platzhalter; ist das Bild da, nur neu zeichnen, keine neuen Zeilen. Entschieden: Referenz, Regel 9.
    - Zu lang: eine Zeile, `elidedText(…, Qt::ElideRight, …)`; ein Emote, das nicht ganz passt, fällt weg, an seiner Stelle «…». Entschieden: Referenz (`:146–147`).
    - Tooltip über einem Emote aus `emoteToolTip` über `helpEvent` des Delegaten, sonst der Text der Zelle. Entschieden: keine Konvention, Frage 2.
    - Animation über den gemeinsamen Takt, nur `viewport()->update()`; aus → erstes Bild. Entschieden: Auftrag, Recherche 6.
  - Nicht übernehmen: Aussehen von Chatterino oder jChat; Werte der Recherche als Abstände; `setIcon` für Emotes; Emotes in «Ereignisse» und in grauen Ereigniszeilen.
  - Die Zeilenhöhe vorher und nachher messen (`visualItemRect`) und im Bericht nennen.
  - Tests: `tst_fensterchatbot` (Zeilenhöhe gleich für Zeilen mit und ohne Emote; ein einfarbiges Test-PNG erscheint an der erwarteten Stelle im Bild von `m_chat->viewport()->grab()`; ohne Bild steht das Wort als Text; Bild kommt später → gleiche Zeilen, neu gezeichnet; Zero-Width; «…» bei zu langer Zeile; Tooltip; Takt läuft nur mit sichtbarem «Live», animiertem Emote und Animation an; Echo ersetzt die Zeile). `scripts/testen.sh -R '^tst_fensterchatbot$'`.
  - Handtest: H1, L1.

- [ ] **em-7 Bedienung, Einstellung, Verdrahtung** (`src/app/chatbotpage.{h,cpp}`, neu `src/app/mainwindowemotes.cpp` nach dem Muster von `mainwindowcommands.cpp`, `src/app/mainwindow.{h,cpp}`, `src/app/CMakeLists.txt`).
  - Verdrahtung: `EmoteLoader` und `EmoteImages` im `MainWindow`; Laden beim ersten `twitch_status` mit Kanal-ID und bei jeder anderen Kanal-ID; `indexChanged` → `m_chatRows.setEmotes` und `resetChat`; Bilder der sichtbaren und neuen Zeilen bestellen; `imageLoaded` → `EmoteImages::add` → `emoteImagesChanged`. Testoption für die Endpunkte (lokaler Stub) nach dem Muster der vorhandenen Testoptionen des Fensters (Agent sucht sie; ohne Muster `[OFFEN]` und Rückfrage an Lotse).
  - Checkliste:
    - Ort nach Frage 1. Empfehlung: Kontextmenü der Liste über `setContextMenuPolicy(Qt::CustomContextMenu)`, `customContextMenuRequested` → `QMenu menu(this); … menu.exec(m_chat->viewport()->mapToGlobal(position))`, Einträge «Emotes neu laden» und «Emotes animieren» (ankreuzbar, Haken bei an). Entschieden: Mehrheit 6 von 6 Kontextmenüs in 5 Dateien für den Weg; ankreuzbarer Eintrag ohne Vorbild (0 von 6), Frage 1. Bei der anderen Antwort: zweite Leiste über `buttonBar` mit `QCheckBox` «Emotes animieren» und `QPushButton` «Emotes neu laden» (2 von 2 Ansichtsschaltern in «Live»).
    - Ergebnis des Ladens über `showStatus()` mit `emoteStatusText`/`emoteReloadWaitText`, nie im Fenster zusammengesetzt; fehlt WebP oder ist ein Anbieter nicht erreichbar, zusätzlich einmal `qWarning` ohne Kanal-ID. Entschieden: geteilter Baustein `showStatus`.
    - «Emotes animieren» in `fenster.ini` (Schlüssel `chat/emotes_animieren`, Standard an), nicht im Dialog «Einstellungen». Entschieden: `settingsdialog.cpp` bleibt unverändert (Entscheid e2); `fenster.ini` ist der Ort der Fenstereinstellungen (`mainwindow.cpp:70–74`).
  - Nicht übernehmen: ein Eintrag im Dialog «Einstellungen»; ein Takt zum Neuladen (Frage 3).
  - Tests: `tst_fensterchatbot` (Einträge des Menüs; «Emotes animieren» aus → Takt steht, Wert in `fenster.ini` unter eigenem `XDG_CONFIG_HOME`; nach Neustart gleich; «Emotes neu laden» zweimal schnell → Wartetext; mit Stub: Statuszeile mit Anzahl; Stub aus → «ohne Netz»). `scripts/testen.sh -R '^tst_fensterchatbot$'`.
  - Handtest: H2, H3.

- [ ] **em-8 Handtest und Plan schliessen (Lotse).** Nach dem Handtest gehen die Entscheide unter «Decided» mit dem Ergebnis an den Nutzer, offene Punkte nach `docs/04-offene-punkte.md`. Danach Stufe «Vor der Installation» und Installation durch den Nutzer (Entscheid 2026-11-16: erst nach den Emotes).

## Ausdrücklich nicht in diesem Abschnitt

- Cheermotes (Bild bräuchte Helix «Get Cheermotes», Recherche 1); Fragmente `cheermote` bleiben Text.
- Effekte der FFZ-Modifier und der BTTV-Modifier (`c!`, `h!` …); solche Emotes bleiben Text (Recherche 2, 4).
- Live-Änderungen über die 7TV-EventAPI und den BTTV-Socket; neue Emotes erscheinen nach «Emotes neu laden» (Recherche 3, 7).
- 7TV-«presences», BTTV-`broadcastMe`, Helix-Abrufe für Emotes, Anmeldung bei den Anbietern.
- AVIF und `kimageformat6-plugins`.
- Ausblenden nach 7TV-Inhaltsflags (`ContentEpilepsy`, `ContentSexual`, Recherche 3); Emotes erscheinen wie bei Chatterino.
- Persönliche 7TV-Emotes und Kosmetik, Emotes geteilter Chats (`source_*`), Abzeichen als Bilder.
- Emote-Auswahl oder Vervollständigung in der Eingabezeile.
- Emotes in «Ereignisse» (Spalte «Details»), in grauen Ereigniszeilen, in Overlays und der Browserquelle.
- Grössere Stufen (2x, 3x) für Bildschirme mit Skalierung; der Studio-PC hat 1920x1080 ohne Skalierung (`docs/02-umgebung.md`).
- «Animieren nur bei Fokus» (für ein Studio-Programm nicht sinnvoll, Recherche 6).

## Handtest Emotes (Regel 10)

Kern und Fenster aus `build/` mit eigenem `XDG_CONFIG_HOME`, Datenordner der Version in Arbeit, nie während eines Streams. Was die Tests prüfen (Erkennung, Vorrang, Grenzen, Zwischenspeicher, Statustexte), wiederholt der Nutzer nicht.

**Prüft Lotse**

**L1 Bildschirmfotos.** «Live» mit Emotes aller vier Anbieter, einem Zero-Width-Emote und einer zu langen Zeile; Ablage nur im Scratchpad. Warum: Aussehen, Zeilenhöhe, Lage der Emotes.

**L2 Echte Nachrichten.** Als Kanal eine Nachricht «DSS-Test» mit einem statischen und einem animierten Twitch-Emote, einem BTTV-, 7TV- und FFZ-Emote und einem Zero-Width-Emote senden (Recht aus `CLAUDE.md` für die Handtests von P14; ob es diesen Abschnitt deckt, bestätigt Lotse vor dem Test beim Nutzer). Erwartet: alle als Bild, animierte bewegen sich (auch animiertes WebP), die eigene Nachricht zeigt das Twitch-Emote nach dem Echo. Warum: echter Weg über Twitch und die vier Anbieter.

**L3 Zwischenspeicher.** Nach L2 Ordner `~/.cache/dons-streaming-studio-entwicklung/emotes` ansehen (Grösse ≤ 100 MiB, nur dieser Ordner), Fenster neu starten: Bilder erscheinen ohne neuen Abruf (laut Protokoll oder Zähler). Warum: echte Cache-Header der CDNs.

**L4 Last.** 20 animierte Emotes sichtbar, 60 s `pidstat -p <PID von dss-app> 1` mit Animation an und aus; Messwerte mit Datum und Befehl im Bericht (Regel 3). Warum: der Takt läuft neben dem Zeichnen der Overlays.

**Prüft der Nutzer**

**H1 Aussehen.** Emotes in «Live»: Grösse, Zeilenhöhe, Zero-Width, Tooltip «<Name> (<Anbieter>)» über einem Emote. Warum: Aussehen.

**H2 Bedienung.** «Emotes neu laden» (Statuszeile mit Anzahl; zweimal schnell → «Emotes erst in 30 s wieder ladbar»), «Emotes animieren» aus und an, nach Neustart unverändert. Warum: Bedienung, Wortlaut.

**H3 Neues Emote im Kanal.** Im eigenen 7TV- oder BTTV-Kanal ein Emote hinzufügen, «Emotes neu laden», im Chat verwenden. Warum: Das Set des Kanals kann nur der Nutzer ändern.

## Fragen an den Nutzer (gebündelt, je mit Empfehlung)

Wortlaut und Gegenvorschlag in der Checkliste, Abschnitt 4. Ohne Antwort baut em-6/em-7 die Empfehlung.

1. **«Emotes neu laden» und «Emotes animieren»:** **Empfehlung:** im Kontextmenü der Chat-Liste («Emotes animieren» ankreuzbar, Standard an), Ergebnis in der Statuszeile. Die andere Möglichkeit: eine zweite Leiste unter der Eingabezeile. Betrifft em-7.
2. **Grösse der Emotes:** **Empfehlung:** 28 px hoch (Twitch-Grösse 1x), alle Chat-Zeilen dafür gleich etwas höher; Tooltip mit Name und Anbieter. Die andere Möglichkeit: Emotes auf Zeilenhöhe verkleinert. Betrifft em-5, em-6.
3. **Neu laden im Takt:** **Empfehlung:** kein Takt; beim Start, bei anderem Kanal und auf Knopf (höchstens alle 30 s). Die andere Möglichkeit: zusätzlich alle 60 min. Betrifft em-4, em-7.
4. **Bedingungen der Anbieter:** **Empfehlung:** alle vier nur über den HTTP-Zwischenspeicher, nichts davon ins Repo; der Nutzer liest die Bedingungen von BTTV, 7TV und das Developer Agreement von Twitch vor dem Handtest im Browser. Die andere Möglichkeit: FFZ weglassen. Betrifft em-2, em-4.

Dazu eine Freigabe (keine Gestaltungsfrage): die Netzabrufe für die Testdaten und `curl -I` aus em-0. Ohne Freigabe Testdaten von Hand (`[OFFEN]`).

## `[OFFEN]` und nicht bestätigt (aus der Recherche)

- Nutzungsbedingungen von BTTV, 7TV und Twitch Developer Agreement: nicht gelesen (Frage 4). Auslegung der FFZ-Bedingung «downloading»: unklar.
- Cache-Header der CDNs und Rate-Limits von BTTV, FFZ und 7TV (Werte): nicht bestätigt; em-0/em-4 holen die Header.
- Ob Twitch das Bauen der Bild-Adresse ohne Helix ausdrücklich gutheisst: nicht bestätigt (Recherche 1, Ableitung).
- Dateiformat der animierten FFZ-Bilder: nicht bestätigt; erkannt über den Inhalt (`setDecideFormatFromContent`).
- Ob animiertes WebP mit dem Debian-Paket abspielt: im Quellcode belegt, nicht getestet (L2).
- Ob nicht freigeschaltete Twitch-Emotes in EventSub als `text` ankommen: nicht bestätigt; sie bleiben dann Text oder werden als Drittanbieter-Emote erkannt.
- Ob das Echo der eigenen Nachricht über EventSub kommt: nicht belegt (em-3).
- Ob die Prüfung der Ereignisse im Kern Felder innerhalb eines Fragments prüft: em-1 klärt.
- Abkündigung von 7TV v3: nicht bestätigt; Zugriff gekapselt (em-2).
- Ob das Recht aus `CLAUDE.md` («Handtests von P13 und P14») die Testnachrichten dieses Abschnitts deckt: Lotse fragt vor L2.
- Ob `scripts/installieren.sh` oder `installieren-pruefen.sh` fehlende Laufzeit-Pakete melden soll: nicht geprüft; das Fenster meldet fehlendes WebP selbst.

## Annahmen, Interna-Entscheide

- Laden, Zwischenspeicher und Dekodieren im Fenster, nicht im Kern: Begründung unter «Wer lädt». Kern nur `emote_format`, ohne neue Version.
- Ein Zwischenspeicher für alles (`QNetworkDiskCache`), keine eigene Bildsammlung: einfacher und vorsichtig gegenüber FFZ; Nachteil: ohne Cache-Header lädt jeder Start neu.
- Grenzen (8 MiB Liste, 2000 Emotes, 2 MiB und 512 px je Bild, 256 Bilder, 15 s, 100 MiB Platte, 64 MiB Speicher): eigene Werte gegen Ausreisser, aus den Mengen der Recherche 3 und 7 abgeleitet, nicht gemessen.
- Ohne WebP keine Abrufe an BTTV und 7TV: keine nutzlosen Anfragen an Dritte (Recherche 8).
- Twitch `theme_mode` `dark` (Theme des Programms ist dunkel), Stufe 1x überall (Bildschirm ohne Skalierung).
- Takt 30 ms mit gemeinsamer Uhr: alle Emotes laufen gleich, ein Neuzeichnen der sichtbaren Liste je Takt; L4 misst die Last.
- Testdaten der Listen aus echten Antworten mit ersetzten Kanaldaten (Regel 7 und Grenze «keine echten Namen»); Testbilder selbst gemacht.
- Abweichung vom Skill `ui-consistency:planning`: Checkliste und Plan liegen auf Auftrag von Lotse im Repo (`docs/archiv/prompts/`), nicht ausserhalb (wie e3). Ende 3 des Skills: Die Aufgaben gehören in den Ablauf des Projekts.

## Quellen

- Recherche `docs/archiv/recherchen/2026-11-16-emotes.md` (alle URLs dort, Abrufdatum 2026-11-16), insbesondere https://dev.twitch.tv/docs/eventsub/reference/chat, https://dev.twitch.tv/docs/api/reference/chat, https://www.frankerfacez.com/terms, https://doc.qt.io/qt-6/qimagereader.html, https://doc.qt.io/qt-6/qnetworkdiskcache.html, https://doc.qt.io/qt-6/qnetworkrequest.html, https://packages.debian.org/trixie/amd64/qt6-image-formats-plugins/filelist.
- https://doc.qt.io/qt-6.8/qstandardpaths.html (`GenericCacheLocation`).
- Checkliste `docs/archiv/prompts/P14-emotes-checkliste.md`; Vorbild `docs/archiv/prompts/P14-e3-plan.md`.
- Code: `src/app/chatbotpage.cpp:66–163`, `:293–337`, `:415–429`; `src/common/chatbotlive.h:81–121`, `chatbotlive.cpp:227–305`; `src/common/windowdata.h:186–194`; `src/twitch/eventsubprotocol.cpp:165–224`; `src/events/eventtypes.h:68–83`; `src/common/protocol.h:59`, `:492–522`; `src/common/configpaths.h`; `src/app/mainwindow.cpp:70–74`, `:323–358`, `:511–539`; `src/app/panels.h:70–83`; `src/app/theme.cpp` (Schrift, `QTreeView::item`, Farben der Abzeichen); `scripts/pakete.txt`, `scripts/einrichten.sh`; `tests/daten/twitch/chat_message.json`.
- `docs/08-kern-schnittstelle.md`, «Version 18» und «Ereignisfluss».
