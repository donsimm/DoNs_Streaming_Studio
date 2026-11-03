#pragma once

// Hilfen für Tests, die dss-core und dss-cli als eigene Prozesse starten (P07). Nur Qt Core.
// Datenordner und Verbindung sind immer eigene temporäre Ordner; die echten Daten unter
// ~/.local/share/dons-streaming-studio und der echte Socket werden nie berührt.

#include <QDeadlineTimer>
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

// Eigener Ordner der Einstellungen (XDG_CONFIG_HOME) für jeden gestarteten Kern (P10-vor): Der
// Kern schreibt dort kern.ini, nie in ~/.config. Die Ordner bleiben bis zum Ende des Tests.
inline QString testConfigHome()
{
    static std::vector<std::unique_ptr<QTemporaryDir>> dirs;
    dirs.push_back(std::make_unique<QTemporaryDir>());
    return dirs.back()->path();
}

// Startet dss-core mit eigenem Datenordner, eigener Verbindung und eigenem Ordner der
// Einstellungen; Protokoll auf stderr wird gesammelt (readAllStandardError).
inline std::unique_ptr<QProcess> startCore(const QString &dataDir, const QString &socketPath,
                                           const QString &configHome = testConfigHome())
{
    auto p = std::make_unique<QProcess>();
    p->setProcessChannelMode(QProcess::SeparateChannels);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), configHome);
    p->setProcessEnvironment(env);
    p->start(QStringLiteral(DSS_CORE_BIN),
             {QStringLiteral("--datenordner"), dataDir, QStringLiteral("--verbindung"), socketPath});
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
