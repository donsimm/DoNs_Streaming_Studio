#pragma once

// P14 Emotes: Testbilder für tst_emotebilder und tst_fensterchatbot. Der Test macht sie selbst:
// PNG und WebP mit QImageWriter, das GIF mit zwei Bildern von Hand nach GIF89a (keine fremden
// Emote-Bilder im Repo, Plan «Für jeden Abschnitt»).

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageWriter>
#include <QList>

inline QByteArray encode(const QImage &image, const char *format)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    if (!writer.write(image))
        qFatal("Testbild %s nicht geschrieben: %s", format, qPrintable(writer.errorString()));
    return bytes;
}

inline QByteArray filled(int width, int height, Qt::GlobalColor color, const char *format = "png")
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(color);
    return encode(image, format);
}

// GIF89a mit zwei Bildern 112x56, Endlosschleife (NETSCAPE2.0): Bild 1 rot, 20 cs (200 ms),
// Bild 2 blau, 0 cs (gilt als 100 ms). Das LZW schreibt vor je zwei Pixeln einen Clear-Code,
// so bleibt die Codebreite bei 3 Bit und es braucht keine Tabelle.
// Quelle: https://www.w3.org/Graphics/GIF/spec-gif89a.txt (Abschnitte 18, 20, 22, 23, 24, Anhang F)
inline QByteArray twoFrameGif()
{
    constexpr int width = 112;
    constexpr int height = 56;
    constexpr int minCodeSize = 2;
    constexpr int clearCode = 1 << minCodeSize;
    constexpr int endCode = clearCode + 1;
    constexpr int codeBits = minCodeSize + 1;
    auto le16 = [](QByteArray &out, int value) {
        out.append(char(value & 0xFF));
        out.append(char((value >> 8) & 0xFF));
    };
    auto frame = [&](QByteArray &out, int colorIndex, int delayCs) {
        out.append("\x21\xF9\x04\x00", 4);
        le16(out, delayCs);
        out.append("\x00\x00", 2);
        out.append('\x2C');
        le16(out, 0);
        le16(out, 0);
        le16(out, width);
        le16(out, height);
        out.append('\x00');
        QList<int> codes;
        for (int i = 0; i < width * height; i += 2)
            codes << clearCode << colorIndex << colorIndex;
        codes << endCode;
        QByteArray data;
        quint32 acc = 0;
        int bits = 0;
        for (int code : std::as_const(codes)) {
            acc |= quint32(code) << bits;
            bits += codeBits;
            while (bits >= 8) {
                data.append(char(acc & 0xFF));
                acc >>= 8;
                bits -= 8;
            }
        }
        if (bits > 0)
            data.append(char(acc & 0xFF));
        out.append(char(minCodeSize));
        for (qsizetype i = 0; i < data.size(); i += 255) {
            const QByteArray chunk = data.mid(i, 255);
            out.append(char(chunk.size()));
            out.append(chunk);
        }
        out.append('\x00');
    };

    QByteArray gif("GIF89a");
    le16(gif, width);
    le16(gif, height);
    gif.append("\x81\x00\x00", 3); // globale Farbtabelle mit 4 Einträgen
    gif.append("\xFF\x00\x00" "\x00\x00\xFF" "\x00\xFF\x00" "\x00\x00\x00", 12);
    gif.append("\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00", 19);
    frame(gif, 0, 20);
    frame(gif, 1, 0);
    gif.append('\x3B');
    return gif;
}
