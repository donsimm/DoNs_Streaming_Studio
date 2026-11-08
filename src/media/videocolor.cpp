#include "videocolor.h"

namespace dss {

namespace {

// Höhe, ab der Chromium ein Video ohne Angabe als BT.709 zeigt (ffmpeg_common.cc, siehe Kopf)
constexpr int kHdHeight = 720;

} // namespace

VideoColor chooseVideoColor(const VideoColorTags &tags)
{
    VideoColor c;
    c.fullRange = tags.range == 2;
    switch (tags.matrix) {
    case 1:
        c.matrix = VideoMatrix::Bt709;
        break;
    case 4:
        c.matrix = VideoMatrix::Fcc;
        break;
    case 5:
    case 6:
        c.matrix = VideoMatrix::Bt601;
        break;
    case 7:
        c.matrix = VideoMatrix::Smpte240m;
        break;
    case 9:
    case 10:
        c.matrix = VideoMatrix::Bt2020;
        break;
    default: // keine Angabe
        c.matrix = tags.heightRule && tags.height >= kHdHeight ? VideoMatrix::Bt709 : VideoMatrix::Bt601;
        break;
    }
    return c;
}

const char *videoMatrixName(VideoMatrix matrix)
{
    switch (matrix) {
    case VideoMatrix::Bt601:
        return "BT.601";
    case VideoMatrix::Bt709:
        return "BT.709";
    case VideoMatrix::Fcc:
        return "FCC";
    case VideoMatrix::Smpte240m:
        return "SMPTE240M";
    case VideoMatrix::Bt2020:
        return "BT.2020";
    }
    return "?";
}

} // namespace dss
