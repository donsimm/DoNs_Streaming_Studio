// Quellen der Overlays in OBS (P10c, E-29), gegen den Testserver auf 127.0.0.1: Anlegen mit allen
// Fehlerfällen, Einfügen in weitere Szenen, Abgleich beim Verbinden, Übernahme von Hand angelegter
// Quellen (F10), fehlende Quellen (F7), gelöschte Overlays (F8). Nur Qt Core, Network, WebSockets.

#include "obssources.h"
#include "obstestserver.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace dss::obs;
using testhelp::ObsTestServer;
using State = ObsConnection::State;
using SourceState = ObsSources::SourceState;

namespace {

const QUuid kOverlayA = QUuid::fromString(QStringLiteral("aaaaaaaa-0000-4000-8000-000000000001"));
const QUuid kOverlayB = QUuid::fromString(QStringLiteral("bbbbbbbb-0000-4000-8000-000000000002"));
const QString kUrlA = QStringLiteral("http://192.168.1.9:8081/o/a/?token=geheimerTokenA");
const QString kUrlB = QStringLiteral("http://192.168.1.9:8081/o/b/?token=geheimerTokenB");

ObsConnection::Timing shortTiming()
{
    ObsConnection::Timing t;
    t.reconnect = 50;
    t.connectTimeout = 1000;
    t.requestTimeout = 200;
    t.busyRetry = 20;
    return t;
}

QJsonObject settingsOf(const QString &url, int width = 1920, int height = 1080)
{
    return {{QStringLiteral("url"), url}, {QStringLiteral("width"), width}, {QStringLiteral("height"), height}};
}

ObsSources::Entry entryOf(const ObsSources &sources, const QUuid &id)
{
    const QList<ObsSources::Entry> entries = sources.entries();
    for (const ObsSources::Entry &e : entries) {
        if (e.overlayId == id)
            return e;
    }
    return {};
}

// Antwort von createSource bzw. addToScene.
struct Answer
{
    bool arrived = false;
    ObsSources::Result result;
    ObsSources::Done done()
    {
        return [this](const ObsSources::Result &r) {
            arrived = true;
            result = r;
        };
    }
};

} // namespace

class TestObsSources : public QObject
{
    Q_OBJECT

    QTemporaryDir m_dir;
    std::unique_ptr<ObsTestServer> m_server;
    std::unique_ptr<ObsConnection> m_connection;
    std::unique_ptr<ObsSources> m_sources;

    QString filePath() const { return m_dir.filePath(QStringLiteral("obs-quellen.json")); }
    QByteArray fileText() const
    {
        QFile f(filePath());
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    Config config() const { return {true, QStringLiteral("127.0.0.1"), m_server->port()}; }

    // Baut die Verbindung auf.
    void connectAll(const QList<ObsSources::Desired> &desired)
    {
        m_connection = std::make_unique<ObsConnection>(
            [](const ObsConnection::PasswordDone &done) { done(QString()); }, shortTiming());
        m_sources = std::make_unique<ObsSources>(m_connection.get(), filePath());
        m_sources->setDesired(desired);
        m_connection->configure(config());
    }
    // Verbunden, Abgleich abgeschlossen (Antworten kommen über 127.0.0.1 innert Millisekunden).
    void waitSynced()
    {
        QTRY_COMPARE(m_connection->state(), State::Connected);
        QTRY_VERIFY(m_server->requestCount(QStringLiteral("GetInputList")) >= 1);
        QTest::qWait(60);
    }

private slots:
    void init()
    {
        QFile::remove(filePath());
        const QStringList rest = QDir(m_dir.path()).entryList({QStringLiteral("obs-quellen.json.ungueltig-*")});
        for (const QString &f : rest)
            QFile::remove(m_dir.filePath(f));
        m_server = std::make_unique<ObsTestServer>();
        QVERIFY(m_server->start());
    }

    void cleanup()
    {
        m_sources.reset();
        m_connection.reset();
        m_server.reset();
    }

    void createSource()
    {
        connectAll({{kOverlayA, kUrlA, 1280, 720}});
        waitSynced();
        QCOMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::None);
        QTRY_COMPARE(m_sources->scenes().size(), 2);
        QCOMPARE(m_sources->scenes().first().name, QStringLiteral("Live"));

        QSignalSpy changed(m_sources.get(), &ObsSources::entriesChanged);
        Answer answer;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-pause"), QStringLiteral("  Mein Overlay "), answer.done());
        QTRY_VERIFY(answer.arrived);
        QCOMPARE(answer.result.failure, ObsSources::Failure::None);
        QCOMPARE(changed.size(), 1);
        const ObsSources::Entry entry = entryOf(*m_sources, kOverlayA);
        QCOMPARE(entry.state, SourceState::Present);
        QCOMPARE(entry.inputName, QStringLiteral("Mein Overlay"));

        // Genau die Felder aus der Doku; sichtbar (F5), mit Grösse
        const QList<testhelp::ObsTestRequest> requests = m_server->requestsOf(QStringLiteral("CreateInput"));
        QCOMPARE(requests.size(), 1);
        const QJsonObject fields = requests.first().data;
        QCOMPARE(fields.value(QStringLiteral("sceneUuid")).toString(), QStringLiteral("scene-pause"));
        QCOMPARE(fields.value(QStringLiteral("inputName")).toString(), QStringLiteral("Mein Overlay"));
        QCOMPARE(fields.value(QStringLiteral("inputKind")).toString(), QStringLiteral("browser_source"));
        QCOMPARE(fields.value(QStringLiteral("sceneItemEnabled")).toBool(false), true);
        QCOMPARE(fields.value(QStringLiteral("inputSettings")).toObject(), settingsOf(kUrlA, 1280, 720));
        QCOMPARE(m_server->inputs.size(), 1);
        QCOMPARE(m_server->sceneItems.value(QStringLiteral("scene-pause")), QStringList{m_server->inputs.first().uuid});

        // Datei: Zuordnung über die Kennung, ohne Adresse und Token
        const QByteArray text = fileText();
        QVERIFY2(text.contains(m_server->inputs.first().uuid.toUtf8()), text.constData());
        QVERIFY2(text.contains("Mein Overlay"), text.constData());
        QVERIFY2(!text.contains("geheimerToken") && !text.contains("http:"), text.constData());

        // Ein zweites Mal geht nicht: Es gibt die Quelle schon (F6: «In weitere Szene einfügen»)
        Answer again;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-live"), QStringLiteral("Zweite"), again.done());
        QCOMPARE(again.result.failure, ObsSources::Failure::Rejected);
        QCOMPARE(m_server->inputs.size(), 1);
    }

    void createFailures()
    {
        connectAll({{kOverlayA, kUrlA, 1920, 1080}, {kOverlayB, QString(), 1920, 1080}});
        waitSynced();
        m_server->addInput(QStringLiteral("Besetzt"), settingsOf(QStringLiteral("http://anderes/")));

        Answer taken;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-live"), QStringLiteral("Besetzt"), taken.done());
        QTRY_VERIFY(taken.arrived);
        QCOMPARE(taken.result.failure, ObsSources::Failure::NameTaken);
        QCOMPARE(taken.result.message, QStringLiteral("Der Name «Besetzt» ist in OBS schon vergeben."));
        QCOMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::None);

        // Szene weg: Meldung, Liste wird neu geholt
        const int lists = m_server->requestCount(QStringLiteral("GetSceneList"));
        Answer gone;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-weg"), QStringLiteral("Neu"), gone.done());
        QTRY_VERIFY(gone.arrived);
        QCOMPARE(gone.result.failure, ObsSources::Failure::SceneMissing);
        QCOMPARE(gone.result.message, QStringLiteral("Die Szene gibt es in OBS nicht mehr."));
        QTRY_VERIFY(m_server->requestCount(QStringLiteral("GetSceneList")) > lists);

        // Ohne Adresse und ohne Namen: Anfrage geht gar nicht erst raus
        Answer noUrl;
        m_sources->createSource(kOverlayB, QStringLiteral("scene-live"), QStringLiteral("Neu"), noUrl.done());
        QCOMPARE(noUrl.result.failure, ObsSources::Failure::Rejected);
        Answer noName;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-live"), QStringLiteral("   "), noName.done());
        QCOMPARE(noName.result.failure, ObsSources::Failure::Rejected);

        // Keine Antwort: Zeitgrenze
        m_server->silentRequests.insert(QStringLiteral("CreateInput"));
        Answer silent;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-live"), QStringLiteral("Neu"), silent.done());
        QTRY_VERIFY(silent.arrived);
        QCOMPARE(silent.result.failure, ObsSources::Failure::Timeout);
        m_server->silentRequests.clear();

        // Beschäftigt (207): wird wiederholt
        m_server->busyAnswers = 2;
        m_server->busyRequest = QStringLiteral("CreateInput");
        Answer busy;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-live"), QStringLiteral("Neu"), busy.done());
        QTRY_VERIFY(busy.arrived);
        QCOMPARE(busy.result.failure, ObsSources::Failure::None);

        // Nicht verbunden
        m_connection->configure({});
        QCOMPARE(m_connection->state(), State::Off);
        Answer off;
        m_sources->createSource(kOverlayB, QStringLiteral("scene-live"), QStringLiteral("Neu"), off.done());
        QCOMPARE(off.result.failure, ObsSources::Failure::NotConnected);
    }

    // Abbruch nach dem Ausführen in OBS: Meldung, nach dem Wiederverbinden übernimmt der Abgleich
    // die schon angelegte Quelle, kein Duplikat.
    void createAbortedThenAdopted()
    {
        connectAll({{kOverlayA, kUrlA, 1920, 1080}});
        waitSynced();
        m_server->closingRequests.insert(QStringLiteral("CreateInput"));
        Answer aborted;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-live"), QStringLiteral("Neu"), aborted.done());
        QTRY_VERIFY(aborted.arrived);
        m_server->closingRequests.clear();
        QCOMPARE(aborted.result.failure, ObsSources::Failure::NotConnected);
        QVERIFY(aborted.result.message.contains(QStringLiteral("zeigt sich nach dem Wiederverbinden")));
        QCOMPARE(m_server->inputs.size(), 1);
        QTRY_COMPARE(m_connection->state(), State::Connected);
        QTRY_COMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Present);
        QCOMPARE(entryOf(*m_sources, kOverlayA).inputName, QStringLiteral("Neu"));
        QCOMPARE(m_server->inputs.size(), 1);
        QCOMPARE(m_server->requestCount(QStringLiteral("CreateInput")), 1);
    }

    void addToScene()
    {
        connectAll({{kOverlayA, kUrlA, 1920, 1080}});
        waitSynced();
        // Ohne Quelle
        Answer none;
        m_sources->addToScene(kOverlayA, QStringLiteral("scene-pause"), none.done());
        QCOMPARE(none.result.failure, ObsSources::Failure::SourceMissing);

        Answer created;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-live"), QStringLiteral("Neu"), created.done());
        QTRY_VERIFY(created.arrived);
        const QString uuid = m_server->inputs.first().uuid;
        Answer added;
        m_sources->addToScene(kOverlayA, QStringLiteral("scene-pause"), added.done());
        QTRY_VERIFY(added.arrived);
        QCOMPARE(added.result.failure, ObsSources::Failure::None);
        QCOMPARE(m_server->sceneItems.value(QStringLiteral("scene-pause")), QStringList{uuid});
        const QJsonObject fields = m_server->requestsOf(QStringLiteral("CreateSceneItem")).first().data;
        QCOMPARE(fields.value(QStringLiteral("sourceUuid")).toString(), uuid);
        QCOMPARE(m_server->inputs.size(), 1); // dieselbe Quelle

        // Szene weg
        Answer gone;
        m_sources->addToScene(kOverlayA, QStringLiteral("scene-weg"), gone.done());
        QTRY_VERIFY(gone.arrived);
        QCOMPARE(gone.result.failure, ObsSources::Failure::SourceMissing);
    }

    // Abgleich beim Verbinden: Name nachgeführt, fehlende Quelle, Zuordnung bleibt.
    void syncOnConnect()
    {
        const QString uuid = m_server->addInput(QStringLiteral("Neuer Name"), settingsOf(kUrlA));
        {
            QFile f(filePath());
            QVERIFY(f.open(QIODevice::WriteOnly));
            const QJsonArray list{
                QJsonObject{{QStringLiteral("overlay"), kOverlayA.toString(QUuid::WithoutBraces)},
                            {QStringLiteral("input_uuid"), uuid},
                            {QStringLiteral("input_name"), QStringLiteral("Alter Name")}},
                QJsonObject{{QStringLiteral("overlay"), kOverlayB.toString(QUuid::WithoutBraces)},
                            {QStringLiteral("input_uuid"), QStringLiteral("fehlt-in-obs")},
                            {QStringLiteral("input_name"), QStringLiteral("Weg")}}};
            f.write(QJsonDocument(QJsonObject{{QStringLiteral("format"), 1}, {QStringLiteral("quellen"), list}}).toJson());
        }
        m_connection = std::make_unique<ObsConnection>(
            [](const ObsConnection::PasswordDone &done) { done(QString()); }, shortTiming());
        m_sources = std::make_unique<ObsSources>(m_connection.get(), filePath());
        m_sources->setDesired({{kOverlayA, kUrlA, 1920, 1080}, {kOverlayB, kUrlB, 1920, 1080}});
        // Vor dem ersten Abgleich: letzter bekannter Stand
        QCOMPARE(entryOf(*m_sources, kOverlayB).state, SourceState::Present);
        m_connection->configure(config());
        QTRY_COMPARE(entryOf(*m_sources, kOverlayB).state, SourceState::Missing);
        QCOMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Present);
        QCOMPARE(entryOf(*m_sources, kOverlayA).inputName, QStringLiteral("Neuer Name"));
        QVERIFY(fileText().contains("Neuer Name"));
        QVERIFY(fileText().contains("fehlt-in-obs")); // F7: bleibt, andere Szenensammlung
        QCOMPARE(m_server->requestCount(QStringLiteral("CreateInput")), 0);

        // Anlegen ist bei fehlender Quelle wieder möglich
        Answer again;
        m_sources->createSource(kOverlayB, QStringLiteral("scene-live"), QStringLiteral("Wieder da"), again.done());
        QTRY_VERIFY(again.arrived);
        QCOMPARE(again.result.failure, ObsSources::Failure::None);
        QCOMPARE(entryOf(*m_sources, kOverlayB).state, SourceState::Present);
        QVERIFY(!fileText().contains("fehlt-in-obs"));
    }

    // F10: nur die ganze Adresse zählt, nicht bloss der Weg oder der Token.
    void adoptionNeedsExactUrl()
    {
        const QString exact = m_server->addInput(QStringLiteral("Von Hand"), settingsOf(kUrlA));
        m_server->addInput(QStringLiteral("Anderer Port"), settingsOf(QStringLiteral("http://192.168.1.9:9000/o/b/?token=geheimerTokenB")));
        m_server->addInput(QStringLiteral("Anderer Token"), settingsOf(QStringLiteral("http://192.168.1.9:8081/o/b/?token=x")));
        connectAll({{kOverlayA, kUrlA, 1920, 1080}, {kOverlayB, kUrlB, 1920, 1080}});
        QTRY_COMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Present);
        QCOMPARE(entryOf(*m_sources, kOverlayA).inputName, QStringLiteral("Von Hand"));
        QVERIFY(fileText().contains(exact.toUtf8()));
        QCOMPARE(entryOf(*m_sources, kOverlayB).state, SourceState::None);

        // Fehlende Quelle wird unter neuer Kennung übernommen (neue Szenensammlung)
        const QString before = m_server->inputs.first().uuid;
        m_server->removeInputWithEvent(before);
        QTRY_COMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Missing);
        const QString next = m_server->addInput(QStringLiteral("Neue Sammlung"), settingsOf(kUrlA));
        m_server->sendEvent(QStringLiteral("CurrentSceneCollectionChanged"),
                            {{QStringLiteral("sceneCollectionName"), QStringLiteral("Irgendeine")}});
        QTRY_COMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Present);
        QCOMPARE(entryOf(*m_sources, kOverlayA).inputName, QStringLiteral("Neue Sammlung"));
        QVERIFY(fileText().contains(next.toUtf8()));
        QVERIFY(!fileText().contains(before.toUtf8()));
    }

    void events()
    {
        connectAll({{kOverlayA, kUrlA, 1920, 1080}});
        waitSynced();
        Answer created;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-live"), QStringLiteral("Neu"), created.done());
        QTRY_VERIFY(created.arrived);
        const QString uuid = m_server->inputs.first().uuid;

        // Umbenannt: nur der Name ändert sich
        QSignalSpy changed(m_sources.get(), &ObsSources::entriesChanged);
        m_server->sendEvent(QStringLiteral("InputNameChanged"), {{QStringLiteral("inputUuid"), uuid},
                                                                 {QStringLiteral("oldInputName"), QStringLiteral("Neu")},
                                                                 {QStringLiteral("inputName"), QStringLiteral("Umbenannt")}});
        QTRY_COMPARE(entryOf(*m_sources, kOverlayA).inputName, QStringLiteral("Umbenannt"));
        QCOMPARE(changed.size(), 1);
        QVERIFY(fileText().contains("Umbenannt"));

        // Szenen
        QSignalSpy scenes(m_sources.get(), &ObsSources::scenesChanged);
        m_server->scenes.append({QStringLiteral("scene-neu"), QStringLiteral("Neu")});
        m_server->sendEvent(QStringLiteral("SceneListChanged"), {{QStringLiteral("scenes"), QJsonArray()}});
        QTRY_COMPARE(m_sources->scenes().size(), 3);
        QCOMPARE(scenes.size(), 1);

        // Während des Wechsels der Szenensammlung ist eine entfernte Quelle kein Fehler
        m_server->sendEvent(QStringLiteral("CurrentSceneCollectionChanging"),
                            {{QStringLiteral("sceneCollectionName"), QStringLiteral("Alt")}});
        m_server->collectionChanging = true;
        const QJsonObject removed{{QStringLiteral("inputName"), QStringLiteral("Umbenannt")},
                                  {QStringLiteral("inputUuid"), uuid}};
        m_server->sendEvent(QStringLiteral("InputRemoved"), removed);
        QTest::qWait(60);
        QCOMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Present);
        QCOMPARE(m_server->requestsDuringCollectionChange, 0);
        m_server->collectionChanging = false;
        m_server->sendEvent(QStringLiteral("CurrentSceneCollectionChanged"),
                            {{QStringLiteral("sceneCollectionName"), QStringLiteral("Alt")}});
        QTest::qWait(60);
        QCOMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Present);

        // Von Hand gelöscht: fehlt, Zuordnung bleibt (F7), nichts passiert von selbst
        m_server->removeInputWithEvent(uuid);
        QTRY_COMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Missing);
        QVERIFY(fileText().contains(uuid.toUtf8()));
        QTest::qWait(60);
        QCOMPARE(m_server->requestCount(QStringLiteral("CreateInput")), 1);

        // Von Hand angelegt: Ereignis InputCreated, Übernahme (F10)
        const QString manual = m_server->addInput(QStringLiteral("Von Hand"), settingsOf(kUrlA));
        m_server->sendEvent(QStringLiteral("InputCreated"), {{QStringLiteral("inputUuid"), manual},
                                                             {QStringLiteral("inputName"), QStringLiteral("Von Hand")},
                                                             {QStringLiteral("inputKind"), QStringLiteral("browser_source")},
                                                             {QStringLiteral("inputSettings"), settingsOf(kUrlA)}});
        QTRY_COMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Present);
        QCOMPARE(entryOf(*m_sources, kOverlayA).inputName, QStringLiteral("Von Hand"));
    }

    // Neue Adresse und neue Grösse (neuer Token, Port, Adresse im Heimnetz, Auflösung, F11): OBS folgt,
    // nur bei einem Unterschied, und andere Einstellungen der Quelle bleiben (overlay=true).
    void followsDesired()
    {
        connectAll({{kOverlayA, kUrlA, 1920, 1080}});
        waitSynced();
        Answer created;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-live"), QStringLiteral("Neu"), created.done());
        QTRY_VERIFY(created.arrived);
        const QString uuid = m_server->inputs.first().uuid;
        m_server->input(uuid)->settings.insert(QStringLiteral("css"), QStringLiteral("von Hand"));
        QCOMPARE(m_server->requestCount(QStringLiteral("SetInputSettings")), 0);

        // Gleiche Werte: nichts
        m_sources->setDesired({{kOverlayA, kUrlA, 1920, 1080}});
        QTest::qWait(60);
        QCOMPARE(m_server->requestCount(QStringLiteral("SetInputSettings")), 0);

        // Neue Adresse
        m_sources->setDesired({{kOverlayA, kUrlB, 1920, 1080}});
        QTRY_COMPARE(m_server->input(uuid)->settings.value(QStringLiteral("url")).toString(), kUrlB);
        QCOMPARE(m_server->requestCount(QStringLiteral("SetInputSettings")), 1);
        const QJsonObject sent = m_server->requestsOf(QStringLiteral("SetInputSettings")).first().data;
        QCOMPARE(sent.value(QStringLiteral("inputUuid")).toString(), uuid);
        QCOMPARE(sent.value(QStringLiteral("overlay")).toBool(false), true);
        QCOMPARE(m_server->input(uuid)->settings.value(QStringLiteral("css")).toString(), QStringLiteral("von Hand"));

        // Neue Grösse
        m_sources->setDesired({{kOverlayA, kUrlB, 1280, 720}});
        QTRY_COMPARE(m_server->input(uuid)->settings.value(QStringLiteral("width")).toInt(), 1280);
        QCOMPARE(m_server->input(uuid)->settings.value(QStringLiteral("height")).toInt(), 720);
        QCOMPARE(m_server->requestCount(QStringLiteral("SetInputSettings")), 2);

        // Keine Adresse möglich: Quelle bleibt, wie sie ist
        m_sources->setDesired({{kOverlayA, QString(), 1280, 720}});
        QTest::qWait(60);
        QCOMPARE(m_server->requestCount(QStringLiteral("SetInputSettings")), 2);
        QCOMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Present);

        // Nicht in die Datei
        QVERIFY(!fileText().contains("geheimerToken"));
    }

    // Änderung, während OBS aus ist: beim nächsten Verbinden nachgeführt (Plan 1.1).
    void followsAfterReconnect()
    {
        connectAll({{kOverlayA, kUrlA, 1920, 1080}});
        waitSynced();
        Answer created;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-live"), QStringLiteral("Neu"), created.done());
        QTRY_VERIFY(created.arrived);
        const QString uuid = m_server->inputs.first().uuid;

        m_server->stop();
        QTRY_COMPARE(m_connection->state(), State::Error);
        m_sources->setDesired({{kOverlayA, kUrlB, 1280, 720}});
        // Zuletzt bekannter Stand, kein Versuch ins Leere
        QCOMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Present);
        QCOMPARE(m_server->requestCount(QStringLiteral("SetInputSettings")), 0);

        QVERIFY(m_server->start());
        QTRY_COMPARE(m_connection->state(), State::Connected);
        QTRY_COMPARE(m_server->input(uuid)->settings.value(QStringLiteral("url")).toString(), kUrlB);
        QCOMPARE(m_server->input(uuid)->settings.value(QStringLiteral("width")).toInt(), 1280);

        // Abbruch während der Anfrage: wird nach dem Wiederverbinden neu gesendet
        m_server->closingRequests.insert(QStringLiteral("SetInputSettings"));
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("nicht nachgeführt")));
        m_sources->setDesired({{kOverlayA, kUrlA, 1280, 720}});
        QTRY_COMPARE(m_connection->state(), State::Error);
        m_server->stop();
        m_server->closingRequests.clear();
        // Der Testserver führt die Anfrage vor dem Abbruch aus; hier ohne Wirkung vortäuschen.
        m_server->input(uuid)->settings.insert(QStringLiteral("url"), kUrlB);
        QVERIFY(m_server->start());
        QTRY_COMPARE(m_connection->state(), State::Connected);
        QTRY_COMPARE(m_server->input(uuid)->settings.value(QStringLiteral("url")).toString(), kUrlA);
    }

    // F10 nachträglich: Übernommene Quelle mit abweichender Grösse folgt ebenfalls.
    void adoptedSourceFollowsSize()
    {
        const QString uuid = m_server->addInput(QStringLiteral("Von Hand"), settingsOf(kUrlA, 800, 600));
        connectAll({{kOverlayA, kUrlA, 1920, 1080}});
        QTRY_COMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Present);
        QTRY_COMPARE(m_server->input(uuid)->settings.value(QStringLiteral("width")).toInt(), 1920);
        // GetInputSettings liefert keine Standardwerte: fehlende Grösse gilt als Unterschied
        const QString bare = m_server->addInput(QStringLiteral("Ohne Grösse"), {{QStringLiteral("url"), kUrlB}});
        m_sources->setDesired({{kOverlayA, kUrlA, 1920, 1080}, {kOverlayB, kUrlB, 800, 600}});
        m_server->sendEvent(QStringLiteral("InputCreated"), {{QStringLiteral("inputKind"), QStringLiteral("browser_source")}});
        QTRY_COMPARE(m_server->input(bare)->settings.value(QStringLiteral("width")).toInt(), 800);
    }

    // F8: Overlay gelöscht: Zuordnung weg, Quelle bleibt in OBS.
    void overlayRemoved()
    {
        connectAll({{kOverlayA, kUrlA, 1920, 1080}, {kOverlayB, kUrlB, 1920, 1080}});
        waitSynced();
        Answer created;
        m_sources->createSource(kOverlayA, QStringLiteral("scene-live"), QStringLiteral("Neu"), created.done());
        QTRY_VERIFY(created.arrived);
        QCOMPARE(m_sources->inputNameOf(kOverlayA), QStringLiteral("Neu"));
        m_sources->setDesired({{kOverlayB, kUrlB, 1920, 1080}});
        QCOMPARE(m_sources->inputNameOf(kOverlayA), QString());
        QVERIFY(!fileText().contains(kOverlayA.toString(QUuid::WithoutBraces).toUtf8()));
        QCOMPARE(m_server->inputs.size(), 1);
        QCOMPARE(m_sources->entries().size(), 1);
    }

    void invalidFileIsRenamed()
    {
        {
            QFile f(filePath());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{ kaputt");
        }
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("obs-quellen.json ungültig")));
        m_server->addInput(QStringLiteral("Von Hand"), settingsOf(kUrlA));
        connectAll({{kOverlayA, kUrlA, 1920, 1080}});
        QCOMPARE(QDir(m_dir.path()).entryList({QStringLiteral("obs-quellen.json.ungueltig-*")}).size(), 1);
        // Der Abgleich baut die Zuordnung wieder auf
        QTRY_COMPARE(entryOf(*m_sources, kOverlayA).state, SourceState::Present);
        QVERIFY(fileText().contains("Von Hand"));
    }
};

QTEST_GUILESS_MAIN(TestObsSources)
#include "tst_obsquellen.moc"
