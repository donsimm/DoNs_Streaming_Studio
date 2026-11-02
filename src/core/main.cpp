// dss-core: Kern ohne Fenster. In P03 nur Start, Protokollzeile und sauberes Beenden.

#include "logging.h"
#include "unixsignals.h"
#include "version.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include <csignal>

int main(int argc, char *argv[])
{
    const QString name = QStringLiteral("dss-core");

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(name);
    QCoreApplication::setApplicationVersion(dss::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DoNs Streaming Studio – Kern"));
    parser.addHelpOption();
    const QCommandLineOption versionOption(QStringLiteral("version"),
                                           QStringLiteral("Version ausgeben und beenden."));
    parser.addOption(versionOption);
    parser.process(app);
    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << dss::versionLine(name) << '\n';
        return 0;
    }

    dss::setupLogging();

    dss::UnixSignalWatcher signalWatcher;
    if (!signalWatcher.isValid()) {
        qCritical("Signale SIGINT/SIGTERM konnten nicht eingerichtet werden");
        return 1;
    }
    QObject::connect(&signalWatcher, &dss::UnixSignalWatcher::terminationRequested, &app,
                     [](int signalNumber) {
                         qInfo("%s erhalten, beende",
                               signalNumber == SIGINT ? "SIGINT" : "SIGTERM");
                         QCoreApplication::quit();
                     });

    qInfo().noquote() << dss::startLine(name);
    const int rc = app.exec();
    qInfo("beendet, Exit-Code %d", rc);
    return rc;
}
