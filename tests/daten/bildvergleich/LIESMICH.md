# Testdaten: Bildvergleich der zwei Zeichner (P11, K-08)

Für `tst_bildvergleich` (Editor gegen Qt WebEngine, mit `DSS_BILDVERGLEICH_OBS` auch gegen OBS).
Nur erzeugte Dateien (Entscheid des Nutzers, 2026-11-08), erzeugt am 2026-11-08 mit
`sh tests/daten/bildvergleich/erzeugen.sh` (FFmpeg 7.1.5-0+deb13u1) und eingefroren (Regel 7).
Die Overlays dazu legt der Test zur Laufzeit an. Wiederverwendet werden `../exif/`,
`../schriften/urbanist/`, Be Vietnam Pro (`src/common/schriften/`) und DejaVu Sans.

## Farbfelder

`farbfelder.png` ist die Vorlage in 1920x1080: 8 x 4 deckende Felder, Zeile für Zeile
- Grundfarben 100 %: Rot, Grün, Blau, Cyan, Magenta, Gelb, Weiss, Schwarz
- 75-%-Balken (191): Weiss, Gelb, Cyan, Grün, Magenta, Rot, Blau, dazu Grau 32
- Graustufen 0, 36, 73, 109, 146, 182, 219, 255
- Hauttöne `FFDBAC F1C27D E0AC69 C68642 8D5524 5C3836`, Mitteltöne `3F7F3F 3F3F7F`

Feldbreite und -höhe sind gerade (4:2:0); was rechts und unten übrig bleibt, ist schwarz. Jedes
Farbvideo hat 10 gleiche Bilder mit 30 Bildern/s, eingeschränkter Bereich ausser «voll».

| Datei | Kodiert mit Matrix | Angaben in der Datei (ffprobe) |
|---|---|---|
| `h264-640x360-ohne.mp4` | BT.601 | keine |
| `h264-1280x718-ohne.mp4` | BT.601 | keine |
| `h264-1280x720-ohne.mp4` | BT.709 | keine |
| `h264-1920x1080-ohne.mp4` | BT.709 | keine |
| `h264-1920x1080-bt709.mp4` | BT.709 | bt709, eingeschränkt |
| `h264-1920x1080-bt601.mp4` | BT.601 | smpte170m, eingeschränkt |
| `h264-1920x1080-voll.mp4` | BT.709 | bt709, voller Bereich (yuvj420p) |
| `vp9-640x360-ohne.webm` | BT.601 | keine (Bereich steht bei VP9 immer im Bitstrom: tv) |
| `vp9-1920x1080-ohne.webm` | BT.709 | keine (Bereich tv) |
| `vp9-1920x1080-bt709.webm` | BT.709 | bt709, eingeschränkt |

Ohne Angabe ist nach der üblichen Regel kodiert (unter 720 Pixel Höhe BT.601, sonst BT.709).
1280x719 aus dem Plan geht mit 4:2:0 nicht (die Höhe muss gerade sein); 1280x718 liegt ebenso
unter 720. VP9 ist verlustfrei kodiert (im YUV-Raum), H.264 mit `-crf 10`.

## Weitere Dateien

- `bildwechsel.mp4`: 320x180, BT.709, 10 Bilder mit Grau 0 und 255 im Wechsel, das letzte 128
  (ffprobe `signalstats`: Y 16/235, zuletzt 125). Mit «Am Ende anhalten» zeigen beide Zeichner das
  letzte Bild (K-05); ein anderes Bild fällt auf.
- `drehung-90.webm`, `drehung-180.webm`, `drehung-270.webm`: VP9 verlustfrei, Bild wie
  `../video-drehung/` (64x32, Viertel rot, grün, blau, gelb), Drehung als Display-Matrix im WebM
  (ffprobe: rotation -90, -180, 90). Für Punkt 80.
- `alpha.webm` (VP9 mit Alpha, `alpha_mode=1`) und `alpha.png` (RGBA): 128x64, Viertel rot, grün,
  blau, gelb, Alpha steigt von links (0) nach rechts (255).
- `einzelheiten.png`: 320x240 aus `testsrc` (feine Linien, Farbverlauf), zum Verkleinern und
  Vergrössern.

Die Dateien sind zusammen rund 75 KB gross.
