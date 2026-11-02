// dss-app: Fenster nach docs/bilder/oberflaeche.png, in P05 noch ohne Daten.

#include "fonts.h"
#include "mainwindow.h"
#include "symbols.h"
#include "theme.h"

#include "logging.h"
#include "unixsignals.h"
#include "version.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QPixmap>
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
    const QCommandLineOption screenshotOption(
        QStringLiteral("bildschirmfoto"),
        QStringLiteral("Fenster in 1280x800 öffnen, nach dem ersten Zeichnen als Bild speichern "
                       "und beenden."),
        QStringLiteral("DATEI"));
    parser.addOption(helpOption);
    parser.addOption(versionOption);
    parser.addOption(screenshotOption);
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
    const QString screenshotFile = parser.value(screenshotOption);
    if (parser.isSet(screenshotOption) && screenshotFile.isEmpty()) {
        QTextStream(stderr) << "Fehler: --bildschirmfoto braucht einen Dateinamen.\n";
        return 2;
    }

    QApplication app(argc, argv);
    QApplication::setApplicationName(name);
    QApplication::setApplicationVersion(dss::version());
    dss::setupLogging();

    // Fusion: derselbe Grundstil unabhängig von der Desktop-Umgebung.
    // https://doc.qt.io/qt-6.8/qstylefactory.html
    QApplication::setStyle(QStringLiteral("Fusion"));
    const Theme theme = darkTheme();
    // Schrift aus src/app/schriften/, nicht aus einer Installation (Auftrag P06).
    const QStringList families = loadWindowFonts(fontDir());
    if (!families.contains(theme.fontFamily))
        qWarning("Schrift «%s» nicht geladen, Qt wählt einen Ersatz", qPrintable(theme.fontFamily));
    QApplication::setFont(QFont(theme.fontFamily, theme.fontSize));
    QApplication::setPalette(palette(theme));
    app.setStyleSheet(styleSheet(theme, symbolDir()));

    MainWindow window(theme, screenshotFile.isEmpty());

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

    if (!screenshotFile.isEmpty()) {
        // Bild des Fensters ohne Rahmen des Fenstermanagers, QWidget::grab():
        // https://doc.qt.io/qt-6.8/qwidget.html#grab
        window.resize(1280, 800);
        QObject::connect(&window, &MainWindow::firstPainted, &app, [&window, screenshotFile] {
            const QPixmap image = window.grab();
            if (!image.save(screenshotFile)) {
                qCritical("Bildschirmfoto konnte nicht gespeichert werden: %s",
                          qPrintable(screenshotFile));
                QCoreApplication::exit(1);
                return;
            }
            qInfo("Bildschirmfoto gespeichert: %s (%dx%d)", qPrintable(screenshotFile),
                  image.width(), image.height());
            window.close();
        });
    }

    qInfo().noquote() << dss::startLine(name);
    window.show();
    const int rc = app.exec();
    qInfo("beendet, Exit-Code %d", rc);
    return rc;
}
