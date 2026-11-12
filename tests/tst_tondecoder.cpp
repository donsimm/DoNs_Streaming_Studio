// P12b b2: Ton von Videos dekodieren (dss::AudioDecoder) in den Ring für den Mischer
// (dss::AudioRing), ohne Qt Gui und ohne Gerät. Daten: tests/daten/ton/ (P12a, wie im Handtest).
// - formats: MP4/AAC, WebM/Opus, Mono, 44,1 kHz und 5.1 ergeben float, 48 kHz, Stereo; 30 Pieps an
//   der Lage aus den Zeitstempeln (alle 2 s ab 0, Opus in WebM trotz Beginn bei −7 ms); Länge
//   60 s; Pieps auf beiden Kanälen, bei Mono beide Kanäle gleich; danach Zustand «Ended».
// - noAudio: Video ohne Ton schreibt nichts, Zustand «NoAudio».
// - missingFile: Datei fehlt, Zustand «Failed».
// - loopLength: mit loop und der Länge, die die Seite meldet, Durchgänge genau dieser Länge, jeder
//   von vorne, Nummern steigend.
// - restart: Neubeginn mitten im Durchgang, neuer Durchgang von vorne mit der Nummer aus restart().
// - stopWhileFull: Zerstören bei vollem Ring endet sofort.

#include "audiodecoder.h"
#include "audioring.h"

#include <QElapsedTimer>
#include <QTest>
#include <QThread>

#include <cmath>
#include <vector>

using namespace dss;

namespace {

const QString kTonDir = QStringLiteral(DSS_TEST_TON_DIR);

QString ton(const char *name)
{
    return kTonDir + QLatin1Char('/') + QLatin1String(name);
}

constexpr int64_t kRate = AudioDecoder::kRate;
constexpr int64_t kPiepAbstand = 2 * kRate; // alle 2 s (LIESMICH.md)
constexpr int kPieps = 30;
constexpr int64_t kLaenge = 60 * kRate;
// Grösste erlaubte Abweichung eines Einsatzes vom Raster, in Rahmen. Gemessen 2026-11-12: höchstens
// 3 Rahmen bei allen Dateien (der Sinus braucht 1,5 bis 2,2 Rahmen bis zur Schwelle, Mono hat die
// kleinere Amplitude); 6 Rahmen (0,125 ms) lassen Reserve und sind klein gegen 1 ms.
constexpr int64_t kOnsetTolerance = 6;

struct Pass
{
    uint32_t pass = 0;
    std::vector<float> frames; // verschränkt L R
    int64_t count() const { return int64_t(frames.size() / AudioRing::kChannels); }
};

// Liest Durchgänge aus dem Ring wie der Mischer (Marken an der Lesestelle). Endet, wenn
// `passes` Durchgänge vollständig sind (der nächste hat begonnen), wenn der Decoder fertig ist
// und der Ring leer, oder nach `timeoutMs`.
std::vector<Pass> readPasses(AudioRing &ring, const AudioDecoder &decoder, int passes, int timeoutMs = 20000)
{
    std::vector<Pass> result;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        AudioRing::Mark mark;
        if (ring.frontMark(&mark) && mark.frame == ring.readPosition()) {
            ring.popMark();
            if (int(result.size()) == passes)
                return result;
            result.push_back(Pass{mark.pass, {}});
            continue;
        }
        const AudioDecoder::State state = decoder.state(); // vor readable(), siehe Ordnung
        size_t n = ring.readable();
        if (n > 0 && ring.frontMark(&mark))
            n = std::min<size_t>(n, size_t(mark.frame - ring.readPosition()));
        if (n == 0) {
            if (state != AudioDecoder::State::Running && state != AudioDecoder::State::Opening)
                return result;
            QThread::msleep(1);
            continue;
        }
        if (result.empty())
            return result; // Proben ohne Marke: Fehler, der Aufrufer prüft
        for (size_t i = 0; i < n; ++i) {
            const float *f = ring.frame(i);
            result.back().frames.insert(result.back().frames.end(), f, f + AudioRing::kChannels);
        }
        ring.consume(n);
    }
    return result;
}

// Einsätze: erster Rahmen über der Schwelle nach mindestens einer halben Sekunde Ruhe; channel -1
// = grösster Betrag beider Kanäle.
std::vector<int64_t> onsets(const Pass &p, int channel = -1, float threshold = 0.1f)
{
    std::vector<int64_t> result;
    const int64_t quiet = kRate / 2;
    int64_t still = quiet;
    for (int64_t i = 0; i < p.count(); ++i) {
        const float l = std::fabs(p.frames[2 * i]);
        const float r = std::fabs(p.frames[2 * i + 1]);
        const float m = channel == 0 ? l : channel == 1 ? r : std::max(l, r);
        if (m > threshold) {
            if (still >= quiet)
                result.push_back(i);
            still = 0;
        } else {
            ++still;
        }
    }
    return result;
}

// Grösste Abweichung der Einsätze vom Raster k * 2 s; -1, wenn die Anzahl nicht stimmt.
int64_t maxOnsetError(const std::vector<int64_t> &found, int expected)
{
    if (int(found.size()) != expected)
        return -1;
    int64_t worst = 0;
    for (int k = 0; k < expected; ++k)
        worst = std::max(worst, std::abs(found[k] - k * kPiepAbstand));
    return worst;
}

} // namespace

class TstTonDecoder : public QObject
{
    Q_OBJECT

private slots:
    void formats_data();
    void formats();
    void noAudio();
    void missingFile();
    void loopLength();
    void restart();
    void stopWhileFull();
};

void TstTonDecoder::formats_data()
{
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("sourceRate");
    QTest::addColumn<int>("sourceChannels");
    QTest::addColumn<QString>("codec");
    // Mono: beide Kanäle genau gleich (libswresample verteilt den einen Kanal). Sonst nur: Pieps auf
    // beiden Kanälen; AAC kodiert die Kanäle verlustbehaftet verschieden (gemessen 2026-11-12:
    // grösster Unterschied L−R 0,0014 bei Stereo, 0,15 bei 5.1 nach dem Abmischen).
    QTest::addColumn<bool>("identicalChannels");

    QTest::newRow("mp4 aac stereo") << ton("blitz-piep.mp4") << 48000 << 2 << "aac" << false;
    QTest::newRow("webm opus") << ton("blitz-piep.webm") << 48000 << 2 << "opus" << false;
    QTest::newRow("mono") << ton("blitz-piep-mono.mp4") << 48000 << 1 << "aac" << true;
    QTest::newRow("44100") << ton("blitz-piep-44100.mp4") << 44100 << 2 << "aac" << false;
    QTest::newRow("5.1") << ton("blitz-piep-5.1.mp4") << 48000 << 6 << "aac" << false;
}

void TstTonDecoder::formats()
{
    QFETCH(QString, file);
    QFETCH(int, sourceRate);
    QFETCH(int, sourceChannels);
    QFETCH(QString, codec);
    QFETCH(bool, identicalChannels);

    AudioRing ring(1 << 15); // kleiner als die Datei: der Decoder wartet auf Platz
    AudioDecoder decoder(file, &ring, false);
    const std::vector<Pass> passes = readPasses(ring, decoder, 1);
    QCOMPARE(decoder.state(), AudioDecoder::State::Ended);
    QCOMPARE(int(passes.size()), 1);
    QCOMPARE(passes[0].pass, decoder.firstPass());
    QCOMPARE(decoder.info().sourceRate, sourceRate);
    QCOMPARE(decoder.info().sourceChannels, sourceChannels);
    QCOMPARE(decoder.info().codec, codec);
    QCOMPARE(decoder.statistics().passes, 1u);

    const Pass &p = passes[0];
    qInfo("%s: %lld Rahmen", qPrintable(file.section(QLatin1Char('/'), -1)), (long long)p.count());
    // Länge: 60 s; der letzte Block kann über das Ende hinausreichen (AAC 1024, Opus 960 Proben).
    QVERIFY2(std::abs(p.count() - kLaenge) <= 1024, qPrintable(QString::number(p.count())));

    const std::vector<int64_t> found = onsets(p);
    const int64_t error = maxOnsetError(found, kPieps);
    qInfo("  Einsätze: %d, grösste Abweichung vom Raster %lld Rahmen", int(found.size()), (long long)error);
    QVERIFY2(error >= 0, "Anzahl der Pieps stimmt nicht");
    QVERIFY2(error <= kOnsetTolerance, qPrintable(QString::number(error)));

    for (int channel = 0; channel < AudioRing::kChannels; ++channel) {
        const int64_t e = maxOnsetError(onsets(p, channel), kPieps);
        QVERIFY2(e >= 0 && e <= kOnsetTolerance, qPrintable(QStringLiteral("Kanal %1: %2").arg(channel).arg(e)));
    }
    if (identicalChannels) {
        float worst = 0.0f;
        for (int64_t i = 0; i < p.count(); ++i)
            worst = std::max(worst, std::fabs(p.frames[2 * i] - p.frames[2 * i + 1]));
        QCOMPARE(worst, 0.0f);
    }
    QCOMPARE(ring.readable(), size_t(0));
}

void TstTonDecoder::noAudio()
{
    AudioRing ring(4096);
    AudioDecoder decoder(ton("blitz-ohne-ton.mp4"), &ring, true);
    QTRY_COMPARE_WITH_TIMEOUT(decoder.state(), AudioDecoder::State::NoAudio, 5000);
    QCOMPARE(ring.readable(), size_t(0));
    AudioRing::Mark mark;
    QVERIFY(!ring.frontMark(&mark));
    QCOMPARE(decoder.statistics().frames, uint64_t(0));
}

void TstTonDecoder::missingFile()
{
    AudioRing ring(4096);
    AudioDecoder decoder(ton("fehlt.mp4"), &ring, true);
    QTRY_COMPARE_WITH_TIMEOUT(decoder.state(), AudioDecoder::State::Failed, 5000);
    QVERIFY(!decoder.error().isEmpty());
    QCOMPARE(ring.readable(), size_t(0));
}

void TstTonDecoder::loopLength()
{
    // Video 5 s lang gemeldet: Durchgänge zu 5 s, darin die Pieps bei 0, 2 und 4 s.
    constexpr int64_t length = 5 * kRate;
    AudioRing ring(1 << 14);
    AudioDecoder decoder(ton("blitz-piep.webm"), &ring, true);
    decoder.setLoopLength(length);
    const std::vector<Pass> passes = readPasses(ring, decoder, 3);
    QCOMPARE(int(passes.size()), 3);
    for (size_t i = 0; i < passes.size(); ++i) {
        QCOMPARE(passes[i].count(), length);
        const int64_t error = maxOnsetError(onsets(passes[i]), 3);
        QVERIFY2(error >= 0 && error <= kOnsetTolerance, qPrintable(QString::number(error)));
        if (i > 0)
            QVERIFY(passes[i].pass > passes[i - 1].pass);
    }
    QCOMPARE(passes[0].pass, decoder.firstPass());
}

void TstTonDecoder::restart()
{
    AudioRing ring(1 << 14);
    AudioDecoder decoder(ton("blitz-piep.mp4"), &ring, false);
    // Ein Stück des ersten Durchgangs lesen (über den ersten Piep hinaus)
    QElapsedTimer timer;
    timer.start();
    int64_t read = 0;
    AudioRing::Mark mark;
    QTRY_VERIFY_WITH_TIMEOUT(ring.frontMark(&mark), 5000);
    QCOMPARE(mark.pass, decoder.firstPass());
    ring.popMark();
    while (read < kRate && timer.elapsed() < 5000) {
        const size_t n = ring.readable();
        ring.consume(n);
        read += int64_t(n);
        QThread::msleep(1);
    }
    QVERIFY(read >= kRate);

    const uint32_t pass = decoder.restart();
    QVERIFY(pass > decoder.firstPass());
    // Rest des ersten Durchgangs verwerfen, wie der Mischer: bis zur Marke des neuen Durchgangs.
    timer.restart();
    bool found = false;
    while (!found && timer.elapsed() < 5000) {
        if (ring.frontMark(&mark)) {
            const size_t skip = size_t(mark.frame - ring.readPosition());
            if (ring.readable() >= skip) {
                ring.consume(skip);
                QCOMPARE(mark.pass, pass);
                found = true;
                break;
            }
        }
        ring.consume(ring.readable());
        QThread::msleep(1);
    }
    QVERIFY(found);
    // Ab der Marke: von vorne, Pieps bei 0, 2, 4 s
    std::vector<Pass> rest = readPasses(ring, decoder, 1, 20000);
    QCOMPARE(int(rest.size()), 1);
    QCOMPARE(rest[0].pass, pass);
    const int64_t error = maxOnsetError(onsets(rest[0]), kPieps);
    QVERIFY2(error >= 0 && error <= kOnsetTolerance, qPrintable(QString::number(error)));
    QCOMPARE(decoder.statistics().passes, 2u);
}

void TstTonDecoder::stopWhileFull()
{
    AudioRing ring(4096);
    QElapsedTimer timer;
    {
        AudioDecoder decoder(ton("blitz-piep.mp4"), &ring, true);
        QTRY_COMPARE_WITH_TIMEOUT(ring.writable(), size_t(0), 5000);
        timer.start();
    }
    QVERIFY2(timer.elapsed() < 500, qPrintable(QString::number(timer.elapsed())));
    QCOMPARE(ring.readable(), ring.capacity());
}

QTEST_GUILESS_MAIN(TstTonDecoder)
#include "tst_tondecoder.moc"
