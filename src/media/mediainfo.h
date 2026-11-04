#pragma once

#include <QString>

#include <cstdint>

// Angaben zu einer Bild- oder Videodatei über libavformat/libavcodec (P09). Nur Qt Core.

namespace dss {

// Grösse in Pixeln (P09, Entscheid 6): Bild = Pixel der Datei, Video = Grösse des ersten
// dekodierten Bildes, seit P10a so, wie es nach der Drehung aus den Metadaten angezeigt wird (bei
// 90° und 270° Breite und Höhe vertauscht). Die EXIF-Ausrichtung von Bildern rechnet der Kern
// dazu (OverlayStore). false mit Meldung, wenn die Datei nicht lesbar ist oder kein Bild enthält.
// Dauert einige Millisekunden (öffnen, ein Bild dekodieren); blockiert den Aufrufer.
bool readMediaSize(const QString &path, int *width, int *height, QString *error);

// Drehung und Spiegelung eines Videos aus seiner Display-Matrix (P10a, Punkt 65) als Wert der
// EXIF-Ausrichtung 1 bis 8 (imageorientation.h), damit Kern und Zeichner dieselben Rechnungen wie
// bei Bildern nutzen. Gleiche Fälle wie ffmpeg mit «autorotate» (fftools/ffmpeg_filter.c,
// configure_input_video_filter, und get_rotation in fftools/cmdutils.c, FFmpeg 7.1):
//   Winkel θ = −round(av_display_rotation_get(m)), auf 0 bis 360 gebracht (im Uhrzeigersinn)
//   θ ≈ 90:  m[3] > 0 → transpose «cclock_flip» = 5, sonst «clock» = 6
//   θ ≈ 180: m[0] < 0 → hflip, m[4] < 0 → vflip; beides = 3, nur hflip = 2, nur vflip = 4
//   θ ≈ 270: m[3] < 0 → transpose «clock_flip» = 7, sonst «cclock» = 8
//   θ ≈ 0:   m[4] < 0 → vflip = 4, sonst 1
// Andere Winkel (ffmpeg: Filter «rotate») und ungültige Matrizen geben 1.
// https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/fftools/ffmpeg_filter.c
// https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/fftools/cmdutils.c
// https://ffmpeg.org/doxygen/7.1/group__lavu__video__display.html (av_display_rotation_get)
int displayMatrixOrientation(const int32_t *matrix);

// Ausrichtung eines Videos wie im Zeichner des Editors (Display-Matrix, 1 bis 8; ohne
// DSS_VIDEO_ROTATION oder bei Fehlern 1). Für den Overlay-Server (P10): die Seite dreht danach.
int readVideoOrientation(const QString &path);

// Format (Codec) des besten Bildstroms einer Videodatei als Name in FFmpeg, zum Beispiel "h264",
// "vp9", "hevc", "mpeg4", "prores" (P10b, Punkt 81; videoformat.h). Öffnet nur die Datei und liest
// die Angaben der Ströme, ohne zu dekodieren. false mit Meldung, wenn das nicht geht.
// https://ffmpeg.org/doxygen/7.1/group__lavf__decoding.html (avformat_find_stream_info,
// av_find_best_stream), https://ffmpeg.org/doxygen/7.1/group__lavc__core.html (avcodec_get_name)
bool readVideoCodec(const QString &path, QString *codec, QString *error);

} // namespace dss
