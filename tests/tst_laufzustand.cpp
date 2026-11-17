// Laufzustand der Overlays (P14 d, Plan docs/archiv/prompts/P14-chatbot.md, Abschnitt 4.4), ohne
// Qt Gui: Platzhalter im Text eines Elements, Anwenden auf ein Overlay, Nachrichten, Prüfungen,
// OverlayLiveState mit Dauer, Zurücksetzen, gelöschten Quellen und dem Laden von Profilbildern
// gegen einen eigenen kleinen HTTP-Server auf 127.0.0.1 (nie das Netz), dazu Kern und dss-cli als
// eigene Prozesse (live-zeigen, live, live-ausblenden, Neustart). Nur erfundene Namen.

#include "bildserver.h"
#include "prozesshilfe.h"

#include "coreclient.h"
#include "overlaylive.h"
#include "overlaylivestate.h"
#include "overlaystore.h"
#include "protocol.h"
#include "version.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace dss;

namespace {

const QString kExif = QStringLiteral(DSS_TEST_EXIF_DIR);

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestLaufzustand : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_data;
    std::unique_ptr<OverlayStore> m_store;
    QUuid m_id;
    QUuid m_text;  // «Willkommen, %user%!»
    QUuid m_image; // Bild, unsichtbar
    QUuid m_name;  // Text ohne Platzhalter, unsichtbar
    QString m_media; // Datei der Mediathek (zweites Bild)

    OverlayLiveState::Options options() const
    {
        OverlayLiveState::Options o;
        o.imageDir = m_data.path() + QStringLiteral("/live-bilder");
        o.imageHosts = {QStringLiteral("127.0.0.1")};
        o.allowHttp = true;
        o.timeoutMs = 3000;
        return o;
    }

    // set() und auf die Antwort warten
    static OverlayLiveState::Result setAndWait(OverlayLiveState *state, const LiveSetRequest &r)
    {
        std::optional<OverlayLiveState::Result> result;
        state->set(r, [&result](const OverlayLiveState::Result &x) { result = x; });
        if (!QTest::qWaitFor([&result] { return result.has_value(); }, 5000))
            return {false, QStringLiteral("timeout"), QStringLiteral("keine Antwort"), QString()};
        return *result;
    }

    LiveSetRequest show(QList<LiveElementRequest> elements, QHash<QString, QString> arguments = {},
                        int durationMs = 0) const
    {
        LiveSetRequest r;
        r.overlayId = m_id;
        r.elements = elements;
        r.arguments = arguments;
        r.durationMs = durationMs;
        return r;
    }

private slots:
    void initTestCase()
    {
        m_store = std::make_unique<OverlayStore>(m_data.path());
        QString error;
        QVERIFY(m_store->load(&error));
        m_id = m_store->createOverlay(QStringLiteral("Probe Laufzustand"), 640, 360, &error);
        QVERIFY2(!m_id.isNull(), qPrintable(error));
        m_text = m_store->addTextSource(m_id, QStringLiteral("Willkommen, %user%!"), QStringLiteral(DSS_TEST_FONT), &error);
        QVERIFY2(!m_text.isNull(), qPrintable(error));
        m_image = m_store->addImageSource(m_id, kExif + QStringLiteral("/orientierung-1.png"), &error);
        QVERIFY2(!m_image.isNull(), qPrintable(error));
        m_name = m_store->addTextSource(m_id, QStringLiteral("Fest"), QStringLiteral(DSS_TEST_FONT), &error);
        QVERIFY(!m_name.isNull());
        for (const QUuid &id : {m_image, m_name}) {
            SourceChanges c;
            c.visible = false;
            QVERIFY(m_store->updateSource(m_id, id, c, &error));
        }
        m_media = m_store->importMedia(kExif + QStringLiteral("/ohne-exif.jpg"), MediaUse::Image, &error);
        QVERIFY2(!m_media.isEmpty(), qPrintable(error));
    }

    void placeholders_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("expected");
        QTest::newRow("einer") << "Neuer Follower: %user%" << "Neuer Follower: Zuschauer42";
        QTest::newRow("zwei") << "%user% mit %bits% Bits" << "Zuschauer42 mit 100 Bits";
        QTest::newRow("unbekannt bleibt") << "Hallo %niemand%" << "Hallo %niemand%";
        QTest::newRow("Prozent davor") << "100 % %user%" << "100 % Zuschauer42";
        QTest::newRow("Gross/klein zählt nicht") << "%User% %user% %USER%" << "Zuschauer42 Zuschauer42 Zuschauer42";
        QTest::newRow("leer") << "%%" << "%%";
        QTest::newRow("Leerzeichen") << "%user name%" << "%user name%";
        QTest::newRow("ohne Ende") << "%user" << "%user";
        QTest::newRow("Variable") << "~zaehler~ und ~unbekannt~" << "7 und ~unbekannt~";
        QTest::newRow("Wert mit Prozent") << "%prozent%%user%" << "50 %Zuschauer42";
        QTest::newRow("Wert nicht nochmals") << "%trick% ~trick~" << "%user% %bits%";
    }

    void placeholders()
    {
        QFETCH(QString, text);
        QFETCH(QString, expected);
        const QHash<QString, QString> args = {{QStringLiteral("user"), QStringLiteral("Zuschauer42")},
                                              {QStringLiteral("bits"), QStringLiteral("100")},
                                              {QStringLiteral("prozent"), QStringLiteral("50 %")},
                                              {QStringLiteral("trick"), QStringLiteral("%user%")}};
        const dss::actions::GlobalLookup globals = [](const QString &name) -> std::optional<QJsonValue> {
            if (name == QLatin1String("zaehler"))
                return QJsonValue(7);
            if (name == QLatin1String("trick"))
                return QJsonValue(QStringLiteral("%bits%"));
            return std::nullopt;
        };
        QCOMPARE(substitutePlaceholders(text, args, globals), expected);
        // Ohne Variablen (Kern ohne Aktionssystem) bleibt «~name~» wörtlich
        QCOMPARE(substitutePlaceholders(QStringLiteral("~zaehler~"), args), QStringLiteral("~zaehler~"));
    }

    void applyToOverlay()
    {
        const Overlay o = *m_store->overlay(m_id);
        LiveElements e;
        LiveElement text;
        text.text = QStringLiteral("Neu");
        LiveElement image;
        image.visible = true;
        image.image = QStringLiteral("live-bilder/abc.png");
        LiveElement hide;
        hide.visible = false;
        e.insert(m_text, hide);
        e.insert(m_image, image);
        e.insert(m_name, text);
        e.insert(QUuid::createUuid(), hide); // gibt es nicht: übergangen
        const Overlay live = applyLiveState(o, e);
        QCOMPARE(live.sources.at(0).visible, false);
        QCOMPARE(live.sources.at(0).text.content, o.sources.at(0).text.content);
        QCOMPARE(live.sources.at(1).visible, true);
        QCOMPARE(live.sources.at(1).file, QStringLiteral("live-bilder/abc.png"));
        QCOMPARE(live.sources.at(2).text.content, QStringLiteral("Neu"));
        QCOMPARE(live.sources.at(2).visible, false);
        // Das Overlay selbst bleibt
        QCOMPARE(*m_store->overlay(m_id), o);
        QCOMPARE(applyLiveState(o, {}), o);
    }

    void messages()
    {
        LiveSetRequest set = show({{m_text, QStringLiteral("Hallo %user%"), QString()},
                                   {m_image, std::nullopt, QStringLiteral("%bild%")}},
                                  {{QStringLiteral("user"), QStringLiteral("Zuschauer42")}}, 8000);
        set.requestId = 7;
        set.mode = LiveMode::Toggle;
        LiveSetRequest back;
        QString error;
        QVERIFY2(parseLiveSet(liveSetMessage(set), &back, &error), qPrintable(error));
        QCOMPARE(back, set);
        QCOMPARE(messageType(liveSetMessage(set)), QString::fromLatin1(msg::kOverlayLiveSet));

        LiveResetRequest reset{3, m_id, {m_text}};
        LiveResetRequest resetBack;
        QVERIFY(parseLiveReset(liveResetMessage(reset), &resetBack, &error));
        QCOMPARE(resetBack, reset);

        LiveElement a;
        a.visible = true;
        a.text = QStringLiteral("x");
        a.remainingMs = 1234;
        LiveElement b;
        b.image = QStringLiteral("media/abc.png");
        const QList<OverlayLiveInfo> infos = {{m_id, {{m_text, a}, {m_image, b}}}};
        QList<OverlayLiveInfo> infosBack;
        QVERIFY2(parseOverlayLive(overlayLiveMessage(infos), &infosBack, &error), qPrintable(error));
        QCOMPARE(infosBack, infos);

        // ungültig
        QJsonObject bad = liveSetMessage(set);
        bad.insert(QStringLiteral("mode"), QStringLiteral("zeigen"));
        QVERIFY(!parseLiveSet(bad, &back, &error));
        bad = liveSetMessage(set);
        bad.insert(QStringLiteral("duration_ms"), kLiveMaxDurationMs + 1);
        QVERIFY(!parseLiveSet(bad, &back, &error));
        bad = liveSetMessage(set);
        bad.insert(QStringLiteral("arguments"), QJsonObject{{QStringLiteral("n"), 3}});
        QVERIFY(!parseLiveSet(bad, &back, &error));
        bad = liveSetMessage(set);
        bad.remove(QStringLiteral("elements"));
        QVERIFY(!parseLiveSet(bad, &back, &error));
        QJsonObject badReset = liveResetMessage(reset);
        badReset.insert(QStringLiteral("sources"), QJsonArray{QStringLiteral("kaputt")});
        QVERIFY(!parseLiveReset(badReset, &resetBack, &error));
    }

    void validation()
    {
        const Overlay o = *m_store->overlay(m_id);
        QVERIFY(validateLiveSet(show({{m_text, std::nullopt, QString()}}), o).isEmpty());
        QVERIFY(!validateLiveSet(show({}), o).isEmpty());
        QVERIFY(!validateLiveSet(show({{QUuid::createUuid(), std::nullopt, QString()}}), o).isEmpty());
        QVERIFY(!validateLiveSet(show({{m_text, std::nullopt, QString()}, {m_text, std::nullopt, QString()}}), o).isEmpty());
        QVERIFY(validateLiveSet(show({{m_image, QStringLiteral("Text"), QString()}}), o).contains(QStringLiteral("kein Text")));
        QVERIFY(validateLiveSet(show({{m_text, std::nullopt, QStringLiteral("media/x.png")}}), o).contains(QStringLiteral("kein Bild")));
        QVERIFY(!validateLiveSet(show({{m_text, QString(kLiveMaxTextChars + 1, QLatin1Char('a')), QString()}}), o).isEmpty());
        QVERIFY(!validateLiveSet(show({{m_text, std::nullopt, QString()}}, {}, -1), o).isEmpty());
    }

    void stateText()
    {
        QCOMPARE(liveStateText(std::nullopt, -1), QStringLiteral("nicht live"));
        LiveElement e;
        e.visible = true;
        QCOMPARE(liveStateText(e, 5200), QStringLiteral("live sichtbar, noch 6 s"));
        e.visible = false;
        QCOMPARE(liveStateText(e, -1), QStringLiteral("live versteckt"));
        e.visible.reset();
        QCOMPARE(liveStateText(e, 0), QStringLiteral("live geändert, noch 0 s"));
    }

    void showHideToggleWithPlaceholders()
    {
        OverlayLiveState state(m_store.get(), options());
        QSignalSpy changed(&state, &OverlayLiveState::changed);
        const OverlayLiveState::Result r =
            setAndWait(&state, show({{m_text, std::nullopt, QString()}, {m_name, std::nullopt, QString()}},
                                    {{QStringLiteral("user"), QStringLiteral("Zuschauer42")}}));
        QVERIFY2(r.ok, qPrintable(r.message));
        QCOMPARE(changed.size(), 1);
        LiveElements e = state.elements(m_id);
        QCOMPARE(e.size(), 2);
        QCOMPARE(e.value(m_text).visible, std::optional<bool>(true));
        QCOMPARE(e.value(m_text).text, std::optional<QString>(QStringLiteral("Willkommen, Zuschauer42!")));
        QCOMPARE(e.value(m_text).remainingMs, -1);
        // ohne Platzhalter: kein eigener Text im Laufzustand
        QCOMPARE(e.value(m_name).text, std::nullopt);
        // Text aus dem Aufruf ersetzt den ganzen Text, auch mit Platzhaltern
        QVERIFY(setAndWait(&state, show({{m_name, QStringLiteral("Raid von %user%"), QString()}},
                                        {{QStringLiteral("user"), QStringLiteral("Kanal7")}})).ok);
        QCOMPARE(state.elements(m_id).value(m_name).text, std::optional<QString>(QStringLiteral("Raid von Kanal7")));
        // umschalten: sichtbar → versteckt → sichtbar; unsichtbares Element ohne Laufzustand → sichtbar
        LiveSetRequest toggle = show({{m_text, std::nullopt, QString()}});
        toggle.mode = LiveMode::Toggle;
        QVERIFY(setAndWait(&state, toggle).ok);
        QCOMPARE(state.elements(m_id).value(m_text).visible, std::optional<bool>(false));
        QVERIFY(setAndWait(&state, toggle).ok);
        QCOMPARE(state.elements(m_id).value(m_text).visible, std::optional<bool>(true));
        LiveSetRequest hide = show({{m_text, std::nullopt, QString()}});
        hide.mode = LiveMode::Hide;
        QVERIFY(setAndWait(&state, hide).ok);
        QCOMPARE(state.elements(m_id).value(m_text).visible, std::optional<bool>(false));
        // Die Overlay-Datei bleibt (nie geschrieben)
        QCOMPARE(m_store->overlay(m_id)->sources.at(0).text.content, QStringLiteral("Willkommen, %user%!"));
        QCOMPARE(m_store->overlay(m_id)->sources.at(2).visible, false);
        // Zurücksetzen: einzeln, dann alle
        QVERIFY(state.reset({0, m_id, {m_name}}).isEmpty());
        QCOMPARE(state.elements(m_id).keys(), QList<QUuid>{m_text});
        QVERIFY(state.reset({0, m_id, {}}).isEmpty());
        QVERIFY(state.elements(m_id).isEmpty());
        QVERIFY(state.all().isEmpty());
        QVERIFY(!state.reset({0, QUuid::createUuid(), {}}).isEmpty());
        // Fehler
        const OverlayLiveState::Result unknown = setAndWait(&state, LiveSetRequest{0, QUuid::createUuid(), LiveMode::Show,
                                                                                     {{m_text, std::nullopt, QString()}}, 0, {}});
        QVERIFY(!unknown.ok);
        QCOMPARE(unknown.code, QString::fromLatin1(err::kUnknownOverlay));
        const OverlayLiveState::Result rejected = setAndWait(&state, show({}));
        QCOMPARE(rejected.code, QString::fromLatin1(err::kRejected));
    }

    void durationEnds()
    {
        OverlayLiveState state(m_store.get(), options());
        QSignalSpy changed(&state, &OverlayLiveState::changed);
        QVERIFY(setAndWait(&state, show({{m_text, std::nullopt, QString()}}, {}, 300)).ok);
        QVERIFY(setAndWait(&state, show({{m_name, std::nullopt, QString()}}, {}, 0)).ok);
        const qint64 remaining = state.elements(m_id).value(m_text).remainingMs;
        QVERIFY2(remaining > 0 && remaining <= 300, qPrintable(QString::number(remaining)));
        QTRY_VERIFY_WITH_TIMEOUT(!state.elements(m_id).contains(m_text), 3000);
        // Element ohne Dauer bleibt
        QVERIFY(state.elements(m_id).contains(m_name));
        QCOMPARE(changed.size(), 3);
    }

    void mediaImageAndDeletedSource()
    {
        OverlayLiveState state(m_store.get(), options());
        OverlayLiveState::Result r = setAndWait(&state, show({{m_image, std::nullopt, QStringLiteral("%bild%")}},
                                                             {{QStringLiteral("bild"), m_media}}));
        QVERIFY(r.ok);
        QVERIFY2(r.warning.isEmpty(), qPrintable(r.warning));
        QCOMPARE(state.elements(m_id).value(m_image).image, m_media);
        QVERIFY(state.usesImage(m_id, m_media));
        QVERIFY(!state.usesImage(m_id, QStringLiteral("media/anderes.png")));
        // Keine Datei der Mediathek: Hinweis, Element mit eigenem Bild
        r = setAndWait(&state, show({{m_image, std::nullopt, QStringLiteral("media/gibt-es-nicht.png")}}));
        QVERIFY(r.ok);
        QVERIFY2(r.warning.contains(QStringLiteral("kein Bild der Mediathek")), qPrintable(r.warning));
        QVERIFY(state.elements(m_id).value(m_image).image.isEmpty());
        QVERIFY(!setAndWait(&state, show({{m_image, std::nullopt, QStringLiteral("../tokens.json")}})).warning.isEmpty());
        // Quelle gelöscht: Laufzustand fällt weg
        QString error;
        const QUuid extra = m_store->addTextSource(m_id, QStringLiteral("Extra"), QStringLiteral(DSS_TEST_FONT), &error);
        QVERIFY(setAndWait(&state, show({{extra, std::nullopt, QString()}})).ok);
        QVERIFY(state.elements(m_id).contains(extra));
        QVERIFY(m_store->removeSource(m_id, extra, &error));
        QSignalSpy changed(&state, &OverlayLiveState::changed);
        state.overlaysChanged();
        QCOMPARE(changed.size(), 1);
        QVERIFY(!state.elements(m_id).contains(extra));
        QVERIFY(state.elements(m_id).contains(m_image));
    }

    void profileImages()
    {
        testhelp::ImageServer server;
        const QByteArray png = readFile(kExif + QStringLiteral("/orientierung-1.png"));
        const QByteArray jpg = readFile(kExif + QStringLiteral("/ohne-exif.jpg"));
        QVERIFY(!png.isEmpty() && !jpg.isEmpty());
        server.bodies.insert("/a.png", png);
        server.bodies.insert("/b", jpg); // Endung aus dem Inhalt, nicht aus der Adresse
        server.bodies.insert("/text.png", "kein Bild");
        QByteArray big(3 * 1024 * 1024, 'x');
        big.replace(0, 8, "\x89PNG\r\n\x1a\n");
        server.bodies.insert("/gross.png", big);

        OverlayLiveState::Options o = options();
        o.maxImages = 2;
        OverlayLiveState state(m_store.get(), o);
        const auto image = [&](const QString &url) {
            return setAndWait(&state, show({{m_image, std::nullopt, QStringLiteral("%targetUserProfileImageUrl%")}},
                                           {{QStringLiteral("targetUserProfileImageUrl"), url}}));
        };
        OverlayLiveState::Result r = image(server.url("/a.png"));
        QVERIFY2(r.ok && r.warning.isEmpty(), qPrintable(r.message + r.warning));
        const QString pngName = QString::fromLatin1(QCryptographicHash::hash(png, QCryptographicHash::Sha256).toHex())
                                + QStringLiteral(".png");
        QCOMPARE(state.elements(m_id).value(m_image).image, QStringLiteral("live-bilder/") + pngName);
        QCOMPARE(readFile(o.imageDir + QLatin1Char('/') + pngName), png);
        QCOMPARE(server.requests, 1);
        // zweites Mal aus dem Zwischenspeicher
        QVERIFY(image(server.url("/a.png")).ok);
        QCOMPARE(server.requests, 1);
        // JPEG
        r = image(server.url("/b"));
        QVERIFY2(r.warning.isEmpty(), qPrintable(r.warning));
        QVERIFY(state.elements(m_id).value(m_image).image.endsWith(QStringLiteral(".jpg")));
        // abgewiesen: kein Bild, zu gross, 404 (Element ohne Bild, Hinweis)
        for (const char *path : {"/text.png", "/gross.png", "/fehlt.png"}) {
            r = image(server.url(path));
            QVERIFY(r.ok);
            QVERIFY2(r.warning.contains(QStringLiteral("nicht geladen")), qPrintable(r.warning));
            QVERIFY(state.elements(m_id).value(m_image).image.isEmpty());
        }
        QVERIFY(r.warning.contains(QStringLiteral("Antwort 404")));
        // falscher Server und ohne HTTPS: ohne Anfrage abgewiesen
        const int before = server.requests;
        OverlayLiveState strict(m_store.get(), [this] {
            OverlayLiveState::Options s = options();
            s.allowHttp = false;
            s.imageHosts = {QStringLiteral("static-cdn.jtvnw.net")};
            return s;
        }());
        for (const QString &url : {server.url("/a.png"), QStringLiteral("https://example.org/a.png"),
                                   QStringLiteral("https://nutzer@static-cdn.jtvnw.net/a.png"),
                                   QStringLiteral("https://static-cdn.jtvnw.net:8443/a.png")}) {
            r = setAndWait(&strict, show({{m_image, std::nullopt, url}}));
            QVERIFY(r.ok);
            QVERIFY2(r.warning.contains(QStringLiteral("nicht geladen")), qPrintable(url + QLatin1Char(' ') + r.warning));
        }
        QCOMPARE(server.requests, before);
        // höchstens maxImages Dateien; das gezeigte bleibt
        server.bodies.insert("/c.png", readFile(kExif + QStringLiteral("/orientierung-2.png")));
        server.bodies.insert("/d.png", readFile(kExif + QStringLiteral("/orientierung-3.png")));
        QVERIFY(image(server.url("/c.png")).ok);
        QVERIFY(image(server.url("/d.png")).ok);
        const QString shown = state.elements(m_id).value(m_image).image;
        QCOMPARE(QDir(o.imageDir).entryList(QDir::Files).size(), 2);
        QVERIFY(QFile::exists(m_data.path() + QLatin1Char('/') + shown));
        QVERIFY(!QFile::exists(o.imageDir + QLatin1Char('/') + pngName));
        // beim Start geleert
        QCOMPARE(state.clearImages(), 2);
        QVERIFY(QDir(o.imageDir).entryList(QDir::Files).isEmpty());
    }

    void coreAndCli()
    {
        // Daten mit dss-cli anlegen, solange kein Kern läuft
        QTemporaryDir data;
        const auto runtime = testhelp::runtimeDir();
        const QString socket = runtime->path() + QStringLiteral("/kern.sock");
        testhelp::CliResult c = testhelp::runCli(data.path(), {QStringLiteral("overlay-neu"), QStringLiteral("Alerts"),
                                                               QStringLiteral("640"), QStringLiteral("360")});
        QCOMPARE(c.exitCode, 0);
        QCOMPARE(testhelp::runCli(data.path(), {QStringLiteral("quelle-text"), QStringLiteral("alerts"),
                                                QStringLiteral("Neuer Follower: %user%"), QStringLiteral(DSS_TEST_FONT)})
                     .exitCode,
                 0);
        QCOMPARE(testhelp::runCli(data.path(), {QStringLiteral("quelle-bild"), QStringLiteral("alerts"),
                                                kExif + QStringLiteral("/orientierung-1.png")})
                     .exitCode,
                 0);
        QDir overlays(data.path() + QStringLiteral("/overlays"));
        const QString file = overlays.entryInfoList({QStringLiteral("*.json")}, QDir::Files).value(0).absoluteFilePath();
        const QByteArray fileBefore = readFile(file);
        QVERIFY(!fileBefore.isEmpty());

        // Kern ohne Kern: Exit-Code 4
        const QStringList connection = {QStringLiteral("--verbindung"), socket};
        QCOMPARE(testhelp::runCliOnly(connection + QStringList{QStringLiteral("live"), QStringLiteral("alerts")}).exitCode, 4);

        std::unique_ptr<QProcess> core = testhelp::startCore(data.path(), socket);
        QString log;
        QVERIFY2(testhelp::waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        const auto cli = [&connection](const QStringList &args) { return testhelp::runCliOnly(connection + args); };

        c = cli({QStringLiteral("live"), QStringLiteral("alerts")});
        QCOMPARE(c.exitCode, 0);
        QVERIFY2(c.out.contains(QStringLiteral("Kein Laufzustand")), qPrintable(c.out + c.err));
        c = cli({QStringLiteral("live-zeigen"), QStringLiteral("alerts"), QStringLiteral("1,2"), QStringLiteral("--dauer"),
                 QStringLiteral("1500"), QStringLiteral("--bild"), QStringLiteral("https://example.org/bild.png"),
                 QStringLiteral("user=Zuschauer42")});
        QVERIFY2(c.exitCode == 0, qPrintable(c.out + c.err));
        // Bild von einem fremden Server: Hinweis, nichts geladen
        QVERIFY2(c.err.contains(QStringLiteral("nur vom Bildserver von Twitch")), qPrintable(c.err));
        c = cli({QStringLiteral("live"), QStringLiteral("alerts")});
        QVERIFY2(c.out.contains(QStringLiteral("Neuer Follower: Zuschauer42")), qPrintable(c.out));
        QVERIFY2(c.out.contains(QStringLiteral("live sichtbar, noch")), qPrintable(c.out));
        // Die Overlay-Datei bleibt unverändert
        QCOMPARE(readFile(file), fileBefore);
        // Nach der Dauer weg
        QTRY_VERIFY_WITH_TIMEOUT(cli({QStringLiteral("live"), QStringLiteral("alerts")}).out.contains(QStringLiteral("Kein Laufzustand")),
                                 5000);
        // ohne Dauer umschalten (sichtbares Element wird versteckt), dann ausblenden
        QCOMPARE(cli({QStringLiteral("live-zeigen"), QStringLiteral("alerts"), QStringLiteral("1"), QStringLiteral("--umschalten")}).exitCode, 0);
        c = cli({QStringLiteral("live"), QStringLiteral("alerts")});
        QVERIFY2(c.out.contains(QStringLiteral("live versteckt")), qPrintable(c.out));
        QCOMPARE(cli({QStringLiteral("live-ausblenden"), QStringLiteral("alerts"), QStringLiteral("1")}).exitCode, 0);
        QVERIFY(cli({QStringLiteral("live"), QStringLiteral("alerts")}).out.contains(QStringLiteral("Kein Laufzustand")));
        // Fenster (CoreClient): overlay_live nach hello und bei Änderung, Restdauer zählt herunter,
        // Ausblenden über overlay_live_reset
        {
            CoreClient::Options o;
            o.socketPath = socket;
            o.program = QStringLiteral("tst_laufzustand");
            o.version = version();
            CoreClient client(o);
            QSignalSpy changed(&client, &CoreClient::liveChanged);
            client.start();
            QTRY_VERIFY_WITH_TIMEOUT(client.isConnected() && changed.size() >= 1, 5000);
            const QUuid overlayId = client.overlays().value(0).id;
            QVERIFY(!overlayId.isNull());
            QVERIFY(client.live(overlayId).isEmpty());
            QCOMPARE(cli({QStringLiteral("live-zeigen"), QStringLiteral("alerts"), QStringLiteral("1,2"), QStringLiteral("--dauer"),
                          QStringLiteral("60000")}).exitCode,
                     0);
            QTRY_COMPARE_WITH_TIMEOUT(client.live(overlayId).size(), 2, 5000);
            const qint64 first = client.live(overlayId).first().remainingMs;
            QVERIFY2(first > 50000 && first <= 60000, qPrintable(QString::number(first)));
            QTest::qWait(200);
            QVERIFY(client.live(overlayId).first().remainingMs < first);
            QVERIFY(client.sendLiveReset(overlayId) > 0);
            QTRY_VERIFY_WITH_TIMEOUT(client.live(overlayId).isEmpty(), 5000);
        }
        // Fehler: falsche NR, falsches Overlay, Text bei einem Bild wird übergangen
        QCOMPARE(cli({QStringLiteral("live-zeigen"), QStringLiteral("alerts"), QStringLiteral("9")}).exitCode, 1);
        QCOMPARE(cli({QStringLiteral("live-zeigen"), QStringLiteral("gibt-es-nicht"), QStringLiteral("1")}).exitCode, 1);
        QCOMPARE(cli({QStringLiteral("live-zeigen"), QStringLiteral("alerts"), QStringLiteral("1"), QStringLiteral("--verstecken"),
                      QStringLiteral("--umschalten")}).exitCode,
                 2);
        QCOMPARE(testhelp::runCli(data.path(), {QStringLiteral("quelle-liste"), QStringLiteral("alerts"), QStringLiteral("--dauer"),
                                                QStringLiteral("5")})
                     .exitCode,
                 2);
        // Protokoll ohne Namen aus den Argumenten (Plan 3.5)
        log += QString::fromUtf8(core->readAllStandardError());
        QVERIFY2(log.contains(QStringLiteral("Laufzustand: show 2 Element(e)")), qPrintable(log));
        QVERIFY2(!log.contains(QStringLiteral("Zuschauer42")), qPrintable(log));
        // Neustart: Laufzustand leer
        QCOMPARE(cli({QStringLiteral("live-zeigen"), QStringLiteral("alerts"), QStringLiteral("1")}).exitCode, 0);
        core->terminate();
        QVERIFY(core->waitForFinished(5000));
        core = testhelp::startCore(data.path(), socket);
        log.clear();
        QVERIFY2(testhelp::waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        QVERIFY(cli({QStringLiteral("live"), QStringLiteral("alerts")}).out.contains(QStringLiteral("Kein Laufzustand")));
        QCOMPARE(readFile(file), fileBefore);
        core->terminate();
        QVERIFY(core->waitForFinished(5000));
    }
};

QTEST_GUILESS_MAIN(TestLaufzustand)
#include "tst_laufzustand.moc"
