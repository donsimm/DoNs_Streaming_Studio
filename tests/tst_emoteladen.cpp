// Prüft das Laden der Emotes (P14 Emotes em-4) ohne Qt Gui und ohne echtes Netz: Ein lokaler
// HTTP-Stub auf 127.0.0.1 liefert die Antworten aus tests/daten/emotes/ (Abrufe em-0, 2026-11-16)
// und selbst gemachte Bild-Bytes, zählt Anfragen und Methoden. Der Zwischenspeicher liegt in einem
// QTemporaryDir, nie in ~/.cache.

#include "emoteloader.h"
#include "emotes.h"

#include <QFile>
#include <QHash>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using namespace dss;

namespace {

const QString kChannel = QStringLiteral("100000001"); // neutrale Kanal-ID der Testdaten

QByteArray readFile(const char *name)
{
    QFile f(QStringLiteral(DSS_TEST_EMOTES_DIR "/") + QLatin1String(name));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Pfad beim Stub für eine echte Adresse: /<host><Pfad>, passend zu endpointsFor()
QByteArray stubPath(const QUrl &real)
{
    return ('/' + real.host() + real.path()).toUtf8();
}

class HttpStub : public QObject
{
public:
    struct Response
    {
        int status = 200;
        QByteArray body;
        QByteArray headers; // zusätzliche Kopfzeilen, je mit \r\n
    };
    struct Request
    {
        QByteArray method;
        QByteArray path;
        QByteArray head;
    };

    HttpStub()
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *s = m_server.nextPendingConnection()) {
                connect(s, &QTcpSocket::readyRead, this, [this, s] { readRequest(s); });
                connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            }
        });
    }

    QUrl base(const QString &host) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/%2").arg(m_server.serverPort()).arg(host));
    }
    EmoteEndpoints endpoints() const
    {
        EmoteEndpoints endpoints;
        for (const EmoteEndpoint &e : EmoteEndpoints::standard().list)
            endpoints.list.append({e.host, base(e.host)});
        return endpoints;
    }
    void close() { m_server.close(); }
    void release()
    {
        hold = false;
        for (const auto &[socket, response] : std::exchange(m_held, {}))
            if (socket)
                send(socket, response);
    }
    int count(const QByteArray &prefix) const
    {
        int n = 0;
        for (const Request &r : requests)
            n += r.path.startsWith(prefix) ? 1 : 0;
        return n;
    }

    QHash<QByteArray, Response> routes;
    QList<Request> requests;
    bool hold = false; // Antworten zurückhalten bis release()

private:
    void readRequest(QTcpSocket *s)
    {
        m_buffer[s] += s->readAll();
        const qsizetype end = m_buffer[s].indexOf("\r\n\r\n");
        if (end < 0)
            return;
        const QByteArray head = m_buffer.take(s).left(end);
        const QList<QByteArray> first = head.left(head.indexOf("\r\n")).split(' ');
        const Request request{first.value(0), first.value(1), head};
        requests.append(request);
        const Response response = routes.value(request.path, Response{404, "{}", {}});
        if (hold)
            m_held.append({s, response});
        else
            send(s, response);
    }
    static void send(QTcpSocket *s, const Response &r)
    {
        static const QHash<int, QByteArray> reasons = {
            {200, "OK"}, {302, "Found"}, {404, "Not Found"}, {502, "Bad Gateway"}};
        s->write("HTTP/1.1 " + QByteArray::number(r.status) + ' ' + reasons.value(r.status, "X")
                 + "\r\nContent-Length: " + QByteArray::number(r.body.size()) + "\r\nConnection: close\r\n"
                 + r.headers + "\r\n" + r.body);
        s->disconnectFromHost();
    }

    QTcpServer m_server;
    QHash<QTcpSocket *, QByteArray> m_buffer;
    QList<QPair<QPointer<QTcpSocket>, Response>> m_held;
};

// Fängt die Signale des Loaders auf
struct Results
{
    explicit Results(EmoteLoader *loader)
    {
        QObject::connect(loader, &EmoteLoader::indexChanged, loader, [this](const EmoteIndex &i) {
            index = i;
            ++indexCount;
        });
        QObject::connect(loader, &EmoteLoader::stateChanged, loader, [this](const EmoteLoadState &s) {
            state = s;
            ++stateCount;
        });
        QObject::connect(loader, &EmoteLoader::imageLoaded, loader, [this](const QString &key, const QByteArray &bytes) {
            images.append({key, bytes});
        });
    }
    EmoteIndex index;
    EmoteLoadState state;
    int indexCount = 0;
    int stateCount = 0;
    QList<QPair<QString, QByteArray>> images;
};

int listSize(EmoteProvider provider, bool channel, const char *file)
{
    return int(parseEmoteList(provider, channel, 200, readFile(file)).emotes.size());
}

} // namespace

class TestEmoteLoading : public QObject
{
    Q_OBJECT

private:
    // Die sechs Listen wie am 2026-11-16: 7TV und FFZ kennen den Kanal nicht (404)
    static void addLists(HttpStub *stub, const QByteArray &headers = {})
    {
        stub->routes.insert(stubPath(emoteListUrl(EmoteProvider::Bttv, std::nullopt)), {200, readFile("bttv-global.json"), headers});
        stub->routes.insert(stubPath(emoteListUrl(EmoteProvider::Bttv, kChannel)), {200, readFile("bttv-kanal.json"), headers});
        stub->routes.insert(stubPath(emoteListUrl(EmoteProvider::SevenTv, std::nullopt)), {200, readFile("7tv-global.json"), headers});
        stub->routes.insert(stubPath(emoteListUrl(EmoteProvider::SevenTv, kChannel)), {404, readFile("7tv-kanal.json"), headers});
        stub->routes.insert(stubPath(emoteListUrl(EmoteProvider::Ffz, std::nullopt)), {200, readFile("ffz-global.json"), headers});
        stub->routes.insert(stubPath(emoteListUrl(EmoteProvider::Ffz, kChannel)), {404, readFile("ffz-kanal.json"), headers});
    }
    static Emote bttvEmote()
    {
        return parseEmoteList(EmoteProvider::Bttv, false, 200, readFile("bttv-global.json")).emotes.value(0);
    }

    int m_bttv = 0;
    int m_sevenTv = 0;
    int m_ffz = 0;

private slots:
    void initTestCase()
    {
        m_bttv = listSize(EmoteProvider::Bttv, false, "bttv-global.json") + listSize(EmoteProvider::Bttv, true, "bttv-kanal.json");
        m_sevenTv = listSize(EmoteProvider::SevenTv, false, "7tv-global.json");
        m_ffz = listSize(EmoteProvider::Ffz, false, "ffz-global.json");
        QVERIFY(m_bttv > 0 && m_sevenTv > 0 && m_ffz > 0);
        QVERIFY(!bttvEmote().id.isEmpty());
    }

    void standardEndpointsOnlyFourProviders()
    {
        const EmoteEndpoints e = EmoteEndpoints::standard();
        QCOMPARE(e.list.size(), 7);
        for (const EmoteEndpoint &endpoint : e.list)
            QCOMPARE(endpoint.base.scheme(), QStringLiteral("https"));
        const QUrl list = emoteListUrl(EmoteProvider::SevenTv, std::nullopt);
        QCOMPARE(e.map(list), list);
        QVERIFY(e.allows(list));
        QVERIFY(e.allows(emoteImageUrl(bttvEmote(), false, true)));
        QVERIFY(!e.allows(QUrl(QStringLiteral("http://7tv.io/v3/emote-sets/global"))));     // kein https
        QVERIFY(!e.allows(QUrl(QStringLiteral("https://7tv.io:8443/v3/emote-sets/global")))); // anderer Port
        QVERIFY(!e.allows(QUrl(QStringLiteral("https://evil.7tv.io.example/x"))));
        QVERIFY(!e.allows(QUrl(QStringLiteral("https://user@7tv.io/x"))));
        QVERIFY(e.map(QUrl(QStringLiteral("https://example.com/x"))).isEmpty());
        // Stub mit Pfad davor: nur darunter, auch nach «..»
        HttpStub stub;
        const EmoteEndpoints local = stub.endpoints();
        QVERIFY(local.allows(local.map(list)));
        QVERIFY(!local.allows(stub.base(QStringLiteral("7tv.iox"))));
        QVERIFY(!local.allows(local.map(list).resolved(QUrl(QStringLiteral("../../../evil/x")))));
    }

    void sixListsOnlyGet()
    {
        HttpStub stub;
        addLists(&stub);
        QTemporaryDir cache;
        EmoteLoader loader(cache.path(), stub.endpoints());
        Results r(&loader);
        loader.load(kChannel, true);
        QVERIFY(loader.isLoading());
        QTRY_COMPARE(r.indexCount, 1);
        QCOMPARE(r.stateCount, 1);
        QVERIFY(!loader.isLoading());
        QCOMPARE(stub.requests.size(), 6);
        for (const HttpStub::Request &request : std::as_const(stub.requests)) {
            QCOMPARE(request.method, QByteArray("GET"));
            QVERIFY2(request.head.toLower().contains("\r\nuser-agent: dons-streaming-studio/"), request.head.constData());
            QVERIFY(!request.head.toLower().contains("authorization"));
        }
        QCOMPARE(r.index.count(EmoteProvider::Bttv), m_bttv);
        QCOMPARE(r.index.count(EmoteProvider::SevenTv), m_sevenTv);
        QCOMPARE(r.index.count(EmoteProvider::Ffz), m_ffz);
        EmoteLoadState expected;
        expected.counts = {{EmoteProvider::Bttv, m_bttv}, {EmoteProvider::SevenTv, m_sevenTv}, {EmoteProvider::Ffz, m_ffz}};
        QCOMPARE(r.state, expected);
    }

    void withoutChannelOnlyGlobal()
    {
        HttpStub stub;
        addLists(&stub);
        QTemporaryDir cache;
        EmoteLoader loader(cache.path(), stub.endpoints());
        Results r(&loader);
        loader.load(std::nullopt, true);
        QTRY_COMPARE(r.indexCount, 1);
        QCOMPARE(stub.requests.size(), 3);
        QVERIFY(!r.state.channel);
        // Kanal-ID nicht nur aus Ziffern: wie ohne Kanal
        loader.load(QStringLiteral("../x"), true);
        QTRY_COMPARE(r.indexCount, 2);
        QCOMPARE(stub.requests.size(), 6);
        QVERIFY(!r.state.channel);
    }

    void withoutWebpNoBttvOrSevenTv()
    {
        HttpStub stub;
        addLists(&stub);
        QTemporaryDir cache;
        EmoteLoader loader(cache.path(), stub.endpoints());
        Results r(&loader);
        QTest::ignoreMessage(QtWarningMsg, "Emotes: WebP fehlt (Paket qt6-image-formats-plugins), BTTV und 7TV nicht geladen");
        loader.load(kChannel, false);
        QTRY_COMPARE(r.indexCount, 1);
        QCOMPARE(stub.requests.size(), 2);
        QCOMPARE(stub.count("/api.frankerfacez.com/"), 2);
        QCOMPARE(r.state.counts, (QList<QPair<EmoteProvider, int>>{{EmoteProvider::Ffz, m_ffz}}));
        QVERIFY(!r.state.webp);
        QVERIFY(r.state.unreachable.isEmpty());
        // Kein Bild von BTTV ohne WebP; die Meldung «WebP fehlt» nur einmal
        QVERIFY(loader.image(bttvEmote(), false).isEmpty());
        loader.load(std::nullopt, false);
        QTRY_COMPARE(r.indexCount, 2);
        QCOMPARE(stub.requests.size(), 3);
    }

    void listTooLargeStaysText()
    {
        HttpStub stub;
        addLists(&stub);
        stub.routes[stubPath(emoteListUrl(EmoteProvider::Bttv, std::nullopt))].body = QByteArray(kEmoteListMaxBytes + 1, ' ');
        QTemporaryDir cache;
        EmoteLoader loader(cache.path(), stub.endpoints());
        Results r(&loader);
        QTest::ignoreMessage(QtWarningMsg, "Emotes: BTTV global: grösser als 8 MiB, nicht gelesen");
        loader.load(kChannel, true);
        QTRY_COMPARE(r.indexCount, 1);
        QCOMPARE(r.index.count(EmoteProvider::Bttv), listSize(EmoteProvider::Bttv, true, "bttv-kanal.json"));
        QCOMPARE(r.state.unreachable, QList<EmoteProvider>{EmoteProvider::Bttv});
        QCOMPARE(r.state.network, EmoteLoadState::Network::Online);
    }

    void redirects()
    {
        HttpStub stub;
        HttpStub foreign;
        addLists(&stub);
        // Fremder Server (anderer Port) abgewiesen, Weiterleitung unter derselben base erlaubt
        const QByteArray bttv = stubPath(emoteListUrl(EmoteProvider::Bttv, std::nullopt));
        stub.routes[bttv] = {302, {}, "Location: " + foreign.base(QStringLiteral("api.betterttv.net")).toEncoded() + bttv + "\r\n"};
        const QByteArray seven = stubPath(emoteListUrl(EmoteProvider::SevenTv, std::nullopt));
        stub.routes.insert(seven + "-neu", stub.routes.value(seven));
        stub.routes[seven] = {302, {}, "Location: " + seven + "-neu\r\n"};
        foreign.routes = stub.routes;
        QTemporaryDir cache;
        EmoteLoader loader(cache.path(), stub.endpoints());
        Results r(&loader);
        QTest::ignoreMessage(QtWarningMsg, "Emotes: BTTV global: Weiterleitung an einen fremden Server abgewiesen");
        loader.load(kChannel, true);
        QTRY_COMPARE(r.indexCount, 1);
        QCOMPARE(foreign.requests.size(), 0);
        QCOMPARE(stub.count(seven + "-neu"), 1);
        QCOMPARE(r.index.count(EmoteProvider::SevenTv), m_sevenTv);
        QCOMPARE(r.index.count(EmoteProvider::Bttv), listSize(EmoteProvider::Bttv, true, "bttv-kanal.json"));
        QCOMPARE(r.state.unreachable, QList<EmoteProvider>{EmoteProvider::Bttv});
    }

    void offlineListsFromCache()
    {
        QTemporaryDir cache;
        HttpStub stub;
        const EmoteEndpoints endpoints = stub.endpoints();
        {
            // Antworten ohne Cache-Control: die Kopfzeilen der Listen sind nicht gemessen (em-0)
            addLists(&stub);
            EmoteLoader loader(cache.path(), endpoints);
            Results r(&loader);
            loader.load(kChannel, true);
            QTRY_COMPARE(r.indexCount, 1);
            QCOMPARE(r.state.network, EmoteLoadState::Network::Online);
        }
        // Stub aus: Listen vom letzten Mal
        stub.close();
        {
            EmoteLoader loader(cache.path(), endpoints);
            Results r(&loader);
            loader.load(kChannel, true);
            QTRY_COMPARE(r.indexCount, 1);
            QCOMPARE(r.state.network, EmoteLoadState::Network::OfflineCached);
            QCOMPARE(r.index.count(EmoteProvider::Bttv), m_bttv);
            QCOMPARE(r.index.count(EmoteProvider::SevenTv), m_sevenTv);
            QCOMPARE(r.index.count(EmoteProvider::Ffz), m_ffz);
            QCOMPARE(emoteStatusText(r.state), QStringLiteral("Emotes: Ohne Netz, Listen vom letzten Mal"));
        }
        // Ohne Netz und leerer Zwischenspeicher
        QTemporaryDir empty;
        EmoteLoader loader(empty.path(), endpoints);
        Results r(&loader);
        loader.load(kChannel, true);
        QTRY_COMPARE(r.indexCount, 1);
        QCOMPARE(r.state.network, EmoteLoadState::Network::OfflineEmpty);
        QVERIFY(r.index.isEmpty());
    }

    void imagesOnceAndFromCache()
    {
        QTemporaryDir cache;
        const Emote emote = bttvEmote();
        const QUrl real = emoteImageUrl(emote, false, true);
        EmoteEndpoints endpoints;
        {
            HttpStub stub;
            endpoints = stub.endpoints();
            // Selbst gemachte Bytes (keine fremden Bilder im Repo); Kopf wie BTTV (em-0, curl -I)
            stub.routes.insert(stubPath(real), {200, "bild-1", "Content-Type: image/webp\r\nCache-Control: public,max-age=15552000,immutable\r\n"});
            EmoteLoader loader(cache.path(), endpoints);
            Results r(&loader);
            const QString key = loader.image(emote, false);
            QCOMPARE(key, real.toString());
            QCOMPARE(loader.image(emote, false), key); // läuft schon: keine zweite Anfrage
            QTRY_COMPARE(r.images.size(), 1);
            QCOMPARE(r.images.at(0), (QPair<QString, QByteArray>{key, "bild-1"}));
            QCOMPARE(stub.requests.size(), 1);
            // Zweites Bild aus dem Zwischenspeicher, ohne Anfrage
            QCOMPARE(loader.image(emote, false), key);
            QTRY_COMPARE(r.images.size(), 2);
            QCOMPARE(r.images.at(1).second, QByteArray("bild-1"));
            QCOMPARE(stub.requests.size(), 1);
        }
        // Server aus: das Bild kommt weiter aus dem Zwischenspeicher
        EmoteLoader loader(cache.path(), endpoints);
        Results r(&loader);
        loader.image(emote, false);
        QTRY_COMPARE(r.images.size(), 1);
        QCOMPARE(r.images.at(0).second, QByteArray("bild-1"));
    }

    void imageTooLargeOrMissing()
    {
        HttpStub stub;
        const Emote emote = bttvEmote();
        stub.routes.insert(stubPath(emoteImageUrl(emote, false, true)), {200, QByteArray(kEmoteImageMaxBytes + 1, 'x'), {}});
        QTemporaryDir cache;
        EmoteLoader loader(cache.path(), stub.endpoints());
        Results r(&loader);
        QTest::ignoreMessage(QtWarningMsg, "Emotes: Bild von cdn.betterttv.net nicht geladen: grösser als 2 MiB, nicht gelesen");
        const QString big = loader.image(emote, false);
        QTRY_COMPARE(r.images.size(), 1);
        QCOMPARE(r.images.at(0), (QPair<QString, QByteArray>{big, {}}));
        Emote missing = emote;
        missing.id = QStringLiteral("fehlt");
        QTest::ignoreMessage(QtWarningMsg, "Emotes: Bild von cdn.betterttv.net nicht geladen: Antwort 404");
        loader.image(missing, false);
        QTRY_COMPARE(r.images.size(), 2);
        QVERIFY(r.images.at(1).second.isEmpty());
    }

    void sameChannelIgnoredOtherChannelRestarts()
    {
        HttpStub stub;
        addLists(&stub);
        stub.hold = true;
        QTemporaryDir cache;
        EmoteLoader loader(cache.path(), stub.endpoints());
        Results r(&loader);
        loader.load(kChannel, true);
        QTRY_COMPARE(stub.requests.size(), 6);
        loader.load(kChannel, true); // läuft schon: nichts
        QTest::qWait(200);
        QCOMPARE(stub.requests.size(), 6);
        // Andere Kanal-ID: laufende Anfragen abgebrochen, neu begonnen
        const QString other = QStringLiteral("100000002");
        loader.load(other, true);
        QTRY_COMPARE(stub.requests.size(), 12);
        QCOMPARE(stub.count(stubPath(emoteListUrl(EmoteProvider::Bttv, other))), 1);
        stub.release();
        QTRY_COMPARE(r.indexCount, 1);
        QTest::qWait(200);
        QCOMPARE(r.indexCount, 1);
        QCOMPARE(r.stateCount, 1);
        // Kanal 100000002 kennt keiner (404): nur die globalen Listen
        QCOMPARE(r.index.count(EmoteProvider::Bttv), listSize(EmoteProvider::Bttv, false, "bttv-global.json"));
        QVERIFY(r.state.unreachable.isEmpty());
        QVERIFY(r.state.channel);
    }
};

QTEST_GUILESS_MAIN(TestEmoteLoading)
#include "tst_emoteladen.moc"
