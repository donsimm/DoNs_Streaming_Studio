// Grösse einer Bild- oder Videodatei: Datei öffnen, besten Bildstrom wählen, ein Bild
// dekodieren und dessen Grösse nehmen (wie der Dekodierweg M1 aus P02).
//
// Quellen:
//   avformat_open_input, av_find_best_stream: https://ffmpeg.org/doxygen/7.1/group__lavf__decoding.html
//   Dekodier-API send/receive:                https://ffmpeg.org/doxygen/7.1/group__lavc__decoding.html

#include "mediainfo.h"

#include "avsupport.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace dss {

bool readMediaSize(const QString &path, int *width, int *height, QString *error)
{
    AvInput input;
    if (!input.open(path, error))
        return false;
    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    bool found = false;
    bool draining = false;
    while (!found) {
        if (!draining) {
            const int r = av_read_frame(input.format, packet);
            if (r < 0) {
                avcodec_send_packet(input.codec, nullptr); // Decoder leeren
                draining = true;
            } else {
                if (packet->stream_index == input.stream)
                    avcodec_send_packet(input.codec, packet);
                av_packet_unref(packet);
            }
        }
        const int r = avcodec_receive_frame(input.codec, frame);
        if (r == 0) {
            *width = frame->width;
            *height = frame->height;
            found = true;
        } else if (r != AVERROR(EAGAIN)) {
            break; // Ende oder Fehler
        }
    }
    av_packet_free(&packet);
    av_frame_free(&frame);
    if (!found)
        *error = QStringLiteral("kein Bild dekodiert");
    return found;
}

} // namespace dss
