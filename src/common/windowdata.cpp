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

std::optional<SettingsUpdate> settingsUpdateFromForm(const CoreSettings &current, const SettingsForm &form,
                                                     bool obsInError)
{
    SettingsUpdate update;
    const QString address = form.address.trimmed();
    const QString obsHost = form.obsHost.trimmed();
    if (form.port != current.port)
        update.port = form.port;
    if (address != current.homeAddress)
        update.homeAddress = address;
    if (form.obsEnabled != current.obsEnabled)
        update.obsEnabled = form.obsEnabled;
    if (obsHost != current.obsHost)
        update.obsHost = obsHost;
    if (form.obsPort != current.obsPort)
        update.obsPort = form.obsPort;
    if (!form.obsPassword.isEmpty())
        update.obsPassword = form.obsPassword;
    else if (form.deleteObsPassword)
        update.obsPassword = QString();
    if (!update.port && !update.homeAddress && !update.obsEnabled && !update.obsHost && !update.obsPort
        && !update.obsPassword) {
        if (!form.obsEnabled || !obsInError)
            return std::nullopt;
        update.obsEnabled = form.obsEnabled;
    }
    return update;
}

QString settingsDialogHint()
{
    return QStringLiteral("Port und Adresse gelten sofort: Der Overlay-Server startet mit den neuen "
                          "Werten neu. Quellen, die das Studio in OBS angelegt hat, führt es nach; "
                          "andere Browserquellen brauchen in OBS die neue Adresse. Nur Adressen dieses "
                          "PCs. Leere Adresse: Der Kern trägt innerhalb von 2 s die Adresse der "
                          "Netzwerkkarte mit der Standardroute ein, sobald es eine gibt.");
}

QString obsPasswordPlaceholder(bool passwordSet, bool deleting)
{
    if (deleting)
        return QStringLiteral("wird mit «OK» gelöscht");
    return passwordSet ? QStringLiteral("gespeichert; leer lassen = behalten")
                       : QStringLiteral("kein Passwort gespeichert");
}

QString obsToolTip(ConnectionState state, const std::optional<ObsStatusInfo> &status)
{
    const QString plain = connectionStateText(Connection::Obs, state);
    if (!status || state == ConnectionState::Off)
        return plain;
    if (state != ConnectionState::Connected)
        return status->message.isEmpty() ? plain : status->message;
    QStringList details;
    if (!status->obsVersion.isEmpty())
        details.append(QStringLiteral("OBS %1").arg(status->obsVersion));
    if (!status->currentScene.name.isEmpty())
        details.append(QStringLiteral("Szene «%1»").arg(status->currentScene.name));
    return details.isEmpty() ? plain : QStringLiteral("%1 (%2)").arg(plain, details.join(QStringLiteral(", ")));
}

ObsSourceRow obsSourceRow(bool overlaySelected, ConnectionState obsState, const ObsSourceInfo &source)
{
    ObsSourceRow row;
    const bool connected = obsState == ConnectionState::Connected;
    row.addsToScene = overlaySelected && source.state == ObsSourceState::Present;
    row.canAct = overlaySelected && connected;
    row.buttonText = row.addsToScene ? QStringLiteral("In weitere Szene einfügen …") : QStringLiteral("In OBS anlegen …");
    if (!row.canAct)
        row.buttonToolTip = QStringLiteral("Geht nur mit gewähltem Overlay und verbundenem OBS");
    else if (row.addsToScene)
        row.buttonToolTip = QStringLiteral("Fügt dieselbe Quelle in eine weitere Szene ein");
    else
        row.buttonToolTip = QStringLiteral("Legt für dieses Overlay eine Browserquelle in OBS an");
    if (!overlaySelected) {
        row.text = QStringLiteral("–");
    } else if (source.state == ObsSourceState::Missing) {
        row.text = QStringLiteral("Quelle in OBS fehlt");
        row.problem = true;
    } else if (source.state == ObsSourceState::Present) {
        row.text = QStringLiteral("Quelle «%1» in OBS").arg(source.inputName);
        if (!connected && obsState != ConnectionState::Off)
            row.text += QStringLiteral(", OBS gerade nicht verbunden");
        row.problem = obsState == ConnectionState::Error;
    } else if (connected) {
        row.text = QStringLiteral("Noch keine Quelle in OBS");
    } else if (obsState == ConnectionState::Connecting) {
        row.text = QStringLiteral("Verbindung zu OBS wird aufgebaut");
    } else if (obsState == ConnectionState::Error) {
        row.text = QStringLiteral("OBS nicht verbunden");
        row.problem = true;
    } else {
        row.text = QStringLiteral("OBS nicht eingerichtet oder abgeschaltet");
    }
    return row;
}

QString settingsDialogProblem(bool connected, const std::optional<SettingsInfo> &info)
{
    if (!connected)
        return QStringLiteral("Keine Verbindung zum Kern, Einstellungen nicht verfügbar.");
    if (info && !info->error.isEmpty())
        return info->error;
    return {};
}

BrowserSourceField browserSourceField(const QUuid &selectedOverlay,
                                      const std::optional<BrowserSourceInfo> &info)
{
    if (selectedOverlay.isNull())
        return {QString(), QStringLiteral("Kein Overlay gewählt"), false};
    if (!info || info->overlayId != selectedOverlay)
        return {QString(), QStringLiteral("Adresse wird vom Kern geholt …"), false};
    if (!info->url.isEmpty())
        return {info->url, QString(), true};
    return {QString(), info->problem, false};
}

QString outputStateText(const std::optional<bool> &output, const std::optional<int> &pages)
{
    if (!output)
        return QStringLiteral("Ausgabe: –");
    QString text = *output ? QStringLiteral("Ausgabe: ein")
                           : QStringLiteral("Ausgabe: aus (Browserquelle leer)");
    if (pages) {
        if (*pages <= 0)
            text += QStringLiteral(", keine Browserquelle verbunden");
        else if (*pages == 1)
            text += QStringLiteral(", 1 Browserquelle verbunden");
        else
            text += QStringLiteral(", %1 Browserquellen verbunden").arg(*pages);
    }
    return text;
}

QString outputToolTip(bool output)
{
    return output ? QStringLiteral("Ausgabe ein: Die Browserquelle zeigt das Overlay. Klicken schaltet aus.")
                  : QStringLiteral("Ausgabe aus: Die Browserquelle ist leer. Klicken schaltet ein.");
}

ServerMessages serverMessages(bool connected, const std::optional<ServerStatusInfo> &info)
{
    if (!connected || !info)
        return {};
    QStringList lines;
    if (!info->enabled)
        lines.append(QStringLiteral("Der Kern ist ohne Overlay-Server gebaut (Schalter "
                                    "DSS_CORE_OVERLAY_SERVER); es gibt keine Browserquelle."));
    lines.append(info->problems);
    if (!info->invalidUse.isEmpty())
        lines.append(info->invalidUse);
    if (lines.isEmpty())
        return {};
    // Erster Satz: bis zum ersten «. » (Adressen wie 192.168.1.9 enthalten Punkte ohne Leerzeichen).
    QString first = lines.first();
    const qsizetype end = first.indexOf(QLatin1String(". "));
    if (end >= 0)
        first = first.left(end + 1);
    constexpr int kMaxShort = 90;
    QString shortText = QStringLiteral("Browserquelle: ") + first;
    if (shortText.size() > kMaxShort)
        shortText = shortText.left(kMaxShort - 1) + QStringLiteral("…");
    if (lines.size() > 1)
        shortText += QStringLiteral(" (+%1)").arg(lines.size() - 1);
    return {lines.join(QLatin1Char('\n')), shortText};
}

} // namespace dss
