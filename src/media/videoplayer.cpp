// Abspielen eines Videos im eigenen Thread (P09, Teil B), Dekodierweg wie M1 aus P02
// (prototypes/a-cpp-qt-libvpx/decoder.cpp).
//
// Quellen:
//   Dekodier-API send/receive, Leeren mit nullptr, avcodec_flush_buffers:
//     https://ffmpeg.org/doxygen/7.1/group__lavc__decoding.html
//   av_read_frame, av_seek_frame (AVSEEK_FLAG_BACKWARD): https://ffmpeg.org/doxygen/7.1/group__lavf__decoding.html
//   best_effort_timestamp: https://ffmpeg.org/doxygen/7.1/structAVFrame.html
//   sws_getCachedContext, sws_scale: https://ffmpeg.org/doxygen/7.1/group__libsws.html
//   AV_PIX_FMT_FLAG_ALPHA: https://ffmpeg.org/doxygen/7.1/pixdesc_8h.html
//   AV_PIX_FMT_BGRA ist auf Little Endian dieselbe Byte-Folge wie QImage::Format_ARGB32
//     (0xAARRGGBB als 32-Bit-Wert): https://doc.qt.io/qt-6.8/qimage.html#Format-enum
//   std::condition_variable::wait: https://en.cppreference.com/w/cpp/thread/condition_variable/wait

#include "videoplayer.h"

#include "avsupport.h"

extern "C" {
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace dss {

namespace {

constexpr auto kLateLimit = std::chrono::milliseconds(50);

// Vormultiplizieren: Farbe * Alpha / 255, gerundet.
void premultiply(VideoFrame *frame)
{
    for (int y = 0; y < frame->height; ++y) {
        uint8_t *p = frame->data.data() + static_cast<size_t>(y) * frame->stride;
        for (int x = 0; x < frame->width; ++x, p += 4) {
            const unsigned a = p[3];
            if (a == 255)
                continue;
            for (int c = 0; c < 3; ++c)
                p[c] = static_cast<uint8_t>((p[c] * a + 127) / 255);
        }
    }
}

double alphaMean(const VideoFrame &frame)
{
    double sum = 0;
    for (int y = 0; y < frame.height; ++y) {
        const uint8_t *p = frame.data.data() + static_cast<size_t>(y) * frame.stride;
        for (int x = 0; x < frame.width; ++x)
            sum += p[4 * x + 3];
    }
    return sum / (static_cast<double>(frame.width) * frame.height);
}

VideoPlayer::Clock::duration seconds(double s)
{
    return std::chrono::duration_cast<VideoPlayer::Clock::duration>(std::chrono::duration<double>(s));
}

} // namespace

VideoPlayer::VideoPlayer(const QString &path, bool loop, bool holdAtEnd)
    : m_path(path), m_loop(loop), m_holdAtEnd(holdAtEnd)
{
    m_thread = std::thread([this] { run(); });
}

VideoPlayer::~VideoPlayer()
{
    {
        const std::lock_guard lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

std::shared_ptr<const VideoFrame> VideoPlayer::frameAt(Clock::time_point t)
{
    bool advanced = false;
    std::shared_ptr<const VideoFrame> result;
    {
        const std::lock_guard lock(m_mutex);
        if (!m_anchored && !m_queue.empty()) {
            m_base = t;
            m_anchored = true;
        }
        while (!m_queue.empty() && isDue(m_base + m_queue.front().offset, t)) {
            m_frame = std::move(m_queue.front().frame);
            m_queue.pop_front();
            m_shown = true;
            advanced = true;
            ++m_serial;
        }
        if (advanced)
            m_refillAt = Clock::now() + kRefillDelay;
        result = m_frame;
    }
    if (advanced)
        m_wake.notify_all(); // Platz für den Decoder
    return result;
}

VideoPlayer::State VideoPlayer::state() const
{
    const std::lock_guard lock(m_mutex);
    if (m_failed)
        return State::Failed;
    if (!m_shown)
        return State::Starting;
    if (m_decoderDone && m_queue.empty())
        return m_frame ? State::Holding : State::Ended;
    return State::Playing;
}

QString VideoPlayer::error() const
{
    const std::lock_guard lock(m_mutex);
    return m_error;
}

QString VideoPlayer::decoderName() const
{
    const std::lock_guard lock(m_mutex);
    return m_decoderName;
}

VideoPlayer::Statistics VideoPlayer::statistics() const
{
    const std::lock_guard lock(m_mutex);
    return m_stats;
}

bool VideoPlayer::push(std::shared_ptr<const VideoFrame> frame, Clock::duration offset)
{
    std::unique_lock lock(m_mutex);
    for (;;) {
        if (m_stop)
            return false;
        if (m_queue.size() < kQueueSize) {
            // Mit Vorrat erst nach kRefillDelay nachfüllen; ohne Vorrat sofort (Decoder im Rückstand).
            if (m_queue.empty() || Clock::now() >= m_refillAt)
                break;
            m_wake.wait_until(lock, m_refillAt);
            continue;
        }
        if (!m_anchored) {
            m_wake.wait(lock); // bis zum ersten frameAt
            continue;
        }
        const Clock::time_point overdue = m_base + m_queue.front().offset + kDropAfter;
        if (Clock::now() >= overdue) {
            // Niemand hat es abgeholt: gilt als gezeigt, aber verworfen.
            m_frame = std::move(m_queue.front().frame);
            m_queue.pop_front();
            m_shown = true;
            ++m_serial;
            ++m_stats.dropped;
            continue;
        }
        m_wake.wait_until(lock, overdue);
    }
    if (frame) {
        ++m_stats.decoded;
        if (m_anchored && Clock::now() > m_base + offset + kLateLimit)
            ++m_stats.late;
    }
    m_queue.push_back({std::move(frame), offset});
    return true;
}

void VideoPlayer::run()
{
    AvInput input;
    QString error;
    if (!input.open(m_path, &error)) {
        const std::lock_guard lock(m_mutex);
        m_error = error;
        m_failed = true;
        m_decoderDone = true;
        return;
    }
    {
        const std::lock_guard lock(m_mutex);
        m_decoderName = input.decoderName;
    }
    const AVStream *stream = input.format->streams[input.stream];
    const double timeBase = av_q2d(stream->time_base);
    const AVRational rate = stream->avg_frame_rate.num > 0 ? stream->avg_frame_rate
                                                           : stream->r_frame_rate;
    const double frameDuration = rate.num > 0 ? av_q2d(av_inv_q(rate)) : 1.0 / 30.0;

    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    SwsContext *sws = nullptr;
    // Wiederverwendete Puffer: frei ist, was nur noch hier gehalten wird (use_count() == 1).
    std::vector<std::shared_ptr<VideoFrame>> pool;

    bool started = false;
    double firstPts = 0.0;
    double loopOffset = 0.0; // Sekunden der schon gespielten Durchgänge
    double lastPts = 0.0;
    bool draining = false;
    bool ended = false;
    bool firstFrame = true;

    while (!ended) {
        {
            const std::lock_guard lock(m_mutex);
            if (m_stop)
                break;
        }
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
        for (;;) {
            const int r = avcodec_receive_frame(input.codec, frame);
            if (r == AVERROR(EAGAIN))
                break;
            if (r == AVERROR_EOF) {
                // Ende der Datei: Das letzte Bild steht noch eine Bilddauer.
                const double played = lastPts - firstPts + frameDuration;
                if (m_loop) {
                    loopOffset += played;
                    av_seek_frame(input.format, input.stream,
                                  stream->start_time == AV_NOPTS_VALUE ? 0 : stream->start_time,
                                  AVSEEK_FLAG_BACKWARD);
                    avcodec_flush_buffers(input.codec);
                    draining = false;
                    const std::lock_guard lock(m_mutex);
                    ++m_stats.loops;
                } else {
                    // Ohne holdAtEnd ab dem Ende des letzten Bildes leer.
                    if (!m_holdAtEnd)
                        push(nullptr, seconds(loopOffset + played));
                    ended = true;
                }
                break;
            }
            if (r < 0) {
                const std::lock_guard lock(m_mutex);
                m_error = QStringLiteral("Fehler beim Dekodieren");
                m_failed = true;
                ended = true;
                break;
            }

            const auto format = static_cast<AVPixelFormat>(frame->format);
            const int w = frame->width;
            const int h = frame->height;
            if (w <= 0 || h <= 0)
                continue; // Bild ohne Grösse: überspringen
            std::shared_ptr<VideoFrame> out;
            for (const std::shared_ptr<VideoFrame> &f : pool) {
                if (f.use_count() == 1 && f->width == w && f->height == h) {
                    out = f;
                    break;
                }
            }
            if (!out) {
                out = std::make_shared<VideoFrame>();
                out->width = w;
                out->height = h;
                out->stride = 4 * w;
                // assign statt resize: GCC 14 meldet bei resize im Release-Bau -Warray-bounds
                // (in __builtin_memset), obwohl die Grösse positiv ist.
                out->data.assign(static_cast<size_t>(out->stride) * h, 0);
                pool.push_back(out);
            }
            sws = sws_getCachedContext(sws, w, h, format, w, h, AV_PIX_FMT_BGRA, SWS_BILINEAR,
                                       nullptr, nullptr, nullptr);
            uint8_t *dst[4] = {out->data.data(), nullptr, nullptr, nullptr};
            int dstStride[4] = {out->stride, 0, 0, 0};
            sws_scale(sws, frame->data, frame->linesize, 0, h, dst, dstStride);
            // Ohne Alpha füllt sws den Alphakanal mit 255; das Bild ist dann schon vormultipliziert.
            const bool hasAlpha = av_pix_fmt_desc_get(format)->flags & AV_PIX_FMT_FLAG_ALPHA;
            if (firstFrame) {
                const std::lock_guard lock(m_mutex);
                m_stats.firstAlphaMean = hasAlpha ? alphaMean(*out) : -1.0;
                firstFrame = false;
            }
            if (hasAlpha)
                premultiply(out.get());

            lastPts = frame->best_effort_timestamp == AV_NOPTS_VALUE
                          ? lastPts + frameDuration
                          : frame->best_effort_timestamp * timeBase;
            out->pts = lastPts;
            if (!started) {
                // Abstand 0: das erste Bild ist beim ersten frameAt fällig (Anker).
                firstPts = lastPts;
                started = true;
            }
            if (!push(out, seconds(loopOffset + lastPts - firstPts))) {
                ended = true;
                break;
            }
        }
    }

    sws_freeContext(sws);
    av_packet_free(&packet);
    av_frame_free(&frame);
    const std::lock_guard lock(m_mutex);
    m_decoderDone = true;
}

} // namespace dss
