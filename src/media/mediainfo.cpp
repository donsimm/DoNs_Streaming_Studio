// Grösse einer Bild- oder Videodatei: Datei öffnen, besten Bildstrom wählen, ein Bild
// dekodieren und dessen Grösse nehmen (wie der Dekodierweg M1 aus P02).
//
// Quellen:
//   avformat_open_input, av_find_best_stream: https://ffmpeg.org/doxygen/7.1/group__lavf__decoding.html
//   Dekodier-API send/receive:                https://ffmpeg.org/doxygen/7.1/group__lavc__decoding.html

#include "mediainfo.h"

#include "avsupport.h"
#include "imageorientation.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
}

#include <cmath>

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
            const QSize shown = orientedSize(QSize(frame->width, frame->height), input.orientation);
            *width = shown.width();
            *height = shown.height();
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

int readVideoOrientation(const QString &path)
{
    AvInput input;
    QString error;
    return input.open(path, &error) ? input.orientation : kOrientationNormal;
}

int displayMatrixOrientation(const int32_t *matrix)
{
    if (!matrix)
        return kOrientationNormal;
    const double angle = av_display_rotation_get(matrix); // gegen den Uhrzeigersinn
    if (std::isnan(angle))
        return kOrientationNormal;
    double theta = -std::round(angle);
    theta -= 360 * std::floor(theta / 360 + 0.9 / 360);
    if (std::fabs(theta - 90) < 1.0)
        return matrix[3] > 0 ? 5 : 6;
    if (std::fabs(theta - 180) < 1.0) {
        const bool h = matrix[0] < 0;
        const bool v = matrix[4] < 0;
        return h && v ? 3 : h ? 2 : v ? 4 : kOrientationNormal;
    }
    if (std::fabs(theta - 270) < 1.0)
        return matrix[3] < 0 ? 7 : 8;
    if (std::fabs(theta) < 1.0)
        return matrix[4] < 0 ? 4 : kOrientationNormal;
    return kOrientationNormal;
}

} // namespace dss
