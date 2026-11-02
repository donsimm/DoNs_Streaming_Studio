// dss-app: Fenster nach docs/bilder/oberflaeche.png. Seit P07 mit Daten vom Kern: startet den
// Kern-Dienst, falls er nicht läuft, und verbindet sich mit ihm (docs/08-kern-schnittstelle.md).

#include "fonts.h"
#include "mainwindow.h"
#include "symbols.h"
#include "theme.h"

#include "configpaths.h"
#include "coreclient.h"
#include "logging.h"
#include "unixsignals.h"
#include "version.h"
#include "windowsize.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFont>
#include <QPixmap>
#include <QTextStream>
#include <QTimer>

#include <memory>

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
        QStringLiteral("Fenster in 1280x800 (oder --fenstergroesse) öffnen, nach dem ersten "
                       "Zeichnen als Bild speichern und beenden."),
        QStringLiteral("DATEI"));
    const QCommandLineOption statesOption(
        QStringLiteral("zustaende-zeigen"),
        QStringLiteral("Nur mit --bildschirmfoto: die vier Verbindungsknöpfe in den vier "
                       "Zuständen zeigen (Twitch grün, YouTube orange, OBS rot, NDI grau)."));
    const QCommandLineOption sizeOption(
        QStringLiteral("fenstergroesse"),
        QStringLiteral("Nur mit --bildschirmfoto: Grösse des Fensters, Standard 1280x800."),
        QStringLiteral("BREITExHOEHE"));
    const QCommandLineOption connectionOption(
        QStringLiteral("verbindung"),
        QStringLiteral("Unix-Socket des Kerns (Standard: "
                       "$XDG_RUNTIME_DIR/dons-streaming-studio/kern.sock). Mit --bildschirmfoto: "
                       "erst nach Verbindung, erstem Overlay und 3.5 s Leistungswerten speichern."),
        QStringLiteral("PFAD"));
    const QCommandLineOption unitOption(
        QStringLiteral("einheit"),
        QStringLiteral("Benutzerdienst, der gestartet wird, wenn der Kern nicht antwortet "
                       "(Standard: dss-core.service)."),
        QStringLiteral("NAME"));
    parser.addOption(helpOption);
    parser.addOption(connectionOption);
    parser.addOption(unitOption);
    parser.addOption(versionOption);
    parser.addOption(screenshotOption);
    parser.addOption(statesOption);
    parser.addOption(sizeOption);
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
    if (parser.isSet(statesOption) && !parser.isSet(screenshotOption)) {
        QTextStream(stderr) << "Fehler: --zustaende-zeigen gilt nur mit --bildschirmfoto.\n";
        return 2;
    }

    QSize screenshotSize(1280, 800);
    if (parser.isSet(sizeOption)) {
        if (!parser.isSet(screenshotOption)) {
            QTextStream(stderr) << "Fehler: --fenstergroesse gilt nur mit --bildschirmfoto.\n";
            return 2;
        }
        if (!dss::parseWindowSize(parser.value(sizeOption), &screenshotSize)) {
            QTextStream(stderr) << "Fehler: --fenstergroesse erwartet BREITExHOEHE, zum Beispiel "
                                   "1920x1080.\n";
            return 2;
        }
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
    app.setStyleSheet(styleSheet(theme, symbolPath));

    // Verbindung zum Kern. Beim Bildschirmfoto nur mit --verbindung und ohne Start der Einheit,
    // damit Prüfungen nie den echten Kern starten.
    std::unique_ptr<dss::CoreClient> client;
    if (screenshotFile.isEmpty() || parser.isSet(connectionOption)) {
        dss::CoreClient::Options options;
        options.socketPath = parser.isSet(connectionOption) ? parser.value(connectionOption)
                                                            : dss::defaultConnectionPath();
        options.program = name;
        options.version = dss::version();
        options.unitName = parser.isSet(unitOption) ? parser.value(unitOption)
                                                    : QString::fromLatin1(dss::kCoreUnitName);
        options.startUnit = screenshotFile.isEmpty();
        client = std::make_unique<dss::CoreClient>(options);
    }

    MainWindow window(theme, screenshotFile.isEmpty(), client.get());
    if (parser.isSet(statesOption)) {
        for (dss::Connection c : {dss::Connection::Twitch, dss::Connection::YouTube,
                                  dss::Connection::Obs, dss::Connection::Ndi})
            window.setConnectionState(c, dss::shownState(c));
    }

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
        window.resize(screenshotSize);
        // Mit --fenstergroesse am Fenstermanager vorbei, sonst begrenzt er das Fenster auf
        // die Arbeitsfläche (1920x1080 wurde unter Xfce zu 1920x1029, P06).
        // https://doc.qt.io/qt-6.8/qt.html#WindowType-enum
        if (parser.isSet(sizeOption))
            window.setWindowFlag(Qt::X11BypassWindowManagerHint);
        const auto takeScreenshot = [&window, screenshotFile] {
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
        };
        if (!client) {
            QObject::connect(&window, &MainWindow::firstPainted, &app, takeScreenshot);
        } else {
            // Mit Daten: erstes Overlay und erste Quelle wählen wie mit der Maus, dann einige
            // Leistungswerte abwarten, damit der Verlauf zu sehen ist.
            QObject::connect(client.get(), &dss::CoreClient::overlayListChanged, &window,
                             [&window] { window.selectOverlayRow(0); });
            QObject::connect(client.get(), &dss::CoreClient::overlayChanged, &window,
                             [&window] { window.selectSourceRow(0); });
            QObject::connect(client.get(), &dss::CoreClient::connectedChanged, &window,
                             [&window, takeScreenshot](bool connected) {
                                 if (connected)
                                     QTimer::singleShot(3500, &window, takeScreenshot);
                             });
        }
    }

    qInfo().noquote() << dss::startLine(name);
    window.show();
    if (client)
        client->start();
    const int rc = app.exec();
    qInfo("beendet, Exit-Code %d", rc);
    return rc;
}
