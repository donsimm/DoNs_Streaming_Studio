#pragma once

// Farbumrechnung der Videos im Editor (P11, Punkt 79): welche Matrix und welcher Bereich für die
// Umrechnung YUV → RGB gelten. Massstab ist die Browserquelle in OBS (Entscheid des Nutzers
// 2026-11-08: OBS ist massgebend), gemessen in Prüfung V (Bericht P11, OBS 32.2.2 mit Chromium 146):
// - Mit Angabe der Matrix gilt die Angabe.
// - Ohne Angabe wie Chromium: H.264 und die meisten anderen Codecs nach der Höhe (unter 720 Pixeln
//   BT.601, sonst BT.709). VP9 und AV1 nimmt Chromium von dieser Regel aus; VP9 ohne Angabe zeigt
//   OBS wie BT.601, auch bei 1080p (gemessen). AV1 folgt im Quellcode demselben Zweig, gemessen ist
//   es nicht.
// - Bereich: voll nur mit Angabe «voll» (AVCOL_RANGE_JPEG), sonst eingeschränkt (16–235), wie
//   Chromium.
// - Grundfarben und Übertragungskurve werden nicht umgerechnet: Bei SMPTE170M lag der Editor ohne
//   Umrechnung im Mittel 0.5 neben OBS (höchstens 2–3), also im Rauschen.
// Nur Rechnung, ohne FFmpeg und ohne Qt (Regel 9); videoplayer.cpp setzt das Ergebnis in swscale um.
//
// Quellen:
//   Zahlen der Matrix wie ITU-T H.273 (MatrixCoefficients), gleich wie FFmpeg AVColorSpace;
//   Bereich wie FFmpeg AVColorRange: https://ffmpeg.org/doxygen/7.1/pixfmt_8h.html,
//   https://www.itu.int/rec/T-REC-H.273
//   Chromium AVStreamToVideoDecoderConfig (Regel nach der Höhe, Ausnahme VP9 und AV1, Bereich), in
//   beiden Versionen gleich:
//   https://raw.githubusercontent.com/chromium/chromium/146.0.7680.177/media/ffmpeg/ffmpeg_common.cc
//   https://raw.githubusercontent.com/chromium/chromium/122.0.6261.171/media/ffmpeg/ffmpeg_common.cc

namespace dss {

enum class VideoMatrix { Bt601, Bt709, Fcc, Smpte240m, Bt2020 };

struct VideoColor
{
    VideoMatrix matrix = VideoMatrix::Bt601;
    bool fullRange = false;
    bool operator==(const VideoColor &) const = default;
};

// Angaben der Datei bzw. des Bildes
struct VideoColorTags
{
    // H.273 MatrixCoefficients: 1 BT.709, 2 keine Angabe, 4 FCC, 5 BT.470BG, 6 SMPTE170M,
    // 7 SMPTE240M, 9 BT.2020 (nicht konstante Leuchtdichte), 10 BT.2020 (konstante). Alle anderen
    // Werte (0 RGB, 3, 8 YCgCo, ab 11) gelten wie «keine Angabe».
    int matrix = 2;
    int range = 0;            // AVColorRange: 0 keine Angabe, 1 eingeschränkt (MPEG), 2 voll (JPEG)
    bool heightRule = true;   // false bei VP9 und AV1
    int height = 0;           // Höhe des Bildes in Pixeln
};

VideoColor chooseVideoColor(const VideoColorTags &tags);

// Kurzname für Protokoll und Tests, zum Beispiel «BT.709»
const char *videoMatrixName(VideoMatrix matrix);

} // namespace dss
