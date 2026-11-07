#pragma once

#include <QString>

#include <optional>

// Videoformate, die die Browserquelle nicht abspielt (P10b, Punkt 81). Nur Qt Core.
//
// Die Liste steht nur hier. Gemessen in P10 mit Chromium aus Qt WebEngine 6.8.2 (Bericht
// docs/archiv/berichte/P10-bericht.md, Abschnitt «Videoformate»): HEVC (H.265), MPEG-4 Part 2 und ProRes
// geben Fehler 4 «DEMUXER_ERROR_NO_SUPPORTED_STREAMS», canPlayType ist leer. H.264, VP9 (auch
// mit Alpha) und AV1 laufen. Ob das Chromium der Browserquelle in OBS dieselben Formate kann, ist
// nicht geprüft (Punkt 81).
// Die Schlüssel sind die Namen der Codecs in FFmpeg (avcodec_get_name, AVCodecDescriptor.name):
// https://ffmpeg.org/doxygen/7.1/group__lavc__core.html (avcodec_get_name)
// https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libavcodec/codec_desc.c

namespace dss {

// Lesbarer Name des Formats, wenn die Browserquelle es nicht abspielt («HEVC», «MPEG-4 Part 2»,
// «ProRes»); sonst nullopt. codec: Name in FFmpeg, zum Beispiel "hevc", "mpeg4", "prores".
std::optional<QString> browserUnsupportedVideoCodec(const QString &codec);

// «zaehler.mov (ProRes) wird in der Browserquelle nicht angezeigt. In H.264 oder VP9 umwandeln.»
QString browserVideoWarning(const QString &name, const QString &codecDisplayName);

} // namespace dss
