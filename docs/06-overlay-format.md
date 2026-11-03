# 06 – Format der Overlay-Datei

Stand: 2026-11-02 (P04, Nachträge P05, Test des Beispiels P07, Liste `liste.json` P08). Diese Datei ist die Vorlage für beide Zeichner (K-01): den Zeichner im Editor und den Zeichner der Browserquelle. Massgebend ist der Code in `src/common/overlay.h`, `src/common/overlayjson.cpp` und `src/common/overlaystore.cpp`; das Beispiel unten lädt ein Test (`tst_overlaystore`, Fall `formatDocExample`).

## Datenordner

Ohne Angabe nimmt das Programm den Ordner, den `QStandardPaths::AppDataLocation` liefert, mit dem Programmnamen `dons-streaming-studio`. Unter Linux ist das `~/.local/share/dons-streaming-studio` bzw. `$XDG_DATA_HOME/dons-streaming-studio`, wenn `XDG_DATA_HOME` gesetzt ist (Quelle: https://doc.qt.io/qt-6.8/qstandardpaths.html; auf dem Studio-PC geprüft, siehe `docs/berichte/P04-bericht.md`). Für Tests und Handtest lässt sich ein anderer Ordner wählen (`dss-cli --datenordner PFAD`).

```
<Datenordner>/
├── overlays/
│   └── <Kennung>.json        ein Overlay je Datei, Name = Kennung des Overlays
├── media/
│   └── <SHA-256>.<Endung>    Kopien der Dateien des Nutzers (Bild, Video, Schrift)
├── liste.json                Reihenfolge und Ordner der Overlay-Liste (P08), siehe «Liste»
└── tokens.json               Token pro Overlay, Dateirechte 0600
```

- **overlays/**: Jede Datei ist JSON in UTF-8. Gespeichert wird atomar (`QSaveFile`: temporäre Datei, danach Umbenennen).
- **media/**: Fügt der Nutzer eine Datei hinzu, legt die Software eine Kopie an. Der Dateiname ist der SHA-256 des Inhalts (64 Zeichen, Kleinbuchstaben) und die Endung der ursprünglichen Datei in Kleinbuchstaben. Gleicher Inhalt ergibt also nur eine Kopie. Ungenutzte Kopien werden (noch) nicht gelöscht. Erlaubte Endungen: Bild `png`, `jpg`, `jpeg`; Video `webm`, `mp4`; Schrift `ttf`, `otf`.
- **tokens.json**: Die Token stehen nicht in den Overlay-Dateien, damit sich diese ohne Token weitergeben lassen. Inhalt: `{"format_version": 1, "tokens": {"<Kennung>": "<Token>"}}`. Ein Token sind 32 Byte aus der Zufallsquelle des Betriebssystems (`QRandomGenerator::system()`), geschrieben als Base64url ohne `=` (43 Zeichen). Der Token erscheint nie im Protokoll.

## Liste (`liste.json`)

Seit P08. Reihenfolge der Overlays in der Liste des Fensters und Ordner (`docs/01-festlegungen.md`, Abschnitt «Overlay-Liste»). Die Datei gehört dem Kern; massgebend ist der Code in `src/common/overlaylist.h/.cpp` und `src/common/overlaystore.cpp`.

| Feld | Typ | Bereich | Bedeutung |
|---|---|---|---|
| `format_version` | ganze Zahl | genau `1` | Version des Formats der Liste (unabhängig von der Overlay-Datei) |
| `entries` | Liste | 0 oder mehr Einträge | Einträge der obersten Ebene, oben zuerst |

Eintrag der Art `overlay`:

| Feld | Typ | Bedeutung |
|---|---|---|
| `kind` | Text `overlay` | Art des Eintrags |
| `id` | Text, UUID | Kennung des Overlays (wie in der Overlay-Datei) |

Eintrag der Art `folder` (Ordner):

| Feld | Typ | Bereich | Bedeutung |
|---|---|---|---|
| `kind` | Text `folder` | | Art des Eintrags |
| `id` | Text, UUID | eindeutig | Kennung des Ordners, ändert sich nie |
| `name` | Text | nicht leer (nicht nur Leerzeichen) | Name des Ordners; gleiche Namen sind erlaubt |
| `expanded` | true/false | | in der Liste aufgeklappt |
| `overlays` | Liste von UUID | | Kennungen der Overlays im Ordner, oben zuerst. Nur Overlays: Ein Ordner im Ordner ist im Format nicht möglich (eine Ebene) |

Regeln:

- Gespeichert wird atomar (`QSaveFile`) bei jeder Änderung der Liste (verschieben, Ordner anlegen, umbenennen, löschen, auf- und zuklappen) und wenn ein Overlay angelegt, kopiert oder gelöscht wird. Ein neues Overlay kommt ans Ende der obersten Ebene, eine Kopie direkt nach das Original (im selben Ordner).
- Fehlt die Datei, gilt die Reihenfolge nach Name (ohne Gross-/Kleinschreibung, dann Adresse); die Datei entsteht bei der ersten Änderung.
- Gelesen wird streng wie die Overlay-Datei: Ein fehlendes, unbekanntes oder ungültiges Feld, eine Kennung, die mehrfach vorkommt, oder eine andere `format_version` machen die Datei ungültig.
- Overlays, die in der Datei fehlen (zum Beispiel mit `dss-cli` bei angehaltenem Kern angelegt), kommen in der Reihenfolge nach Name ans Ende der obersten Ebene. Einträge zu Overlays, die es nicht gibt, fallen weg. Das geschieht beim Laden nur im Speicher; die Datei wird erst bei der nächsten Änderung neu geschrieben.
- Eine ungültige Datei wird beim Laden gemeldet (`liste.json: …`) und nicht überschrieben. Die Liste fällt auf «nach Name» zurück; Änderungen der Liste werden mit einer Meldung abgewiesen, bis die Datei repariert oder entfernt und der Kern neu geladen ist (SIGHUP). Overlays anlegen, kopieren und löschen geht weiter.
- `dss-cli overlay-liste` zeigt die Overlays in derselben Reihenfolge, mit dem Namen des Ordners (`–` auf der obersten Ebene).

Beispiel: «Überblick Start» oben, darunter der Ordner «Szenen» mit einem Overlay.

```json
{
    "entries": [
        {
            "id": "db7f47a9-dee3-4de5-b056-40b5b3507607",
            "kind": "overlay"
        },
        {
            "expanded": true,
            "id": "5c0f9a1e-2b7d-4e43-9d5a-8f1e2c3b4a50",
            "kind": "folder",
            "name": "Szenen",
            "overlays": [
                "7be08f35-6885-4a95-9c32-939c45a688ea"
            ]
        }
    ],
    "format_version": 1
}
```

## Overlay

| Feld | Typ | Bereich | Bedeutung |
|---|---|---|---|
| `format_version` | ganze Zahl | `1`, `2` oder `3` | Version des Formats. Geschrieben wird immer `3`. Eine höhere Zahl wird abgewiesen, nicht geraten. Versionen 1 (P04 bis P08) und 2 (P09) werden weiter gelesen, siehe «Versionen». |
| `id` | Text | UUID, Kleinbuchstaben, ohne Klammern | Kennung, ändert sich nie. Bestimmt den Dateinamen. |
| `name` | Text | nicht leer (nicht nur Leerzeichen) | frei wählbarer Name |
| `address` | Text | `a-z`, `0-9`, einzelne `-` dazwischen | Adresse für die Browserquelle, siehe «Adresse» |
| `width` | ganze Zahl | 1 bis 7680 | Breite der Leinwand in Pixeln |
| `height` | ganze Zahl | 1 bis 4320 | Höhe der Leinwand in Pixeln |
| `sources` | Liste | 0 oder mehr Quellen | Zeichenreihenfolge: das erste Element liegt hinten, das letzte vorne |

## Quelle

Gemeinsame Felder aller Arten:

| Feld | Typ | Bereich | Bedeutung |
|---|---|---|---|
| `id` | Text | UUID wie oben, eindeutig im Overlay | Kennung, ändert sich nie |
| `kind` | Text | `text`, `image`, `video` | Art der Quelle; `video` umfasst WebM mit Alpha |
| `name` | Text | nicht leer | Anzeigename in der Liste «Quellen» |
| `x`, `y` | ganze Zahl | beliebig (auch negativ) | linke obere Ecke der ungedrehten Quelle in Pixeln der Leinwand; negativ = teilweise ausserhalb |
| `width`, `height` | ganze Zahl | ab 1 | Grösse in Pixeln |
| `rotation` | ganze Zahl | 0 bis 3599 | Drehung in Zehntelgrad um die Mitte der Quelle (125 = 12.5 °), Anzeige mit einer Nachkommastelle |
| `flip_horizontal` | true/false | | seit Version 3 (P09b): Inhalt waagrecht gespiegelt (links ↔ rechts); beim Hinzufügen `false` |
| `flip_vertical` | true/false | | seit Version 3 (P09b): Inhalt senkrecht gespiegelt (oben ↔ unten); beim Hinzufügen `false` |
| `visible` | true/false | | eingeblendet |
| `text` / `image` / `video` | Objekt | genau das zur Art passende | Eigenschaften der Art |

Drehrichtung: Ein positiver Winkel dreht im Uhrzeigersinn um die Mitte der Quelle (entschieden 2026-11-02, `docs/01-festlegungen.md`). Das Format speichert nur den Winkel.

Spiegeln (Entscheid 4, P09b, wie OBS): Der Inhalt wird in seinem Kasten um dessen Mitte gespiegelt, und zwar vor der Drehung, also in der Richtung der Quelle selbst (wie ein negativer Massstab in OBS, `libobs/obs-scene.c`, `update_item_transform`). `x`, `y`, `width`, `height` beschreiben weiter den ungedrehten, ungespiegelten Kasten; Breite und Höhe bleiben positiv. Für die Browserquelle (P10) entspricht das CSS `transform: rotate(<Winkel>) scale(-1, 1)` bzw. `scale(1, -1)` um die Mitte. Gilt für alle Arten, auch Text.

### Art `text` (TX-01 bis TX-05)

| Feld | Typ | Bereich | Bedeutung |
|---|---|---|---|
| `content` | Text | beliebig, auch leer | Inhalt, Zeilenwechsel als `\n` (TX-01) |
| `font_file` | Text | Verweis `media/<SHA-256>.<Endung>` | Schriftdatei (TX-02, E-11) |
| `weight` | ganze Zahl | 100 bis 900 | Gewicht (TX-02) |
| `italic` | true/false | | kursiv (TX-02) |
| `font_size` | ganze Zahl | ab 1 | Schriftgrösse in Pixeln (TX-03) |
| `color` | Text | `#RRGGBBAA`, Hexadezimal | Textfarbe mit Deckkraft, `AA` = `FF` voll deckend (TX-04) |
| `align` | Text | `left`, `center`, `right` | Ausrichtung waagrecht (TX-05) |

### Art `image`

| Feld | Typ | Bereich | Bedeutung |
|---|---|---|---|
| `file` | Text | Verweis `media/<SHA-256>.<Endung>` | Bilddatei |

EXIF-Ausrichtung (Entscheid 7, P09b): Ein Bild wird so gezeigt, wie seine EXIF-Ausrichtung (Tag 0x0112, Werte 1 bis 8) es verlangt, wie im Browser mit dem Anfangswert `image-orientation: from-image` (https://developer.mozilla.org/en-US/docs/Web/CSS/image-orientation). Gelesen wird sie bei JPEG aus dem APP1-Segment «Exif», bei PNG aus dem Chunk `eXIf` vor dem ersten `IDAT` (EXIF 2.32, https://www.cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf, Abschnitt 4.6.4; PNG Third Edition, https://www.w3.org/TR/png-3/#eXIf). Die Datei selbst bleibt unverändert. Die Grösse der Datei (beim Hinzufügen, Zurücksetzen und Seitenverhältnis an den Ecken) ist die angezeigte: Bei den Werten 5 bis 8 sind Breite und Höhe gegenüber den gespeicherten Pixeln vertauscht. Die Ausrichtung kommt vor dem Strecken in den Kasten, dem Spiegeln und der Drehung der Quelle.

### Art `video`

| Feld | Typ | Bereich | Bedeutung |
|---|---|---|---|
| `file` | Text | Verweis `media/<SHA-256>.<Endung>` | Videodatei, auch WebM (VP9) mit Alpha |
| `loop` | true/false | | am Ende wieder von vorne; beim Hinzufügen `false` (nicht wiederholen) |
| `hold_at_end` | true/false | | seit Version 2 (P09): am Ende das letzte Bild stehen lassen; beim Hinzufügen `false` |

`loop` und `hold_at_end` gibt es nur bei `video`; bei `image` sind sie unbekannte Felder. Am Ende eines Videos (Entscheid 9, P09): mit `loop` wieder von vorne, sonst mit `hold_at_end` das letzte Bild, sonst wird die Quelle leer (durchsichtig). Mit `loop` hat `hold_at_end` keine Wirkung, der Wert bleibt aber gespeichert (im Editor ist «Am Ende anhalten» dann ausgegraut). Ein Video läuft im Editor ab der Wahl des Overlays (Entscheid 10). Zum Ton speichert das Format nichts: Der Studio-PC spielt ihn über eine Audiokarte ab, die Browserquelle ist immer stumm (`docs/01-festlegungen.md`, Abschnitt «Overlay-Daten»).

Bild und Video füllen ihren Kasten gestreckt (Entscheid 6, P09). Neue Bild- und Videoquellen bekommen beim Hinzufügen die Grösse der Datei (Bild: Pixel des Bildes; Video: Grösse des ersten Bildes), Lage (0, 0), Drehung 0. Lässt sich die Grösse nicht lesen, bekommt die Quelle die Grösse des Overlays, und Kern bzw. dss-cli melden das als Warnung. Text wird ab der oberen Kante gesetzt, Zeilen nur an den Zeilenwechseln des Inhalts; was über den Kasten hinausgeht, wird im Editor abgeschnitten (Annahme P09, nicht entschieden).

## Versionen

| Version | seit | Unterschied |
|---|---|---|
| 1 | P04 | `video.loop` kam in P05 ohne neue Version dazu (auf dem Studio-PC gab es noch keine Overlay-Dateien, geprüft 2026-11-02, Bericht P05) |
| 2 | P09 | neues Pflichtfeld `video.hold_at_end`. Neue Version, weil es auf dem Studio-PC eine echte Overlay-Datei mit `format_version` 1 gibt (geprüft 2026-11-03, Bericht P09) |
| 3 | P09b | neue Pflichtfelder `flip_horizontal` und `flip_vertical` je Quelle. Neue Version, weil es auf dem Studio-PC eine echte Overlay-Datei mit `format_version` 2 gibt (geprüft 2026-11-03, Bericht P09b) |

Eine Datei mit Version 1 wird gelesen wie bisher, `hold_at_end` ist dann `false`; in Version 1 ist `hold_at_end` ein unbekanntes Feld, ab Version 2 Pflicht. In Version 1 und 2 sind `flip_horizontal` und `flip_vertical` unbekannte Felder, die Quellen sind dann nicht gespiegelt; ab Version 3 sind sie Pflicht. Beim nächsten Speichern wird die Datei als Version 3 geschrieben. Seit P10a wandelt `scripts/installieren.sh` beim Installieren alle Dateien in älterem Format um (`dss-core --umwandeln`, nach einer Sicherung des ganzen Datenordners); eine Datei in neuerem Format, als die neue Version lesen kann, bricht die Installation ab (`dss-core --format-pruefen`, Exit-Code 5).

## Regeln beim Lesen

- Alle Felder sind Pflicht. Ein fehlendes, ein unbekanntes oder ein ungültiges Feld macht die Datei ungültig; die Meldung nennt das Feld (zum Beispiel `sources[2].text.weight`). Nichts wird still korrigiert.
- Zahlen müssen ganze Zahlen sein (`12.5` ist ungültig).
- Eine ungültige Datei wird beim Start gemeldet (mit Dateiname), übersprungen und nicht überschrieben. Die übrigen Overlays werden geladen.
- Der Dateiname muss `<id>.json` sein, die Adresse muss unter den geladenen Overlays eindeutig sein; sonst ist die Datei ungültig.
- Fehlt eine Datei unter `media/`, auf die eine Quelle verweist, wird das pro Quelle gemeldet; das Overlay wird trotzdem geladen.
- Die Reihenfolge der Felder in der Datei ist ohne Bedeutung (das Programm schreibt sie alphabetisch).

## Adresse

Die Adresse wird beim Anlegen aus dem Namen gebildet:

1. Kleinbuchstaben.
2. `ä` → `ae`, `ö` → `oe`, `ü` → `ue` (auch wenn der Umlaut als Buchstabe mit Akzentzeichen eingegeben ist).
3. `ß` → `ss`, `æ` → `ae`, `ø` → `oe`, `ł` → `l`, gross wie klein (diese Buchstaben haben keine Unicode-Zerlegung; entschieden nach P04, umgesetzt in P05).
4. Andere Akzente fallen weg (`é` → `e`, `ñ` → `n`): Zerlegung nach Unicode NFD, Akzentzeichen (Kategorie Mn) werden entfernt.
5. Jedes andere Zeichen ausser `a-z` und `0-9` wird `-`; mehrere `-` hintereinander werden eines; `-` am Anfang und am Ende fällt weg.
6. Leeres Ergebnis → `overlay`.
7. Ist die Adresse vergeben, wird `-2`, `-3` usw. angehängt.

Beispiele: «Überblick Start» → `ueberblick-start`, «Café Crème» → `cafe-creme`, «Chat & Alerts #2!» → `chat-alerts-2`, «Straße» → `strasse`, «Grüße aus Køge» → `gruesse-aus-koege`, «!!!» → `overlay`, zweites «Überblick Start» → `ueberblick-start-2`.

Beim Umbenennen bleibt die Adresse gleich, ausser der Aufrufer verlangt ausdrücklich eine neue (dann nach denselben Regeln, die eigene alte Adresse gilt dabei nicht als vergeben). Beim Kopieren heisst die Kopie «<Name> Kopie» und bekommt eine eigene Adresse und einen eigenen Token.

## Vollständiges Beispiel

Overlay «Überblick Start» mit je einer Quelle `image` (hinten), `video` und `text` (vorne). Die Prüfsummen in den Verweisen sind die der Testdateien `bild1.png`, `alpha.webm` (beide aus `prototypes/messung/media_erzeugen.sh`) und `DejaVuSans.ttf` (Paket `fonts-dejavu-core`) im Arbeitsordner des Studio-PCs. `media_erzeugen.sh` erzeugt `alpha.webm` nicht jedes Mal mit demselben Inhalt; der Test legt deshalb die Testdateien unter genau den Namen aus dem Beispiel ab und hängt nicht von den Prüfsummen ab (Punkt 33, P07).

<!-- beispiel-anfang -->
```json
{
    "format_version": 3,
    "id": "db7f47a9-dee3-4de5-b056-40b5b3507607",
    "name": "Überblick Start",
    "address": "ueberblick-start",
    "width": 1920,
    "height": 1080,
    "sources": [
        {
            "id": "e7992e97-e48e-42dc-a15b-0438e7c19f50",
            "kind": "image",
            "name": "bild1.png",
            "x": -100,
            "y": 200,
            "width": 640,
            "height": 360,
            "rotation": 125,
            "flip_horizontal": true,
            "flip_vertical": false,
            "visible": true,
            "image": {
                "file": "media/3bb2f4e86362dd6d44bfa852d36109788d1854c38cded41ce74f3758cecb099e.png"
            }
        },
        {
            "id": "b814d820-5d53-42a8-9683-ef3419cf4d7e",
            "kind": "video",
            "name": "alpha.webm",
            "x": 1300,
            "y": 100,
            "width": 512,
            "height": 512,
            "rotation": 0,
            "flip_horizontal": false,
            "flip_vertical": false,
            "visible": false,
            "video": {
                "file": "media/09445b269e30ac5f3eddf936bc7a19a960ea82dbcd9ebfcc17ff5acace4c7bdf.webm",
                "loop": false,
                "hold_at_end": true
            }
        },
        {
            "id": "24b4efb3-8bde-45fe-af64-75f947cbdb95",
            "kind": "text",
            "name": "Text: Willkommen",
            "x": 400,
            "y": 900,
            "width": 800,
            "height": 140,
            "rotation": 0,
            "flip_horizontal": false,
            "flip_vertical": false,
            "visible": true,
            "text": {
                "content": "Willkommen\nZweite Zeile",
                "font_file": "media/57f73e11f51999432bf7ab22ce55b6f945d5eca1bf824404cfa9ec2e3718c84e.ttf",
                "weight": 700,
                "italic": false,
                "font_size": 56,
                "color": "#FFFFFFFF",
                "align": "center"
            }
        }
    ]
}
```
<!-- beispiel-ende -->

Dazu passend in `tokens.json` (Token hier nur als Platzhalter):

```json
{
    "format_version": 1,
    "tokens": {
        "db7f47a9-dee3-4de5-b056-40b5b3507607": "<43 Zeichen Base64url>"
    }
}
```
