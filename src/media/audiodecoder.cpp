// Ton einer Videodatei dekodieren (P12b b2), Weg wie der Prototyp prototypes/ton/tondatei.cpp
// aus P12a.
//
// Quellen (FFmpeg 7.1.5-0+deb13u1):
//   avformat_open_input, avformat_find_stream_info, av_find_best_stream, av_read_frame:
//     https://ffmpeg.org/doxygen/7.1/group__lavf__decoding.html
//   avcodec_send_packet, avcodec_receive_frame, Leeren mit nullptr:
//     https://ffmpeg.org/doxygen/7.1/group__lavc__decoding.html
//   best_effort_timestamp: https://ffmpeg.org/doxygen/7.1/structAVFrame.html
//   swr_alloc_set_opts2, swr_init, swr_get_out_samples, swr_convert (Rest mit in = nullptr):
//     https://ffmpeg.org/doxygen/7.1/group__lswr.html
//   av_channel_layout_default (Layout ohne Ordnung): https://ffmpeg.org/doxygen/7.1/group__lavu__audio__channels.html
//   std::condition_variable::wait_for: https://en.cppreference.com/w/cpp/thread/condition_variable/wait_for

#include "audiodecoder.h"

#include "avsupport.h"

#include <algorithm>
#include <chrono>
#include <cmath>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

namespace dss {

namespace {

// Wie oft der Thread bei vollem Ring nachsieht. Der Leser (Echtzeit-Abruf) weckt ihn nicht, damit
// der Abruf keinen Systemaufruf macht; 10 ms sind klein gegen den Vorrat und gegen 40 ms Puffer.
constexpr auto kPoll = std::chrono::milliseconds(10);

struct AvAudio
{
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    SwrContext *swr = nullptr;
    AVPacket *packet = nullptr;
    AVFrame *frame = nullptr;
    int stream = -1;

    AvAudio() = default;
    AvAudio(const AvAudio &) = delete;
    AvAudio &operator=(const AvAudio &) = delete;
    ~AvAudio()
    {
        av_frame_free(&frame);
        av_packet_free(&packet);
        swr_free(&swr);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
};

enum class Opened { Ok, NoAudio, Failed };

Opened openAudio(const QString &path, AvAudio *av, AudioDecoder::Info *info, QString *error)
{
    const QByteArray file = path.toUtf8();
    if (avformat_open_input(&av->format, file.constData(), nullptr, nullptr) < 0) {
        *error = QStringLiteral("Datei nicht lesbar");
        return Opened::Failed;
    }
    if (avformat_find_stream_info(av->format, nullptr) < 0) {
        *error = QStringLiteral("Inhalt nicht erkannt");
        return Opened::Failed;
    }
    const AVCodec *decoder = nullptr;
    av->stream = av_find_best_stream(av->format, AVMEDIA_TYPE_AUDIO, -1, -1, &decoder, 0);
    if (av->stream < 0 || !decoder) {
        *error = QStringLiteral("kein Ton in der Datei");
        return av->stream == AVERROR_STREAM_NOT_FOUND ? Opened::NoAudio : Opened::Failed;
    }
    const AVStream *s = av->format->streams[av->stream];
    av->codec = avcodec_alloc_context3(decoder);
    if (!av->codec || avcodec_parameters_to_context(av->codec, s->codecpar) < 0) {
        *error = QStringLiteral("Decoder nicht einrichtbar");
        return Opened::Failed;
    }
    av->codec->pkt_timebase = s->time_base;
    if (avcodec_open2(av->codec, decoder, nullptr) < 0) {
        *error = QStringLiteral("Decoder %1 öffnet nicht").arg(QString::fromLatin1(decoder->name));
        return Opened::Failed;
    }
    AVChannelLayout in = {};
    if (av->codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC)
        av_channel_layout_default(&in, av->codec->ch_layout.nb_channels);
    else
        av_channel_layout_copy(&in, &av->codec->ch_layout);
    const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    const int r = swr_alloc_set_opts2(&av->swr, &stereo, AV_SAMPLE_FMT_FLT, AudioDecoder::kRate, &in,
                                      av->codec->sample_fmt, av->codec->sample_rate, 0, nullptr);
    av_channel_layout_uninit(&in);
    if (r < 0 || swr_init(av->swr) < 0) {
        *error = QStringLiteral("Umwandlung des Tons nicht einrichtbar");
        return Opened::Failed;
    }
    av->packet = av_packet_alloc();
    av->frame = av_frame_alloc();
    if (!av->packet || !av->frame) {
        *error = QStringLiteral("kein Speicher");
        return Opened::Failed;
    }
    info->sourceRate = av->codec->sample_rate;
    info->sourceChannels = av->codec->ch_layout.nb_channels;
    info->codec = QString::fromLatin1(decoder->name);
    return Opened::Ok;
}

} // namespace

AudioDecoder::AudioDecoder(const QString &path, AudioRing *ring, bool loop)
    : m_path(path), m_ring(ring), m_loop(loop), m_firstPass(ring->allocatePass())
{
    m_thread = std::thread([this] { run(); });
}

AudioDecoder::~AudioDecoder()
{
    {
        const std::lock_guard lock(m_mutex);
        m_stopFlag = true;
    }
    m_wake.notify_all();
    if (m_thread.joinable())
        m_thread.join();
}

uint32_t AudioDecoder::restart()
{
    uint32_t pass = 0;
    {
        // Unter der Sperre wie die Vergabe im Thread (run), damit die Marken im Ring steigen.
        const std::lock_guard lock(m_mutex);
        pass = m_ring->allocatePass();
        m_requestedPass = pass;
        m_restartFlag = true;
    }
    m_wake.notify_all();
    return pass;
}

AudioDecoder::State AudioDecoder::state() const
{
    const std::lock_guard lock(m_mutex);
    return m_state;
}

QString AudioDecoder::error() const
{
    const std::lock_guard lock(m_mutex);
    return m_error;
}

AudioDecoder::Info AudioDecoder::info() const
{
    const std::lock_guard lock(m_mutex);
    return m_info;
}

AudioDecoder::Statistics AudioDecoder::statistics() const
{
    return Statistics{m_frames.load(), m_passes.load()};
}

void AudioDecoder::setState(State state, const QString &error)
{
    const std::lock_guard lock(m_mutex);
    m_state = state;
    if (!error.isEmpty())
        m_error = error;
}

void AudioDecoder::run()
{
    quietAvLog();
    uint32_t pass = m_firstPass;
    setState(State::Running);
    for (;;) {
        int64_t written = 0;
        const Wake wake = decodePass(pass, &written);
        if (wake == Wake::Stop)
            return;
        std::unique_lock lock(m_mutex);
        if (wake == Wake::Go && !m_restartFlag) {
            if (m_loop && written > 0) {
                pass = m_ring->allocatePass(); // unter der Sperre, siehe restart()
                continue;
            }
            m_state = State::Ended;
            m_wake.wait(lock, [this] { return m_stopFlag.load() || m_restartFlag.load(); });
        }
        if (m_stopFlag)
            return;
        pass = m_requestedPass;
        m_restartFlag = false;
        m_state = State::Running;
    }
}

AudioDecoder::Wake AudioDecoder::waitForRoom(bool needMark)
{
    for (;;) {
        if (m_stopFlag)
            return Wake::Stop;
        if (m_restartFlag)
            return Wake::Restart;
        if (needMark ? m_ring->markWritable() : m_ring->writable() > 0)
            return Wake::Go;
        std::unique_lock lock(m_mutex);
        m_wake.wait_for(lock, kPoll, [this] { return m_stopFlag.load() || m_restartFlag.load(); });
    }
}

AudioDecoder::Wake AudioDecoder::writeFrames(const float *frames, int64_t n)
{
    while (n > 0) {
        if (m_stopFlag)
            return Wake::Stop;
        if (m_restartFlag)
            return Wake::Restart;
        const size_t room = m_ring->writable();
        if (room == 0) {
            const Wake wake = waitForRoom(false);
            if (wake != Wake::Go)
                return wake;
            continue;
        }
        const size_t k = size_t(std::min<int64_t>(int64_t(room), n));
        m_ring->write(frames, k);
        if (frames)
            frames += k * kChannels;
        n -= int64_t(k);
        m_frames += k;
    }
    return Wake::Go;
}

// Wake::Go heisst hier: Ende des Durchgangs erreicht.
AudioDecoder::Wake AudioDecoder::decodePass(uint32_t pass, int64_t *written)
{
    AvAudio av;
    Info info;
    QString error;
    const Opened opened = openAudio(m_path, &av, &info, &error);
    if (opened != Opened::Ok) {
        setState(opened == Opened::NoAudio ? State::NoAudio : State::Failed, error);
        return Wake::Stop;
    }
    {
        const std::lock_guard lock(m_mutex);
        m_info = info;
    }
    Wake wake = waitForRoom(true);
    if (wake != Wake::Go)
        return wake;
    m_ring->markPass(pass);
    ++m_passes;

    const AVStream *stream = av.format->streams[av.stream];
    int64_t &pos = *written; // Lage im Durchgang, Rahmen ab Medienzeit 0
    int64_t skip = 0;        // Proben vor Medienzeit 0, noch zu verwerfen
    bool first = true;
    bool end = false;

    // Schreibt bis zur Länge des Durchgangs; danach end.
    auto put = [&](const float *frames, int64_t n) -> Wake {
        const int64_t limit = m_loopLength.load(std::memory_order_relaxed);
        if (limit > 0 && pos + n >= limit) {
            n = std::max<int64_t>(0, limit - pos);
            end = true;
        }
        const Wake w = writeFrames(frames, n);
        if (w == Wake::Go)
            pos += n;
        return w;
    };
    // Block (oder mit nullptr den Rest im Resampler) umwandeln und schreiben.
    auto convert = [&](const AVFrame *f) -> Wake {
        if (f && first) {
            first = false;
            const int64_t pts = f->best_effort_timestamp;
            const double start = pts == AV_NOPTS_VALUE ? 0.0 : double(pts) * av_q2d(stream->time_base);
            const int64_t lage = std::llround(start * kRate);
            if (lage > 0) {
                const Wake w = put(nullptr, lage); // Stille bis zum ersten Block
                if (w != Wake::Go || end)
                    return w;
            } else {
                skip = -lage;
            }
        }
        const int max = swr_get_out_samples(av.swr, f ? f->nb_samples : 0);
        if (max <= 0)
            return Wake::Go;
        m_buffer.resize(size_t(max) * kChannels);
        uint8_t *out = reinterpret_cast<uint8_t *>(m_buffer.data());
        const int n = swr_convert(av.swr, &out, max, f ? f->extended_data : nullptr, f ? f->nb_samples : 0);
        if (n <= 0)
            return Wake::Go;
        const int64_t from = std::min<int64_t>(skip, n);
        skip -= from;
        return put(m_buffer.data() + from * kChannels, n - from);
    };

    while (!end && av_read_frame(av.format, av.packet) >= 0) {
        if (av.packet->stream_index == av.stream && avcodec_send_packet(av.codec, av.packet) >= 0) {
            while (!end && avcodec_receive_frame(av.codec, av.frame) >= 0) {
                wake = convert(av.frame);
                if (wake != Wake::Go) {
                    av_packet_unref(av.packet);
                    return wake;
                }
            }
        }
        av_packet_unref(av.packet);
    }
    if (!end) {
        avcodec_send_packet(av.codec, nullptr);
        while (!end && avcodec_receive_frame(av.codec, av.frame) >= 0) {
            wake = convert(av.frame);
            if (wake != Wake::Go)
                return wake;
        }
        if (!end) {
            wake = convert(nullptr);
            if (wake != Wake::Go)
                return wake;
        }
    }
    return Wake::Go;
}

} // namespace dss
