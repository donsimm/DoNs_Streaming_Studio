#pragma once

#include <QByteArray>
#include <QString>

#include <optional>

// Leistungswerte des Studio-PCs für den Kopfbereich (P07, Teil E): CPU-Auslastung gesamt,
// RAM belegt und gesamt, LAN empfangen und gesendet für die Netzwerkkarte der Standardroute
// (Entscheid 3). Nur Qt Core (CLAUDE.md Regel 9). Die Leser nehmen den Inhalt der Dateien als
// Text, damit sie mit festen Testdaten geprüft werden können.
//
// Quellen (proc(5), abgerufen 2026-11-02):
//   /proc/stat, Zeile «cpu»: https://man7.org/linux/man-pages/man5/proc_stat.5.html
//   /proc/meminfo:            https://man7.org/linux/man-pages/man5/proc_meminfo.5.html
//   /proc/net/dev:            https://man7.org/linux/man-pages/man5/proc_pid_net.5.html
//   /proc/net/route: Spalten wie in der Kopfzeile der Datei; Flags aus
//   /usr/include/linux/route.h (Paket linux-libc-dev 6.12.113-1): RTF_UP 0x0001,
//   RTF_GATEWAY 0x0002.

namespace dss {

// Summen der Zeile «cpu» in /proc/stat (Einheit USER_HZ).
struct CpuTimes
{
    quint64 busy = 0;  // user + nice + system + irq + softirq + steal
    quint64 total = 0; // busy + idle + iowait
};

// Liest die erste Zeile «cpu …» (mindestens die Felder user bis steal; guest und guest_nice
// werden nicht mitgezählt, siehe docs/08-kern-schnittstelle.md).
bool parseProcStat(const QByteArray &text, CpuTimes *times);

// Auslastung in Prozent zwischen zwei Messungen; leer, wenn keine Zeit vergangen ist oder ein
// Zähler kleiner geworden ist.
std::optional<double> cpuPercent(const CpuTimes &before, const CpuTimes &after);

struct MemoryInfo
{
    qint64 totalKiB = 0;
    qint64 availableKiB = 0;
    qint64 usedKiB() const { return totalKiB - availableKiB; }
};

// Liest MemTotal und MemAvailable (Einheit kB) aus /proc/meminfo.
// Belegt = MemTotal − MemAvailable.
bool parseMeminfo(const QByteArray &text, MemoryInfo *info);

// Netzwerkkarte der Standardroute aus /proc/net/route: Ziel 00000000, Maske 00000000,
// Flag RTF_UP gesetzt. Bei mehreren die mit der kleinsten Metrik. Leer, wenn es keine gibt.
QString defaultRouteInterface(const QByteArray &text);

struct InterfaceBytes
{
    quint64 received = 0;
    quint64 sent = 0;
};

// Bytes empfangen (erste Zahl nach dem Namen) und gesendet (neunte Zahl) der Karte iface
// aus /proc/net/dev. false, wenn die Karte fehlt.
bool parseNetDev(const QByteArray &text, const QString &iface, InterfaceBytes *bytes);

struct LanStats
{
    QString interface;
    double receivedKbitPerSecond = 0.0; // 1 kbit = 1000 bit
    double sentKbitPerSecond = 0.0;

    bool operator==(const LanStats &) const = default;
};

// Eine Messung, wie sie der Kern einmal pro Sekunde verschickt. Leere Werte: nicht bekannt
// (zum Beispiel CPU bei der ersten Messung, LAN ohne Standardroute).
struct SystemStats
{
    std::optional<double> cpuPercent;
    std::optional<qint64> ramUsedKiB;
    std::optional<qint64> ramTotalKiB;
    std::optional<LanStats> lan;

    bool operator==(const SystemStats &) const = default;
};

// Liest die Dateien unter procRoot (Standard /proc; Tests nehmen einen Ordner mit festen
// Testdaten) und rechnet die Werte seit der letzten Messung aus.
class StatsSampler
{
public:
    explicit StatsSampler(const QString &procRoot = QStringLiteral("/proc"));

    // elapsedMs: Zeit seit der letzten Messung (monotone Uhr). Die erste Messung hat
    // noch keine CPU- und LAN-Werte.
    SystemStats sample(qint64 elapsedMs);

private:
    QByteArray readFile(const QString &relativePath) const;

    QString m_procRoot;
    std::optional<CpuTimes> m_lastCpu;
    QString m_lastInterface;
    std::optional<InterfaceBytes> m_lastBytes;
};

} // namespace dss
