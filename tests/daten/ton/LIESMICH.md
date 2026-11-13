# Testdaten: Ton von Videos, «Blitz und Piep» (P12a, P12b)

Für den Messtest P12a (Prototyp `prototypes/ton/`, Messseite, Aufnahme in OBS) und für die Tests
von P12b (`tst_tondecoder`, `tst_kernton`, Handtest). Nur erzeugte Dateien, erzeugt am 2026-11-09
mit `sh tests/daten/ton/erzeugen.sh 30` (FFmpeg 7.1.5-0+deb13u1) und eingefroren (Regel 7). Schrift
des Zählers: Be Vietnam Pro Regular aus `src/common/schriften/`.

## Inhalt

Alle Dateien: 60 s, 1280x720, **30 Bilder/s** (wie OBS, M3). Alle 2 s (bei 0, 2, … 58 s, also 30-mal)
ist ein Bild ganz weiss («Blitz»), gleichzeitig läuft ein Sinus von 1 kHz mit halber Vollaussteuerung
genau ein Bild lang («Piep», 33,3 ms). Sonst ist das Bild schwarz mit grauem Zähler in der Mitte
(Bildnummer, Zeit, Nummer des Blitzes, Bildrate), der Ton still. Vorbild ist das Messsignal «flash»
und «blip» aus EBU R37-2007. Schlüsselbilder alle 2 s, auf dem Blitz.

| Datei | Bild | Ton |
|---|---|---|
| `blitz-piep.mp4` | H.264 | AAC, 48 kHz, Stereo |
| `blitz-piep.webm` | VP9 | Opus, 48 kHz, Stereo |
| `blitz-piep-mono.mp4` | H.264 | AAC, 48 kHz, Mono |
| `blitz-piep-44100.mp4` | H.264 | AAC, 44,1 kHz, Stereo |
| `blitz-piep-5.1.mp4` | H.264 | AAC, 48 kHz, 5.1 (FL FR FC LFE BL BR), Piep in allen Kanälen ausser LFE |
| `blitz-ohne-ton.mp4` | H.264 | keiner |

Alle MP4 haben dasselbe Bild (einmal kodiert, dann kopiert). Zusammen rund 4,7 MB; die Bildqualität
ist niedrig gewählt (H.264 `-crf 32`, VP9 `-crf 50`), der Zähler bleibt lesbar.

## Bildrate

Die Bildrate soll der von OBS entsprechen (Messgenauigkeit ±½ Bild). In M3 (2026-11-13) hat Lotse
sie mit `GetVideoSettings` gelesen: **30/1**, die Dateien bleiben (Messbericht P12a, «M3
(2026-11-13)»). Weicht OBS später ab, die Dateien mit der Bildrate von OBS neu erzeugen,
z. B. `sh tests/daten/ton/erzeugen.sh 60` oder `… 30000/1001`, und diese Datei nachführen.
Bei einer Bildrate, die 2 s nicht in ganze Bilder teilt, ist der Blitz das erste Bild, das in einem
neuen 2-s-Abschnitt beginnt; der Piep folgt diesem Bild auf die Probe genau.

## Geprüft (2026-11-09)

- Streams: `ffprobe -v error -show_entries stream=… -of compact <Datei>`: Codec, Grösse, Rate und
  Kanäle wie in der Tabelle; Bild 1800 Bilder, 60,000 s; Ton beginnt bei 0. Ausnahme WebM: Opus
  beginnt bei −0,007 s (Vorlauf von Opus, den der Decoder verwirft).
- Blitze: `ffprobe -v error -f lavfi "movie=<Datei>,signalstats" -show_entries
  frame=pts_time:frame_tags=lavfi.signalstats.YAVG -of csv=p=0`: in `blitz-piep.mp4` und
  `blitz-piep.webm` je genau 30 Bilder mit Y-Mittel 233, bei 0, 2, … 58 s; alle anderen um 17,7.
- Pieps: `ffmpeg -nostdin -i <Datei> -map 0:a -af silencedetect=n=-30dB:d=0.005 -f null -`:
  - `blitz-piep.mp4`, `-mono`, `-44100`: Beginn 2,000021 s, 4,000021 s … (die 0,02 ms sind der Weg
    des Sinus bis zur Schwelle −30 dB), Ende nach 33,35 ms; 30 Pieps.
  - `blitz-piep-5.1.mp4`: 30 Pieps.
  - `blitz-piep.webm`: ohne `-copyts` scheinbar 7 ms später (ffmpeg verschiebt den negativen Start
    auf 0), mit `-copyts` wie MP4 bei 2,000021 s. **Für P12b:** Der Decoder muss die Lage des Tons
    aus den Zeitstempeln nehmen, nicht die erste ausgegebene Probe als 0 zählen.
- Gegenprobe mit `sh tests/daten/ton/erzeugen.sh 60`: Blitze bei 0, 2, 4 … s, Piep 16,7 ms lang.
  Danach mit 30 neu erzeugt. Eine Bildrate als Bruch (`30000/1001`) ist **nicht geprüft**.

## Quellen

- https://ffmpeg.org/ffmpeg-filters.html (aevalsrc, color, drawbox, drawtext, signalstats,
  silencedetect)
- https://ffmpeg.org/ffmpeg-codecs.html (aac, libopus, libx264, libvpx-vp9)
- https://tech.ebu.ch/files/live/sites/tech/files/shared/r/r037.pdf (EBU R37-2007, «flash» und «blip»)
