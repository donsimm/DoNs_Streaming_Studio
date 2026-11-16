// P14 Emotes em-5: Bilder dekodieren, Grenzen, gemeinsamer Takt (ohne Fenster, Plattform offscreen).
// Alle Testbilder macht der Test selbst: PNG und WebP mit QImageWriter, das GIF mit zwei Bildern
// von Hand nach GIF89a (keine fremden Emote-Bilder im Repo, Plan «Für jeden Abschnitt»).
#include "emoteimages.h"
#include "emotetestbilder.h"

#include <QBuffer>
#include <QImageReader>
#include <QImageWriter>
#include <QRegularExpression>
#include <QTest>

using namespace dss;

namespace {

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
