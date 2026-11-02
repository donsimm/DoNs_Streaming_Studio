// dss-app: Fenster nach docs/bilder/oberflaeche.png, in P05 noch ohne Daten.

#include "mainwindow.h"
#include "theme.h"

#include "logging.h"
#include "unixsignals.h"
#include "version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QTextStream>

#include <csignal>

int main(int argc, char *argv[])
{
    const QString name = QStringLiteral("dss-app");

    // --version vor QApplication auswerten, damit es auch ohne Fenstersystem geht.
    QStringList arguments;
    for (int i = 0; i < argc; ++i)
        arguments << QString::fromLocal8Bit(argv[i]);
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DoNs Streaming Studio – Fenster"));
    const QCommandLineOption helpOption(QStringList{QStringLiteral("h"), QStringLiteral("help")},
                                        QStringLiteral("Hilfe ausgeben und beenden."));
    const QCommandLineOption versionOption(QStringLiteral("version"),
                                           QStringLiteral("Version ausgeben und beenden."));
    parser.addOption(helpOption);
    parser.addOption(versionOption);
    // Unbekannte Optionen (zum Beispiel von Qt wie -platform) bleiben QApplication überlassen.
    parser.parse(arguments);
    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << dss::versionLine(name) << '\n';
        return 0;
    }
    if (parser.isSet(helpOption)) {
        QTextStream(stdout) << parser.helpText();
        return 0;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(name);
    QApplication::setApplicationVersion(dss::version());
    dss::setupLogging();

    // Fusion: derselbe Grundstil unabhängig von der Desktop-Umgebung.
    // https://doc.qt.io/qt-6.8/qstylefactory.html
    QApplication::setStyle(QStringLiteral("Fusion"));
    const Theme theme = darkTheme();
    QApplication::setPalette(palette(theme));
    app.setStyleSheet(styleSheet(theme));

    MainWindow window(theme, true);

    // SIGINT/SIGTERM schliessen das Fenster, also derselbe Weg wie beim Schliessen von Hand.
    dss::UnixSignalWatcher signalWatcher;
    if (!signalWatcher.isValid()) {
        qCritical("Signale SIGINT/SIGTERM konnten nicht eingerichtet werden");
        return 1;
    }
    QObject::connect(&signalWatcher, &dss::UnixSignalWatcher::terminationRequested, &window,
                     [&window](int signalNumber) {
                         qInfo("%s erhalten, schliesse Fenster",
                               signalNumber == SIGINT ? "SIGINT" : "SIGTERM");
                         window.close();
                     });

    qInfo().noquote() << dss::startLine(name);
    window.show();
    const int rc = app.exec();
    qInfo("beendet, Exit-Code %d", rc);
    return rc;
}
