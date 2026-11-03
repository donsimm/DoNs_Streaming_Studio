#pragma once

// Gemeinsames Öffnen einer Datei für mediainfo.cpp und videoplayer.cpp (nur in dss_media).

#include <QString>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace dss {

// Datei mit dem besten Bildstrom und geöffnetem Decoder. Decoder: libvpx-vp9 für VP9 mit Alpha
// (Matroska/WebM-Feld alpha_mode = 1), sonst der Standard-Decoder von FFmpeg (P02, M1).
struct AvInput
{
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    int stream = -1;
    QString decoderName;

    AvInput() = default;
    AvInput(const AvInput &) = delete;
    AvInput &operator=(const AvInput &) = delete;
    ~AvInput();

    bool open(const QString &path, QString *error);
};

// Meldungen von FFmpeg und libvpx nur ab «Fehler» (sonst schreibt libvpx bei jedem Öffnen seine
// Version nach stderr, P02).
void quietAvLog();

} // namespace dss
