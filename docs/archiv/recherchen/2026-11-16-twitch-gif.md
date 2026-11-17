# Twitch-GIFs im Chat: EventSub und Anzeige in Qt 6.8

Abrufdatum aller Quellen: 2026-11-16. «Ableitung» = eigener Schluss, «nicht bestätigt» = keine Quelle gefunden.
Die Seiten wurden über Lesehilfen (Subagenten) abgerufen. Einzelne Zitate stammen aus deren Zusammenfassungen und sind unten vermerkt.

## 1. Fragment `type: gif` (EventSub)
Quelle: https://dev.twitch.tv/docs/eventsub/reference/chat (Abschnitt `channel.chat.message`, Version 1)
- `event.message.fragments.type` (String): Werte `text`, `cheermote`, `emote`, `mention`, `gif`.
- `event.message.fragments.gif` (ObjectNullable): «Metadata pertaining to the GIF.»
- `...gif.id` (String): «An ID that uniquely identifies this GIF.»
- `...gif.url` (String): «The URL of the GIF asset. Applications rendering the GIF must use the full URL provided; it must not be modified.»
- Beispiel-JSON mit gif-Fragment: auf der Referenzseite **keines** (das Beispiel enthält nur ein `text`-Fragment).
- `channel.chat.notification`: `gif` fehlt in der Typliste (wörtlich geprüft). `user_message_hold`/`_update`: nicht wörtlich geprüft, nicht bestätigt.
- Seit wann: Changelog 2026-07-16, https://dev.twitch.tv/docs/change-log/: «The channel.chat.message EventSub subscription type has been updated to include the new gif fragment type. GIF messages now include a gif object with gif_id and url fields alongside the existing text on the message fragment.»
- **Widerspruch:** Changelog sagt `gif_id`, Referenz sagt `gif.id`. Ableitung: Referenz massgebend; beide Namen tolerant lesen und mit echter Nachricht prüfen.
- Beta/Status: kein Beta-Hinweis auf den Seiten (Subscription bleibt Version 1). Die Angabe, das Feature sei zunächst nur in ausgewählten Kanälen verfügbar, stammt aus Sekundärquelle (siehe 3).

## 2. Herkunft des Bildes
- Feld: **URL und ID** (`gif.url`, `gif.id`).
- Anbieter: **Giphy** (Ankündigung Twitch auf X: https://x.com/Twitch/status/2060651601619042410, über Suchtreffer, nicht selbst gelesen). Beispiel-URL in der IRC-Doku: `https://media4.giphy.com/media/joSNxeswxuc74Juo8X/giphy.gif?cid=…&ep=…&rid=giphy.gif&ct=g` (https://dev.twitch.tv/docs/chat/irc/).
- Format: im Twitch-Beispiel `.gif`. Webp/MP4 in der Twitch-Doku nicht erwähnt. Eine Host-Liste oder Grössenvarianten nennt Twitch nicht (nicht bestätigt). Seitenverhältnis: nicht bestätigt (Ableitung: je GIF verschieden, Grösse aus dem Bild lesen).
- Giphy-Doku (https://developers.giphy.com/branch/master/docs/api/): Jedes GIF hat mehrere Renditions, MP4 wird für GIFs empfohlen, WebP für Sticker (Teil davon laut Zusammenfassung, nicht wörtlich geprüft). Beispiel-Host `media1.giphy.com`; ein festes Muster `media*.giphy.com` definiert die Seite nicht.
- Chatterino baut selbst andere URLs aus der ID: `https://media4.giphy.com/media/<id>/100.webp` und `/200.webp` (Link: `https://i.giphy.com/<id>.webp`), siehe 6. Das ist ein Weg an der Vorgabe «URL nicht verändern» vorbei; Twitch-Doku-Konformität nicht bestätigt.

## 3. Wer darf senden, Ein-/Ausschalten, Moderation
- Sekundärquellen (Suche, nicht selbst gelesen): Tier-2- und Tier-3-Abonnenten, per Emote-Menü, Reiter «GIFs»; ca. 30 s Abklingzeit; Streamer können GIFs moderieren, die Häufigkeit festlegen und abschalten; Moderatoren löschen einzelne GIFs; Giphy-Altersstufe Standard PG. Quellen: https://unboxholics.com/en/news/gaming/134441-twitch-adds-giphy-gifs-directly-to-chat, https://tos.gg/gifs-in-twitch-chat/, https://tigerjek.com/twitch-finally-lets-subscribers-send-gifs-directly-in-chat/. Nicht bestätigt durch Twitch-Hilfe (help.twitch.tv war nicht lesbar).
- Helix: «Get/Update Chat Settings» enthält laut Zusammenfassung (https://dev.twitch.tv/docs/api/reference/chat) kein GIF-Feld (Felder: emote_mode, follower_mode, slow_mode, subscriber_mode, unique_chat_mode, non_moderator_chat_delay). Eigener Scope für GIFs: nicht gefunden. Ableitung: Lesen braucht denselben Scope wie `channel.chat.message` (`user:read:chat`), nichts Zusätzliches.

## 4. Feld `text` und IRC-Rückfall
- EventSub: Inhalt von `text` im gif-Fragment **nicht spezifiziert** («Message text in fragment»). Ableitung aus IRC-Beispiel: Beschreibungstext, z. B. `[Y A Y Yes GIF by Djemilah Birnie]`.
- IRC (https://dev.twitch.tv/docs/chat/irc/, Changelog 2026-07-17): Tag **`gifs`** (Plural; der Changelog schreibt «gif»): «Comma-separated list of GIFs and their positions in the message. Each GIF is in the form, `<start position>-<end position>|<gifID>|<gifURL>`. The position indices are zero-based.» Beispiel:
```
@badge-info=subscriber/30;badges=broadcaster/1,subscriber/0;…;gifs=0-33|joSNxeswxuc74Juo8X|https://media4.giphy.com/media/joSNxeswxuc74Juo8X/giphy.gif?cid=…&ct=g;id=401abf17-…;…;tmi-sent-ts=1783632907018;… :twitchdev!twitchdev@twitchdev.tmi.twitch.tv PRIVMSG #twitch :[Y A Y Yes GIF by Djemilah Birnie]
```
- Auffällig: Bereich `0-33` = 34 Zeichen, der Text hat nach Zählung der Lesehilfe 38. Abweichung nicht erklärt; Positionen nicht blind zum Ausschneiden verwenden, Ableitung: den gesamten Text als Beschreibung/Tooltip nehmen, wenn die Nachricht nur aus dem GIF besteht.

## 5. Bedingungen und Datenschutz
- Twitch: «must use the full URL provided; it must not be modified» (EventSub und IRC). Aussagen zu Caching in Twitch-Doku oder Developer Agreement: nicht gefunden (Vertragstext war nicht abrufbar, das heisst nicht, dass er nichts enthält).
- Giphy (https://developers.giphy.com/branch/master/docs/api/, wörtlich): «Do not cache API responses or media URLs or cache or store copies of GIPHY's media assets.» «Do not strip or modify URLs … do not remove or modify query params in our media URLs.» «Do not proxy requests to GIPHY … All requests to GIPHY should be made directly from the client side.» Attribution «Powered By GIPHY» wird für Apps verlangt, die die **API** nutzen. Ob das für reines Rendern von Twitch-gelieferten URLs gilt: nicht bestätigt. Giphy-Nutzungsbedingungen selbst waren nicht abrufbar (403).
- Ableitung: Dauerhafter Datei-Cache auf der Platte und HTTP-Cache sind nach Giphy-Doku heikel. Sicher: URL unverändert laden, nur im Arbeitsspeicher halten (kleiner LRU), kein Proxy. Das ist **keine Rechtsauskunft**.
- Was beim Abruf übermittelt wird: nicht bestätigt. Ableitung: bei jedem Abruf sieht Giphy mindestens IP-Adresse und User-Agent; die `cid`/`rid`-Parameter in der URL gehören zu Giphy. Eine Option «GIFs nicht laden / nur Link» ist deshalb sinnvoll.

## 6. Andere Clients: Chatterino
Quellen: https://github.com/Chatterino/chatterino2/pull/7222 («add Twitch gifs as links», gemergt 2026-09-06) und /pull/7223 («as images», gemergt 2026-09-12).
- Liest den **IRC-Tag `gifs`**, kein EventSub. `src/providers/twitch/TwitchIrc.cpp`: Eintrag an `|` teilen (Bereich, ID, URL); übernommen wird nur die **ID**, die URL nur auf «nicht leer» geprüft (`TwitchGifOccurrence { QString id; }`).
- Anzeige (`MessageBuilder::addTwitchGif`): `ImageSet` aus `https://media4.giphy.com/media/<id>/100.webp` (Faktor 1.0, 100×100) und `/200.webp` (0.5, 200×200), vor dem GIF ein Zeilenumbruch, Klick-Link `https://i.giphy.com/<id>.webp`, Tooltip = Originaltext.
- Einstellung `showTwitchGifs` (Standard an, «Show Twitch GIFs»); aus = nur Link. Später laut Suchergebnis Skalierungseinstellung (Oktober 2026), https://github.com/Chatterino/chatterino2/releases (nicht selbst gelesen).
- Bekanntes Problem aus den PR-Kommentaren: manche GIFs erzeugen «image too large in RAM».
- Animation/Decoder-Code von Chatterino (`Image`-Klasse) wurde nicht gelesen. Andere Clients: nicht untersucht.

## 7. Qt 6.8 (alles Ableitung, nichts davon in diesem Lauf geprüft)
- GIF: `QMovie`/`QImageReader` (qtbase-Plugin `gif`) genügt. Animierte Bilder mit `QImageReader::imageCount()`/`nextImageDelay()` oder `QMovie`.
- WebP (so lädt Chatterino): Qt-Plugin `qwebp` aus `qt6-image-formats-plugins` (Debian-Paket, nicht geprüft); auch animiertes WebP wird von `QMovie` abgespielt (nicht geprüft).
- MP4/WebM: `QMovie` geht nicht; nötig wäre Qt Multimedia (`QMediaPlayer` + `QVideoSink`/`QVideoWidget`, Decoder via FFmpeg-Backend, Abhängigkeit schwer). Für eine Chatliste mit vielen Clips ungünstig.
- Nicht über `QImageReader` laden mit unbegrenzter Grösse: vor dem Dekodieren Bildgrösse und Bildzahl begrenzen (Speicher, vgl. Chatterino-Problem). Netz: `QNetworkAccessManager` mit eigener Speicherstrategie, kein Platten-Cache.

## Empfehlung
1. EventSub `channel.chat.message` lesen, Fragment `type == "gif"`, `gif.url` (Fallback `gif_id`) unverändert verwenden; `text` als Tooltip/Alt-Text.
2. Die Original-GIF-URL laden (Format zuerst echt prüfen), mit `QMovie` anzeigen, begrenzte Höhe (z. B. 140 px wie Web-Chat laut Chatterino-Review), nur im RAM, Schalter «GIFs laden» (Standard an/aus nach Datenschutzgefühl), sonst Link.
3. Kein MP4-Weg, solange Twitch GIF/WebP liefert.

## Offene Punkte
- Echte EventSub-Nachricht mitschneiden: `id` vs. `gif_id`, Inhalt `text`, Format der URL (gif/webp/mp4), Hosts.
- Twitch-Hilfeartikel und Giphy-Nutzungsbedingungen (beide nicht abrufbar) zu Caching/Attribution/Datenschutz lesen.
- Ob Twitch-eigene Vorgabe zu Chatterinos abgeleiteten WebP-URLs besteht.
