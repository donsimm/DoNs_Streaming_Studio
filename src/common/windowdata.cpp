#include "windowdata.h"

#include <algorithm>
#include <cmath>

namespace dss {

QString cpuText(const std::optional<double> &percent)
{
    if (!percent)
        return QStringLiteral("–");
    return QStringLiteral("%1 %").arg(std::lround(*percent));
}

QString ramText(const std::optional<qint64> &usedKiB, const std::optional<qint64> &totalKiB)
{
    if (!usedKiB || !totalKiB)
        return QStringLiteral("–");
    // GiB = KiB / 1024², eine Nachkommastelle wie auf dem Bild («0.8 / 7.8 GiB»).
    const double gib = 1024.0 * 1024.0;
    return QStringLiteral("%1 / %2 GiB")
        .arg(double(*usedKiB) / gib, 0, 'f', 1)
        .arg(double(*totalKiB) / gib, 0, 'f', 1);
}

QString lanText(const std::optional<LanStats> &lan)
{
    if (!lan)
        return QStringLiteral("–");
    return QStringLiteral("↓ %1 ↑ %2 kbit/s")
        .arg(std::llround(lan->receivedKbitPerSecond))
        .arg(std::llround(lan->sentKbitPerSecond));
}

QString lanToolTip(const std::optional<LanStats> &lan)
{
    if (!lan)
        return QStringLiteral("LAN: keine Standardroute oder noch kein Wert");
    return QStringLiteral("LAN über %1 (Standardroute): empfangen %2 kbit/s, gesendet %3 kbit/s")
        .arg(lan->interface)
        .arg(lan->receivedKbitPerSecond, 0, 'f', 1)
        .arg(lan->sentKbitPerSecond, 0, 'f', 1);
}

ValueHistory::ValueHistory(int capacity)
    : m_capacity(std::max(1, capacity))
{
}

void ValueHistory::add(const std::optional<double> &value)
{
    m_values.append(value);
    while (m_values.size() > m_capacity)
        m_values.removeFirst();
}

double ValueHistory::maximum() const
{
    double m = 0.0;
    for (const std::optional<double> &v : m_values) {
        if (v)
            m = std::max(m, *v);
    }
    return m;
}

double lanScaleMaximum(const ValueHistory &history)
{
    return std::max(1.0, history.maximum());
}

QList<SourceRow> sourceRows(const Overlay &overlay)
{
    QList<SourceRow> rows;
    for (auto it = overlay.sources.crbegin(); it != overlay.sources.crend(); ++it)
        rows.append({it->id, it->name, it->visible});
    return rows;
}

QString visibilityToolTip(bool visible)
{
    return visible ? QStringLiteral("eingeblendet") : QStringLiteral("ausgeblendet");
}

const Source *findSource(const Overlay &overlay, const QUuid &sourceId)
{
    const int index = sourceIndex(overlay, sourceId);
    return index < 0 ? nullptr : &overlay.sources.at(index);
}

double rotationDegrees(int tenthDegrees)
{
    return tenthDegrees / 10.0;
}

QString coreStatusText(bool connected)
{
    return connected ? QStringLiteral("Kern-Dienst: verbunden")
                     : QStringLiteral("Kern-Dienst: nicht verbunden");
}

QString importStatusText(const QStringList &fileNames)
{
    if (fileNames.isEmpty())
        return {};
    QStringList quoted;
    for (const QString &name : fileNames)
        quoted.append(QStringLiteral("«%1»").arg(name));
    if (fileNames.size() == 1)
        return QStringLiteral("Übernehme %1 …").arg(quoted.first());
    return QStringLiteral("Übernehme %1 Dateien (%2) …")
        .arg(fileNames.size()).arg(quoted.join(QStringLiteral(", ")));
}

std::optional<SettingsUpdate> settingsUpdateFromForm(const CoreSettings &current, int port,
                                                     const QString &addressText)
{
    SettingsUpdate update;
    const QString address = addressText.trimmed();
    if (port != current.port)
        update.port = port;
    if (address != current.homeAddress)
        update.homeAddress = address;
    if (!update.port && !update.homeAddress)
        return std::nullopt;
    return update;
}

QString settingsDialogHint()
{
    return QStringLiteral("Port und Adresse gelten erst mit der Browserquelle (P10). Leere Adresse: "
                          "Der Kern trägt beim nächsten Start die Adresse der Netzwerkkarte mit der "
                          "Standardroute ein.");
}

QString settingsDialogProblem(bool connected, const std::optional<SettingsInfo> &info)
{
    if (!connected)
        return QStringLiteral("Keine Verbindung zum Kern, Einstellungen nicht verfügbar.");
    if (info && !info->error.isEmpty())
        return info->error;
    return {};
}

} // namespace dss
