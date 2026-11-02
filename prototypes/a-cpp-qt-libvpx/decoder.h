// Dekodierweg für M1 (Prompt P02): WebM über libavformat/libavcodec in einem eigenen Thread.
// Ohne Qt-Oberfläche; liefert fertige Bilder als QImage (Format_ARGB32_Premultiplied) über
// einen Rückruf, der im Decoder-Thread läuft.
#pragma once

#include <QImage>
#include <QString>

#include <atomic>
#include <functional>
#include <thread>

class VideoDecoder
{
public:
    struct Info
    {
        QString decoder;      // Name des gewählten Decoders, z. B. "libvpx-vp9"
        QString pixFmt;       // Pixelformat der dekodierten Bilder, z. B. "yuva420p"
        double firstAlphaMean = -1; // Mittel des Alphakanals im ersten Bild (0..255), vor dem Vormultiplizieren
        QString error;
    };

    using FrameCallback = std::function<void(const QImage &)>;
    using InfoCallback = std::function<void(const Info &)>;

    // decoderName leer = Standard-Decoder von FFmpeg für den Codec des Stroms.
    VideoDecoder(const QString &file, const QString &decoderName, FrameCallback onFrame, InfoCallback onInfo);
    ~VideoDecoder();

private:
    void run();

    QString m_file, m_decoderName;
    FrameCallback m_onFrame;
    InfoCallback m_onInfo;
    std::atomic<bool> m_stop{false};
    std::thread m_thread;
};
