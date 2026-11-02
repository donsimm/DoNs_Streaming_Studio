#include "systemstats.h"

#include <QFile>
#include <QList>

#include <limits>

namespace dss {

namespace {

// Zerlegt eine Zeile an Leerzeichen und Tabulatoren.
QList<QByteArray> fields(const QByteArray &line)
{
    QList<QByteArray> result;
    for (const QByteArray &f : line.simplified().split(' ')) {
        if (!f.isEmpty())
            result.append(f);
    }
    return result;
}

bool toUInt64(const QByteArray &text, quint64 *value)
{
    bool ok = false;
    *value = text.toULongLong(&ok);
    return ok;
}

// RTF_UP aus /usr/include/linux/route.h
constexpr quint32 kRouteUp = 0x0001;

} // namespace

bool parseProcStat(const QByteArray &text, CpuTimes *times)
{
    for (const QByteArray &line : text.split('\n')) {
        const QList<QByteArray> f = fields(line);
        if (f.isEmpty() || f.first() != "cpu")
            continue;
        // cpu user nice system idle iowait irq softirq steal [guest guest_nice]
        if (f.size() < 9)
            return false;
        quint64 v[8];
        for (int i = 0; i < 8; ++i) {
            if (!toUInt64(f.at(i + 1), &v[i]))
                return false;
        }
        const quint64 idle = v[3] + v[4];
        times->busy = v[0] + v[1] + v[2] + v[5] + v[6] + v[7];
        times->total = times->busy + idle;
        return true;
    }
    return false;
}

std::optional<double> cpuPercent(const CpuTimes &before, const CpuTimes &after)
{
    if (after.total <= before.total || after.busy < before.busy)
        return std::nullopt;
    const double busy = double(after.busy - before.busy);
    const double total = double(after.total - before.total);
    if (busy > total)
        return std::nullopt;
    return 100.0 * busy / total;
}

bool parseMeminfo(const QByteArray &text, MemoryInfo *info)
{
    bool haveTotal = false;
    bool haveAvailable = false;
    for (const QByteArray &line : text.split('\n')) {
        const QList<QByteArray> f = fields(line);
        if (f.size() < 2)
            continue;
        quint64 value = 0;
        if (!toUInt64(f.at(1), &value) || value > quint64(std::numeric_limits<qint64>::max()))
            continue;
        if (f.size() >= 3 && f.at(2) != "kB")
            continue;
        if (f.first() == "MemTotal:") {
            info->totalKiB = qint64(value);
            haveTotal = true;
        } else if (f.first() == "MemAvailable:") {
            info->availableKiB = qint64(value);
            haveAvailable = true;
        }
    }
    return haveTotal && haveAvailable && info->availableKiB <= info->totalKiB;
}

QString defaultRouteInterface(const QByteArray &text)
{
    // Kopfzeile: Iface Destination Gateway Flags RefCnt Use Metric Mask MTU Window IRTT
    QString best;
    quint64 bestMetric = 0;
    const QList<QByteArray> lines = text.split('\n');
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QList<QByteArray> f = fields(lines.at(i));
        if (f.size() < 8)
            continue;
        bool okFlags = false;
        const quint32 flags = f.at(3).toUInt(&okFlags, 16);
        quint64 metric = 0;
        if (!okFlags || !toUInt64(f.at(6), &metric))
            continue;
        if (f.at(1) != "00000000" || f.at(7) != "00000000" || !(flags & kRouteUp))
            continue;
        if (best.isEmpty() || metric < bestMetric) {
            best = QString::fromLatin1(f.at(0));
            bestMetric = metric;
        }
    }
    return best;
}

bool parseNetDev(const QByteArray &text, const QString &iface, InterfaceBytes *bytes)
{
    const QByteArray name = iface.toLatin1();
    for (const QByteArray &line : text.split('\n')) {
        const qsizetype colon = line.indexOf(':');
        if (colon < 0 || line.left(colon).trimmed() != name)
            continue;
        const QList<QByteArray> f = fields(line.mid(colon + 1));
        // 8 Zahlen Empfang, 8 Zahlen Senden
        if (f.size() < 16)
            return false;
        return toUInt64(f.at(0), &bytes->received) && toUInt64(f.at(8), &bytes->sent);
    }
    return false;
}

StatsSampler::StatsSampler(const QString &procRoot)
    : m_procRoot(procRoot)
{
}

QByteArray StatsSampler::readFile(const QString &relativePath) const
{
    // Dateien unter /proc haben keine Grösse; readAll liest bis zum Ende.
    QFile file(m_procRoot + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

SystemStats StatsSampler::sample(qint64 elapsedMs)
{
    SystemStats stats;

    CpuTimes cpu;
    if (parseProcStat(readFile(QStringLiteral("stat")), &cpu)) {
        if (m_lastCpu)
            stats.cpuPercent = cpuPercent(*m_lastCpu, cpu);
        m_lastCpu = cpu;
    } else {
        m_lastCpu.reset();
    }

    MemoryInfo memory;
    if (parseMeminfo(readFile(QStringLiteral("meminfo")), &memory)) {
        stats.ramUsedKiB = memory.usedKiB();
        stats.ramTotalKiB = memory.totalKiB;
    }

    const QString iface = defaultRouteInterface(readFile(QStringLiteral("net/route")));
    InterfaceBytes bytes;
    if (!iface.isEmpty() && parseNetDev(readFile(QStringLiteral("net/dev")), iface, &bytes)) {
        if (m_lastBytes && iface == m_lastInterface && elapsedMs > 0
            && bytes.received >= m_lastBytes->received && bytes.sent >= m_lastBytes->sent) {
            const double seconds = double(elapsedMs) / 1000.0;
            LanStats lan;
            lan.interface = iface;
            lan.receivedKbitPerSecond = double(bytes.received - m_lastBytes->received) * 8.0
                                        / 1000.0 / seconds;
            lan.sentKbitPerSecond = double(bytes.sent - m_lastBytes->sent) * 8.0 / 1000.0
                                    / seconds;
            stats.lan = lan;
        }
        m_lastInterface = iface;
        m_lastBytes = bytes;
    } else {
        m_lastInterface.clear();
        m_lastBytes.reset();
    }
    return stats;
}

} // namespace dss
