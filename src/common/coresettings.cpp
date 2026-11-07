#include "coresettings.h"

#include "systemstats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

namespace dss {

namespace {

constexpr char kSection[] = "overlay-server";
constexpr char kPortKey[] = "port";
constexpr char kAddressKey[] = "adresse-im-heimnetz";
// P10c: Verbindung zu OBS
constexpr char kObsSection[] = "obs";
constexpr char kObsEnabledKey[] = "aktiv";
constexpr char kObsHostKey[] = "adresse";
constexpr char kObsPortKey[] = "port";
constexpr char kYes[] = "ja";
constexpr char kNo[] = "nein";

} // namespace

CoreSettings defaultCoreSettings(Edition edition)
{
    CoreSettings s;
    s.port = edition == Edition::Installed ? 8080 : 8081;
    return s;
}

QString coreSettingsPath(Edition edition)
{
    return configDir(edition) + QStringLiteral("/kern.ini");
}

QString validatePort(int port)
{
    if (port < kPortMin || port > kPortMax)
        return QStringLiteral("Port %1 ungültig, erlaubt sind %2 bis %3.")
            .arg(port).arg(kPortMin).arg(kPortMax);
    return {};
}

QString validateHomeAddress(const QString &address)
{
    if (address.isEmpty())
        return {};
    // Nur die übliche Schreibweise; QHostAddress nähme auch «1» oder «192.168.1» an (inet_aton).
    static const QRegularExpression form(QStringLiteral(
        "\\A(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\.(0|[1-9][0-9]{0,2})\\z"));
    const QRegularExpressionMatch m = form.match(address);
    bool ok = m.hasMatch();
    for (int i = 1; ok && i <= 4; ++i)
        ok = m.captured(i).toInt() <= 255;
    if (!ok)
        return QStringLiteral("Adresse «%1» ungültig, erwartet ist eine IPv4-Adresse wie 192.168.1.9.")
            .arg(address);
    const int first = m.captured(1).toInt();
    if (address == QLatin1String("0.0.0.0") || first == 127 || first >= 224)
        return QStringLiteral("Adresse %1 ist keine Adresse im Heimnetz (nicht 0.0.0.0, 127.x.x.x, "
                              "224.0.0.0 bis 255.255.255.255).")
            .arg(address);
    return {};
}

QString validateObsHost(const QString &host)
{
    static const QRegularExpression form(QStringLiteral("\\A[A-Za-z0-9]([A-Za-z0-9.-]*[A-Za-z0-9])?\\z"));
    if (!host.isEmpty() && !form.match(host).hasMatch())
        return QStringLiteral("Adresse von OBS «%1» ungültig, erwartet ist eine Adresse wie 192.168.1.116.")
            .arg(host);
    return {};
}

QString validateObsPort(int port)
{
    if (port < kObsPortMin || port > kObsPortMax)
        return QStringLiteral("Port von OBS %1 ungültig, erlaubt sind %2 bis %3.")
            .arg(port).arg(kObsPortMin).arg(kObsPortMax);
    return {};
}

QString validateObsCombination(bool enabled, const QString &host)
{
    if (enabled && host.isEmpty())
        return QStringLiteral("«Mit OBS verbinden» braucht die Adresse von OBS.");
    return {};
}

bool parseCoreSettings(const QByteArray &text, const CoreSettings &defaults, CoreSettings *settings,
                       QString *error)
{
    CoreSettings s = defaults;
    QString section;
    QSet<QString> seen;
    const QList<QByteArray> lines = text.split('\n');
    for (qsizetype i = 0; i < lines.size(); ++i) {
        const QString line = QString::fromUtf8(lines.at(i)).trimmed();
        const auto fail = [&](const QString &what) {
            *error = QStringLiteral("Zeile %1: %2").arg(i + 1).arg(what);
            return false;
        };
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            section = line.mid(1, line.size() - 2).trimmed();
            if (section != QLatin1String(kSection) && section != QLatin1String(kObsSection))
                return fail(QStringLiteral("unbekannter Abschnitt [%1]").arg(section));
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            return fail(QStringLiteral("«%1» ist keine Angabe der Form Name=Wert").arg(line));
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (section.isEmpty())
            return fail(QStringLiteral("«%1» steht vor [%2]").arg(key, QLatin1String(kSection)));
        if (seen.contains(section + QLatin1Char('/') + key))
            return fail(QStringLiteral("«%1» doppelt").arg(key));
        seen.insert(section + QLatin1Char('/') + key);
        if (section == QLatin1String(kObsSection)) {
            if (key == QLatin1String(kObsEnabledKey)) {
                if (value != QLatin1String(kYes) && value != QLatin1String(kNo))
                    return fail(QStringLiteral("«%1» muss ja oder nein sein, nicht «%2»").arg(key, value));
                s.obsEnabled = value == QLatin1String(kYes);
            } else if (key == QLatin1String(kObsHostKey)) {
                if (const QString e = validateObsHost(value); !e.isEmpty())
                    return fail(e);
                s.obsHost = value;
            } else if (key == QLatin1String(kObsPortKey)) {
                bool number = false;
                const int port = value.toInt(&number);
                if (!number)
                    return fail(QStringLiteral("Port von OBS «%1» ist keine ganze Zahl").arg(value));
                if (const QString e = validateObsPort(port); !e.isEmpty())
                    return fail(e);
                s.obsPort = port;
            } else {
                return fail(QStringLiteral("unbekannte Angabe «%1»").arg(key));
            }
        } else if (key == QLatin1String(kPortKey)) {
            bool number = false;
            const int port = value.toInt(&number);
            if (!number)
                return fail(QStringLiteral("Port «%1» ist keine ganze Zahl").arg(value));
            if (const QString e = validatePort(port); !e.isEmpty())
                return fail(e);
            s.port = port;
        } else if (key == QLatin1String(kAddressKey)) {
            if (const QString e = validateHomeAddress(value); !e.isEmpty())
                return fail(e);
            s.homeAddress = value;
        } else {
            return fail(QStringLiteral("unbekannte Angabe «%1»").arg(key));
        }
    }
    if (const QString e = validateObsCombination(s.obsEnabled, s.obsHost); !e.isEmpty()) {
        *error = e;
        return false;
    }
    *settings = s;
    return true;
}

QByteArray serializeCoreSettings(const CoreSettings &settings)
{
    QByteArray text;
    text += "# Einstellungen des Kerns von DoNs Streaming Studio. Nur der Kern schreibt diese Datei;\n"
            "# ändern im Fenster unter «Werkzeuge → Einstellungen …» oder mit dss-cli.\n"
            "# port: Port des Overlay-Servers (" + QByteArray::number(kPortMin) + " bis "
            + QByteArray::number(kPortMax) + ").\n"
            "# adresse-im-heimnetz: IPv4-Adresse des Studio-PCs im Heimnetz; leer = beim nächsten\n"
            "# Start aus der Standardroute eintragen.\n"
            "# [obs]: aktiv ja oder nein, Adresse und Port von obs-websocket auf dem Streaming-PC. Das\n"
            "# Passwort steht nicht hier, sondern verschlüsselt in obs-passwort.cred.\n";
    text += '[' + QByteArray(kSection) + "]\n";
    text += QByteArray(kPortKey) + '=' + QByteArray::number(settings.port) + '\n';
    text += QByteArray(kAddressKey) + '=' + settings.homeAddress.toUtf8() + '\n';
    text += '[' + QByteArray(kObsSection) + "]\n";
    text += QByteArray(kObsEnabledKey) + '=' + (settings.obsEnabled ? kYes : kNo) + '\n';
    text += QByteArray(kObsHostKey) + '=' + settings.obsHost.toUtf8() + '\n';
    text += QByteArray(kObsPortKey) + '=' + QByteArray::number(settings.obsPort) + '\n';
    return text;
}

QString homeAddressFromRoute(const QByteArray &routeText,
                             const std::function<QList<QHostAddress>(const QString &)> &addressesOf,
                             QString *interface, QString *reason)
{
    const QString iface = defaultRouteInterface(routeText);
    if (interface)
        *interface = iface;
    if (iface.isEmpty()) {
        if (reason)
            *reason = QStringLiteral("keine Standardroute mit IPv4");
        return {};
    }
    for (const QHostAddress &a : addressesOf(iface)) {
        if (a.protocol() != QAbstractSocket::IPv4Protocol || a.isLoopback() || a.isLinkLocal())
            continue;
        return a.toString();
    }
    if (reason)
        *reason = QStringLiteral("Netzwerkkarte %1 der Standardroute hat keine IPv4-Adresse").arg(iface);
    return {};
}

QString detectHomeAddress(QString *interface, QString *reason)
{
    QFile route(QStringLiteral("/proc/net/route"));
    const QByteArray text = route.open(QIODevice::ReadOnly) ? route.readAll() : QByteArray();
    return homeAddressFromRoute(text, [](const QString &name) {
        QList<QHostAddress> list;
        for (const QNetworkAddressEntry &e : QNetworkInterface::interfaceFromName(name).addressEntries())
            list.append(e.ip());
        return list;
    }, interface, reason);
}

QStringList localIpv4Addresses()
{
    QStringList result;
    for (const QHostAddress &a : QNetworkInterface::allAddresses()) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol && !a.isLoopback())
            result.append(a.toString());
    }
    result.sort();
    result.removeDuplicates();
    return result;
}

QString checkLocalAddress(const QString &address, const QStringList &localAddresses)
{
    if (address.isEmpty() || localAddresses.contains(address))
        return {};
    if (localAddresses.isEmpty())
        return QStringLiteral("%1 gehört nicht zu diesem PC. Dieser PC hat gerade keine IPv4-Adresse "
                              "ausser 127.0.0.1 (Netzwerkkabel?).").arg(address);
    return QStringLiteral("%1 gehört nicht zu diesem PC. Adressen dieses PCs: %2.")
        .arg(address, localAddresses.join(QStringLiteral(", ")));
}

QString invalidSettingsPath(const QString &path, const QDateTime &when)
{
    return path + QStringLiteral(".ungueltig-") + when.toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
}

CoreSettingsStore::CoreSettingsStore(const QString &path, const CoreSettings &defaults,
                                     AddressDetector detector, LocalAddresses localAddresses)
    : m_path(path), m_defaults(defaults), m_detector(std::move(detector)),
      m_localAddresses(std::move(localAddresses)), m_settings(defaults)
{
}

bool CoreSettingsStore::fillHomeAddress(CoreSettings *settings, QStringList *log, QStringList *warnings)
{
    QString iface;
    QString reason;
    const QString address = m_detector(&iface, &reason);
    if (address.isEmpty()) {
        warnings->append(QStringLiteral("Einstellungen: %1, Adresse im Heimnetz bleibt leer").arg(reason));
        return false;
    }
    settings->homeAddress = address;
    log->append(QStringLiteral("Einstellungen: Adresse im Heimnetz %1 eingetragen (Netzwerkkarte "
                               "%2 der Standardroute)").arg(address, iface));
    return true;
}

void CoreSettingsStore::load(QStringList *log, QStringList *warnings)
{
    m_settings = m_defaults;
    m_fileError.clear();
    bool exists = QFileInfo::exists(m_path);
    if (exists) {
        QFile file(m_path);
        CoreSettings read;
        QString error;
        if (!file.open(QIODevice::ReadOnly)) {
            m_fileError = QStringLiteral("%1 nicht lesbar (%2)").arg(m_path, file.errorString());
        } else if (!parseCoreSettings(file.readAll(), m_defaults, &read, &error)) {
            m_fileError = QStringLiteral("%1 ungültig: %2").arg(m_path, error);
        } else {
            m_settings = read;
        }
        if (!m_fileError.isEmpty()) {
            // Meldungen der Prüfung enden mit Punkt; hier folgt noch ein Satz.
            if (m_fileError.endsWith(QLatin1Char('.')))
                m_fileError.chop(1);
            warnings->append(m_fileError + QStringLiteral(". Es gelten die Standardwerte; die Datei "
                                                          "bleibt unverändert, bis sie von Hand "
                                                          "repariert ist."));
            return;
        }
    }
    bool changed = !exists;
    // Einmal ermitteln und festschreiben (Entscheid 2026-11-03); danach nicht mehr neu.
    if (m_settings.homeAddress.isEmpty() && fillHomeAddress(&m_settings, log, warnings))
        changed = true;
    if (changed) {
        QString error;
        if (!write(m_settings, &error))
            warnings->append(QStringLiteral("Einstellungen nicht gespeichert: ") + error);
        else if (!exists)
            log->append(QStringLiteral("Einstellungen: %1 angelegt").arg(m_path));
    }
    log->append(QStringLiteral("Einstellungen: Port %1, Adresse im Heimnetz %2 (%3)")
                    .arg(m_settings.port)
                    .arg(m_settings.homeAddress.isEmpty() ? QStringLiteral("leer") : m_settings.homeAddress,
                         m_path));
}

bool CoreSettingsStore::change(std::optional<int> port, std::optional<QString> homeAddress,
                               QString *error)
{
    return change(CoreSettingsChange{port, homeAddress, {}, {}, {}}, error);
}

std::optional<CoreSettings> CoreSettingsStore::applied(const CoreSettingsChange &change, QString *error) const
{
    const auto fail = [error](const QString &message) {
        *error = message;
        return std::optional<CoreSettings>();
    };
    if (!m_fileError.isEmpty())
        return fail(QStringLiteral("Einstellungen nicht änderbar: %1. «Auf Standardwerte zurücksetzen» "
                                   "oder die Datei von Hand reparieren.").arg(m_fileError));
    if (change.isEmpty())
        return fail(QStringLiteral("Keine Einstellung angegeben."));
    CoreSettings next = m_settings;
    if (change.port) {
        if (const QString e = validatePort(*change.port); !e.isEmpty())
            return fail(e);
        next.port = *change.port;
    }
    if (change.homeAddress) {
        if (const QString e = validateHomeAddress(*change.homeAddress); !e.isEmpty())
            return fail(e);
        // Punkt 78: nur eine Adresse, die eine Netzwerkkarte dieses PCs gerade hat (die
        // bisherige darf bleiben, auch wenn das Kabel gerade fehlt).
        if (*change.homeAddress != m_settings.homeAddress) {
            if (const QString e = checkLocalAddress(*change.homeAddress, m_localAddresses()); !e.isEmpty())
                return fail(e);
        }
        next.homeAddress = *change.homeAddress;
    }
    if (change.obsHost) {
        if (const QString e = validateObsHost(*change.obsHost); !e.isEmpty())
            return fail(e);
        next.obsHost = *change.obsHost;
    }
    if (change.obsPort) {
        if (const QString e = validateObsPort(*change.obsPort); !e.isEmpty())
            return fail(e);
        next.obsPort = *change.obsPort;
    }
    if (change.obsEnabled)
        next.obsEnabled = *change.obsEnabled;
    if (const QString e = validateObsCombination(next.obsEnabled, next.obsHost); !e.isEmpty())
        return fail(e);
    return next;
}

QString CoreSettingsStore::checkChange(const CoreSettingsChange &change) const
{
    QString error;
    applied(change, &error);
    return error;
}

bool CoreSettingsStore::change(const CoreSettingsChange &change, QString *error)
{
    const std::optional<CoreSettings> next = applied(change, error);
    if (!next)
        return false;
    if (*next == m_settings)
        return true;
    if (!write(*next, error))
        return false;
    m_settings = *next;
    return true;
}

bool CoreSettingsStore::resetInvalid(const QDateTime &now, QString *renamedTo, QStringList *log,
                                     QString *error)
{
    if (m_fileError.isEmpty()) {
        *error = QStringLiteral("%1 ist gültig; nichts zurückgesetzt.").arg(m_path);
        return false;
    }
    const QString target = invalidSettingsPath(m_path, now);
    if (QFileInfo::exists(target)) {
        *error = QStringLiteral("%1 gibt es schon; bitte in einer Sekunde noch einmal versuchen.").arg(target);
        return false;
    }
    // Eine nicht lesbare Datei fehlt vielleicht ganz (dann gibt es nichts umzubenennen).
    if (QFileInfo::exists(m_path) && !QFile::rename(m_path, target)) {
        *error = QStringLiteral("%1 kann nicht in %2 umbenannt werden.").arg(m_path, target);
        return false;
    }
    CoreSettings next = m_defaults;
    QStringList warnings;
    fillHomeAddress(&next, log, &warnings);
    log->append(warnings);
    if (!write(next, error)) {
        // Alte Datei zurück, damit der Stand derselbe bleibt.
        QFile::rename(target, m_path);
        return false;
    }
    m_settings = next;
    m_fileError.clear();
    *renamedTo = target;
    log->append(QStringLiteral("Einstellungen: ungültige Datei umbenannt in %1, %2 mit Standardwerten "
                               "neu geschrieben").arg(target, m_path));
    return true;
}

bool CoreSettingsStore::fillMissingHomeAddress(QStringList *log)
{
    if (!m_fileError.isEmpty() || !m_settings.homeAddress.isEmpty())
        return false;
    QString iface;
    QString reason;
    const QString address = m_detector(&iface, &reason);
    if (address.isEmpty())
        return false;
    CoreSettings next = m_settings;
    next.homeAddress = address;
    QString error;
    if (!write(next, &error)) {
        log->append(QStringLiteral("Einstellungen: Adresse im Heimnetz %1 nicht gespeichert: %2")
                        .arg(address, error));
        return false;
    }
    m_settings = next;
    log->append(QStringLiteral("Einstellungen: Adresse im Heimnetz %1 eingetragen (Netzwerkkarte "
                               "%2 der Standardroute, ohne Neustart)").arg(address, iface));
    return true;
}

HomeAddressWatcher::HomeAddressWatcher(CoreSettingsStore *settings, int intervalMs, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
{
    auto *timer = new QTimer(this);
    timer->setInterval(intervalMs);
    connect(timer, &QTimer::timeout, this, &HomeAddressWatcher::check);
    timer->start();
}

bool HomeAddressWatcher::check()
{
    QStringList log;
    const bool filled = m_settings->fillMissingHomeAddress(&log);
    if (filled)
        emit this->filled(log.join(QLatin1Char(' ')));
    else if (!log.isEmpty())
        qWarning("%s", qPrintable(log.join(QLatin1Char(' '))));
    return filled;
}

bool CoreSettingsStore::write(const CoreSettings &settings, QString *error)
{
    const QString dir = QFileInfo(m_path).absolutePath();
    if (!QDir().mkpath(dir)) {
        *error = QStringLiteral("Ordner %1 kann nicht angelegt werden").arg(dir);
        return false;
    }
    // Atomar: vorläufige Datei im selben Ordner, erst commit() benennt um.
    QSaveFile file(m_path);
    const QByteArray data = serializeCoreSettings(settings);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QStringLiteral("%1 kann nicht geschrieben werden (%2)").arg(m_path, file.errorString());
        return false;
    }
    return true;
}

} // namespace dss
