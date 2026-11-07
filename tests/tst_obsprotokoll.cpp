// Protokoll von obs-websocket (P10c, T-05): Anmeldung, Nachrichten, Listen. Ohne Verbindung.

#include "obsprotocol.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QRandomGenerator>
#include <QTest>

using namespace dss::obs;

namespace {

// SHA-256 mit einem anderen Programm gerechnet (coreutils), damit der Test nicht denselben Code
// prüft wie den, der die Anmeldung rechnet.
QByteArray base64Sha256External(const QByteArray &text)
{
    QProcess p;
    p.start(QStringLiteral("sha256sum"), {});
    p.write(text);
    p.closeWriteChannel();
    if (!p.waitForFinished(5000) || p.exitCode() != 0)
        return {};
    return QByteArray::fromHex(p.readAllStandardOutput().left(64)).toBase64();
}

QString randomText()
{
    return QString::number(QRandomGenerator::system()->generate64(), 36);
}

} // namespace

class TestObsProtocol : public QObject
{
    Q_OBJECT

private slots:
    void authenticationMatchesDocumentedSteps()
    {
        // Regel 12: zufällige Testwerte, nichts gespeichert.
        const QString password = randomText() + QStringLiteral("äß");
        const QString salt = randomText();
        const QString challenge = randomText();
        const QByteArray secret = base64Sha256External(password.toUtf8() + salt.toUtf8());
        QVERIFY(!secret.isEmpty());
        const QByteArray expected = base64Sha256External(secret + challenge.toUtf8());
        QCOMPARE(authenticationString(password, salt, challenge), QString::fromLatin1(expected));
        // Das Passwort steht nicht in der Nachricht.
        Hello hello;
        hello.authenticationRequired = true;
        hello.salt = salt;
        hello.challenge = challenge;
        const QString frame = encodeFrame(OpCode::Identify, identifyData(hello, password));
        QVERIFY(!frame.contains(password));
        QVERIFY(frame.contains(QString::fromLatin1(expected)));
    }

    void helloWithAndWithoutAuthentication()
    {
        const QString text = QStringLiteral(
            R"({"op":0,"d":{"obsStudioVersion":"33.0.1","obsWebSocketVersion":"5.7.6","rpcVersion":1,)"
            R"("authentication":{"challenge":"c","salt":"s"}}})");
        OpCode op = OpCode::Event;
        QJsonObject data;
        QString error;
        QVERIFY2(parseFrame(text, &op, &data, &error), qPrintable(error));
        QCOMPARE(op, OpCode::Hello);
        Hello hello;
        QVERIFY2(parseHello(data, &hello, &error), qPrintable(error));
        QCOMPARE(hello.obsVersion, QStringLiteral("33.0.1"));
        QCOMPARE(hello.websocketVersion, QStringLiteral("5.7.6"));
        QCOMPARE(hello.rpcVersion, 1);
        QVERIFY(hello.authenticationRequired);
        QCOMPARE(hello.salt, QStringLiteral("s"));
        QCOMPARE(hello.challenge, QStringLiteral("c"));
        // Ohne Anmeldung: kein Feld authentication in Identify.
        data.remove(QStringLiteral("authentication"));
        QVERIFY(parseHello(data, &hello, &error));
        QVERIFY(!hello.authenticationRequired);
        const QJsonObject identify = identifyData(hello, QStringLiteral("egal"));
        QVERIFY(!identify.contains(QStringLiteral("authentication")));
        QCOMPARE(identify.value(QStringLiteral("rpcVersion")).toInt(), 1);
        QCOMPARE(identify.value(QStringLiteral("eventSubscriptions")).toInt(), 15);
    }

    void invalidFramesAreRejected()
    {
        OpCode op = OpCode::Event;
        QJsonObject data;
        QString error;
        QVERIFY(!parseFrame(QStringLiteral("kein json"), &op, &data, &error));
        QVERIFY(!parseFrame(QStringLiteral("[]"), &op, &data, &error));
        QVERIFY(!parseFrame(QStringLiteral(R"({"op":0})"), &op, &data, &error));
        QVERIFY(!parseFrame(QStringLiteral(R"({"d":{}})"), &op, &data, &error));
        Hello hello;
        QVERIFY(!parseHello(QJsonObject{}, &hello, &error));
        QVERIFY(!parseHello(QJsonObject{{QStringLiteral("rpcVersion"), 1},
                                        {QStringLiteral("authentication"), QJsonObject{}}},
                            &hello, &error));
        Response response;
        QVERIFY(!parseResponse(QJsonObject{}, &response, &error));
        Event event;
        QVERIFY(!parseEvent(QJsonObject{}, &event, &error));
    }

    void requestAndResponse()
    {
        const QJsonObject fields{{QStringLiteral("inputKind"), QString::fromLatin1(kBrowserKind)}};
        const QJsonObject request = requestData(QStringLiteral("GetInputList"), QStringLiteral("7"), fields);
        QCOMPARE(request.value(QStringLiteral("requestType")).toString(), QStringLiteral("GetInputList"));
        QCOMPARE(request.value(QStringLiteral("requestId")).toString(), QStringLiteral("7"));
        QCOMPARE(request.value(QStringLiteral("requestData")).toObject(), fields);
        QVERIFY(!requestData(QStringLiteral("GetVersion"), QStringLiteral("8"), {})
                     .contains(QStringLiteral("requestData")));

        const QJsonObject answer{
            {QStringLiteral("requestId"), QStringLiteral("7")},
            {QStringLiteral("requestType"), QStringLiteral("CreateInput")},
            {QStringLiteral("requestStatus"),
             QJsonObject{{QStringLiteral("result"), false},
                         {QStringLiteral("code"), kStatusResourceAlreadyExists},
                         {QStringLiteral("comment"), QStringLiteral("schon da")}}}};
        Response response;
        QString error;
        QVERIFY2(parseResponse(answer, &response, &error), qPrintable(error));
        QCOMPARE(response.requestId, QStringLiteral("7"));
        QVERIFY(!response.ok);
        QCOMPARE(response.code, 601);
        QCOMPARE(response.comment, QStringLiteral("schon da"));
        QVERIFY(response.data.isEmpty());
    }

    void closeCodes()
    {
        QVERIFY(!closeCodeAllowsReconnect(4009));
        QVERIFY(!closeCodeAllowsReconnect(4010));
        QVERIFY(!closeCodeAllowsReconnect(4011));
        QVERIFY(closeCodeAllowsReconnect(1000));
        QVERIFY(closeCodeAllowsReconnect(1006));
        QVERIFY(closeCodeAllowsReconnect(4000));
    }

    void scenesInOrderOfObs()
    {
        // Wie Obs_ArrayHelper.cpp: sceneIndex von unten gezählt.
        const QJsonObject data{
            {QStringLiteral("currentProgramSceneName"), QStringLiteral("Live")},
            {QStringLiteral("currentProgramSceneUuid"), QStringLiteral("u-live")},
            {QStringLiteral("scenes"),
             QJsonArray{QJsonObject{{QStringLiteral("sceneIndex"), 0},
                                    {QStringLiteral("sceneName"), QStringLiteral("Pause")},
                                    {QStringLiteral("sceneUuid"), QStringLiteral("u-pause")}},
                        QJsonObject{{QStringLiteral("sceneIndex"), 1},
                                    {QStringLiteral("sceneName"), QStringLiteral("Live")},
                                    {QStringLiteral("sceneUuid"), QStringLiteral("u-live")}},
                        // Ohne Kennung: übergangen, nicht geraten.
                        QJsonObject{{QStringLiteral("sceneIndex"), 2},
                                    {QStringLiteral("sceneName"), QStringLiteral("Kaputt")}}}}};
        const QList<Scene> scenes = parseScenes(data);
        QCOMPARE(scenes.size(), 2);
        QCOMPARE(scenes.at(0), (Scene{QStringLiteral("u-live"), QStringLiteral("Live")}));
        QCOMPARE(scenes.at(1), (Scene{QStringLiteral("u-pause"), QStringLiteral("Pause")}));
        QCOMPARE(parseCurrentScene(data), (Scene{QStringLiteral("u-live"), QStringLiteral("Live")}));
        // Die Doku erlaubt null.
        QVERIFY(parseCurrentScene(QJsonObject{{QStringLiteral("currentProgramSceneName"), QJsonValue()}})
                    .isNull());
        QCOMPARE(parseChangedScene(QJsonObject{{QStringLiteral("sceneName"), QStringLiteral("A")},
                                               {QStringLiteral("sceneUuid"), QStringLiteral("u-a")}}),
                 (Scene{QStringLiteral("u-a"), QStringLiteral("A")}));
    }

    void inputsTolerant()
    {
        const QJsonObject data{
            {QStringLiteral("inputs"),
             QJsonArray{QJsonObject{{QStringLiteral("inputName"), QStringLiteral("Quelle")},
                                    {QStringLiteral("inputUuid"), QStringLiteral("u-1")},
                                    {QStringLiteral("inputKind"), QString::fromLatin1(kBrowserKind)}},
                        QJsonObject{{QStringLiteral("inputName"), QStringLiteral("ohne Kennung")}},
                        QStringLiteral("kein Objekt")}}};
        const QList<Input> inputs = parseInputs(data);
        QCOMPARE(inputs.size(), 1);
        QCOMPARE(inputs.first(), (Input{QStringLiteral("u-1"), QStringLiteral("Quelle")}));
        QVERIFY(parseInputs(QJsonObject{}).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestObsProtocol)
#include "tst_obsprotokoll.moc"
