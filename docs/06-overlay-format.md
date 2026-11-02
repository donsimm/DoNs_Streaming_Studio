# 06 – Format der Overlay-Datei

Stand: 2026-11-02 (P04). Diese Datei ist die Vorlage für beide Zeichner (K-01): den Zeichner im Editor und den Zeichner der Browserquelle. Massgebend ist der Code in `src/common/overlay.h`, `src/common/overlayjson.cpp` und `src/common/overlaystore.cpp`; das Beispiel unten lädt ein Test (`tst_overlaystore`, Fall `formatDocExample`).

## Datenordner

Ohne Angabe nimmt das Programm den Ordner, den `QStandardPaths::AppDataLocation` liefert, mit dem Programmnamen `dons-streaming-studio`. Unter Linux ist das `~/.local/share/dons-streaming-studio` bzw. `$XDG_DATA_HOME/dons-streaming-studio`, wenn `XDG_DATA_HOME` gesetzt ist (Quelle: https://doc.qt.io/qt-6.8/qstandardpaths.html; auf dem Studio-PC geprüft, siehe `docs/berichte/P04-bericht.md`). Für Tests und Handtest lässt sich ein anderer Ordner wählen (`dss-cli --datenordner PFAD`).

```
<Datenordner>/
├── overlays/
│   └── <Kennung>.json        ein Overlay je Datei, Name = Kennung des Overlays
├── media/
│   └── <SHA-256>.<Endung>    Kopien der Dateien des Nutzers (Bild, Video, Schrift)
└── tokens.json               Token pro Overlay, Dateirechte 0600
```

- **overlays/**: Jede Datei ist JSON in UTF-8. Gespeichert wird atomar (`QSaveFile`: temporäre Datei, danach Umbenennen).
- **media/**: Fügt der Nutzer eine Datei hinzu, legt die Software eine Kopie an. Der Dateiname ist der SHA-256 des Inhalts (64 Zeichen, Kleinbuchstaben) und die Endung der ursprünglichen Datei in Kleinbuchstaben. Gleicher Inhalt ergibt also nur eine Kopie. Ungenutzte Kopien werden (noch) nicht gelöscht. Erlaubte Endungen: Bild `png`, `jpg`, `jpeg`; Video `webm`, `mp4`; Schrift `ttf`, `otf`.
- **tokens.json**: Die Token stehen nicht in den Overlay-Dateien, damit sich diese ohne Token weitergeben lassen. Inhalt: `{"format_version": 1, "tokens": {"<Kennung>": "<Token>"}}`. Ein Token sind 32 Byte aus der Zufallsquelle des Betriebssystems (`QRandomGenerator::system()`), geschrieben als Base64url ohne `=` (43 Zeichen). Der Token erscheint nie im Protokoll.

## Overlay

| Feld | Typ | Bereich | Bedeutung |
|---|---|---|---|
| `format_version` | ganze Zahl | genau `1` | Version des Formats. Eine höhere Zahl wird abgewiesen, nicht geraten. |
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
| `visible` | true/false | | eingeblendet |
| `text` / `image` / `video` | Objekt | genau das zur Art passende | Eigenschaften der Art |

Nicht festgelegt und daher von P04 nicht beschrieben: die Drehrichtung (im Uhrzeigersinn oder dagegen), siehe Bericht P04, `[OFFEN]`.

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

### Art `video`

| Feld | Typ | Bereich | Bedeutung |
|---|---|---|---|
| `file` | Text | Verweis `media/<SHA-256>.<Endung>` | Videodatei, auch WebM (VP9) mit Alpha |

Wiederholen, Ton und weitere Eigenschaften von Videos sind nicht entschieden (Bericht P04, `[OFFEN]`).

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
3. Andere Akzente fallen weg (`é` → `e`, `ñ` → `n`): Zerlegung nach Unicode NFD, Akzentzeichen (Kategorie Mn) werden entfernt.
4. Jedes andere Zeichen ausser `a-z` und `0-9` wird `-`; mehrere `-` hintereinander werden eines; `-` am Anfang und am Ende fällt weg. Auch `ß` wird nach dieser Regel zu `-` (Bericht P04, `[OFFEN]`).
5. Leeres Ergebnis → `overlay`.
6. Ist die Adresse vergeben, wird `-2`, `-3` usw. angehängt.

Beispiele: «Überblick Start» → `ueberblick-start`, «Café Crème» → `cafe-creme`, «Chat & Alerts #2!» → `chat-alerts-2`, «!!!» → `overlay`, zweites «Überblick Start» → `ueberblick-start-2`.

Beim Umbenennen bleibt die Adresse gleich, ausser der Aufrufer verlangt ausdrücklich eine neue (dann nach denselben Regeln, die eigene alte Adresse gilt dabei nicht als vergeben). Beim Kopieren heisst die Kopie «<Name> Kopie» und bekommt eine eigene Adresse und einen eigenen Token.

## Vollständiges Beispiel

Overlay «Überblick Start» mit je einer Quelle `image` (hinten), `video` und `text` (vorne). Die Verweise zeigen auf die Testdateien `bild1.png`, `alpha.webm` (beide aus `prototypes/messung/media_erzeugen.sh`) und `DejaVuSans.ttf` (Paket `fonts-dejavu-core`).

<!-- beispiel-anfang -->
```json
{
    "format_version": 1,
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
            "visible": false,
            "video": {
                "file": "media/09445b269e30ac5f3eddf936bc7a19a960ea82dbcd9ebfcc17ff5acace4c7bdf.webm"
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
