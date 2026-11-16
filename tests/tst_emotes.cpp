// Prüft das Lesen der Emote-Listen von BTTV, 7TV und FFZ und die Adressen der Listen und Bilder
// ohne Netz und ohne Fenster (P14 Emotes em-2, Regel 9). Antworten vom 2026-11-16 aus
// tests/daten/emotes/ (Kanaldaten neutral); 7tv-kanal-mit-set.json und ffz-kanal-mit-raum.json
// sind erfunden, in der Form der echten Antworten (der eigene Kanal hat dort nichts).
// em-3: Vorrang im Index, Abschnitte einer Nachricht, Statustexte und Tooltips.

#include "emotes.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTest>

using namespace dss;

Q_DECLARE_METATYPE(dss::EmoteProvider)
Q_DECLARE_METATYPE(dss::EmoteLoadState)

namespace {

QByteArray testFile(const QString &name)
{
    QFile file(QStringLiteral(DSS_TEST_EMOTES_DIR "/") + name);
    if (!file.open(QIODevice::ReadOnly))
        qFatal("Testdatei fehlt: %s", qPrintable(file.fileName()));
    return file.readAll();
}

QStringList names(const QList<Emote> &emotes)
{
    QStringList out;
    for (const Emote &e : emotes)
        out.append(e.name);
    return out;
}

QSet<QString> zeroWidthNames(const QList<Emote> &emotes)
{
    QSet<QString> out;
    for (const Emote &e : emotes) {
        if (e.zeroWidth)
            out.insert(e.name);
    }
    return out;
}

const Emote *find(const QList<Emote> &emotes, const QString &name)
{
    for (const Emote &e : emotes) {
        if (e.name == name)
            return &e;
    }
    return nullptr;
}

bool allFrom(const QList<Emote> &emotes, EmoteProvider provider)
{
    for (const Emote &e : emotes) {
        if (e.provider != provider)
            return false;
    }
    return true;
}

QByteArray bttvArray(int count)
{
    QJsonArray array;
    for (int i = 0; i < count; ++i) {
        array.append(QJsonObject{{QStringLiteral("id"), QStringLiteral("id%1").arg(i)},
                                 {QStringLiteral("code"), QStringLiteral("emote%1").arg(i)},
                                 {QStringLiteral("animated"), false},
                                 {QStringLiteral("modifier"), false}});
    }
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

} // namespace

class TestEmotes : public QObject
{
    Q_OBJECT

private slots:
    void bttvGlobal()
    {
        const EmoteListResult r = parseEmoteList(EmoteProvider::Bttv, false, 200, testFile(QStringLiteral("bttv-global.json")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY(r.warning.isEmpty());
        // 65 in der Antwort, davon 9 Modifier (c!, h!, l!, r!, v!, z!, w!, p!, s!)
        QCOMPARE(r.emotes.size(), 56);
        QVERIFY(allFrom(r.emotes, EmoteProvider::Bttv));
        QCOMPARE(r.emotes.first().name, QStringLiteral(":tf:"));
        QCOMPARE(r.emotes.first().id, QStringLiteral("54fa8f1401e468494b85b537"));
        QVERIFY(!names(r.emotes).contains(QStringLiteral("c!")));
        QVERIFY(!names(r.emotes).contains(QStringLiteral("w!")));
        // Zero-Width nur aus der festen Liste; am 2026-11-16 waren davon cvMask und cvHazmat global
        QCOMPARE(zeroWidthNames(r.emotes), (QSet<QString>{QStringLiteral("cvMask"), QStringLiteral("cvHazmat")}));
        const Emote *sourPls = find(r.emotes, QStringLiteral("SourPls"));
        QVERIFY(sourPls);
        QVERIFY(sourPls->animated);
        const Emote *mm = find(r.emotes, QStringLiteral("M&Mjc"));
        QVERIFY(mm);
        QVERIFY(!mm->animated);
        QCOMPARE(mm->width, 36);
        QCOMPARE(mm->height, 30);
        QCOMPARE(find(r.emotes, QStringLiteral(":tf:"))->width, 0); // Grösse fehlt in der Antwort
    }

    void bttvChannel()
    {
        const EmoteListResult r = parseEmoteList(EmoteProvider::Bttv, true, 200, testFile(QStringLiteral("bttv-kanal.json")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.emotes.size(), 28); // 0 channelEmotes, 28 sharedEmotes
        QVERIFY(allFrom(r.emotes, EmoteProvider::Bttv));
        QVERIFY(zeroWidthNames(r.emotes).isEmpty());
        const Emote *clap = find(r.emotes, QStringLiteral("Clap"));
        QVERIFY(clap);
        QCOMPARE(clap->id, QStringLiteral("55b6f480e66682f576dd94f5"));
        QVERIFY(clap->animated);
        QVERIFY(!find(r.emotes, QStringLiteral("EZ"))->animated);
    }

    void bttvChannelNeverZeroWidth()
    {
        // Derselbe Name wie ein globales Zero-Width-Emote ist im Kanal ein normales Emote
        const QByteArray body = R"({"channelEmotes":[{"id":"a1","code":"cvMask","animated":false}],
                                    "sharedEmotes":[{"id":"a2","code":"TopHat","animated":true}]})";
        const EmoteListResult r = parseEmoteList(EmoteProvider::Bttv, true, 200, body);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(names(r.emotes), (QStringList{QStringLiteral("cvMask"), QStringLiteral("TopHat")}));
        QVERIFY(zeroWidthNames(r.emotes).isEmpty());
        const EmoteListResult global = parseEmoteList(EmoteProvider::Bttv, false, 200,
                                                      R"([{"id":"a2","code":"TopHat"},{"id":"a3","code":"tophat"}])");
        QCOMPARE(zeroWidthNames(global.emotes), QSet<QString>{QStringLiteral("TopHat")}); // exakt
    }

    void sevenTvGlobal()
    {
        const EmoteListResult r = parseEmoteList(EmoteProvider::SevenTv, false, 200, testFile(QStringLiteral("7tv-global.json")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.emotes.size(), 45);
        QVERIFY(allFrom(r.emotes, EmoteProvider::SevenTv));
        // flags & 1 im Set (data.flags 256 ist nur eine Empfehlung)
        QCOMPARE(zeroWidthNames(r.emotes),
                 (QSet<QString>{QStringLiteral("RainTime"), QStringLiteral("PETPET"), QStringLiteral("SteerR")}));
        const Emote *rain = find(r.emotes, QStringLiteral("RainTime"));
        QVERIFY(rain);
        QCOMPARE(rain->id, QStringLiteral("01FCY771D800007PQ2DF3GDTN6"));
        QVERIFY(rain->animated);
        QCOMPARE(rain->width, 32);
        QCOMPARE(rain->height, 32);
    }

    void sevenTvChannelUnknown()
    {
        // Echte Antwort: 404 «user not found», der Kanal hat kein 7TV-Konto
        const EmoteListResult r = parseEmoteList(EmoteProvider::SevenTv, true, 404, testFile(QStringLiteral("7tv-kanal.json")));
        QVERIFY(r.ok());
        QVERIFY(r.warning.isEmpty());
        QVERIFY(r.emotes.isEmpty());
        // Dieselbe Antwort mit 200 ist kein Benutzer
        QVERIFY(!parseEmoteList(EmoteProvider::SevenTv, true, 200, testFile(QStringLiteral("7tv-kanal.json"))).ok());
    }

    void sevenTvChannelWithSet()
    {
        const EmoteListResult r = parseEmoteList(EmoteProvider::SevenTv, true, 200,
                                                 testFile(QStringLiteral("7tv-kanal-mit-set.json")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        // testNurAvif hat keine WebP-Datei und fällt weg; Name aus dem Set, nicht aus data.name
        QCOMPARE(names(r.emotes),
                 (QStringList{QStringLiteral("testWinken"), QStringLiteral("testHut"), QStringLiteral("testUmbenannt")}));
        QCOMPARE(zeroWidthNames(r.emotes), QSet<QString>{QStringLiteral("testHut")});
        const Emote &winken = r.emotes.at(0);
        QVERIFY(winken.animated);
        QCOMPARE(winken.height, 32); // kleinste WebP-Stufe, auch wenn 2x vorne steht
        QCOMPARE(winken.width, 32);
        QCOMPARE(r.emotes.at(2).id, QStringLiteral("01TESTEMOTE0000000000000003"));
        QCOMPARE(r.emotes.at(2).width, 48);
        // Benutzer ohne aktives Set
        const EmoteListResult noSet = parseEmoteList(EmoteProvider::SevenTv, true, 200, R"({"id":"x","emote_set":null})");
        QVERIFY(noSet.ok());
        QVERIFY(noSet.emotes.isEmpty());
    }

    void ffzGlobal()
    {
        const EmoteListResult r = parseEmoteList(EmoteProvider::Ffz, false, 200, testFile(QStringLiteral("ffz-global.json")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        // Set 3: 11 Emotes, davon 2 hidden; Set 1539687 nur Modifier; Set 1532818 nicht in default_sets
        QCOMPARE(r.emotes.size(), 9);
        QVERIFY(allFrom(r.emotes, EmoteProvider::Ffz));
        QVERIFY(!names(r.emotes).contains(QStringLiteral("ZrehplaR"))); // hidden
        const Emote *yooHoo = find(r.emotes, QStringLiteral("YooHoo"));
        QVERIFY(yooHoo);
        QCOMPARE(yooHoo->id, QStringLiteral("6"));
        QCOMPARE(yooHoo->width, 28);
        QCOMPARE(yooHoo->height, 34);
        QVERIFY(!yooHoo->animated);
        QVERIFY(zeroWidthNames(r.emotes).isEmpty());
    }

    void ffzGlobalOnlyDefaultSets()
    {
        const QByteArray body = R"({"default_sets":[1],"sets":{
            "1":{"emoticons":[{"id":11,"name":"imStandard"}]},
            "2":{"emoticons":[{"id":21,"name":"nichtStandard"}]}}})";
        const EmoteListResult r = parseEmoteList(EmoteProvider::Ffz, false, 200, body);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(names(r.emotes), QStringList{QStringLiteral("imStandard")});
    }

    void ffzChannelUnknown()
    {
        // Echte Antwort: 404 «No such room»
        const EmoteListResult r = parseEmoteList(EmoteProvider::Ffz, true, 404, testFile(QStringLiteral("ffz-kanal.json")));
        QVERIFY(r.ok());
        QVERIFY(r.emotes.isEmpty());
        QVERIFY(!parseEmoteList(EmoteProvider::Ffz, true, 200, testFile(QStringLiteral("ffz-kanal.json"))).ok());
    }

    void ffzChannelWithRoom()
    {
        const EmoteListResult r = parseEmoteList(EmoteProvider::Ffz, true, 200,
                                                 testFile(QStringLiteral("ffz-kanal-mit-raum.json")));
        QVERIFY2(r.ok(), qPrintable(r.error));
        // testVersteckt (hidden) und testModifier fallen weg
        QCOMPARE(names(r.emotes), (QStringList{QStringLiteral("testStatisch"), QStringLiteral("testAnimiert")}));
        QVERIFY(!r.emotes.at(0).animated);
        QVERIFY(r.emotes.at(1).animated);
        QCOMPARE(r.emotes.at(1).id, QStringLiteral("900102"));
    }

    void brokenJson_data()
    {
        QTest::addColumn<EmoteProvider>("provider");
        QTest::addColumn<bool>("channel");
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<QString>("error");
        const QByteArray broken = R"([{"id":"a1","code":"GEHEIM")";
        QTest::newRow("BTTV kaputt") << EmoteProvider::Bttv << false << broken
                                     << QStringLiteral("Globale Liste von BTTV: kein gültiges JSON");
        QTest::newRow("7TV kaputt") << EmoteProvider::SevenTv << true << broken
                                    << QStringLiteral("Liste des Kanals von 7TV: kein gültiges JSON");
        QTest::newRow("FFZ leer") << EmoteProvider::Ffz << false << QByteArray()
                                  << QStringLiteral("Globale Liste von FFZ: kein gültiges JSON");
        QTest::newRow("BTTV global Objekt") << EmoteProvider::Bttv << false << QByteArray(R"({"GEHEIM":1})")
                                            << QStringLiteral("Globale Liste von BTTV: unerwartetes Format");
        QTest::newRow("BTTV Kanal Array") << EmoteProvider::Bttv << true << QByteArray("[]")
                                          << QStringLiteral("Liste des Kanals von BTTV: unerwartetes Format");
        QTest::newRow("7TV global Array") << EmoteProvider::SevenTv << false << QByteArray("[]")
                                          << QStringLiteral("Globale Liste von 7TV: unerwartetes Format");
        QTest::newRow("7TV emotes Zahl") << EmoteProvider::SevenTv << false << QByteArray(R"({"emotes":5})")
                                         << QStringLiteral("Globale Liste von 7TV: unerwartetes Format");
        QTest::newRow("7TV emote_set Text") << EmoteProvider::SevenTv << true << QByteArray(R"({"emote_set":"x"})")
                                            << QStringLiteral("Liste des Kanals von 7TV: unerwartetes Format");
        QTest::newRow("FFZ ohne default_sets") << EmoteProvider::Ffz << false << QByteArray(R"({"sets":{}})")
                                               << QStringLiteral("Globale Liste von FFZ: unerwartetes Format");
        QTest::newRow("FFZ sets Array") << EmoteProvider::Ffz << true << QByteArray(R"({"room":{},"sets":[]})")
                                        << QStringLiteral("Liste des Kanals von FFZ: unerwartetes Format");
    }

    void brokenJson()
    {
        QFETCH(EmoteProvider, provider);
        QFETCH(bool, channel);
        QFETCH(QByteArray, body);
        QFETCH(QString, error);
        const EmoteListResult r = parseEmoteList(provider, channel, 200, body);
        QCOMPARE(r.error, error);
        QVERIFY(r.emotes.isEmpty());
        QVERIFY(!r.error.contains(QStringLiteral("GEHEIM"))); // kein Inhalt der Antwort
    }

    void wrongFieldTypesSkipped()
    {
        // Einträge mit falschen Typen fallen weg, die übrigen bleiben
        const QByteArray bttv = R"([{"id":5,"code":"idZahl"},{"id":"a1","code":7},{"id":"a2"},"text",null,
                                    {"id":"a/b","code":"pfad"},{"id":"a3","code":"gut","animated":"ja","width":"36","height":-3}])";
        const EmoteListResult r = parseEmoteList(EmoteProvider::Bttv, false, 200, bttv);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(names(r.emotes), QStringList{QStringLiteral("gut")});
        QVERIFY(!r.emotes.first().animated);
        QCOMPARE(r.emotes.first().width, 0);
        QCOMPARE(r.emotes.first().height, 0);

        const QByteArray seventv = R"({"emotes":[{"name":"ohneData"},{"name":"filesFalsch","data":{"id":"b1","host":{"files":"x"}}},
            {"name":"gut","flags":"1","data":{"id":"b2","animated":1,"host":{"files":[{"format":"WEBP","width":"32"}]}}}]})";
        const EmoteListResult s = parseEmoteList(EmoteProvider::SevenTv, false, 200, seventv);
        QVERIFY2(s.ok(), qPrintable(s.error));
        QCOMPARE(names(s.emotes), QStringList{QStringLiteral("gut")});
        QVERIFY(!s.emotes.first().zeroWidth);
        QVERIFY(!s.emotes.first().animated);
        QCOMPARE(s.emotes.first().width, 0);

        const QByteArray ffz = R"({"default_sets":[1,"x",2],"sets":{"1":{"emoticons":[{"id":1.5,"name":"bruch"},
            {"id":3,"name":"gut","animated":{}}]},"2":"kein Set"}})";
        const EmoteListResult f = parseEmoteList(EmoteProvider::Ffz, false, 200, ffz);
        QVERIFY2(f.ok(), qPrintable(f.error));
        QCOMPARE(names(f.emotes), QStringList{QStringLiteral("gut")});
        QVERIFY(!f.emotes.first().animated); // leeres animated-Objekt
    }

    void httpStatus()
    {
        const QByteArray body = testFile(QStringLiteral("bttv-global.json"));
        EmoteListResult r = parseEmoteList(EmoteProvider::Bttv, false, 500, body);
        QCOMPARE(r.error, QStringLiteral("Globale Liste von BTTV: Antwort mit HTTP-Status 500"));
        QVERIFY(r.emotes.isEmpty());
        // 404 heisst nur bei der Liste des Kanals «nicht eingerichtet»
        r = parseEmoteList(EmoteProvider::SevenTv, false, 404, testFile(QStringLiteral("7tv-kanal.json")));
        QCOMPARE(r.error, QStringLiteral("Globale Liste von 7TV: Antwort mit HTTP-Status 404"));
        r = parseEmoteList(EmoteProvider::Bttv, true, 404, QByteArray());
        QVERIFY(r.ok());
        QVERIFY(r.emotes.isEmpty());
        r = parseEmoteList(EmoteProvider::Twitch, false, 200, body);
        QCOMPARE(r.error, QStringLiteral("Twitch hat keine Emote-Liste"));
    }

    void limitEmoteCount()
    {
        EmoteListResult r = parseEmoteList(EmoteProvider::Bttv, false, 200, bttvArray(kEmoteListMaxEmotes));
        QVERIFY(r.ok());
        QVERIFY(r.warning.isEmpty());
        QCOMPARE(r.emotes.size(), 2000);

        r = parseEmoteList(EmoteProvider::Bttv, false, 200, bttvArray(kEmoteListMaxEmotes + 5));
        QVERIFY(r.ok());
        QCOMPARE(r.emotes.size(), 2000);
        QCOMPARE(r.emotes.first().name, QStringLiteral("emote0"));
        QCOMPARE(r.emotes.last().name, QStringLiteral("emote1999"));
        QCOMPARE(r.warning, QStringLiteral("Globale Liste von BTTV: mehr als 2000 Emotes, 5 ausgelassen"));
    }

    void limitBytes()
    {
        QByteArray body = bttvArray(1);
        body.append(QByteArray(kEmoteListMaxBytes - body.size(), ' '));
        QCOMPARE(body.size(), 8 * 1024 * 1024);
        EmoteListResult r = parseEmoteList(EmoteProvider::Bttv, false, 200, body);
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.emotes.size(), 1);

        body.append(' ');
        r = parseEmoteList(EmoteProvider::Bttv, false, 200, body);
        QCOMPARE(r.error, QStringLiteral("Globale Liste von BTTV: grösser als 8 MiB, nicht gelesen"));
        QVERIFY(r.emotes.isEmpty());
    }

    void listUrls()
    {
        const QString id = QStringLiteral("100000001");
        QCOMPARE(emoteListUrl(EmoteProvider::Bttv, std::nullopt),
                 QUrl(QStringLiteral("https://api.betterttv.net/3/cached/emotes/global")));
        QCOMPARE(emoteListUrl(EmoteProvider::Bttv, id),
                 QUrl(QStringLiteral("https://api.betterttv.net/3/cached/users/twitch/100000001")));
        QCOMPARE(emoteListUrl(EmoteProvider::SevenTv, std::nullopt), QUrl(QStringLiteral("https://7tv.io/v3/emote-sets/global")));
        QCOMPARE(emoteListUrl(EmoteProvider::SevenTv, id), QUrl(QStringLiteral("https://7tv.io/v3/users/twitch/100000001")));
        QCOMPARE(emoteListUrl(EmoteProvider::Ffz, std::nullopt), QUrl(QStringLiteral("https://api.frankerfacez.com/v1/set/global")));
        QCOMPARE(emoteListUrl(EmoteProvider::Ffz, id), QUrl(QStringLiteral("https://api.frankerfacez.com/v1/room/id/100000001")));
        QVERIFY(emoteListUrl(EmoteProvider::Twitch, std::nullopt).isEmpty());
        QVERIFY(emoteListUrl(EmoteProvider::Twitch, id).isEmpty());
        // Kanal-ID nur aus Ziffern
        QVERIFY(emoteListUrl(EmoteProvider::Bttv, QString()).isEmpty());
        QVERIFY(emoteListUrl(EmoteProvider::Bttv, QStringLiteral("../emotes/global")).isEmpty());
        QVERIFY(emoteListUrl(EmoteProvider::Ffz, QStringLiteral("123?x=1")).isEmpty());
    }

    void imageUrls_data()
    {
        QTest::addColumn<EmoteProvider>("provider");
        QTest::addColumn<QString>("id");
        QTest::addColumn<bool>("animated");
        QTest::addColumn<bool>("animate");
        QTest::addColumn<bool>("webp");
        QTest::addColumn<QString>("url");

        const QString tw = QStringLiteral("emotesv2_0123abcd");
        QTest::newRow("Twitch statisch") << EmoteProvider::Twitch << tw << false << true << true
                                         << QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/emotesv2_0123abcd/static/dark/1.0");
        QTest::newRow("Twitch animiert") << EmoteProvider::Twitch << tw << true << true << false
                                         << QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/emotesv2_0123abcd/animated/dark/1.0");
        QTest::newRow("Twitch animiert, Animation aus") << EmoteProvider::Twitch << tw << true << false << true
                                         << QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/emotesv2_0123abcd/static/dark/1.0");

        const QString bt = QStringLiteral("566ca38765dbbdab32ec0560");
        QTest::newRow("BTTV animiert") << EmoteProvider::Bttv << bt << true << true << true
                                       << QStringLiteral("https://cdn.betterttv.net/emote/566ca38765dbbdab32ec0560/1x.webp");
        QTest::newRow("BTTV animiert, Animation aus") << EmoteProvider::Bttv << bt << true << false << true
                                       << QStringLiteral("https://cdn.betterttv.net/emote/566ca38765dbbdab32ec0560/static/1x.webp");
        QTest::newRow("BTTV statisch") << EmoteProvider::Bttv << bt << false << true << true
                                       << QStringLiteral("https://cdn.betterttv.net/emote/566ca38765dbbdab32ec0560/1x.webp");
        QTest::newRow("BTTV ohne WebP") << EmoteProvider::Bttv << bt << false << false << false << QString();

        const QString st = QStringLiteral("01FCY771D800007PQ2DF3GDTN6");
        QTest::newRow("7TV animiert") << EmoteProvider::SevenTv << st << true << true << true
                                      << QStringLiteral("https://cdn.7tv.app/emote/01FCY771D800007PQ2DF3GDTN6/1x.webp");
        QTest::newRow("7TV animiert, Animation aus") << EmoteProvider::SevenTv << st << true << false << true
                                      << QStringLiteral("https://cdn.7tv.app/emote/01FCY771D800007PQ2DF3GDTN6/1x_static.webp");
        QTest::newRow("7TV statisch") << EmoteProvider::SevenTv << st << false << true << true
                                      << QStringLiteral("https://cdn.7tv.app/emote/01FCY771D800007PQ2DF3GDTN6/1x.webp");
        QTest::newRow("7TV ohne WebP") << EmoteProvider::SevenTv << st << true << true << false << QString();

        QTest::newRow("FFZ statisch") << EmoteProvider::Ffz << QStringLiteral("6") << false << true << true
                                      << QStringLiteral("https://cdn.frankerfacez.com/emote/6/1");
        QTest::newRow("FFZ animiert") << EmoteProvider::Ffz << QStringLiteral("900102") << true << true << true
                                      << QStringLiteral("https://cdn.frankerfacez.com/emote/900102/animated/1");
        QTest::newRow("FFZ animiert, Animation aus") << EmoteProvider::Ffz << QStringLiteral("900102") << true << false << true
                                      << QStringLiteral("https://cdn.frankerfacez.com/emote/900102/1");
        QTest::newRow("FFZ animiert ohne WebP") << EmoteProvider::Ffz << QStringLiteral("900102") << true << true << false
                                      << QStringLiteral("https://cdn.frankerfacez.com/emote/900102/1");

        QTest::newRow("ID mit Pfad") << EmoteProvider::Twitch << QStringLiteral("../x") << false << true << true << QString();
        QTest::newRow("ID leer") << EmoteProvider::Ffz << QString() << false << true << true << QString();
    }

    void imageUrls()
    {
        QFETCH(EmoteProvider, provider);
        QFETCH(QString, id);
        QFETCH(bool, animated);
        QFETCH(bool, animate);
        QFETCH(bool, webp);
        QFETCH(QString, url);
        Emote e;
        e.provider = provider;
        e.id = id;
        e.name = QStringLiteral("Test");
        e.animated = animated;
        QCOMPARE(emoteImageUrl(e, animate, webp), url.isEmpty() ? QUrl() : QUrl(url));
    }

    void imageUrlsFromLists()
    {
        // Jedes gelesene Emote hat mit WebP eine Adresse beim eigenen CDN
        const struct { EmoteProvider provider; const char *file; const char *host; } lists[] = {
            {EmoteProvider::Bttv, "bttv-global.json", "cdn.betterttv.net"},
            {EmoteProvider::SevenTv, "7tv-global.json", "cdn.7tv.app"},
            {EmoteProvider::Ffz, "ffz-global.json", "cdn.frankerfacez.com"},
        };
        for (const auto &list : lists) {
            const EmoteListResult r = parseEmoteList(list.provider, false, 200, testFile(QString::fromLatin1(list.file)));
            QVERIFY(!r.emotes.isEmpty());
            for (const Emote &e : r.emotes) {
                const QUrl url = emoteImageUrl(e, true, true);
                QCOMPARE(url.scheme(), QStringLiteral("https"));
                QCOMPARE(url.host(), QString::fromLatin1(list.host));
            }
        }
        QCOMPARE(emoteProviderName(EmoteProvider::Twitch), QStringLiteral("Twitch"));
        QCOMPARE(emoteProviderName(EmoteProvider::Bttv), QStringLiteral("BTTV"));
        QCOMPARE(emoteProviderName(EmoteProvider::SevenTv), QStringLiteral("7TV"));
        QCOMPARE(emoteProviderName(EmoteProvider::Ffz), QStringLiteral("FFZ"));
    }

    // --- em-3: Erkennung, Abschnitte, Statustexte ---

    void indexPriority()
    {
        // Derselbe Name in allen sechs Listen; Vorrang FFZ-Kanal, BTTV-Kanal, 7TV-Kanal, FFZ-global,
        // BTTV-global, 7TV-global (Recherche 5). Jede Runde entfernt die Liste, die gewonnen hat.
        struct List
        {
            EmoteProvider provider;
            bool channel;
        };
        const QList<List> order{{EmoteProvider::Ffz, true},      {EmoteProvider::Bttv, true},
                                {EmoteProvider::SevenTv, true},  {EmoteProvider::Ffz, false},
                                {EmoteProvider::Bttv, false},    {EmoteProvider::SevenTv, false}};
        EmoteIndex index;
        // In umgekehrter Reihenfolge setzen: die Reihenfolge des Setzens zählt nicht
        for (auto it = order.crbegin(); it != order.crend(); ++it)
            index.setList(it->provider, it->channel, {named(it->provider, it->channel, "Dupe")});
        for (const List &list : order) {
            const Emote *found = index.find(QStringLiteral("Dupe"));
            QVERIFY(found);
            QCOMPARE(found->id, idOf(list.provider, list.channel));
            index.setList(list.provider, list.channel, {});
        }
        QVERIFY(!index.find(QStringLiteral("Dupe")));
        QVERIFY(index.isEmpty());
        // Twitch hat keine Liste
        index.setList(EmoteProvider::Twitch, false, {named(EmoteProvider::Twitch, false, "Kappa")});
        QVERIFY(index.isEmpty());
        // Innerhalb einer Liste gilt das erste
        Emote second = named(EmoteProvider::Bttv, false, "Dupe");
        second.id = QStringLiteral("zweites");
        index.setList(EmoteProvider::Bttv, false, {named(EmoteProvider::Bttv, false, "Dupe"), second});
        QCOMPARE(index.find(QStringLiteral("Dupe"))->id, idOf(EmoteProvider::Bttv, false));
        QCOMPARE(index.count(EmoteProvider::Bttv), 2);
    }

    void indexFromTestLists()
    {
        const EmoteListResult bttv = parseEmoteList(EmoteProvider::Bttv, false, 200, testFile("bttv-global.json"));
        const EmoteListResult ffz = parseEmoteList(EmoteProvider::Ffz, false, 200, testFile("ffz-global.json"));
        QVERIFY(!bttv.emotes.isEmpty());
        EmoteIndex index;
        index.setList(EmoteProvider::Bttv, false, bttv.emotes);
        index.setList(EmoteProvider::Ffz, false, ffz.emotes);
        QCOMPARE(index.count(EmoteProvider::Bttv), int(bttv.emotes.size()));
        QCOMPARE(index.count(EmoteProvider::Ffz), int(ffz.emotes.size()));
        QCOMPARE(index.count(EmoteProvider::SevenTv), 0);
        const Emote &first = bttv.emotes.first();
        QVERIFY(index.find(first.name));
    }

    void segmentsWords()
    {
        EmoteIndex index;
        index.setList(EmoteProvider::Bttv, false, {named(EmoteProvider::Bttv, false, "catJAM")});
        const auto text = [](const QString &t) { return QList<ChatFragment>{{"text", t, {}, {}, {}, {}}}; };
        const ChatSegment cat{"catJAM", named(EmoteProvider::Bttv, false, "catJAM")};

        // Gross- und Kleinschreibung zählt, Satzzeichen bleiben am Wort (dann Text)
        QCOMPARE(chatSegments(text("catjam CATJAM catJAM! (catJAM)"), index),
                 (QList<ChatSegment>{{"catjam CATJAM catJAM! (catJAM)", std::nullopt}}));
        // Mehrere Leerzeichen bleiben erhalten, Text wird zusammengefasst
        QCOMPARE(chatSegments(text("  a  catJAM   b "), index),
                 (QList<ChatSegment>{{"  a  ", std::nullopt}, cat, {"   b ", std::nullopt}}));
        QCOMPARE(chatSegments(text("catJAM catJAM"), index), (QList<ChatSegment>{cat, {" ", std::nullopt}, cat}));
        QVERIFY(chatSegments(text(QString()), index).isEmpty());
        QVERIFY(chatSegments({}, index).isEmpty());
        // Leerer Index: alles Text
        QCOMPARE(chatSegments(text("catJAM"), EmoteIndex()), (QList<ChatSegment>{{"catJAM", std::nullopt}}));
    }

    void segmentsFragmentTypes()
    {
        EmoteIndex index;
        index.setList(EmoteProvider::Bttv, false, {named(EmoteProvider::Bttv, false, "Kappa")});
        const QList<ChatFragment> fragments{
            {"mention", "@Kappa", {}, {}, {}, {}},
            {"text", " ", {}, {}, {}, {}},
            {"cheermote", "Kappa100", {}, {}, {}, {}},
            {"text", " ", {}, {}, {}, {}},
            {"emote", "Kappa", "25", {"static"}, {}, {}},
            {"emote", "testka1Tanz", "emotesv2_1", {"static", "animated"}, {}, {}},
            {"gif", "[GIF]", {}, {}, "g1", "https://media.example/g1.gif"},
            // GIF ohne Adresse: Text
            {"gif", "[ohne]", {}, {}, "g2", {}},
            // Fragment emote ohne Kennung: Text, ohne Suche im Index
            {"emote", "Kappa", {}, {}, {}, {}},
        };
        const QList<ChatSegment> s = chatSegments(fragments, index);
        QCOMPARE(s.size(), 4);
        QCOMPARE(s.at(0), (ChatSegment{"@Kappa Kappa100 ", std::nullopt}));
        // Twitch-Emote aus dem Fragment hat Vorrang vor dem gleichnamigen BTTV-Emote
        QCOMPARE(s.at(1), (ChatSegment{"Kappa", Emote{EmoteProvider::Twitch, "25", "Kappa"}}));
        QCOMPARE(s.at(2), (ChatSegment{"testka1Tanz", Emote{EmoteProvider::Twitch, "emotesv2_1", "testka1Tanz", true}}));
        // em-9: GIF mit Adresse steht in der eigenen Zeile, nicht im Text
        QCOMPARE(s.at(3), (ChatSegment{"[ohne]Kappa", std::nullopt}));
        QCOMPARE(chatGifs(fragments), (QList<ChatGif>{{"https://media.example/g1.gif", "[GIF]"}}));
    }

    // P14 Emotes em-9: GIFs in der Reihenfolge der Fragmente, Adresse unverändert (auch Query)
    void gifsFromFragments()
    {
        const QString url = QStringLiteral("https://media4.giphy.com/media/x/giphy.gif?cid=a%2Fb&rid=giphy.gif&ct=g");
        const QList<ChatFragment> fragments = chatFragments(QJsonArray{
            QJsonObject{{"type", "text"}, {"text", "vorher "}},
            QJsonObject{{"type", "gif"}, {"text", "[eins]"}, {"gif_id", "g1"}, {"gif_url", url}},
            QJsonObject{{"type", "gif"}, {"text", "[zwei]"}, {"gif_id", "g2"}, {"gif_url", "https://i.giphy.com/g2.gif"}},
            QJsonObject{{"type", "gif"}, {"text", "[null]"}, {"gif_id", QJsonValue::Null}, {"gif_url", QJsonValue::Null}},
        });
        QCOMPARE(chatGifs(fragments), (QList<ChatGif>{{url, "[eins]"}, {"https://i.giphy.com/g2.gif", "[zwei]"}}));
        QVERIFY(!fragments.at(3).isGif());
        QCOMPARE(chatSegments(fragments, EmoteIndex()), (QList<ChatSegment>{{"vorher [null]", std::nullopt}}));
        QVERIFY(chatGifs({}).isEmpty());
    }

    void segmentsZeroWidth()
    {
        EmoteIndex index;
        Emote snowy = named(EmoteProvider::Bttv, false, "SoSnowy");
        snowy.zeroWidth = true;
        index.setList(EmoteProvider::Bttv, false, {named(EmoteProvider::Bttv, false, "catJAM"), snowy});
        const auto zeroWidth = [&](const QString &t) {
            QList<bool> out;
            for (const ChatSegment &s : chatSegments({{"text", t, {}, {}, {}, {}}}, index)) {
                if (s.emote)
                    out.append(s.emote->zeroWidth);
            }
            return out;
        };
        // Vorne und hinter Text: normales Emote
        QCOMPARE(zeroWidth("SoSnowy catJAM"), (QList<bool>{false, false}));
        QCOMPARE(zeroWidth("hallo SoSnowy"), QList<bool>{false});
        // Hinter einem Emote (auch über Leerzeichen und hinter einem anderen Zero-Width)
        QCOMPARE(zeroWidth("catJAM SoSnowy"), (QList<bool>{false, true}));
        QCOMPARE(zeroWidth("catJAM   SoSnowy SoSnowy"), (QList<bool>{false, true, true}));
        // Hinter einem Twitch-Emote aus dem Fragment
        const QList<ChatSegment> s =
            chatSegments({{"emote", "Kappa", "25", {}, {}, {}}, {"text", " SoSnowy", {}, {}, {}, {}}}, index);
        QVERIFY(s.last().emote->zeroWidth);
    }

    void statusTexts_data()
    {
        QTest::addColumn<EmoteLoadState>("state");
        QTest::addColumn<QString>("text");
        using Network = EmoteLoadState::Network;
        const QList<QPair<EmoteProvider, int>> all{
            {EmoteProvider::Bttv, 74}, {EmoteProvider::SevenTv, 312}, {EmoteProvider::Ffz, 15}};

        QTest::newRow("geladen") << EmoteLoadState{Network::Online, all, {}, true, true}
                                 << QStringLiteral("Emotes geladen: BTTV 74, 7TV 312, FFZ 15");
        QTest::newRow("7TV nicht erreichbar")
            << EmoteLoadState{Network::Online, {{EmoteProvider::Bttv, 74}, {EmoteProvider::Ffz, 15}}, {EmoteProvider::SevenTv}, true, true}
            << QStringLiteral("Emotes geladen: BTTV 74, FFZ 15 · 7TV nicht erreichbar, die Wörter bleiben Text");
        QTest::newRow("keiner erreichbar")
            << EmoteLoadState{Network::Online, {}, {EmoteProvider::Bttv, EmoteProvider::SevenTv, EmoteProvider::Ffz}, true, true}
            << QStringLiteral("Emotes: BTTV, 7TV und FFZ nicht erreichbar, die Wörter bleiben Text");
        QTest::newRow("zwei nicht erreichbar")
            << EmoteLoadState{Network::Online, {{EmoteProvider::Ffz, 0}}, {EmoteProvider::Bttv, EmoteProvider::SevenTv}, true, true}
            << QStringLiteral("Emotes geladen: FFZ 0 · BTTV und 7TV nicht erreichbar, die Wörter bleiben Text");
        QTest::newRow("ohne Netz, gespeichert") << EmoteLoadState{Network::OfflineCached, all, {}, true, true}
                                                << QStringLiteral("Emotes: Ohne Netz, Listen vom letzten Mal");
        QTest::newRow("ohne Netz, nichts gespeichert") << EmoteLoadState{Network::OfflineEmpty, {}, {}, true, true}
                                                       << QStringLiteral("Emotes: Ohne Netz und ohne gespeicherte Listen");
        QTest::newRow("ohne WebP")
            << EmoteLoadState{Network::Online, {{EmoteProvider::Ffz, 15}}, {}, false, true}
            << QStringLiteral("Emotes geladen: FFZ 15 · WebP fehlt (Paket qt6-image-formats-plugins), BTTV und 7TV bleiben Text");
        QTest::newRow("kein Kanal") << EmoteLoadState{Network::Online, all, {}, true, false}
                                    << QStringLiteral("Emotes geladen: BTTV 74, 7TV 312, FFZ 15 · kein Kanal angemeldet");
        // «Emotes» höchstens einmal, auch wenn der erste Teil nicht «geladen» ist
        QTest::newRow("ohne Netz, kein Kanal") << EmoteLoadState{Network::OfflineEmpty, {}, {}, true, false}
                                               << QStringLiteral("Emotes: Ohne Netz und ohne gespeicherte Listen · kein Kanal angemeldet");
        QTest::newRow("FFZ nicht erreichbar, ohne WebP, kein Kanal")
            << EmoteLoadState{Network::Online, {}, {EmoteProvider::Ffz}, false, false}
            << QStringLiteral("Emotes: FFZ nicht erreichbar, die Wörter bleiben Text · WebP fehlt "
                              "(Paket qt6-image-formats-plugins), BTTV und 7TV bleiben Text · kein Kanal angemeldet");
        QTest::newRow("nichts") << EmoteLoadState{Network::Online, {}, {}, true, true} << QString();
    }

    void statusTexts()
    {
        QFETCH(EmoteLoadState, state);
        QFETCH(QString, text);
        QCOMPARE(emoteStatusText(state), text);
    }

    void toolTips()
    {
        QCOMPARE(emoteToolTip(named(EmoteProvider::Twitch, false, "Kappa")), QStringLiteral("Kappa (Twitch)"));
        QCOMPARE(emoteToolTip(named(EmoteProvider::Bttv, false, "catJAM")), QStringLiteral("catJAM (BTTV)"));
        QCOMPARE(emoteToolTip(named(EmoteProvider::SevenTv, false, "peepoHey")), QStringLiteral("peepoHey (7TV)"));
        QCOMPARE(emoteToolTip(named(EmoteProvider::Ffz, false, "LilZ")), QStringLiteral("LilZ (FFZ)"));
    }

    void fragmentsFromJson()
    {
        const QJsonArray array{
            QJsonObject{{"type", "text"}, {"text", "Hallo "}, {"emote_id", QJsonValue::Null},
                        {"emote_format", QJsonValue::Null}, {"gif_id", QJsonValue::Null}, {"gif_url", QJsonValue::Null}},
            QJsonObject{{"type", "emote"}, {"text", "Kappa"}, {"emote_id", "25"},
                        {"emote_format", QJsonArray{"static", "animated"}}},
            QJsonObject{{"type", "gif"}, {"text", "[GIF]"}, {"gif_id", "g1"}, {"gif_url", "https://media.example/g1.gif"}},
            QStringLiteral("kein Objekt"),
        };
        const QList<ChatFragment> f = chatFragments(array);
        QCOMPARE(f.size(), 3);
        QCOMPARE(f.at(0), (ChatFragment{"text", "Hallo ", {}, {}, {}, {}}));
        QCOMPARE(f.at(1), (ChatFragment{"emote", "Kappa", "25", {"static", "animated"}, {}, {}}));
        QCOMPARE(f.at(2), (ChatFragment{"gif", "[GIF]", {}, {}, "g1", "https://media.example/g1.gif"}));
        QVERIFY(chatFragments(QJsonValue()).isEmpty());
    }

private:
    static QString idOf(EmoteProvider provider, bool channel)
    {
        return emoteProviderName(provider) + (channel ? QStringLiteral("-kanal") : QStringLiteral("-global"));
    }

    static Emote named(EmoteProvider provider, bool channel, const QString &name)
    {
        return Emote{provider, idOf(provider, channel), name};
    }
};

QTEST_GUILESS_MAIN(TestEmotes)
#include "tst_emotes.moc"
