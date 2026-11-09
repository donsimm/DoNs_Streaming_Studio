// Quellen: https://doc.qt.io/qt-6.8/qaudiosink.html (start(QIODevice*) holt die Daten selbst,
// setBufferSize nur vor start(), bufferSize/bytesFree, processedUSecs, elapsedUSecs, stop leert den
// Puffer unter Linux synchron), https://doc.qt.io/qt-6.8/qaudiosource.html,
// https://doc.qt.io/qt-6.8/qmediadevices.html, https://doc.qt.io/qt-6.8/qtmultimedia-linux.html
// (Qt 6.8 braucht unter Linux PulseAudio; hier pipewire-pulse).
#include "klickmessung.h"

#include "auswertung.h"
#include "zeit.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QAudioSource>
#include <QCoreApplication>
#include <QFile>
#include <QIODevice>
#include <QMediaDevices>
#include <QThread>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <limits>
#include <mutex>

namespace {

QAudioFormat format()
{
    QAudioFormat f;
    f.setSampleRate(kRate);
    f.setChannelCount(kKanaele);
    f.setSampleFormat(QAudioFormat::Float);
    return f;
}

constexpr qint64 kBytesJeFrame = qint64(sizeof(float)) * kKanaele;
constexpr int kKlickProben = 48; // 1 ms
constexpr float kKlickPegel = 0.5f;

const char *zustandName(QtAudio::State s)
{
    switch (s) {
    case QtAudio::ActiveState: return "aktiv";
    case QtAudio::SuspendedState: return "angehalten";
    case QtAudio::StoppedState: return "gestoppt";
    case QtAudio::IdleState: return "leer";
    }
    return "?";
}

const char *fehlerName(QtAudio::Error e)
{
    switch (e) {
    case QtAudio::NoError: return "kein";
    case QtAudio::OpenError: return "Öffnen";
    case QtAudio::IOError: return "E/A";
    case QtAudio::UnderrunError: return "Unterlauf";
    case QtAudio::FatalError: return "schwer";
    }
    return "?";
}

QAudioDevice suchen(const QList<QAudioDevice> &liste, const QAudioDevice &standard, const QString &teil)
{
    if (teil.isEmpty())
        return standard;
    for (const QAudioDevice &d : liste)
        if (QString::fromUtf8(d.id()) == teil)
            return d;
    for (const QAudioDevice &d : liste)
        if (QString::fromUtf8(d.id()).contains(teil) || d.description().contains(teil))
            return d;
    return {};
}

// Zugmodus: QAudioSink holt die Proben hier ab. Klicks (1 ms, halber Pegel) an festen Probenlagen.
// stille = true: immer nur Nullen (Kurzprobe auf echtem Ausgang).
class KlickGenerator : public QIODevice
{
public:
    struct Aufruf
    {
        int64_t ns;
        qint64 lage; // erste Probe dieses Aufrufs
        qint64 frames;
        qint64 maxBytes; // so viel wollte QAudioSink (≈ freier Platz)
    };

    KlickGenerator(bool stille, qint64 abstand) : m_stille(stille), m_abstand(abstand) {}

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override
    {
        return std::numeric_limits<qint32>::max() + QIODevice::bytesAvailable();
    }

    std::vector<Aufruf> aufrufe;

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const int64_t ns = steadyNs();
        const qint64 frames = maxlen / kBytesJeFrame;
        float *p = reinterpret_cast<float *>(data);
        for (qint64 i = 0; i < frames; ++i) {
            float v = 0.0f;
            if (!m_stille && (m_lage + i) % m_abstand < kKlickProben)
                v = kKlickPegel;
            p[2 * i] = v;
            p[2 * i + 1] = v;
        }
        aufrufe.push_back({ns, m_lage, frames, maxlen});
        m_lage += frames;
        return frames * kBytesJeFrame;
    }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    const bool m_stille;
    const qint64 m_abstand;
    qint64 m_lage = 0;
};

struct Zustand
{
    int64_t ns;
    QtAudio::State zustand;
    QtAudio::Error fehler;
};

// Aufnahme im eigenen Thread: Einsätze (Betrag > 0,1 nach 250 ms Ruhe) mit Probenlage und Zeit
struct Aufnahme
{
    struct Einsatz
    {
        qint64 lage;
        int64_t ns;
    };
    QAudioSource *quelle = nullptr;
    QIODevice *io = nullptr;
    QByteArray rest;
    qint64 frames = 0;
    qint64 ruhe = kRate;
    std::vector<Einsatz> einsaetze;
    std::vector<Zustand> zustaende;
    int64_t ersteDatenNs = 0;

    void lesen()
    {
        const int64_t ns = steadyNs();
        rest += io->readAll();
        const qint64 n = rest.size() / kBytesJeFrame;
        if (n == 0)
            return;
        if (ersteDatenNs == 0)
            ersteDatenNs = ns;
        const float *p = reinterpret_cast<const float *>(rest.constData());
        for (qint64 i = 0; i < n; ++i) {
            const float m = std::max(std::fabs(p[2 * i]), std::fabs(p[2 * i + 1]));
            if (m > 0.1f) {
                if (ruhe >= kRate / 4)
                    einsaetze.push_back({frames + i, ns - int64_t((n - i) * 1e9 / kRate)});
                ruhe = 0;
            } else {
                ++ruhe;
            }
        }
        frames += n;
        rest.remove(0, n * kBytesJeFrame);
    }
};

void zeile(QFile &f, const QString &s)
{
    f.write(s.toUtf8());
    f.write("\n");
}

} // namespace

QString geraeteListe()
{
    QString t;
    auto geraet = [&](const QAudioDevice &d, bool standard) {
        const QAudioFormat f = d.preferredFormat();
        t += QStringLiteral("  %1 «%2»%3, bevorzugt %4 Hz %5 Kanäle, Raten %6–%7, Kanäle %8–%9, "
                            "float 48 kHz Stereo: %10\n")
                 .arg(QString::fromUtf8(d.id()), d.description(),
                      standard ? QStringLiteral(" [Standard]") : QString())
                 .arg(f.sampleRate())
                 .arg(f.channelCount())
                 .arg(d.minimumSampleRate())
                 .arg(d.maximumSampleRate())
                 .arg(d.minimumChannelCount())
                 .arg(d.maximumChannelCount())
                 .arg(d.isFormatSupported(format()) ? QStringLiteral("ja") : QStringLiteral("nein"));
    };
    const QAudioDevice so = QMediaDevices::defaultAudioOutput();
    const QAudioDevice si = QMediaDevices::defaultAudioInput();
    t += QStringLiteral("Ausgänge (QMediaDevices::audioOutputs):\n");
    for (const QAudioDevice &d : QMediaDevices::audioOutputs())
        geraet(d, d == so);
    t += QStringLiteral("Eingänge (QMediaDevices::audioInputs):\n");
    for (const QAudioDevice &d : QMediaDevices::audioInputs())
        geraet(d, d == si);
    return t;
}

QAudioDevice ausgangSuchen(const QString &teil)
{
    return suchen(QMediaDevices::audioOutputs(), QMediaDevices::defaultAudioOutput(), teil);
}

QAudioDevice eingangSuchen(const QString &teil)
{
    return suchen(QMediaDevices::audioInputs(), QMediaDevices::defaultAudioInput(), teil);
}

int klickMessung(const KlickOptionen &o)
{
    const QAudioDevice aus = ausgangSuchen(o.ausgang);
    const QAudioDevice ein = eingangSuchen(o.eingang);
    if (aus.isNull() || ein.isNull()) {
        std::fprintf(stderr, "Gerät nicht gefunden (Ausgang «%s»: %s, Eingang «%s»: %s)\n%s",
                     qPrintable(o.ausgang), aus.isNull() ? "fehlt" : "ok", qPrintable(o.eingang),
                     ein.isNull() ? "fehlt" : "ok", qPrintable(geraeteListe()));
        return 2;
    }
    // Sicherung (unhörbar): Klicks nur auf die Null-Senke des Messtests, nie auf ein echtes Gerät
    if (!aus.id().startsWith("dss_p12a")) {
        std::fprintf(stderr, "Klicks nur auf eine Null-Senke «dss_p12a…», nicht auf %s\n", aus.id().constData());
        return 64;
    }
    const qint64 abstand = qint64(o.abstandMs) * kRate / 1000;

    QThread threadAus, threadEin;
    QObject ctxAus, ctxEin;
    ctxAus.moveToThread(&threadAus);
    ctxEin.moveToThread(&threadEin);
    threadAus.start(QThread::HighPriority);
    threadEin.start(QThread::HighPriority);

    KlickGenerator *gen = nullptr;
    QAudioSink *sink = nullptr;
    std::vector<Zustand> zustaendeAus;
    Aufnahme auf;
    int64_t startNs = 0, startEpoch = 0;
    qint64 pufferIst = 0, pufferAufnahme = 0;
    qint64 processedUs = 0, elapsedUs = 0;

    // Aufnahme zuerst, damit der erste Klick nicht fehlt
    QMetaObject::invokeMethod(&ctxEin, [&] {
        auf.quelle = new QAudioSource(ein, format());
        QObject::connect(auf.quelle, &QAudioSource::stateChanged, &ctxEin, [&](QtAudio::State s) {
            auf.zustaende.push_back({steadyNs(), s, auf.quelle->error()});
        });
        auf.io = auf.quelle->start();
        if (auf.io)
            QObject::connect(auf.io, &QIODevice::readyRead, &ctxEin, [&] { auf.lesen(); });
        pufferAufnahme = auf.quelle->bufferSize();
    }, Qt::BlockingQueuedConnection);
    if (!auf.io) {
        std::fprintf(stderr, "Aufnahme startet nicht: Fehler %s\n", fehlerName(auf.quelle->error()));
        QMetaObject::invokeMethod(&ctxEin, [&] { delete auf.quelle; }, Qt::BlockingQueuedConnection);
        threadAus.quit();
        threadEin.quit();
        threadAus.wait();
        threadEin.wait();
        return 3;
    }
    QThread::msleep(500);

    QMetaObject::invokeMethod(&ctxAus, [&] {
        gen = new KlickGenerator(false, abstand);
        gen->open(QIODevice::ReadOnly);
        sink = new QAudioSink(aus, format());
        sink->setBufferSize(qint64(o.pufferMs) * kRate / 1000 * kBytesJeFrame);
        QObject::connect(sink, &QAudioSink::stateChanged, &ctxAus, [&](QtAudio::State s) {
            zustaendeAus.push_back({steadyNs(), s, sink->error()});
        });
        startNs = steadyNs();
        startEpoch = epochMs();
        sink->start(gen);
        pufferIst = sink->bufferSize();
    }, Qt::BlockingQueuedConnection);

    std::printf("Messung läuft: %d s, Puffer %d ms gewünscht, %lld Bytes = %.2f ms erhalten, "
                "Aufnahmepuffer %lld Bytes\n",
                o.dauerS, o.pufferMs, qlonglong(pufferIst), double(pufferIst) / kBytesJeFrame * 1e3 / kRate,
                qlonglong(pufferAufnahme));
    std::fflush(stdout);
    QThread::sleep(std::chrono::seconds(o.dauerS));

    std::vector<KlickGenerator::Aufruf> aufrufe;
    QMetaObject::invokeMethod(&ctxAus, [&] {
        processedUs = sink->processedUSecs();
        elapsedUs = sink->elapsedUSecs();
        sink->stop();
        aufrufe = std::move(gen->aufrufe);
        delete sink;
        delete gen;
    }, Qt::BlockingQueuedConnection);
    QThread::msleep(500); // letzte Klicks aufnehmen
    QMetaObject::invokeMethod(&ctxEin, [&] {
        auf.quelle->stop();
        delete auf.quelle;
    }, Qt::BlockingQueuedConnection);
    threadAus.quit();
    threadEin.quit();
    threadAus.wait();
    threadEin.wait();

    // Geschriebene Klicks: Zeit des Aufrufs, der die erste Probe des Klicks lieferte, und die
    // Schätzung, wie viel davor noch im Puffer lag (Puffergrösse − angeforderte Bytes).
    struct Klick
    {
        qint64 k, lage;
        int64_t ns;
        double wartendMs;
        bool gefunden = false;
        qint64 aufLage = 0;
        int64_t aufNs = 0;
    };
    std::vector<Klick> klicks;
    for (const auto &a : aufrufe) {
        for (qint64 k = (a.lage + abstand - 1) / abstand; k * abstand < a.lage + a.frames; ++k) {
            const double wartend = double(std::max<qint64>(0, pufferIst - a.maxBytes)) / kBytesJeFrame;
            const double imAufruf = double(k * abstand - a.lage);
            klicks.push_back({k, k * abstand, a.ns, (wartend + imAufruf) * 1e3 / kRate});
        }
    }
    // Einsätze den Klicks zuordnen: Der erste Einsatz ist Klick 0 (auf der Null-Senke spielt sonst
    // nichts); weitere nach ihrer Lage relativ dazu.
    int fremd = 0;
    if (!auf.einsaetze.empty()) {
        const qint64 d0 = auf.einsaetze.front().lage;
        for (const auto &e : auf.einsaetze) {
            const qint64 k = std::llround(double(e.lage - d0) / double(abstand));
            if (k >= 0 && k < qint64(klicks.size()) && !klicks[size_t(k)].gefunden) {
                klicks[size_t(k)].gefunden = true;
                klicks[size_t(k)].aufLage = e.lage;
                klicks[size_t(k)].aufNs = e.ns;
            } else {
                ++fremd;
            }
        }
    }

    QFile csv(o.aus + QStringLiteral(".csv"));
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        std::fprintf(stderr, "kann %s nicht schreiben\n", qPrintable(csv.fileName()));
        return 4;
    }
    zeile(csv, QStringLiteral("art,k,lage,geschrieben_ms,wartend_ms,aufnahme_lage,aufnahme_ms,"
                              "versatz_proben,verzoegerung_ms,verzoegerung_ohne_puffer_ms,epoch_ms"));
    std::vector<double> verz, verzOhne, aufrufAbstand, aufrufMs;
    std::vector<std::pair<qint64, qint64>> spruenge; // Klick, Änderung in Proben
    qint64 versatzVorher = std::numeric_limits<qint64>::min();
    int fehlend = 0;
    for (const Klick &k : klicks) {
        const double geschrieben = (k.ns - startNs) / 1e6;
        if (!k.gefunden) {
            ++fehlend;
            zeile(csv, QStringLiteral("klick,%1,%2,%3,%4,,,,,,%5")
                           .arg(k.k).arg(k.lage).arg(geschrieben, 0, 'f', 3).arg(k.wartendMs, 0, 'f', 3)
                           .arg(startEpoch + qint64(geschrieben)));
            continue;
        }
        const qint64 versatz = k.aufLage - k.lage;
        const double v = (k.aufNs - k.ns) / 1e6;
        verz.push_back(v);
        verzOhne.push_back(v - k.wartendMs);
        if (versatzVorher != std::numeric_limits<qint64>::min() && std::llabs(versatz - versatzVorher) > 2)
            spruenge.push_back({k.k, versatz - versatzVorher});
        versatzVorher = versatz;
        zeile(csv, QStringLiteral("klick,%1,%2,%3,%4,%5,%6,%7,%8,%9,%10")
                       .arg(k.k).arg(k.lage).arg(geschrieben, 0, 'f', 3).arg(k.wartendMs, 0, 'f', 3)
                       .arg(k.aufLage).arg((k.aufNs - startNs) / 1e6, 0, 'f', 3).arg(versatz)
                       .arg(v, 0, 'f', 3).arg(v - k.wartendMs, 0, 'f', 3)
                       .arg(startEpoch + qint64(geschrieben)));
    }
    for (size_t i = 0; i < aufrufe.size(); ++i) {
        aufrufMs.push_back(double(aufrufe[i].frames) * 1e3 / kRate);
        if (i > 0)
            aufrufAbstand.push_back((aufrufe[i].ns - aufrufe[i - 1].ns) / 1e6);
    }
    int unterlaeufe = 0, leer = 0;
    for (const Zustand &z : zustaendeAus) {
        if (z.fehler == QtAudio::UnderrunError)
            ++unterlaeufe;
        if (z.zustand == QtAudio::IdleState)
            ++leer;
        zeile(csv, QStringLiteral("zustand_ausgang,,,%1,,,,,,,%2 %3")
                       .arg((z.ns - startNs) / 1e6, 0, 'f', 3)
                       .arg(QString::fromUtf8(zustandName(z.zustand)), QString::fromUtf8(fehlerName(z.fehler))));
    }
    for (const Zustand &z : auf.zustaende)
        zeile(csv, QStringLiteral("zustand_aufnahme,,,%1,,,,,,,%2 %3")
                       .arg((z.ns - startNs) / 1e6, 0, 'f', 3)
                       .arg(QString::fromUtf8(zustandName(z.zustand)), QString::fromUtf8(fehlerName(z.fehler))));
    csv.close();

    QString t;
    t += QStringLiteral("M1 Klickmessung, Beginn (Wanduhr) %1 ms, Dauer %2 s\n").arg(startEpoch).arg(o.dauerS);
    t += QStringLiteral("Ausgang: %1 «%2»\nEingang: %3 «%4»\n")
             .arg(QString::fromUtf8(aus.id()), aus.description(), QString::fromUtf8(ein.id()), ein.description());
    t += QStringLiteral("Puffer: gewünscht %1 ms, erhalten %2 Bytes = %3 ms; Aufnahmepuffer %4 Bytes\n")
             .arg(o.pufferMs).arg(pufferIst).arg(double(pufferIst) / kBytesJeFrame * 1e3 / kRate, 0, 'f', 2)
             .arg(pufferAufnahme);
    t += QStringLiteral("processedUSecs %1, elapsedUSecs %2 (Differenz %3 ms)\n")
             .arg(processedUs).arg(elapsedUs).arg((processedUs - elapsedUs) / 1e3, 0, 'f', 1);
    t += QStringLiteral("Klicks geschrieben %1, gefunden %2, fehlend %3, fremde Einsätze %4\n")
             .arg(klicks.size()).arg(klicks.size() - size_t(fehlend)).arg(fehlend).arg(fremd);
    if (!klicks.empty() && klicks.front().gefunden)
        t += QStringLiteral("Startverzögerung (start() bis Klick 0 in der Aufnahme): %1 ms\n")
                 .arg((klicks.front().aufNs - startNs) / 1e6, 0, 'f', 3);
    if (auf.ersteDatenNs)
        t += QStringLiteral("Erste Aufnahmedaten nach start() der Aufnahme: vor start() der Ausgabe %1 ms\n")
                 .arg((startNs - auf.ersteDatenNs) / 1e6, 0, 'f', 1);
    t += QStringLiteral("Verzögerung Schreiben → Aufnahme: %1\n").arg(kennzahlenText(kennzahlen(verz), "ms"));
    t += QStringLiteral("  ohne geschätzten Pufferinhalt: %1\n").arg(kennzahlenText(kennzahlen(verzOhne), "ms"));
    t += QStringLiteral("Abstand der Abrufe durch QAudioSink: %1\n")
             .arg(kennzahlenText(kennzahlen(aufrufAbstand), "ms"));
    t += QStringLiteral("Länge der Abrufe: %1\n").arg(kennzahlenText(kennzahlen(aufrufMs), "ms"));
    t += QStringLiteral("Zustand Ausgang: %1 Wechsel, davon «leer» %2, Fehler «Unterlauf» %3\n")
             .arg(zustaendeAus.size()).arg(leer).arg(unterlaeufe);
    t += QStringLiteral("Sprünge der Lage (Aufnahme − Ausgabe, > 2 Proben): %1\n").arg(spruenge.size());
    for (const auto &[k, d] : spruenge)
        t += QStringLiteral("  bei Klick %1 (%2 s): %3 Proben = %4 ms\n")
                 .arg(k).arg(double(k) * o.abstandMs / 1e3, 0, 'f', 1).arg(d).arg(d * 1e3 / kRate, 0, 'f', 2);

    QFile z(o.aus + QStringLiteral("-zusammenfassung.txt"));
    if (z.open(QIODevice::WriteOnly | QIODevice::Truncate))
        z.write(t.toUtf8());
    std::printf("%s", qPrintable(t));
    return 0;
}

int stilleProbe(const QString &ausgang, int pufferMs, int dauerS)
{
    const QAudioDevice aus = ausgangSuchen(ausgang);
    if (aus.isNull()) {
        std::fprintf(stderr, "Ausgang «%s» nicht gefunden\n%s", qPrintable(ausgang), qPrintable(geraeteListe()));
        return 2;
    }
    QThread thread;
    QObject ctx;
    ctx.moveToThread(&thread);
    thread.start(QThread::HighPriority);
    KlickGenerator *gen = nullptr;
    QAudioSink *sink = nullptr;
    std::vector<Zustand> zustaende;
    int64_t startNs = 0;
    qint64 pufferIst = 0, processedUs = 0, elapsedUs = 0;
    std::vector<KlickGenerator::Aufruf> aufrufe;
    QMetaObject::invokeMethod(&ctx, [&] {
        gen = new KlickGenerator(true, kRate); // nur Nullen
        gen->open(QIODevice::ReadOnly);
        sink = new QAudioSink(aus, format());
        sink->setVolume(0.0);
        sink->setBufferSize(qint64(pufferMs) * kRate / 1000 * kBytesJeFrame);
        QObject::connect(sink, &QAudioSink::stateChanged, &ctx, [&](QtAudio::State s) {
            zustaende.push_back({steadyNs(), s, sink->error()});
        });
        startNs = steadyNs();
        sink->start(gen);
        pufferIst = sink->bufferSize();
    }, Qt::BlockingQueuedConnection);
    QThread::sleep(std::chrono::seconds(dauerS));
    QMetaObject::invokeMethod(&ctx, [&] {
        processedUs = sink->processedUSecs();
        elapsedUs = sink->elapsedUSecs();
        sink->stop();
        aufrufe = std::move(gen->aufrufe);
        delete sink;
        delete gen;
    }, Qt::BlockingQueuedConnection);
    thread.quit();
    thread.wait();

    std::vector<double> abstand, laenge;
    qint64 frames = 0;
    for (size_t i = 0; i < aufrufe.size(); ++i) {
        frames += aufrufe[i].frames;
        laenge.push_back(double(aufrufe[i].frames) * 1e3 / kRate);
        if (i > 0)
            abstand.push_back((aufrufe[i].ns - aufrufe[i - 1].ns) / 1e6);
    }
    std::printf("Stille-Probe (nur Nullen, Lautstärke 0) auf %s «%s», %d s\n", aus.id().constData(),
                qPrintable(aus.description()), dauerS);
    std::printf("Puffer gewünscht %d ms, erhalten %lld Bytes = %.2f ms\n", pufferMs, qlonglong(pufferIst),
                double(pufferIst) / kBytesJeFrame * 1e3 / kRate);
    std::printf("Proben abgeholt %lld = %.3f s; processedUSecs %lld, elapsedUSecs %lld\n", qlonglong(frames),
                double(frames) / kRate, qlonglong(processedUs), qlonglong(elapsedUs));
    std::printf("Abstand der Abrufe: %s\n", qPrintable(kennzahlenText(kennzahlen(abstand), "ms")));
    std::printf("Länge der Abrufe: %s\n", qPrintable(kennzahlenText(kennzahlen(laenge), "ms")));
    for (const Zustand &z : zustaende)
        std::printf("Zustand bei %.1f ms: %s, Fehler %s\n", (z.ns - startNs) / 1e6, zustandName(z.zustand),
                    fehlerName(z.fehler));
    return 0;
}
