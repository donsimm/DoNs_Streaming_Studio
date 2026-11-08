// P11, Punkt 79: Farbumrechnung der Videos im Editor wie die Browserquelle in OBS (videocolor.h).
// Ohne Qt Gui (Regel 9).
// - choice: Regel aus Matrix, Bereich, Codec und Höhe (Tabelle).
// - fieldColors: Die Farbvideos aus tests/daten/bildvergleich/ (erzeugen.sh) mit dss::VideoPlayer
//   dekodieren und die Mitte jedes der 32 Farbfelder mit dem Modell vergleichen: Sollfarbe mit der
//   Matrix und dem Bereich der Kodierung (wie erzeugen.sh) nach YUV, ganzzahlig gerundet, mit der
//   erwarteten Matrix und dem erwarteten Bereich zurück nach RGB. Dazu die Gegenprobe, dass die
//   jeweils andere Matrix (BT.601 statt BT.709 und umgekehrt) mindestens das Doppelte der Toleranz
//   daneben läge, und dass der Spieler die erwartete Umrechnung meldet.
//
// Quellen: Formeln für Y'CbCr aus R'G'B' (Kr, Kb; eingeschränkt 16–235/16–240, voll 0–255):
//   ITU-R BT.601-7 https://www.itu.int/rec/R-REC-BT.601, ITU-R BT.709-6 https://www.itu.int/rec/R-REC-BT.709

#include "videocolor.h"
#include "videoplayer.h"

#include <QTest>

#include <algorithm>
#include <cmath>

using namespace dss;

namespace {

const QString kData = QStringLiteral(DSS_TEST_BILDVERGLEICH_DIR);

// Grösste erlaubte Abweichung eines Kanals vom Modell. Gemessen 2026-11-08 (Bericht P11): höchstens
// 3 bei allen Dateien, auch beim verlustfreien VP9, also aus dem ganzzahligen Rechnen von swscale
// beim Kodieren und Dekodieren; 4 lässt 1 Reserve. Die andere Matrix läge 32 bis 39 daneben.
constexpr int kTolerance = 4;

// Farben der Felder wie erzeugen.sh (RRGGBB), Zeile für Zeile
const unsigned kFields[32] = {
    0xFF0000, 0x00FF00, 0x0000FF, 0x00FFFF, 0xFF00FF, 0xFFFF00, 0xFFFFFF, 0x000000,
    0xBFBFBF, 0xBFBF00, 0x00BFBF, 0x00BF00, 0xBF00BF, 0xBF0000, 0x0000BF, 0x202020,
    0x000000, 0x242424, 0x494949, 0x6D6D6D, 0x929292, 0xB6B6B6, 0xDBDBDB, 0xFFFFFF,
    0xFFDBAC, 0xF1C27D, 0xE0AC69, 0xC68642, 0x8D5524, 0x5C3836, 0x3F7F3F, 0x3F3F7F};

struct Rgb
{
    int r = 0, g = 0, b = 0;
};

struct Coefficients
{
    double kr, kb;
};

Coefficients coefficients(VideoMatrix m)
{
    return m == VideoMatrix::Bt709 ? Coefficients{0.2126, 0.0722} : Coefficients{0.299, 0.114};
}

int clamp255(double v)
{
    return std::clamp(int(std::lround(v)), 0, 255);
}

// Sollfarbe → Y'CbCr (gerundet wie im Bitstrom) → R'G'B'
Rgb model(Rgb in, VideoMatrix encMatrix, bool encFull, VideoMatrix decMatrix, bool decFull)
{
    const Coefficients e = coefficients(encMatrix);
    const double r = in.r / 255.0, g = in.g / 255.0, b = in.b / 255.0;
    const double y = e.kr * r + (1 - e.kr - e.kb) * g + e.kb * b;
    const double pb = (b - y) / (2 * (1 - e.kb));
    const double pr = (r - y) / (2 * (1 - e.kr));
    const int qy = clamp255(encFull ? 255 * y : 16 + 219 * y);
    const int qu = clamp255(128 + (encFull ? 255 : 224) * pb);
    const int qv = clamp255(128 + (encFull ? 255 : 224) * pr);

    const Coefficients d = coefficients(decMatrix);
    const double Y = decFull ? qy / 255.0 : (qy - 16) / 219.0;
    const double Pb = (qu - 128) / (decFull ? 255.0 : 224.0);
    const double Pr = (qv - 128) / (decFull ? 255.0 : 224.0);
    const double R = Y + 2 * (1 - d.kr) * Pr;
    const double B = Y + 2 * (1 - d.kb) * Pb;
    const double G = (Y - d.kr * R - d.kb * B) / (1 - d.kr - d.kb);
    return {clamp255(255 * R), clamp255(255 * G), clamp255(255 * B)};
}

int distance(Rgb a, Rgb b)
{
    return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
}

// Mittel über 5 x 5 Pixel um (x, y) im Bild BGRA (deckend)
Rgb sample(const VideoFrame &f, int x, int y)
{
    int r = 0, g = 0, b = 0, n = 0;
    for (int yy = y - 2; yy <= y + 2; ++yy) {
        for (int xx = x - 2; xx <= x + 2; ++xx) {
            const uint8_t *p = f.data.data() + size_t(yy) * size_t(f.stride) + 4 * size_t(xx);
            b += p[0];
            g += p[1];
            r += p[2];
            ++n;
        }
    }
    return {(r + n / 2) / n, (g + n / 2) / n, (b + n / 2) / n};
}

QString text(Rgb c)
{
    return QStringLiteral("%1/%2/%3").arg(c.r).arg(c.g).arg(c.b);
}

} // namespace

Q_DECLARE_METATYPE(dss::VideoMatrix)

class TestVideofarbe : public QObject
{
    Q_OBJECT

private slots:
    void choice_data()
    {
        QTest::addColumn<int>("matrix");
        QTest::addColumn<int>("range");
        QTest::addColumn<bool>("heightRule");
        QTest::addColumn<int>("height");
        QTest::addColumn<VideoMatrix>("expected");
        QTest::addColumn<bool>("full");
        // ohne Angabe: Höhe entscheidet (H.264 …), nicht bei VP9/AV1
        QTest::newRow("ohne 360") << 2 << 0 << true << 360 << VideoMatrix::Bt601 << false;
        QTest::newRow("ohne 718") << 2 << 0 << true << 718 << VideoMatrix::Bt601 << false;
        QTest::newRow("ohne 719") << 2 << 0 << true << 719 << VideoMatrix::Bt601 << false;
        QTest::newRow("ohne 720") << 2 << 0 << true << 720 << VideoMatrix::Bt709 << false;
        QTest::newRow("ohne 1080") << 2 << 0 << true << 1080 << VideoMatrix::Bt709 << false;
        QTest::newRow("VP9/AV1 ohne 360") << 2 << 0 << false << 360 << VideoMatrix::Bt601 << false;
        QTest::newRow("VP9/AV1 ohne 1080") << 2 << 0 << false << 1080 << VideoMatrix::Bt601 << false;
        // mit Angabe gilt die Angabe, auch gegen die Höhe
        QTest::newRow("BT.709 360") << 1 << 0 << true << 360 << VideoMatrix::Bt709 << false;
        QTest::newRow("VP9 BT.709 1080") << 1 << 1 << false << 1080 << VideoMatrix::Bt709 << false;
        QTest::newRow("SMPTE170M 1080") << 6 << 1 << true << 1080 << VideoMatrix::Bt601 << false;
        QTest::newRow("BT.470BG 1080") << 5 << 0 << true << 1080 << VideoMatrix::Bt601 << false;
        QTest::newRow("FCC") << 4 << 0 << true << 1080 << VideoMatrix::Fcc << false;
        QTest::newRow("SMPTE240M") << 7 << 0 << true << 1080 << VideoMatrix::Smpte240m << false;
        QTest::newRow("BT.2020 NCL") << 9 << 0 << true << 2160 << VideoMatrix::Bt2020 << false;
        QTest::newRow("BT.2020 CL") << 10 << 0 << true << 2160 << VideoMatrix::Bt2020 << false;
        // unbekannte oder nicht umrechenbare Angaben wie «keine Angabe»
        QTest::newRow("RGB 1080") << 0 << 0 << true << 1080 << VideoMatrix::Bt709 << false;
        QTest::newRow("reserviert 360") << 3 << 0 << true << 360 << VideoMatrix::Bt601 << false;
        QTest::newRow("YCgCo 1080") << 8 << 0 << true << 1080 << VideoMatrix::Bt709 << false;
        QTest::newRow("14 1080") << 14 << 0 << true << 1080 << VideoMatrix::Bt709 << false;
        // Bereich: voll nur mit Angabe «voll»
        QTest::newRow("voll") << 1 << 2 << true << 1080 << VideoMatrix::Bt709 << true;
        QTest::newRow("voll ohne Matrix 360") << 2 << 2 << true << 360 << VideoMatrix::Bt601 << true;
        QTest::newRow("eingeschränkt") << 1 << 1 << true << 1080 << VideoMatrix::Bt709 << false;
        QTest::newRow("Bereich unbekannt") << 1 << 3 << true << 1080 << VideoMatrix::Bt709 << false;
    }

    void choice()
    {
        QFETCH(int, matrix);
        QFETCH(int, range);
        QFETCH(bool, heightRule);
        QFETCH(int, height);
        QFETCH(VideoMatrix, expected);
        QFETCH(bool, full);
        const VideoColor c = chooseVideoColor({matrix, range, heightRule, height});
        QCOMPARE(videoMatrixName(c.matrix), videoMatrixName(expected));
        QCOMPARE(c.fullRange, full);
    }

    void fieldColors_data()
    {
        QTest::addColumn<QString>("file");
        QTest::addColumn<QSize>("size");
        QTest::addColumn<VideoMatrix>("encMatrix"); // wie erzeugen.sh
        QTest::addColumn<bool>("encFull");
        QTest::addColumn<VideoMatrix>("decMatrix"); // Regel von videocolor.h
        QTest::addColumn<bool>("decFull");
        const auto row = [](const char *file, QSize size, VideoMatrix enc, bool encFull, VideoMatrix dec, bool decFull) {
            QTest::newRow(file) << QString::fromLatin1(file) << size << enc << encFull << dec << decFull;
        };
        const VideoMatrix b601 = VideoMatrix::Bt601, b709 = VideoMatrix::Bt709;
        row("h264-640x360-ohne.mp4", QSize(640, 360), b601, false, b601, false);
        row("h264-1280x718-ohne.mp4", QSize(1280, 718), b601, false, b601, false);
        row("h264-1280x720-ohne.mp4", QSize(1280, 720), b709, false, b709, false);
        row("h264-1920x1080-ohne.mp4", QSize(1920, 1080), b709, false, b709, false);
        row("h264-1920x1080-bt709.mp4", QSize(1920, 1080), b709, false, b709, false);
        row("h264-1920x1080-bt601.mp4", QSize(1920, 1080), b601, false, b601, false);
        row("h264-1920x1080-voll.mp4", QSize(1920, 1080), b709, true, b709, true);
        row("vp9-640x360-ohne.webm", QSize(640, 360), b601, false, b601, false);
        // Kodiert mit BT.709, ohne Angabe: OBS zeigt es wie BT.601 (Prüfung V), der Editor auch.
        row("vp9-1920x1080-ohne.webm", QSize(1920, 1080), b709, false, b601, false);
        row("vp9-1920x1080-bt709.webm", QSize(1920, 1080), b709, false, b709, false);
    }

    void fieldColors()
    {
        QFETCH(QString, file);
        QFETCH(QSize, size);
        QFETCH(VideoMatrix, encMatrix);
        QFETCH(bool, encFull);
        QFETCH(VideoMatrix, decMatrix);
        QFETCH(bool, decFull);

        VideoPlayer player(kData + QLatin1Char('/') + file, false, true);
        QVERIFY(QTest::qWaitFor([&] {
            player.currentFrame();
            return player.state() == VideoPlayer::State::Holding || player.state() == VideoPlayer::State::Failed;
        }, 10000));
        QCOMPARE(player.state(), VideoPlayer::State::Holding);
        const std::shared_ptr<const VideoFrame> frame = player.currentFrame();
        QVERIFY(frame);
        QCOMPARE(QSize(frame->width, frame->height), size);
        const VideoColor reported = player.statistics().color;
        QCOMPARE(videoMatrixName(reported.matrix), videoMatrixName(decMatrix));
        QCOMPARE(reported.fullRange, decFull);

        const VideoMatrix other = decMatrix == VideoMatrix::Bt709 ? VideoMatrix::Bt601 : VideoMatrix::Bt709;
        const int fw = size.width() / 16 * 2;
        const int fh = size.height() / 8 * 2;
        int worst = 0;
        int otherWorst = 0;
        QString worstText;
        for (int i = 0; i < 32; ++i) {
            const Rgb soll{int(kFields[i] >> 16), int((kFields[i] >> 8) & 0xFF), int(kFields[i] & 0xFF)};
            const Rgb expected = model(soll, encMatrix, encFull, decMatrix, decFull);
            const Rgb got = sample(*frame, (i % 8) * fw + fw / 2, (i / 8) * fh + fh / 2);
            const int d = distance(got, expected);
            if (d > worst) {
                worst = d;
                worstText = QStringLiteral("Feld %1 (Soll %2): Modell %3, Editor %4")
                                .arg(i + 1).arg(text(soll), text(expected), text(got));
            }
            otherWorst = std::max(otherWorst, distance(expected, model(soll, encMatrix, encFull, other, decFull)));
        }
        qInfo("%s: %s %s, grösste Abweichung vom Modell %d%s; mit %s wären es %d", qPrintable(file),
              videoMatrixName(decMatrix), decFull ? "voll" : "eingeschränkt", worst,
              worstText.isEmpty() ? "" : qPrintable(QStringLiteral(" (") + worstText + QLatin1Char(')')),
              videoMatrixName(other), otherWorst);
        QVERIFY2(worst <= kTolerance, qPrintable(worstText));
        QVERIFY2(otherWorst >= 2 * kTolerance, "andere Matrix liesse sich nicht unterscheiden");
    }
};

QTEST_GUILESS_MAIN(TestVideofarbe)

#include "tst_videofarbe.moc"
