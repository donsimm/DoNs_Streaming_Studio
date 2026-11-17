// dss-core: Kern ohne Fenster (P07): hält die Overlays aus dem Datenordner, nimmt die Sperre
// im Datenordner (einziger, der schreibt) und bedient die Fenster über einen Unix-Socket.
// Läuft als Benutzerdienst von systemd (scripts/dienst-einrichten.sh), lässt sich aber auch
// direkt starten. Schnittstelle: docs/08-kern-schnittstelle.md

#include "actionservice.h"
#include "configpaths.h"
#include "coreservice.h"
#include "coresettings.h"
#include "dataformat.h"
#include "datalock.h"
#include "logging.h"
#include "mediaimport.h"
#include "obsconnection.h"
#include "obssecretstore.h"
#include "obssources.h"
#include "overlaystore.h"
#if DSS_CORE_OVERLAY_SERVER
#include "overlayserver.h"
#endif
#if DSS_MEDIA_SIZE
#include "mediainfo.h"
#endif
#if DSS_CORE_AUDIO
#include "actionsound.h"
#include "mixer.h"
#include "nulloutput.h"
#include "pwoutput.h"
#endif
#if DSS_CORE_TWITCH
#include "twitchchat.h"
#include "twitchservice.h"
#endif
#if DSS_CORE_CAMERA
#include "cameraserviceclient.h"
#endif
#include "systemdnotify.h"
#include "unixsignals.h"
#include "version.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QTextStream>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <csignal>
#include <memory>
#include <optional>

namespace {

// Exit-Codes. 3 verhindert in der Einheit den Neustart (RestartPreventExitStatus=3), weil ein
// zweiter Start an derselben Sperre oder Verbindung wieder scheitern würde.
constexpr int kExitOk = 0;
constexpr int kExitError = 1;
constexpr int kExitAlreadyRunning = 3;
// --format-pruefen (P10a, Teil B 4): 4 = Dateien in älterem Format, Umwandeln nötig;
// 5 = Dateien in neuerem Format, als dieses Programm lesen kann (nicht installieren).
constexpr int kExitConversionNeeded = 4;
constexpr int kExitNewerFormat = 5;

// --format-pruefen und --umwandeln: Meldungen auf stdout, Fehler auf stderr.
int checkFormat(const QString &dataDir)
{
    QTextStream out(stdout);
    QTextStream err(stderr);
    const dss::DataFormatStatus status = dss::checkDataFormat(dataDir);
    if (!status.error.isEmpty()) {
        err << "Fehler: " << status.error << '\n';
        return kExitError;
    }
    for (const QString &f : status.unreadable)
        err << "Warnung: nicht lesbar, bleibt unverändert: " << f << '\n';
    for (const QString &f : status.newer)
        err << "Fehler: neueres Format, als dieses Programm lesen kann: " << f << '\n';
    for (const QString &f : status.older)
        out << "älteres Format: " << f << '\n';
    if (!status.newer.isEmpty())
        return kExitNewerFormat;
    if (status.needsConversion())
        return kExitConversionNeeded;
    out << "Format aktuell: " << dataDir << '\n';
    return kExitOk;
}

int convertFormat(const QString &dataDir)
{
    QStringList converted;
    QString error;
    if (!dss::convertDataFormat(dataDir, &converted, &error)) {
        QTextStream(stderr) << "Fehler: Umwandeln gescheitert, Dateien unverändert: " << error << '\n';
        return error.startsWith(QStringLiteral("Datenordner gesperrt")) ? kExitAlreadyRunning
                                                                        : kExitError;
    }
    QTextStream out(stdout);
    for (const QString &f : converted)
        out << "umgewandelt: " << f << '\n';
    out << converted.size() << (converted.size() == 1 ? " Datei" : " Dateien")
        << " umgewandelt: " << dataDir << '\n';
    return kExitOk;
}

} // namespace

int main(int argc, char *argv[])
{
    const QString name = QStringLiteral("dss-core");

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(name);
    QCoreApplication::setApplicationVersion(dss::version());

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("DoNs Streaming Studio – Kern (%1)")
                                         .arg(dss::editionLabel()));
    parser.addHelpOption();
    const QCommandLineOption versionOption(QStringLiteral("version"),
                                           QStringLiteral("Version ausgeben und beenden."));
    const QCommandLineOption dataDirOption(
        QStringLiteral("datenordner"),
        QStringLiteral("Datenordner mit den Overlays (Standard: ~/.local/share/%1).")
            .arg(dss::appDirName()),
        QStringLiteral("PFAD"));
    const QCommandLineOption connectionOption(
        QStringLiteral("verbindung"),
        QStringLiteral("Unix-Socket für die Fenster (Standard: $XDG_RUNTIME_DIR/%1/kern.sock).")
            .arg(dss::appDirName()),
        QStringLiteral("PFAD"));
    const QCommandLineOption checkFormatOption(
        QStringLiteral("format-pruefen"),
        QStringLiteral("Format der Dateien im Datenordner prüfen und beenden (P10a). Exit-Code 0 = "
                       "aktuell, 4 = Umwandeln nötig, 5 = neueres Format, 1 = Fehler."));
    const QCommandLineOption convertOption(
        QStringLiteral("umwandeln"),
        QStringLiteral("Dateien in älterem Format ins aktuelle umwandeln und beenden (P10a). Nur "
                       "bei angehaltenem Kern. Exit-Code 0 = erledigt, 3 = Kern läuft, 1 = Fehler "
                       "(Dateien unverändert)."));
    const QCommandLineOption settingsOption(
        QStringLiteral("einstellungen"),
        QStringLiteral("Datei der Einstellungen des Kerns (Standard: ~/.config/%1/kern.ini).")
            .arg(dss::appDirName()),
        QStringLiteral("PFAD"));
    // P10b, Punkt 83: Tests und Startprüfung geben jedem Kern einen eigenen Port, auch für eine
    // neu angelegte oder zurückgesetzte kern.ini; so stört ein laufender dss-core-entwicklung.service
    // (Port 8081) sie nicht.
    const QCommandLineOption defaultPortOption(
        QStringLiteral("standardport"),
        QStringLiteral("Port, der beim Anlegen oder Zurücksetzen von kern.ini gilt (Standard: %1; "
                       "für Tests).").arg(dss::defaultCoreSettings().port),
        QStringLiteral("PORT"));
#if DSS_CORE_AUDIO
    // P12b b4: tst_kernton prüft den Ton der Videos ohne Gerät und ohne PipeWire.
    const QCommandLineOption substituteOutputOption(
        QStringLiteral("ersatzausgang"),
        QStringLiteral("Ton der Videos an einen Ersatzausgang ohne Gerät statt an PipeWire (Gerät «%1»; "
                       "für Tests).").arg(QLatin1String(dss::NullOutput::kDeviceName)));
    parser.addOption(substituteOutputOption);
#endif
    // P14 b3, Stumm-Regel (Plan 3.4): Verbindung des Kerns der Version im Einsatz; Tests geben
    // einen eigenen Pfad, damit ein laufender Kern im Einsatz sie nicht stumm schaltet.
    const QCommandLineOption inUseOption(
        QStringLiteral("einsatz-verbindung"),
        QStringLiteral("Unix-Socket des Kerns der Version im Einsatz; antwortet dort ein Kern, startet "
                       "dieser Kern keine Läufe aus Ereignissen (Standard in Arbeit: %1; für Tests).")
            .arg(dss::defaultConnectionPath(dss::Edition::Installed)),
        QStringLiteral("PFAD"));
    const QCommandLineOption testEventsOption(
        QStringLiteral("testereignisse"),
        QStringLiteral("Nachricht test_event erlauben: Ereignisse vom Fenster auf den Ereignisfluss "
                       "(nur für Tests)."));
    parser.addOption(inUseOption);
    parser.addOption(testEventsOption);
#if DSS_CORE_TWITCH
    // P13 (Plan 2.6): Testserver statt id.twitch.tv, api.twitch.tv und eventsub.wss.twitch.tv. Nur
    // http://127.0.0.1:PORT, damit die Option kein Token an einen fremden Rechner schicken kann.
    const QCommandLineOption twitchTestServerOption(
        QStringLiteral("twitch-testserver"),
        QStringLiteral("Adressen von Twitch durch einen Testserver ersetzen, nur http://127.0.0.1:PORT "
                       "(für Tests)."),
        QStringLiteral("ADRESSE"));
    parser.addOption(twitchTestServerOption);
#endif
    parser.addOption(versionOption);
    parser.addOption(defaultPortOption);
    parser.addOption(dataDirOption);
    parser.addOption(settingsOption);
    parser.addOption(connectionOption);
    parser.addOption(checkFormatOption);
    parser.addOption(convertOption);
    parser.process(app);
    if (parser.isSet(versionOption)) {
        QTextStream(stdout) << dss::versionLine(name) << '\n';
        return kExitOk;
    }
    if (parser.isSet(checkFormatOption) || parser.isSet(convertOption)) {
        const QString dir = parser.isSet(dataDirOption) ? parser.value(dataDirOption)
                                                        : dss::defaultDataDir();
        return parser.isSet(convertOption) ? convertFormat(dir) : checkFormat(dir);
    }

    dss::setupLogging();
    qInfo().noquote() << dss::startLine(name);

#if DSS_CORE_TWITCH
    std::optional<QUrl> twitchTestServer;
    if (parser.isSet(twitchTestServerOption)) {
        const QUrl url(parser.value(twitchTestServerOption), QUrl::StrictMode);
        if (!url.isValid() || url.scheme() != QLatin1String("http") || url.host() != QLatin1String("127.0.0.1")
            || url.port() <= 0 || !url.userInfo().isEmpty() || (!url.path().isEmpty() && url.path() != QLatin1String("/"))
            || url.hasQuery() || url.hasFragment()) {
            qCritical("Kern startet nicht: --twitch-testserver %s ungültig, erlaubt ist nur http://127.0.0.1:PORT",
                      qPrintable(parser.value(twitchTestServerOption)));
            return kExitError;
        }
        twitchTestServer = url.adjusted(QUrl::StripTrailingSlash);
        qWarning("Twitch: Testserver %s statt Twitch (--twitch-testserver)", qPrintable(twitchTestServer->toString()));
    }
#endif

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

    // Reste von QSaveFile in media/ (Punkt 68): Nach einem Absturz oder SIGKILL mitten im
    // Übernehmen kann eine vorläufige Datei liegen bleiben (unter Linux nur, wenn das Dateisystem
    // keine Dateien ohne Namen kann, mediaimport.h). Jetzt läuft keine Übernahme, und die Sperre
    // hält nur dieser Kern.
    {
        const dss::LeftoverCleanup cleanup =
            dss::removeSaveFileLeftovers(dataDir + QStringLiteral("/media"));
        for (const QString &f : cleanup.removed)
            qInfo("Aufräumen: vorläufige Datei gelöscht: media/%s", qPrintable(f));
        for (const QString &f : cleanup.failed)
            qWarning("Aufräumen: nicht gelöscht: media/%s", qPrintable(f));
        qInfo("Aufräumen media/: %d vorläufige Dateien von QSaveFile gelöscht (%lld Byte)",
              cleanup.count, static_cast<long long>(cleanup.bytes));
    }

    // Einstellungen (P10-vor): kern.ini lesen, eine fehlende Adresse im Heimnetz einmal aus der
    // Standardroute eintragen.
    dss::CoreSettings settingsDefaults = dss::defaultCoreSettings();
    if (parser.isSet(defaultPortOption)) {
        bool ok = false;
        const int port = parser.value(defaultPortOption).toInt(&ok);
        if (!ok || !dss::validatePort(port).isEmpty()) {
            qCritical("Kern startet nicht: --standardport %s ungültig (%d bis %d)",
                      qPrintable(parser.value(defaultPortOption)), dss::kPortMin, dss::kPortMax);
            return kExitError;
        }
        settingsDefaults.port = port;
    }
    dss::CoreSettingsStore settings(parser.isSet(settingsOption) ? parser.value(settingsOption)
                                                                 : dss::coreSettingsPath(),
                                    settingsDefaults);
    {
        QStringList log;
        QStringList warnings;
        settings.load(&log, &warnings);
        for (const QString &line : log)
            qInfo("%s", qPrintable(line));
        for (const QString &line : warnings)
            qWarning("%s", qPrintable(line));
    }

    dss::OverlayStore store(dataDir);
#if DSS_MEDIA_SIZE
    // Grösse neuer Bild- und Videoquellen aus der Datei (P09, Entscheid 6)
    store.setMediaSizeReader(dss::readMediaSize);
    // Format der Videos (P10b, Punkt 81): Hinweis, wenn die Browserquelle es nicht abspielt
    store.setVideoCodecReader(dss::readVideoCodec);
    // P14 t: Länge der Töne im Dock «Medien»
    store.setMediaDurationReader(dss::readMediaDuration);
#endif
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

    CoreService service(&store, socketPath, &settings);
    switch (service.listen(&error)) {
    case CoreService::ListenResult::Ok:
        break;
    case CoreService::ListenResult::InUse:
        qCritical("Kern startet nicht: %s", qPrintable(error));
        return kExitAlreadyRunning;
    case CoreService::ListenResult::Failed:
        qCritical("Kern startet nicht: %s", qPrintable(error));
        return kExitError;
    }

#if DSS_CORE_OVERLAY_SERVER
    // Overlay-Server für die Browserquelle (P10, Teil A)
    dss::OverlayServer::Options serverOptions;
    serverOptions.webDir = QStringLiteral(DSS_WEB_DIR);
    // Ort der Version im Einsatz (scripts/installieren.sh), für die Meldung «Port belegt»
    serverOptions.installedDir = QDir::homePath() + QStringLiteral("/.local/opt/dons-streaming-studio");
#if DSS_MEDIA_SIZE
    serverOptions.videoOrientation = dss::readVideoOrientation;
#endif
    dss::OverlayServer overlayServer(&store, serverOptions);
    service.setOverlayServer(&overlayServer);
#endif

    // Verbindung zu OBS (P10c): standardmässig aus (kern.ini, Abschnitt [obs]). Das Passwort liegt
    // verschlüsselt neben kern.ini, je Version getrennt.
    dss::obs::ObsSecretStore obsSecrets(QFileInfo(settings.path()).absolutePath()
                                        + QStringLiteral("/obs-passwort.cred"));
    dss::obs::ObsConnection obsConnection([&obsSecrets](const dss::obs::ObsConnection::PasswordDone &done) {
        obsSecrets.read(done);
    });
    dss::obs::ObsSources obsSources(&obsConnection, dataDir + QStringLiteral("/obs-quellen.json"));
    service.setObsConnection(&obsConnection, &obsSecrets, &obsSources);

#if DSS_CORE_TWITCH
    // Anbindung an Twitch (P13): standardmässig aus (kern.ini, Abschnitt [twitch]). Token und Client
    // Secret liegen verschlüsselt neben kern.ini, je Version getrennt.
    dss::twitch::TwitchService::Options twitchOptions;
    twitchOptions.settingsDir = QFileInfo(settings.path()).absolutePath();
    twitchOptions.dataDir = dataDir;
    if (twitchTestServer)
        twitchOptions.endpoints = dss::twitch::Endpoints::testServer(*twitchTestServer);
    dss::twitch::TwitchService twitch(service.eventBus(), twitchOptions);
    service.setTwitchService(&twitch);
#endif

#if DSS_CORE_CAMERA
    // Kamera-Dienst auf dem Streaming-PC (P15 d2): standardmässig aus (kern.ini, Abschnitt
    // [kamera-dienst]). Nach service angelegt, also vor ihm zerstört.
    dss::camera::CameraServiceClient cameraService;
    service.setCameraService(&cameraService);
#endif

#if DSS_CORE_ACTIONS
    // Aktionssystem (P14 b3). Nach service angelegt, also vor ihm zerstört (speichert dabei).
    ActionService::Options actionOptions;
    actionOptions.dataDir = dataDir;
    if (parser.isSet(inUseOption))
        actionOptions.inUseSocket = parser.value(inUseOption);
    else if (dss::buildEdition() == dss::Edition::Development)
        actionOptions.inUseSocket = dss::defaultConnectionPath(dss::Edition::Installed);
    if (QFileInfo(actionOptions.inUseSocket).absoluteFilePath() == QFileInfo(socketPath).absoluteFilePath())
        actionOptions.inUseSocket.clear(); // nie sich selbst prüfen
    actionOptions.testEvents = parser.isSet(testEventsOption);
#if DSS_CORE_TWITCH
    // c1a: Chat senden über Twitch; nach twitch angelegt, also vor ihm zerstört
    TwitchChatTransport chatTransport(&twitch);
    actionOptions.chatTransport = &chatTransport;
    actionOptions.broadcasterId = [&chatTransport] { return chatTransport.broadcasterId(); };
    // c1b: Benutzerinfo, Shoutout, Ansage
    TwitchActions twitchActions(&twitch);
    actionOptions.twitchApi = &twitchActions;
#endif
    // c2: Laufzustand der Overlays und Töne des Chat-Bots (ActionSound legt erst der Block «Ton» an)
    actionOptions.overlayLive = service.overlayLive();
    // f2: Overlay «Chat-Bot» zusammen mit den mitgelieferten Aktionen
    actionOptions.templateCreated = [&service] { service.createChatBotOverlay(); };
#if DSS_CORE_AUDIO
    actionOptions.actionSound = [&service] { return service.actionSound(); };
#endif
    ActionService actions(service.eventBus(), actionOptions);
    actions.load();
    service.setActionService(&actions);
#if DSS_CORE_TWITCH
    QObject::connect(&twitch, &dss::twitch::TwitchService::stateChanged, &actions, &ActionService::accountsChanged);
#endif
#else
    if (parser.isSet(testEventsOption))
        qWarning("--testereignisse ohne Aktionssystem (DSS_CORE_ACTIONS aus)");
#endif

#if DSS_CORE_AUDIO
    // Ton der Videos über PipeWire (P12b b3): eigener Strom mit dem Ausgang aus kern.ini
    // ([ton-video]), standardmässig «kein Ausgang». Ohne PipeWire läuft der Kern ohne Ton weiter.
    // Nach service angelegt, also vor ihm zerstört; danach läuft keine Hauptschleife mehr. Die
    // Decoder des Tons beendet service.stopAudio() nach der Hauptschleife, solange beide leben.
    const bool installed = dss::buildEdition() == dss::Edition::Installed;
    dss::Mixer audioMixer;
    std::unique_ptr<dss::AudioOutput> audioOutput;
    if (parser.isSet(substituteOutputOption)) {
        audioOutput = std::make_unique<dss::NullOutput>(&audioMixer);
        qInfo("Ton: Ersatzausgang ohne Gerät (--ersatzausgang)");
    } else {
        audioOutput = std::make_unique<dss::PwOutput>(&audioMixer, QStringLiteral("ton-video"),
                                                      installed ? QStringLiteral("dss_core_ton_video")
                                                                : QStringLiteral("dss_core_entwicklung_ton_video"),
                                                      QStringLiteral("DoNs Streaming Studio (%1): Ton der Videos")
                                                          .arg(dss::editionLabel()));
    }
    service.setAudioOutput(audioOutput.get(), &audioMixer);
    if (!audioOutput->start(&error))
        qWarning("Ton: kein PipeWire, Kern läuft ohne Ton: %s", qPrintable(error));

    // Töne des Chat-Bots (P14 t): zweiter Strom mit eigenem Mischer und Ausgang aus kern.ini
    // ([ton-aktionen]), standardmässig «kein Ausgang». Freie Stimmen ohne Seitentakt (mixer.h).
    dss::Mixer actionMixer(dss::ActionSound::kMaxSounds);
    std::unique_ptr<dss::AudioOutput> actionOutput;
    if (parser.isSet(substituteOutputOption)) {
        actionOutput = std::make_unique<dss::NullOutput>(&actionMixer);
    } else {
        actionOutput = std::make_unique<dss::PwOutput>(&actionMixer, QStringLiteral("ton-aktionen"),
                                                       installed ? QStringLiteral("dss_core_ton_aktionen")
                                                                 : QStringLiteral("dss_core_entwicklung_ton_aktionen"),
                                                       QStringLiteral("DoNs Streaming Studio (%1): Ton des Chat-Bots")
                                                           .arg(dss::editionLabel()));
    }
    service.setActionAudioOutput(actionOutput.get(), &actionMixer);
    if (!actionOutput->start(&error))
        qWarning("Ton Chat-Bot: kein PipeWire, Kern läuft ohne Ton: %s", qPrintable(error));
#endif

    QObject::connect(&signalWatcher, &dss::UnixSignalWatcher::terminationRequested, &app,
                     [](int signalNumber) {
                         qInfo("%s erhalten, beende",
                               signalNumber == SIGINT ? "SIGINT" : "SIGTERM");
                         QCoreApplication::quit();
                     });
    QObject::connect(&signalWatcher, &dss::UnixSignalWatcher::hangupReceived, &service, [&service] {
        qInfo("SIGHUP erhalten, lade Datenordner neu");
        service.reload();
    });
    QObject::connect(&service, &CoreService::quitRequested, &app, &QCoreApplication::quit);

    // Bereit: erst jetzt kehrt «systemctl --user start» zurück (Type=notify).
    if (dss::notifySystemd("READY=1"))
        qInfo("systemd: bereit gemeldet");
#if DSS_CORE_WATCHDOG
    // Watchdog (P10a, Punkt 41): «WATCHDOG=1» alle halbe Zeit aus der Hauptschleife. Hängt sie,
    // bleibt die Meldung aus, und systemd beendet und startet den Kern neu (WatchdogSec in der
    // Einheit, scripts/dienst-einrichten.sh).
    // https://www.freedesktop.org/software/systemd/man/latest/sd_watchdog_enabled.html
    QTimer watchdog;
    const auto watchdogTime = dss::watchdogTimeout();
    QElapsedTimer sinceWatchdog;
    if (watchdogTime.count() > 0) {
#if !DSS_CORE_IMPORT_THREAD
        // Beim Übernehmen einer grossen Datei steht die Hauptschleife lange still (1 GiB etwa
        // 16 s, Bericht P10a); das ist kein Hängen. Dann höchstens einmal je Sekunde melden.
        // Mit DSS_CORE_IMPORT_THREAD entfällt das (P10-vor): Die Hauptschleife steht beim
        // Übernehmen nicht still, und eine Meldung aus dem Thread würde eine hängende
        // Hauptschleife verdecken.
        sinceWatchdog.start();
        store.setProgressCallback([&sinceWatchdog] {
            if (sinceWatchdog.elapsed() < 1000)
                return;
            dss::notifySystemd("WATCHDOG=1");
            sinceWatchdog.restart();
        });
#endif
        const auto interval = std::chrono::duration_cast<std::chrono::milliseconds>(watchdogTime / 2);
        watchdog.setInterval(std::max<std::chrono::milliseconds>(interval, std::chrono::milliseconds(1)));
        QObject::connect(&watchdog, &QTimer::timeout, [] { dss::notifySystemd("WATCHDOG=1"); });
        watchdog.start();
        qInfo("systemd: Watchdog %lld ms, Meldung alle %lld ms",
              static_cast<long long>(watchdogTime.count() / 1000),
              static_cast<long long>(watchdog.interval()));
    }
#endif
    const int rc = app.exec();
    dss::notifySystemd("STOPPING=1");
    // P12b b4: Decoder des Tons beenden, solange Mischer und Ausgang noch leben
    service.stopAudio();
    // Laufende Übernahmen abbrechen (P10-vor, Punkt 67): QSaveFile verwirft die vorläufigen
    // Dateien, unter dem endgültigen Namen entsteht nichts.
    QElapsedTimer stopping;
    stopping.start();
    if (const int canceled = service.stopImports(); canceled > 0)
        qInfo("%d Übernahmen abgebrochen (%lld ms)", canceled,
              static_cast<long long>(stopping.elapsed()));
    // Änderungen, die noch nicht in der Datei sind, vor dem Beenden speichern (P08, Teil A).
    if (!service.savePending(&error))
        qCritical("Beim Beenden nicht gespeichert: %s", qPrintable(error));
    qInfo("beendet, Exit-Code %d", rc);
    return rc;
}
