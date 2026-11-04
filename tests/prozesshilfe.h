#pragma once

// Hilfen für Tests, die dss-core und dss-cli als eigene Prozesse starten (P07). Nur Qt Core.
// Datenordner und Verbindung sind immer eigene temporäre Ordner; die echten Daten unter
// ~/.local/share/dons-streaming-studio und der echte Socket werden nie berührt.

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QTest>

#include <memory>
#include <vector>

namespace testhelp {

// Kurzer Ordner für den Socket unter $XDG_RUNTIME_DIR (Pfad höchstens 107 Byte, unix(7)).
inline std::unique_ptr<QTemporaryDir> runtimeDir()
{
    return std::make_unique<QTemporaryDir>(
        QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
        + QStringLiteral("/dss-test-XXXXXX"));
}

// kern.ini im Ordner der Einstellungen configHome (Aufbau wie CoreSettingsStore); leere
// Adresse = der Kern trägt sie beim Start aus der Standardroute ein.
inline bool writeCoreSettings(const QString &configHome, int port, const QString &homeAddress = QString())
{
    const QString dir = configHome + QStringLiteral("/dons-streaming-studio-entwicklung");
    if (!QDir().mkpath(dir))
        return false;
    QFile f(dir + QStringLiteral("/kern.ini"));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QStringLiteral("[overlay-server]\nport=%1\nadresse-im-heimnetz=%2\n").arg(port).arg(homeAddress).toUtf8());
    return true;
}

// Eigener Port für jeden Testkern (P10, seit P10b Punkt 83 auch für eine neu angelegte oder
// zurückgesetzte kern.ini): 24000 bis 31999, je Prozess vier Ports nach der PID, danach weiter
// gezählt. Nie 8080 oder 8081: Der Overlay-Server der Testkerne stört so weder die Version in
// Arbeit (dss-core-entwicklung.service) noch die im Einsatz.
inline int nextTestPort()
{
    static int next = 0;
    return 24000 + int(QCoreApplication::applicationPid() % 2000) * 4 + (next++ % 4);
}

// Port, der zu einem Ordner aus testConfigHome() gehört (0, wenn unbekannt).
inline QHash<QString, int> &testPorts()
{
    static QHash<QString, int> ports;
    return ports;
}

inline int testPort(const QString &configHome)
{
    return testPorts().value(configHome, 0);
}

// Eigener Ordner der Einstellungen (XDG_CONFIG_HOME) für jeden gestarteten Kern (P10-vor): Der
// Kern schreibt dort kern.ini, nie in ~/.config. Die Ordner bleiben bis zum Ende des Tests.
inline QString testConfigHome()
{
    static std::vector<std::unique_ptr<QTemporaryDir>> dirs;
    dirs.push_back(std::make_unique<QTemporaryDir>());
    const int port = nextTestPort();
    writeCoreSettings(dirs.back()->path(), port);
    testPorts().insert(dirs.back()->path(), port);
    return dirs.back()->path();
}

// Startet dss-core mit eigenem Datenordner, eigener Verbindung und eigenem Ordner der
// Einstellungen; Protokoll auf stderr wird gesammelt (readAllStandardError). Der Kern bekommt
// den Port des Ordners auch als --standardport (Punkt 83): Eine fehlende, ungültige oder
// zurückgesetzte kern.ini bekommt so nie 8081.
inline std::unique_ptr<QProcess> startCore(const QString &dataDir, const QString &socketPath,
                                           const QString &configHome = testConfigHome())
{
    auto p = std::make_unique<QProcess>();
    p->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), configHome);
    p->setProcessEnvironment(env);
    if (testPort(configHome) == 0)
        testPorts().insert(configHome, nextTestPort());
    p->start(QStringLiteral(DSS_CORE_BIN),
             {QStringLiteral("--datenordner"), dataDir, QStringLiteral("--verbindung"), socketPath,
              QStringLiteral("--standardport"), QString::number(testPort(configHome))});
    return p;
}

// Wartet, bis das gesammelte Protokoll text enthält. log sammelt alles Gelesene.
inline bool waitForLog(QProcess *p, const QString &text, QString *log, int timeoutMs = 5000)
{
    const QDeadlineTimer deadline(timeoutMs);
    for (;;) {
        *log += QString::fromUtf8(p->readAllStandardError());
        if (log->contains(text))
            return true;
        if (deadline.hasExpired() || p->state() == QProcess::NotRunning) {
            *log += QString::fromUtf8(p->readAllStandardError());
            return log->contains(text);
        }
        p->waitForReadyRead(int(qMin<qint64>(100, deadline.remainingTime())));
    }
}

struct CliResult
{
    int exitCode = -1;
    QString out;
    QString err;
};

#ifdef DSS_CLI_BIN
// input: Eingabe auf stdin (zum Beispiel die Antwort auf eine Rückfrage); stdin wird danach
// geschlossen, damit eine Rückfrage nie wartet.
inline CliResult runCli(const QString &dataDir, const QStringList &args, const QByteArray &input = {})
{
    QProcess p;
    p.start(QStringLiteral(DSS_CLI_BIN), QStringList{QStringLiteral("--datenordner"), dataDir} + args);
    p.write(input);
    p.closeWriteChannel();
    p.waitForFinished(10000);
    return {p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1,
            QString::fromUtf8(p.readAllStandardOutput()), QString::fromUtf8(p.readAllStandardError())};
}
#endif

} // namespace testhelp
