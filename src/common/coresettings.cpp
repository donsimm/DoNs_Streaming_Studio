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
            if (section != QLatin1String(kSection))
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
        if (seen.contains(key))
            return fail(QStringLiteral("«%1» doppelt").arg(key));
        seen.insert(key);
        if (key == QLatin1String(kPortKey)) {
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
            "# Start aus der Standardroute eintragen.\n";
    text += '[' + QByteArray(kSection) + "]\n";
    text += QByteArray(kPortKey) + '=' + QByteArray::number(settings.port) + '\n';
    text += QByteArray(kAddressKey) + '=' + settings.homeAddress.toUtf8() + '\n';
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
    if (!m_fileError.isEmpty()) {
        *error = QStringLiteral("Einstellungen nicht änderbar: %1. «Auf Standardwerte zurücksetzen» "
                                "oder die Datei von Hand reparieren.").arg(m_fileError);
        return false;
    }
    if (!port && !homeAddress) {
        *error = QStringLiteral("Keine Einstellung angegeben.");
        return false;
    }
    CoreSettings next = m_settings;
    if (port) {
        if (const QString e = validatePort(*port); !e.isEmpty()) {
            *error = e;
            return false;
        }
        next.port = *port;
    }
    if (homeAddress) {
        if (const QString e = validateHomeAddress(*homeAddress); !e.isEmpty()) {
            *error = e;
            return false;
        }
        // Punkt 78: nur eine Adresse, die eine Netzwerkkarte dieses PCs gerade hat (die
        // bisherige darf bleiben, auch wenn das Kabel gerade fehlt).
        if (*homeAddress != m_settings.homeAddress) {
            if (const QString e = checkLocalAddress(*homeAddress, m_localAddresses()); !e.isEmpty()) {
                *error = e;
                return false;
            }
        }
        next.homeAddress = *homeAddress;
    }
    if (next == m_settings)
        return true;
    if (!write(next, error))
        return false;
    m_settings = next;
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
