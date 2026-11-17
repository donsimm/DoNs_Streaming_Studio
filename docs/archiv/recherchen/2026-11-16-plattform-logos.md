# Plattform-Logos (Twitch, YouTube) in einem privaten Chat-Programm

Abrufdatum aller Quellen: 2026-11-16. Keine Rechtsauskunft. «Ableitung» = eigener Schluss, nicht belegt.
**Wichtig:** Die Twitch-Richtlinien und die YouTube-Seiten mit Grösse/Farbe waren mit den verfügbaren Werkzeugen nicht lesbar (Seiten liefern nur Navigation oder leiten um). Dazu gibt es unten keine belegten Zahlen.

## Kurzfassung

| | erlaubt? | Farbe | Bezug |
|---|---|---|---|
| Twitch Glitch | **unklar** (Richtlinientext nicht lesbar) | Hex nicht bestätigt | https://brand.twitch.com/uploads/Twitch-Brand.zip |
| YouTube-Icon | **wahrscheinlich ja als Quellenhinweis, aber mit Auflagen** | rot, nicht veränderbar; Hex nicht bestätigt | https://www.youtube.com/yt/brand/downloads.html (nicht abgerufen) |

## 1. Twitch

- Brand-Seite: https://brand.twitch.tv/ leitet (302) auf https://brand.twitch.com/ weiter. Text dort nur: «Thank you for finding our correct brand assets instead of downloading fanart from 2017.» Download-Link «Download ↓» auf `/uploads/Twitch-Brand.zip`, also **https://brand.twitch.com/uploads/Twitch-Brand.zip**.
- Das ZIP ist grösser als 10 MB und konnte nicht geöffnet werden (Abruf scheiterte an der Grössengrenze). Ob darin eine Lizenz- oder Richtliniendatei liegt: **nicht bestätigt**.
- https://legal.twitch.com/legal/trademark/ («Trademark Guidelines») und https://legal.twitch.com/legal/developer-agreement/ lieferten nur Navigation und Fusszeile, kein Haupttext. Aussagen zu Drittanbieter-Apps, Mindestgrösse, Schutzraum, Verboten (verändern, einfärben, animieren) und Lizenz: **nicht bestätigt**.
- Twitch-Violett (z. B. #9146FF): **nicht bestätigt**; Websuche fand nur Drittblogs, die sich widersprechen (Schutzraum «halbe Icon-Höhe» vs. «100 %»). Nicht verwenden.
- Sekundärzitat, ungeprüft: «Do use Twitch Brand Assets to refer to Twitch, our services and websites, our apps and anything else we offer.» (aus Websuche-Ergebnis, Originalseite nicht gelesen).
- Ableitung: Ein Hinweis «Quelle ist Twitch» wäre ein Verweis auf Twitch und liegt damit am ehesten im erlaubten Bereich. Verbindlich ist das erst nach Lesen der Trademark-Seite im Browser.

## 2. YouTube

Quelle: https://developers.google.com/youtube/terms/branding-guidelines (HTTP 200, gelesen).

- Es gibt drei Zeichen: «developed with YouTube»-Logo, YouTube-Logo, YouTube-Icon. «The YouTube Icon provides attribution for YouTube content.» Gedacht für den Fall, dass «content from different sources is intermingled such that you need to display attribution next to multiple individual content elements». Das passt zu einem Mehrplattform-Chat (Ableitung).
- Auflage: Jedes Logo/Icon «must link back to YouTube content or to a YouTube component of that application.» → Icon sollte anklickbar sein und auf YouTube (z. B. den Stream/Chat) verlinken (Ableitung aus Zitat).
- Farbe: «You cannot modify the colors of the YouTube logos or YouTube Icons.» Schwarz-Weiss-Version nur als Wasserzeichen. Hex-Rot: **nicht bestätigt** (steht auf https://www.youtube.com/yt/brand/color.html, nicht lesbar).
- Verbote: «cannot change YouTube branding images or remove, obstruct, distort, or alter any element»; kein Eindruck von Verbindung oder Billigung («imply an association with or endorsement of …»); nicht zusammen mit App-Namen/-Beschreibung; Logo nicht als auffälligstes Element. Animieren: **nicht bestätigt**.
- Mindestgrösse: «must meet the minimum size requirements provided on the YouTube brand site» (https://www.youtube.com/yt/brand/using-logo.html). Wert **nicht bestätigt** (Seite leitet auf https://brand.youtube/ um, dort nur Bilder). Ob 16–20 px reichen: unklar.
- Developer Policies F.2.a (https://developers.google.com/youtube/terms/developer-policies): Fremde Inhalte dürfen nicht so gezeigt werden, als stammten sie von YouTube. → Das Icon nur bei echten YouTube-Nachrichten setzen (Ableitung).
- Vorgaben speziell für Live-Chat-Clients: **nicht bestätigt** (nichts gefunden).
- Download: Logos/Icons laut Richtlinie auf https://www.youtube.com/yt/brand/downloads.html (nicht lesbar). SVG: **nicht bestätigt**. Direkte Dateien gibt es nur für «developed with YouTube» (z. B. https://developers.google.com/static/youtube/images/developed-with-youtube-light.eps.zip).
- Lizenz: «Use of these logos is governed by the YouTube API Terms of Service.» → Nutzung ist an die API-Bedingungen gebunden.

## 3. Dateien im öffentlichen Git-Repo

- Twitch: **nicht bestätigt** (keine lesbare Lizenz).
- YouTube: Keine ausdrückliche Aussage zu Repos gefunden. Developer Policies G.1 verbietet u. a. «redistribute … all or any portion of YouTube API Services»; ob Logodateien darunterfallen, ist **nicht bestätigt**. API-Zugangsdaten dürfen nie ins Open-Source-Repo (III.D.1.4).
- Ableitung (vorsichtigste Lösung): Dateien nicht ins Repo legen; `.gitignore` plus Hinweis in der README, wo man sie selbst holt (Links oben), und Programm lädt sie beim ersten Start oder liest sie aus einem lokalen Ordner. Fehlt die Datei, zeigt das Programm Text «Twitch»/«YouTube» oder einen neutralen Punkt.

## 4. Bekannte Clients

- Chatterino2: https://github.com/Chatterino/chatterino2/tree/master/resources/twitch enthält nur `automod.png`, `sharedChat.png`; kein Glitch-/YouTube-Logo gefunden (Code unter `src/` nicht geprüft). `resources/licenses/` enthält u. a. `fluenticons.txt`.
- Social Stream Ninja: https://github.com/steveseguin/social_stream/tree/main/icons enthält Icons u. a. für Discord, GitHub, Instagram, X; Twitch/YouTube nicht in der gesehenen Liste (Liste gekürzt, nicht bestätigt). README: «trademarks and logos of third party companies/services are the rights of those respective entities. … Use them according to the terms that those entities may offer them under.»
- Streamlabs, Restream, Streamer.bot, ChatIS: nicht geprüft.
- Ableitung: Die zwei geprüften Projekte legen offenbar keine farbigen Plattformlogos als Dateien ab; Belege für gängige Praxis fehlen.

## Empfehlung

1. Vor dem Einbau im Browser lesen: https://legal.twitch.com/legal/trademark/ und https://www.youtube.com/yt/brand/using-logo.html (Mindestgrösse, Farben) – beides war hier nicht abrufbar.
2. YouTube: Icon farbig und unverändert, verlinkt, nur bei YouTube-Nachrichten. Mindestgrösse erst nach Lesen von using-logo.html festlegen.
3. Twitch: Glitch unverändert in Originalfarbe aus dem offiziellen ZIP; Hex erst aus den Dateien/Richtlinie übernehmen.
4. Dateien nicht ins öffentliche Repo, sondern lokal beziehen (Punkt 3).
