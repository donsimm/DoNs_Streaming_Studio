// dss-core: Kern ohne Fenster (P07): hält die Overlays aus dem Datenordner, nimmt die Sperre
// im Datenordner (einziger, der schreibt). Die Verbindung zu den Fenstern folgt in Teil D.
// Läuft als Benutzerdienst von systemd (scripts/dienst-einrichten.sh), lässt sich aber auch
// direkt starten. Schnittstelle: docs/08-kern-schnittstelle.md

#include "configpaths.h"
#include "datalock.h"
#include "logging.h"
#include "overlaystore.h"
#include "systemdnotify.h"
#include "unixsignals.h"
#include "version.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

#include <csignal>

namespace {

// Exit-Codes. 3 verhindert in der Einheit den Neustart (RestartPreventExitStatus=3), weil ein
// zweiter Start an derselben Sperre oder Verbindung wieder scheitern würde.
constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitAlreadyRunning = 3;

} // namespace

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
    const QCommandLineOption dataDirOption(
        QStringLiteral("datenordner"),
        QStringLiteral("Datenordner mit den Overlays (Standard: ~/.local/share/dons-streaming-studio)."),
        QStringLiteral("PFAD"));
    const QCommandLineOption connectionOption(
        QStringLiteral("verbindung"),
        QStringLiteral("Unix-Socket für die Fenster (Standard: "
                       "$XDG_RUNTIME_DIR/dons-streaming-studio/kern.sock)."),
        QStringLiteral("PFAD"));
    parser.addOption(versionOption);
    parser.addOption(dataDirOption);
    parser.addOption(connectionOption);
    parser.process(app);
    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << dss::versionLine(name) << '\n';
        return kExitOk;
    }

    dss::setupLogging();
    qInfo().noquote() << dss::startLine(name);

    const QString dataDir = parser.isSet(dataDirOption) ? parser.value(dataDirOption)
                                                        : dss::defaultDataDir();
    const QString socketPath = parser.isSet(connectionOption) ? parser.value(connectionOption)
                                                              : dss::defaultConnectionPath();

    dss::UnixSignalWatcher signalWatcher(nullptr, true);
    if (!signalWatcher.isValid()) {
        qCritical("Signale SIGINT/SIGTERM/SIGHUP konnten nicht eingerichtet werden");
        return kExitError;
    }

    // Sperre zuerst: Solange der Kern läuft, ändert nur er die Overlay-Dateien (Entscheid 2).
    dss::DataLock lock(dataDir);
    QString error;
    if (!lock.tryLock(&error)) {
        qCritical("Kern startet nicht: %s (%s)", qPrintable(error),
                  qPrintable(dss::lockFilePath(dataDir)));
        return kExitAlreadyRunning;
    }

    dss::OverlayStore store(dataDir);
    if (!store.load(&error)) {
        qCritical("Datenordner %s: %s", qPrintable(store.dataDir()), qPrintable(error));
        return kExitError;
    }
    for (const QString &e : store.loadErrors())
        qWarning("Fehler beim Laden: %s", qPrintable(e));
    for (const QString &w : store.loadWarnings())
        qWarning("Warnung: %s", qPrintable(w));
    qInfo("Datenordner %s: %lld Overlays geladen", qPrintable(store.dataDir()),
          static_cast<long long>(store.overlays().size()));

    QObject::connect(&signalWatcher, &dss::UnixSignalWatcher::terminationRequested, &app,
                     [](int signalNumber) {
                         qInfo("%s erhalten, beende",
                               signalNumber == SIGINT ? "SIGINT" : "SIGTERM");
                         QCoreApplication::quit();
                     });
    Q_UNUSED(socketPath);

    // Bereit: erst jetzt kehrt «systemctl --user start» zurück (Type=notify).
    if (dss::notifySystemd("READY=1"))
        qInfo("systemd: bereit gemeldet");
    const int rc = app.exec();
    dss::notifySystemd("STOPPING=1");
    qInfo("beendet, Exit-Code %d", rc);
    return rc;
}
