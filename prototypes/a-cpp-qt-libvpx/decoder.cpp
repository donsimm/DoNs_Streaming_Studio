// Dekodierweg für M1, siehe decoder.h.
//
// Ablauf: avformat_open_input -> av_find_best_stream -> Decoder wählen (ausdrücklich per Name
// oder Standard) -> av_read_frame / avcodec_send_packet / avcodec_receive_frame -> sws_scale nach
// BGRA -> QImage. Abgespielt wird in Echtzeit nach den Zeitstempeln der Bilder, am Ende in Schleife
// (Rücksprung an den Anfang, Decoder leeren).
//
// Quellen:
//   Dekodier-API send/receive:  https://ffmpeg.org/doxygen/7.1/group__lavc__decoding.html
//   avcodec_find_decoder_by_name: https://ffmpeg.org/doxygen/7.1/group__lavc__core.html
//   libvpx-Decoder mit Alpha (BlockAdditional aus Matroska/WebM): FFmpeg-Ticket
//                               https://trac.ffmpeg.org/ticket/8344
//   sws_scale:                  https://ffmpeg.org/doxygen/7.1/group__libsws.html
//   QImage::Format_ARGB32 liegt auf little endian als B, G, R, A im Speicher:
//                               https://doc.qt.io/qt-6.8/qimage.html#Format-enum
#include "decoder.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

#include <chrono>

VideoDecoder::VideoDecoder(const QString &file, const QString &decoderName, FrameCallback onFrame, InfoCallback onInfo)
    : m_file(file), m_decoderName(decoderName), m_onFrame(std::move(onFrame)), m_onInfo(std::move(onInfo))
{
    m_thread = std::thread([this] { run(); });
}

VideoDecoder::~VideoDecoder()
{
    m_stop = true;
    if (m_thread.joinable())
        m_thread.join();
}

void VideoDecoder::run()
{
    using clock = std::chrono::steady_clock;
    Info info;
    AVFormatContext *fmt = nullptr;
    AVCodecContext *ctx = nullptr;
    SwsContext *sws = nullptr;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool infoSent = false;

    auto fail = [&](const QString &msg) {
        info.error = msg;
        m_onInfo(info);
    };

    const QByteArray path = m_file.toUtf8();
    if (avformat_open_input(&fmt, path.constData(), nullptr, nullptr) < 0 || avformat_find_stream_info(fmt, nullptr) < 0) {
        fail(QStringLiteral("Datei nicht lesbar: ") + m_file);
    } else {
        const int si = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        const AVCodec *codec = nullptr;
        if (si >= 0)
            codec = m_decoderName.isEmpty() ? avcodec_find_decoder(fmt->streams[si]->codecpar->codec_id)
                                            : avcodec_find_decoder_by_name(m_decoderName.toUtf8().constData());
        if (!codec) {
            fail(QStringLiteral("kein Decoder: ") + m_decoderName);
        } else {
            ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(ctx, fmt->streams[si]->codecpar);
            ctx->thread_count = 0; // automatisch, wie die Voreinstellung des ffmpeg-Befehls
            if (avcodec_open2(ctx, codec, nullptr) < 0) {
                fail(QStringLiteral("Decoder öffnet nicht"));
            } else {
                info.decoder = QString::fromLatin1(codec->name);
                const AVRational tb = fmt->streams[si]->time_base;
                const auto base = clock::now();
                double loopOffset = 0, lastPts = 0, frameDur = 1.0 / 30;
                if (fmt->streams[si]->avg_frame_rate.num > 0)
                    frameDur = av_q2d(av_inv_q(fmt->streams[si]->avg_frame_rate));
                bool draining = false;

                while (!m_stop) {
                    // Nächstes Paket an den Decoder geben (am Dateiende: Decoder leeren)
                    if (!draining) {
                        const int r = av_read_frame(fmt, pkt);
                        if (r == AVERROR_EOF) {
                            avcodec_send_packet(ctx, nullptr);
                            draining = true;
                        } else if (r < 0) {
                            fail(QStringLiteral("Lesefehler"));
                            break;
                        } else {
                            if (pkt->stream_index == si)
                                avcodec_send_packet(ctx, pkt);
                            av_packet_unref(pkt);
                        }
                    }
                    for (;;) {
                        const int r = avcodec_receive_frame(ctx, frame);
                        if (r == AVERROR(EAGAIN))
                            break;
                        if (r == AVERROR_EOF) {
                            // Schleife: zurück an den Anfang
                            loopOffset += lastPts + frameDur;
                            av_seek_frame(fmt, si, 0, AVSEEK_FLAG_BACKWARD);
                            avcodec_flush_buffers(ctx);
                            draining = false;
                            break;
                        }
                        if (r < 0) {
                            m_stop = true;
                            break;
                        }
                        const auto pf = static_cast<AVPixelFormat>(frame->format);
                        const int w = frame->width, h = frame->height;
                        sws = sws_getCachedContext(sws, w, h, pf, w, h, AV_PIX_FMT_BGRA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                        QImage img(w, h, QImage::Format_ARGB32);
                        uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
                        int dstStride[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
                        sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dstStride);

                        const bool hasAlpha = av_pix_fmt_desc_get(pf)->flags & AV_PIX_FMT_FLAG_ALPHA;
                        if (!infoSent) {
                            info.pixFmt = QString::fromLatin1(av_get_pix_fmt_name(pf));
                            double sum = 0;
                            for (int y = 0; y < h; ++y) {
                                const uchar *line = img.constScanLine(y);
                                for (int x = 0; x < w; ++x)
                                    sum += line[4 * x + 3];
                            }
                            info.firstAlphaMean = sum / (static_cast<double>(w) * h);
                            m_onInfo(info);
                            infoSent = true;
                        }
                        // Ohne Alpha füllt sws den Alphakanal mit 255; dann ist das Bild schon vormultipliziert.
                        if (hasAlpha)
                            img.convertTo(QImage::Format_ARGB32_Premultiplied);
                        else
                            img.reinterpretAsFormat(QImage::Format_ARGB32_Premultiplied);

                        lastPts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? lastPts + frameDur
                                                                                 : frame->best_effort_timestamp * av_q2d(tb);
                        const auto due = base + std::chrono::duration_cast<clock::duration>(
                                                    std::chrono::duration<double>(loopOffset + lastPts));
                        std::this_thread::sleep_until(due);
                        if (m_stop)
                            break;
                        m_onFrame(img);
                    }
                }
            }
        }
    }

    sws_freeContext(sws);
    avcodec_free_context(&ctx);
    avformat_close_input(&fmt);
    av_frame_free(&frame);
    av_packet_free(&pkt);
}
