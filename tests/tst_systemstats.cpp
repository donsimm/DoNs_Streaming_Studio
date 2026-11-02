// Prüft die Leistungswerte (P07, Teil E) mit festen Testdaten ohne Qt Gui. Die Dateien unter
// tests/daten/proc/ sind Kopien von /proc/stat, /proc/meminfo, /proc/net/route und
// /proc/net/dev des Studio-PCs vom 2026-11-02 19:48:24 (messung1, eine Sekunde später
// messung2; CLAUDE.md Regel 7). ohne-route: messung2 ohne die Standardroute.
// Erwartete Werte von Hand gerechnet, siehe die Kommentare.

#include "systemstats.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace dss;

namespace {

const QString kData = QStringLiteral(DSS_TEST_PROC_DIR);

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Kopiert einen Satz Testdaten nach dir (so wie /proc aufgebaut).
bool copySet(const QString &set, const QString &dir)
{
    QDir().mkpath(dir + QStringLiteral("/net"));
    for (const char *f : {"stat", "meminfo", "net/route", "net/dev"}) {
        const QString target = dir + QLatin1Char('/') + QLatin1String(f);
        QFile::remove(target);
        if (!QFile::copy(kData + QLatin1Char('/') + set + QLatin1Char('/') + QLatin1String(f), target))
            return false;
    }
    return true;
}

} // namespace

class TestSystemStats : public QObject
{
    Q_OBJECT

private slots:
    void procStat()
    {
        // cpu 471458 158 35526 8473035 6708 0 300 0 0 0
        CpuTimes t;
        QVERIFY(parseProcStat(readFile(kData + QStringLiteral("/messung1/stat")), &t));
        QCOMPARE(t.busy, quint64(471458 + 158 + 35526 + 0 + 300 + 0));
        QCOMPARE(t.total, t.busy + quint64(8473035 + 6708));
    }

    void procStatInvalid()
    {
        CpuTimes t;
        QVERIFY(!parseProcStat(QByteArray(), &t));
        QVERIFY(!parseProcStat("cpu0 1 2 3 4 5 6 7 8\n", &t));      // nur einzelne Kerne
        QVERIFY(!parseProcStat("cpu  1 2 3 4 5 6 7\n", &t));        // zu wenige Felder
        QVERIFY(!parseProcStat("cpu  1 2 x 4 5 6 7 8\n", &t));      // keine Zahl
    }

    void cpuPercentBetweenSamples()
    {
        CpuTimes a;
        CpuTimes b;
        QVERIFY(parseProcStat(readFile(kData + QStringLiteral("/messung1/stat")), &a));
        QVERIFY(parseProcStat(readFile(kData + QStringLiteral("/messung2/stat")), &b));
        // Δbelegt = 507475 − 507442 = 33, Δgesamt = 33 + (8480514 − 8479743) = 804
        const std::optional<double> p = cpuPercent(a, b);
        QVERIFY(p);
        QVERIFY(qAbs(*p - 100.0 * 33 / 804) < 1e-9);
        QVERIFY(!cpuPercent(a, a));  // keine Zeit vergangen
        QVERIFY(!cpuPercent(b, a));  // Zähler kleiner geworden
    }

    void meminfo()
    {
        MemoryInfo m;
        QVERIFY(parseMeminfo(readFile(kData + QStringLiteral("/messung2/meminfo")), &m));
        QCOMPARE(m.totalKiB, qint64(32809248));
        QCOMPARE(m.availableKiB, qint64(29095376));
        QCOMPARE(m.usedKiB(), qint64(32809248 - 29095376));
        QVERIFY(!parseMeminfo("MemTotal: 100 kB\n", &m));               // MemAvailable fehlt
        QVERIFY(!parseMeminfo("MemTotal: 100 kB\nMemAvailable: 200 kB\n", &m)); // mehr frei als da
    }

    void defaultRoute()
    {
        QCOMPARE(defaultRouteInterface(readFile(kData + QStringLiteral("/messung1/net/route"))),
                 QStringLiteral("enx2aea2d88f6c1"));
        QCOMPARE(defaultRouteInterface(readFile(kData + QStringLiteral("/ohne-route/net/route"))),
                 QString());
        QCOMPARE(defaultRouteInterface(QByteArray()), QString());
        // Mehrere Standardrouten: kleinste Metrik. Route ohne RTF_UP zählt nicht.
        const QByteArray two =
            "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\t\tMTU\tWindow\tIRTT\n"
            "wlan0\t00000000\t0101A8C0\t0003\t0\t0\t600\t00000000\t0\t0\t0\n"
            "enp3s0\t00000000\t0101A8C0\t0003\t0\t0\t100\t00000000\t0\t0\t0\n"
            "down0\t00000000\t0101A8C0\t0002\t0\t0\t1\t00000000\t0\t0\t0\n";
        QCOMPARE(defaultRouteInterface(two), QStringLiteral("enp3s0"));
    }

    void netDev()
    {
        InterfaceBytes b;
        QVERIFY(parseNetDev(readFile(kData + QStringLiteral("/messung1/net/dev")),
                            QStringLiteral("enx2aea2d88f6c1"), &b));
        // Empfangen: erste Zahl, gesendet: neunte Zahl der Zeile
        QCOMPARE(b.received, quint64(61874997));
        QCOMPARE(b.sent, quint64(19665410));
        QVERIFY(!parseNetDev(readFile(kData + QStringLiteral("/messung1/net/dev")),
                             QStringLiteral("gibtsnicht0"), &b));
        // «lo» darf nicht auf eine Karte mit ähnlichem Namen passen
        QVERIFY(!parseNetDev("  lo0: 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16\n",
                             QStringLiteral("lo"), &b));
    }

    void samplerWithFixedData()
    {
        QTemporaryDir proc;
        QVERIFY(proc.isValid());
        StatsSampler sampler(proc.path());

        QVERIFY(copySet(QStringLiteral("messung1"), proc.path()));
        const SystemStats first = sampler.sample(0);
        QVERIFY(!first.cpuPercent);      // erste Messung: noch keine Auslastung
        QVERIFY(!first.lan);             // erste Messung: noch keine Rate
        QVERIFY(first.ramTotalKiB);

        QVERIFY(copySet(QStringLiteral("messung2"), proc.path()));
        const SystemStats second = sampler.sample(1000);
        QVERIFY(second.cpuPercent);
        QVERIFY(qAbs(*second.cpuPercent - 100.0 * 33 / 804) < 1e-9);
        QCOMPARE(*second.ramUsedKiB, qint64(32809248 - 29095376));
        QCOMPARE(*second.ramTotalKiB, qint64(32809248));
        QVERIFY(second.lan);
        QCOMPARE(second.lan->interface, QStringLiteral("enx2aea2d88f6c1"));
        // 61875129 − 61874997 = 132 Byte in 1 s → 132 × 8 / 1000 = 1.056 kbit/s; gesendet ebenso
        // 19665542 − 19665410 = 132 Byte.
        QVERIFY(qAbs(second.lan->receivedKbitPerSecond - 1.056) < 1e-9);
        QVERIFY(qAbs(second.lan->sentKbitPerSecond - 1.056) < 1e-9);

        // Halbe Sekunde: doppelte Rate.
        QVERIFY(copySet(QStringLiteral("messung1"), proc.path()));
        sampler = StatsSampler(proc.path());
        sampler.sample(0);
        QVERIFY(copySet(QStringLiteral("messung2"), proc.path()));
        QVERIFY(qAbs(sampler.sample(500).lan->receivedKbitPerSecond - 2.112) < 1e-9);
    }

    void samplerWithoutDefaultRoute()
    {
        // Kabel gezogen: keine Standardroute → kein LAN-Wert (Anzeige «–»), auch danach nicht
        // aus alten Zählern gerechnet.
        QTemporaryDir proc;
        QVERIFY(proc.isValid());
        StatsSampler sampler(proc.path());
        QVERIFY(copySet(QStringLiteral("messung1"), proc.path()));
        sampler.sample(0);
        QVERIFY(copySet(QStringLiteral("ohne-route"), proc.path()));
        const SystemStats s = sampler.sample(1000);
        QVERIFY(!s.lan);
        QVERIFY(s.cpuPercent);
        QVERIFY(copySet(QStringLiteral("messung2"), proc.path()));
        QVERIFY(!sampler.sample(1000).lan); // erste Messung nach der Lücke
    }

    void samplerMissingFiles()
    {
        StatsSampler sampler(QStringLiteral("/gibt/es/nicht"));
        const SystemStats s = sampler.sample(1000);
        QVERIFY(!s.cpuPercent && !s.ramUsedKiB && !s.ramTotalKiB && !s.lan);
    }

    void realProc()
    {
        // Gegenprobe mit dem echten /proc dieses Geräts: Werte vorhanden und im Bereich.
        StatsSampler sampler;
        sampler.sample(0);
        QTest::qWait(200);
        const SystemStats s = sampler.sample(200);
        QVERIFY(s.cpuPercent && *s.cpuPercent >= 0 && *s.cpuPercent <= 100);
        QVERIFY(s.ramTotalKiB && *s.ramTotalKiB > 0 && *s.ramUsedKiB <= *s.ramTotalKiB);
    }
};

QTEST_GUILESS_MAIN(TestSystemStats)
#include "tst_systemstats.moc"
