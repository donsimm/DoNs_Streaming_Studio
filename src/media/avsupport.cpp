// Öffnen einer Datei mit libavformat und Wahl des Decoders.
//
// Quellen:
//   avformat_open_input, avformat_find_stream_info, av_find_best_stream:
//     https://ffmpeg.org/doxygen/7.1/group__lavf__decoding.html
//   avcodec_find_decoder_by_name, avcodec_open2: https://ffmpeg.org/doxygen/7.1/group__lavc__core.html
//   Feld alpha_mode setzt der Matroska-Leser (MATROSKA_ID_VIDEOALPHAMODE):
//     https://ffmpeg.org/doxygen/7.1/matroskadec_8c_source.html
//   FFmpegs eigener VP9-Decoder verwirft den Alpha, libvpx-vp9 liefert ihn: https://trac.ffmpeg.org/ticket/8344
//   av_log_set_level: https://ffmpeg.org/doxygen/7.1/group__lavu__log.html
//   Display-Matrix als Seitendaten des Stroms (AVCodecParameters::coded_side_data,
//     AV_PKT_DATA_DISPLAYMATRIX, 9 x int32_t), av_packet_side_data_get:
//     https://ffmpeg.org/doxygen/7.1/group__lavc__packet__side__data.html

#include "avsupport.h"

#include "mediainfo.h"

extern "C" {
#include <libavutil/dict.h>
#include <libavutil/log.h>
}

namespace dss {

void quietAvLog()
{
    av_log_set_level(AV_LOG_ERROR);
}

AvInput::~AvInput()
{
    avcodec_free_context(&codec);
    avformat_close_input(&format);
}

bool AvInput::open(const QString &path, QString *error)
{
    quietAvLog();
    const QByteArray file = path.toUtf8();
    if (avformat_open_input(&format, file.constData(), nullptr, nullptr) < 0) {
        *error = QStringLiteral("Datei nicht lesbar");
        return false;
    }
    if (avformat_find_stream_info(format, nullptr) < 0) {
        *error = QStringLiteral("Inhalt nicht erkannt");
        return false;
    }
    stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0) {
        *error = QStringLiteral("kein Bild in der Datei");
        return false;
    }
    const AVStream *s = format->streams[stream];
    const AVDictionaryEntry *alpha = av_dict_get(s->metadata, "alpha_mode", nullptr, 0);
    const AVCodec *decoder = nullptr;
    if (s->codecpar->codec_id == AV_CODEC_ID_VP9 && alpha && QByteArray(alpha->value) == "1")
        decoder = avcodec_find_decoder_by_name("libvpx-vp9");
    if (!decoder)
        decoder = avcodec_find_decoder(s->codecpar->codec_id);
    if (!decoder) {
        *error = QStringLiteral("kein Decoder für %1").arg(QString::fromLatin1(avcodec_get_name(s->codecpar->codec_id)));
        return false;
    }
    codec = avcodec_alloc_context3(decoder);
    if (!codec || avcodec_parameters_to_context(codec, s->codecpar) < 0) {
        *error = QStringLiteral("Decoder nicht einrichtbar");
        return false;
    }
    codec->thread_count = 0; // automatisch, wie in P02
    if (avcodec_open2(codec, decoder, nullptr) < 0) {
        *error = QStringLiteral("Decoder %1 öffnet nicht").arg(QString::fromLatin1(decoder->name));
        return false;
    }
    decoderName = QString::fromLatin1(decoder->name);
#if DSS_VIDEO_ROTATION
    // Drehung aus den Metadaten (P10a, Punkt 65), zum Beispiel Box tkhd in MP4.
    const AVPacketSideData *matrix = av_packet_side_data_get(
        s->codecpar->coded_side_data, s->codecpar->nb_coded_side_data, AV_PKT_DATA_DISPLAYMATRIX);
    if (matrix && matrix->size >= 9 * sizeof(int32_t))
        orientation = displayMatrixOrientation(reinterpret_cast<const int32_t *>(matrix->data));
#endif
    return true;
}

} // namespace dss
