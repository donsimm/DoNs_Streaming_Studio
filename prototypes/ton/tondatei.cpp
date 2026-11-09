// Quellen: https://ffmpeg.org/doxygen/7.1/group__lavf__decoding.html (avformat_open_input,
// av_read_frame), https://ffmpeg.org/doxygen/7.1/group__lavc__decoding.html (avcodec_send_packet,
// avcodec_receive_frame), https://ffmpeg.org/doxygen/7.1/group__lswr.html (swr_alloc_set_opts2,
// swr_convert); FFmpeg 7.1.5-0+deb13u1.
#include "tondatei.h"

#include "zeit.h"

#include <cmath>
#include <cstdlib>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace {

QString avFehler(int code)
{
    char text[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, text, sizeof text);
    return QString::fromUtf8(text);
}

} // namespace

bool tonLaden(const QString &pfad, TonDatei *aus, QString *fehler)
{
    *aus = TonDatei();
    AVFormatContext *fmt = nullptr;
    AVCodecContext *dec = nullptr;
    SwrContext *swr = nullptr;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool ok = false;
    bool ersterBlock = true;
    qint64 verwerfen = 0; // Proben vor Medienzeit 0 (negativer Start)
    auto aufraeumen = [&] {
        av_frame_free(&frame);
        av_packet_free(&pkt);
        swr_free(&swr);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
    };

    int r = avformat_open_input(&fmt, pfad.toUtf8().constData(), nullptr, nullptr);
    if (r < 0) {
        *fehler = QStringLiteral("Datei nicht lesbar: %1").arg(avFehler(r));
        aufraeumen();
        return false;
    }
    avformat_find_stream_info(fmt, nullptr);
    const AVCodec *codec = nullptr;
    const int index = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
    if (index < 0 || !codec) {
        *fehler = QStringLiteral("kein Ton in der Datei");
        aufraeumen();
        return false;
    }
    AVStream *st = fmt->streams[index];
    dec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(dec, st->codecpar);
    dec->pkt_timebase = st->time_base;
    if ((r = avcodec_open2(dec, codec, nullptr)) < 0) {
        *fehler = QStringLiteral("Decoder: %1").arg(avFehler(r));
        aufraeumen();
        return false;
    }
    aus->quellRate = dec->sample_rate;
    aus->quellKanaele = dec->ch_layout.nb_channels;
    aus->codec = QString::fromUtf8(codec->name);

    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if ((r = swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, kRate, &dec->ch_layout,
                                 dec->sample_fmt, dec->sample_rate, 0, nullptr))
            < 0
        || (r = swr_init(swr)) < 0) {
        *fehler = QStringLiteral("swresample: %1").arg(avFehler(r));
        aufraeumen();
        return false;
    }

    std::vector<float> puffer;
    auto anhaengen = [&](const AVFrame *f) -> bool {
        const int max = swr_get_out_samples(swr, f ? f->nb_samples : 0);
        if (max <= 0)
            return true;
        puffer.resize(size_t(max) * kKanaele);
        uint8_t *ziel = reinterpret_cast<uint8_t *>(puffer.data());
        const int n = swr_convert(swr, &ziel, max, f ? f->extended_data : nullptr,
                                  f ? f->nb_samples : 0);
        if (n < 0)
            return false;
        qint64 start = 0;
        if (verwerfen > 0) {
            start = std::min<qint64>(verwerfen, n);
            verwerfen -= start;
        }
        aus->proben.insert(aus->proben.end(), puffer.begin() + start * kKanaele,
                           puffer.begin() + qint64(n) * kKanaele);
        return true;
    };
    auto blockAnnehmen = [&]() -> bool {
        if (ersterBlock) {
            ersterBlock = false;
            const int64_t pts = frame->best_effort_timestamp;
            aus->erstePts = pts == AV_NOPTS_VALUE ? 0.0 : pts * av_q2d(st->time_base);
            const qint64 lage = std::llround(aus->erstePts * kRate);
            if (lage > 0)
                aus->proben.assign(size_t(lage) * kKanaele, 0.0f); // Stille bis zum ersten Block
            else
                verwerfen = -lage;
        }
        return anhaengen(frame);
    };

    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == index && avcodec_send_packet(dec, pkt) >= 0) {
            while (avcodec_receive_frame(dec, frame) >= 0) {
                if (!blockAnnehmen()) {
                    *fehler = QStringLiteral("swr_convert fehlgeschlagen");
                    av_packet_unref(pkt);
                    aufraeumen();
                    return false;
                }
            }
        }
        av_packet_unref(pkt);
    }
    avcodec_send_packet(dec, nullptr);
    while (avcodec_receive_frame(dec, frame) >= 0)
        blockAnnehmen();
    anhaengen(nullptr); // Rest im Resampler
    ok = !aus->proben.empty();
    if (!ok)
        *fehler = QStringLiteral("keine Proben dekodiert");
    aufraeumen();
    return ok;
}

std::vector<qint64> einsaetzeFinden(const float *proben, qint64 frames, int kanaele, float schwelle,
                                    qint64 ruheProben)
{
    std::vector<qint64> einsaetze;
    qint64 ruhe = ruheProben; // am Anfang gilt ein Einsatz bei 0 als Einsatz
    for (qint64 i = 0; i < frames; ++i) {
        float m = 0.0f;
        for (int k = 0; k < kanaele; ++k)
            m = std::max(m, std::fabs(proben[i * kanaele + k]));
        if (m > schwelle) {
            if (ruhe >= ruheProben)
                einsaetze.push_back(i);
            ruhe = 0;
        } else {
            ++ruhe;
        }
    }
    return einsaetze;
}
