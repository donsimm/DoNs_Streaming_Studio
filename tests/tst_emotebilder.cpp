// P14 Emotes em-5: Bilder dekodieren, Grenzen, gemeinsamer Takt (ohne Fenster, Plattform offscreen).
// Alle Testbilder macht der Test selbst: PNG und WebP mit QImageWriter, das GIF mit zwei Bildern
// von Hand nach GIF89a (keine fremden Emote-Bilder im Repo, Plan «Für jeden Abschnitt»).
#include "emoteimages.h"

#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <QRegularExpression>
#include <QTest>

using namespace dss;

namespace {

QByteArray encode(const QImage &image, const char *format)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, format);
    if (!writer.write(image))
        qFatal("Testbild %s nicht geschrieben: %s", format, qPrintable(writer.errorString()));
    return bytes;
}

QByteArray filled(int width, int height, Qt::GlobalColor color, const char *format = "png")
{
    QImage image(width, height, QImage::Format_ARGB32);
    image.fill(color);
    return encode(image, format);
}

// GIF89a mit zwei Bildern 112x56, Endlosschleife (NETSCAPE2.0): Bild 1 rot, 20 cs (200 ms),
// Bild 2 blau, 0 cs (gilt als 100 ms). Das LZW schreibt vor je zwei Pixeln einen Clear-Code,
// so bleibt die Codebreite bei 3 Bit und es braucht keine Tabelle.
// Quelle: https://www.w3.org/Graphics/GIF/spec-gif89a.txt (Abschnitte 18, 20, 22, 23, 24, Anhang F)
QByteArray twoFrameGif()
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

QRgb centre(const QImage *image)
{
    return image->pixel(image->width() / 2, image->height() / 2);
}

void expectReject(const char *reason)
{
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("^Emotes: Bild .* nicht angezeigt: ")
                                            + QRegularExpression::escape(QString::fromUtf8(reason))
                                            + QLatin1Char('$')));
}

} // namespace

class TestEmoteImages : public QObject
{
    Q_OBJECT

private slots:
    void webpAvailable()
    {
        // Das Paket qt6-image-formats-plugins steht in scripts/pakete.txt
        QCOMPARE(EmoteImages::webpAvailable(),
                 QImageReader::supportedImageFormats().contains("webp"));
        QVERIFY(EmoteImages::webpAvailable());
    }

    void pngScaledToHeight()
    {
        EmoteImages images;
        QVERIFY(images.add(QStringLiteral("p"), filled(56, 56, Qt::red)));
        QVERIFY(images.contains(QStringLiteral("p")));
        QCOMPARE(images.frameCount(QStringLiteral("p")), 1);
        QCOMPARE(images.duration(QStringLiteral("p")), qint64(0));
        QVERIFY(!images.hasAnimated());
        const QImage *image = images.frame(QStringLiteral("p"), 12345);
        QVERIFY(image);
        QCOMPARE(image->size(), QSize(kEmoteHeight, kEmoteHeight));
        QCOMPARE(centre(image), qRgb(255, 0, 0));
        QVERIFY(!images.frame(QStringLiteral("fehlt"), 0));
    }

    void widthFollowsAspectUpToFourTimesHeight()
    {
        EmoteImages images;
        QVERIFY(images.add(QStringLiteral("breit"), filled(84, 28, Qt::green)));
        QCOMPARE(images.frame(QStringLiteral("breit"), 0)->size(), QSize(84, 28));
        // 8:1 ist breiter als 4 × Höhe: Breite 112, Höhe nach Seitenverhältnis
        QVERIFY(images.add(QStringLiteral("sehr breit"), filled(224, 28, Qt::green)));
        QCOMPARE(images.frame(QStringLiteral("sehr breit"), 0)->size(), QSize(112, 14));
    }

    void gifTwoFramesSharedClock()
    {
        EmoteImages images;
        const QString key = QStringLiteral("g");
        QVERIFY(images.add(key, twoFrameGif()));
        QCOMPARE(images.frameCount(key), 2);
        QVERIFY(images.hasAnimated());
        // 200 ms rot, dann 0 cs → 100 ms blau
        QCOMPARE(images.duration(key), qint64(300));
        QCOMPARE(images.frame(key, 0)->size(), QSize(56, 28));
        const struct { qint64 at; QRgb color; } expected[] = {
            {0, qRgb(255, 0, 0)},   {199, qRgb(255, 0, 0)}, {200, qRgb(0, 0, 255)},
            {299, qRgb(0, 0, 255)}, {300, qRgb(255, 0, 0)}, {3250, qRgb(0, 0, 255)},
            {-5, qRgb(255, 0, 0)},
        };
        for (const auto &e : expected)
            QCOMPARE(centre(images.frame(key, e.at)), e.color);
        // Gleicher Zeitpunkt, gleiches Bild: eine Uhr für alle
        QVERIFY(images.add(QStringLiteral("g2"), twoFrameGif()));
        QCOMPARE(centre(images.frame(QStringLiteral("g2"), 3250)), centre(images.frame(key, 3250)));
    }

    void animationOffGivesFirstFrame()
    {
        EmoteImages images;
        QVERIFY(images.add(QStringLiteral("g"), twoFrameGif()));
        QCOMPARE(centre(images.frame(QStringLiteral("g"), 250, false)), qRgb(255, 0, 0));
    }

    void ownHeightAndLimitsPerImage()
    {
        // Vorbereitung em-9: GIFs im Chat mit eigener Höhe
        EmoteImages images;
        EmoteImageLimits limits;
        limits.height = 112;
        QVERIFY(images.add(QStringLiteral("gross"), twoFrameGif(), limits));
        QCOMPARE(images.frame(QStringLiteral("gross"), 0)->size(), QSize(224, 112));
        limits.maxFrames = 1;
        expectReject("zu viele Bilder in der Animation");
        QVERIFY(!images.add(QStringLiteral("einbild"), twoFrameGif(), limits));
        QVERIFY(!images.contains(QStringLiteral("einbild")));
    }

    void webpStatic()
    {
        EmoteImages images;
        QVERIFY(images.add(QStringLiteral("w"), filled(40, 20, Qt::blue, "webp")));
        QCOMPARE(images.frameCount(QStringLiteral("w")), 1);
        const QImage *image = images.frame(QStringLiteral("w"), 0);
        QCOMPARE(image->size(), QSize(56, 28));
        // WebP verlustbehaftet: nur ungefähr blau
        const QRgb pixel = centre(image);
        QVERIFY2(qBlue(pixel) > 200 && qRed(pixel) < 40 && qGreen(pixel) < 40,
                 qPrintable(QString::number(pixel, 16)));
    }

    void tooLargeRejected()
    {
        EmoteImages images;
        expectReject("Bild zu gross");
        QVERIFY(!images.add(QStringLiteral("breit"), filled(513, 10, Qt::red)));
        expectReject("Bild zu gross");
        QVERIFY(!images.add(QStringLiteral("hoch"), filled(10, 513, Qt::red)));
        QVERIFY(images.add(QStringLiteral("grenze"), filled(512, 512, Qt::red)));
        expectReject("zu viele Bytes");
        QVERIFY(!images.add(QStringLiteral("bytes"), QByteArray(kEmoteImageMaxBytes + 1, '\0')));
        QVERIFY(!images.contains(QStringLiteral("breit")));
        QVERIFY(!images.contains(QStringLiteral("bytes")));
    }

    void brokenBytes()
    {
        EmoteImages images;
        expectReject("keine Daten");
        QVERIFY(!images.add(QStringLiteral("leer"), QByteArray()));
        expectReject("Daten nicht lesbar");
        QVERIFY(!images.add(QStringLiteral("text"), QByteArrayLiteral("kein Bild")));
        expectReject("Daten nicht lesbar");
        QVERIFY(!images.add(QStringLiteral("halb"), filled(56, 56, Qt::red).left(40)));
        // Ein fehlgeschlagenes add ersetzt den alten Eintrag nicht durch etwas Halbes
        QVERIFY(images.add(QStringLiteral("p"), filled(28, 28, Qt::red)));
        expectReject("Daten nicht lesbar");
        QVERIFY(!images.add(QStringLiteral("p"), QByteArrayLiteral("kaputt")));
        QVERIFY(!images.contains(QStringLiteral("p")));
    }

    void memoryLimit()
    {
        // Ein Bild 28x28 kostet 28 × 28 × 4 Bytes; Platz für zwei
        constexpr qint64 one = kEmoteHeight * kEmoteHeight * 4;
        EmoteImages images(2 * one);
        QVERIFY(images.add(QStringLiteral("a"), filled(28, 28, Qt::red)));
        QVERIFY(images.add(QStringLiteral("b"), filled(28, 28, Qt::red)));
        QVERIFY(images.add(QStringLiteral("c"), filled(28, 28, Qt::red)));
        QVERIFY(!images.contains(QStringLiteral("a")));
        QVERIFY(images.contains(QStringLiteral("b")));
        QVERIFY(images.contains(QStringLiteral("c")));
        expectReject("grösser als der Speicher für Bilder");
        QVERIFY(!images.add(QStringLiteral("gross"), filled(112, 28, Qt::red)));
        images.clear();
        QVERIFY(!images.contains(QStringLiteral("c")));
        QVERIFY(!images.hasAnimated());
    }
};

QTEST_MAIN(TestEmoteImages)
#include "tst_emotebilder.moc"
