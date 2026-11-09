// Quellen: https://doc.qt.io/qt-6.8/qaudiosink.html, https://wicg.github.io/video-rvfc/
// (mediaTime: Zeitstempel des gezeigten Bildes in s; expectedDisplayTime: wann es zu sehen sein
// soll, Zeitachse von performance.now()).
#include "folgen.h"

#include "auswertung.h"
#include "klickmessung.h"
#include "zeit.h"

#include <QAudioFormat>
#include <QAudioSink>
#include <QFile>
#include <QIODevice>

#include <cmath>
#include <limits>

namespace {
constexpr qint64 kBytesJeFrame = qint64(sizeof(float)) * kKanaele;
constexpr double kSprungS = 0.020;
constexpr double kMaxRate = 0.005;
constexpr int64_t kStilleNs = 3'000'000'000;
constexpr int64_t kFensterNs = 10'000'000'000;
} // namespace

class FolgeGenerator : public QIODevice
{
public:
    explicit FolgeGenerator(Folger *f) : m_f(f) {}
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override
    {
        return std::numeric_limits<qint32>::max() + QIODevice::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxlen) override
    {
        const int64_t ns = steadyNs();
        const qint64 frames = maxlen / kBytesJeFrame;
        float *p = reinterpret_cast<float *>(data);
        std::fill(p, p + frames * kKanaele, 0.0f);

        Folger::Zustand z;
        {
            std::lock_guard<std::mutex> l(m_f->m_mutex);
            if (m_f->m_z.laeuft && m_f->m_z.letzteMeldungNs && ns - m_f->m_z.letzteMeldungNs > kStilleNs) {
                m_f->m_z.laeuft = false;
                m_f->m_ereignisse.push_back({ns, QStringLiteral("Stopp: 3 s ohne Meldung")});
            }
            z = m_f->m_z;
        }
        if (!z.laeuft || !z.hatAnker) {
            m_init = false;
            return frames * kBytesJeFrame;
        }
        const double laenge = double(m_f->m_ton.frames());
        const double wartend = double(std::max<qint64>(0, m_f->m_pufferIst - maxlen)) / kBytesJeFrame;
        const int64_t hoerbarNs = ns + int64_t(wartend * 1e9 / kRate) + int64_t(m_f->m_o.geraetMs * 1e6);
        double soll = (z.ankerMedien + (hoerbarNs - z.ankerLokalNs) / 1e9 - m_f->m_o.versatzMs / 1e3) * kRate;
        soll = std::fmod(soll, laenge);
        if (soll < 0)
            soll += laenge;
        double abw = m_pos - soll;
        if (abw > laenge / 2)
            abw -= laenge;
        else if (abw < -laenge / 2)
            abw += laenge;
        bool sprung = false;
        if (!m_init || std::fabs(abw) > kSprungS * kRate) {
            sprung = m_init; // erster Einsatz zählt nicht als Sprung
            m_pos = soll;
            m_rate = 1.0;
            m_init = true;
        } else {
            m_rate = std::clamp(1.0 - abw / kRate, 1.0 - kMaxRate, 1.0 + kMaxRate);
        }
        if (ns - m_letzterEintrag >= 100'000'000 || sprung) {
            m_letzterEintrag = ns;
            m_f->m_protokoll.push_back({ns, soll / kRate, m_pos / kRate, abw * 1e3 / kRate, m_rate, sprung});
        }
        const float *q = m_f->m_ton.proben.data();
        const float pegel = float(m_f->m_o.lautstaerke);
        for (qint64 i = 0; i < frames; ++i) {
            const qint64 a = qint64(m_pos);
            const double t = m_pos - double(a);
            const qint64 b = a + 1 < qint64(laenge) ? a + 1 : 0;
            for (int k = 0; k < kKanaele; ++k)
                p[i * kKanaele + k] = pegel * float(q[a * kKanaele + k] * (1.0 - t) + q[b * kKanaele + k] * t);
            m_pos += m_rate;
            if (m_pos >= laenge)
                m_pos -= laenge;
        }
        return frames * kBytesJeFrame;
    }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    Folger *m_f;
    double m_pos = 0;
    double m_rate = 1.0;
    bool m_init = false;
    int64_t m_letzterEintrag = 0;
};

Folger::Folger(TonDatei ton, FolgeOptionen o) : m_ton(std::move(ton)), m_o(std::move(o)) {}

Folger::~Folger()
{
    beenden();
}

bool Folger::starten(QString *fehler)
{
    const QAudioDevice aus = ausgangSuchen(m_o.ausgang);
    if (aus.isNull()) {
        *fehler = QStringLiteral("Ausgang «%1» nicht gefunden").arg(m_o.ausgang);
        return false;
    }
    m_ctx = std::make_unique<QObject>();
    m_ctx->moveToThread(&m_thread);
    m_thread.start(QThread::HighPriority);
    QMetaObject::invokeMethod(m_ctx.get(), [&] {
        QAudioFormat f;
        f.setSampleRate(kRate);
        f.setChannelCount(kKanaele);
        f.setSampleFormat(QAudioFormat::Float);
        m_gen = new FolgeGenerator(this);
        m_gen->open(QIODevice::ReadOnly);
        m_sink = new QAudioSink(aus, f);
        m_sink->setBufferSize(qint64(m_o.pufferMs) * kRate / 1000 * kBytesJeFrame);
        m_startNs = steadyNs();
        m_sink->start(m_gen);
        m_pufferIst = m_sink->bufferSize();
    }, Qt::BlockingQueuedConnection);
    std::lock_guard<std::mutex> l(m_mutex);
    m_ereignisse.push_back({m_startNs, QStringLiteral("Ausgang %1 «%2», Puffer %3 Bytes")
                                           .arg(QString::fromUtf8(aus.id()), aus.description())
                                           .arg(m_pufferIst)});
    return true;
}

void Folger::beenden()
{
    if (!m_ctx)
        return;
    QMetaObject::invokeMethod(m_ctx.get(), [&] {
        m_sink->stop();
        delete m_sink;
        delete m_gen;
        m_sink = nullptr;
        m_gen = nullptr;
    }, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
    m_ctx.reset();
}

void Folger::meldung(const QJsonObject &m, qint64 ankunftNs)
{
    const QString art = m.value(QStringLiteral("art")).toString();
    std::lock_guard<std::mutex> l(m_mutex);
    m_z.letzteMeldungNs = ankunftNs;
    const double seiteMs = m.value(QStringLiteral("now")).toDouble();
    m_abstaende.push_back({ankunftNs, ankunftNs - int64_t(seiteMs * 1e6)});
    while (!m_abstaende.empty() && ankunftNs - m_abstaende.front().first > kFensterNs)
        m_abstaende.erase(m_abstaende.begin());
    int64_t versatz = std::numeric_limits<int64_t>::max();
    for (const auto &a : m_abstaende)
        versatz = std::min(versatz, a.second);

    if (art == QLatin1String("bild")) {
        if (!m_z.laeuft && !m.value(QStringLiteral("obs")).toBool() && !m_z.hatAnker) {
            // Ohne OBS (M2) läuft die Seite sofort: erster Anker startet den Ton
            m_z.laeuft = true;
            m_ereignisse.push_back({ankunftNs, QStringLiteral("Start mit der ersten Meldung «bild»")});
        }
        m_z.hatAnker = true;
        m_z.ankerMedien = m.value(QStringLiteral("mediaTime")).toDouble();
        m_z.ankerLokalNs = int64_t(m.value(QStringLiteral("expectedDisplayTime")).toDouble() * 1e6) + versatz;
    } else if (art == QLatin1String("start")
               || (art == QLatin1String("aktiv") && m.value(QStringLiteral("aktiv")).toBool())) {
        m_z.laeuft = true;
        m_z.hatAnker = false; // auf das erste Bild nach dem Start warten
        m_ereignisse.push_back({ankunftNs, QStringLiteral("Start (%1)").arg(art)});
    } else if (art == QLatin1String("stop")
               || (art == QLatin1String("aktiv") && !m.value(QStringLiteral("aktiv")).toBool())) {
        m_z.laeuft = false;
        m_ereignisse.push_back({ankunftNs, QStringLiteral("Stopp (%1)").arg(art)});
    }
}

QString Folger::zusammenfassung() const
{
    std::vector<double> abw;
    int spruenge = 0;
    for (const Eintrag &e : m_protokoll) {
        if (e.sprung)
            ++spruenge;
        else
            abw.push_back(std::fabs(e.abweichungMs));
    }
    QString t = QStringLiteral("Nachführen: %1 Einträge (alle 100 ms), Sprünge > 20 ms: %2\n")
                    .arg(m_protokoll.size())
                    .arg(spruenge);
    t += QStringLiteral("  |Abweichung Ton − Soll| vor dem Ausgleich: %1\n").arg(kennzahlenText(kennzahlen(abw), "ms"));
    std::lock_guard<std::mutex> l(m_mutex);
    for (const Ereignis &e : m_ereignisse)
        t += QStringLiteral("  %1 s: %2\n").arg((e.ns - m_startNs) / 1e9, 0, 'f', 3).arg(e.text);
    return t;
}

bool Folger::protokollSchreiben(const QString &pfad) const
{
    QFile f(pfad);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write("zeit_s,soll_s,ist_s,abweichung_ms,rate,sprung\n");
    for (const Eintrag &e : m_protokoll)
        f.write(QStringLiteral("%1,%2,%3,%4,%5,%6\n")
                    .arg((e.ns - m_startNs) / 1e9, 0, 'f', 4)
                    .arg(e.sollS, 0, 'f', 5)
                    .arg(e.istS, 0, 'f', 5)
                    .arg(e.abweichungMs, 0, 'f', 3)
                    .arg(e.rate, 0, 'f', 6)
                    .arg(e.sprung ? 1 : 0)
                    .toUtf8());
    return true;
}
