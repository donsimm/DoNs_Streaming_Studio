// Prüft Speichern und Laden im Datenordner, Kopien unter media/ und die Token-Datei.
// Jeder Test hat einen eigenen temporären Datenordner. Mediendateien: dieselben wie im
// Handtest (prototypes/media, erzeugt mit prototypes/messung/media_erzeugen.sh) und die
// Schriftdatei DSS_TEST_FONT (CLAUDE.md Regel 7).

#include "overlayjson.h"
#include "overlaystore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace dss;

namespace {

const QString kMediaDir = QStringLiteral(DSS_TEST_MEDIA_DIR);
const QString kImage = kMediaDir + QStringLiteral("/bild1.png");
const QString kImage2 = kMediaDir + QStringLiteral("/bild2.png");
const QString kVideo = kMediaDir + QStringLiteral("/video.webm");
const QString kAlphaVideo = kMediaDir + QStringLiteral("/alpha.webm");
const QString kFont = QStringLiteral(DSS_TEST_FONT);
const QString kFormatDoc = QStringLiteral(DSS_FORMAT_DOC);

constexpr QFileDevice::Permissions kUnixBits =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
    | QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
    | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;

QByteArray readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

QStringList mediaFiles(const QString &dataDir)
{
    return QDir(dataDir + QStringLiteral("/media")).entryList(QDir::Files);
}

QJsonObject readTokens(const QString &dataDir)
{
    return QJsonDocument::fromJson(readFile(dataDir + QStringLiteral("/tokens.json")))
        .object().value(QStringLiteral("tokens")).toObject();
}

} // namespace

class TestOverlayStore : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_temp; // je Test neu angelegt in init()

    QString dataDir() const { return m_temp->path(); }

    // Lädt den Datenordner frisch, wie beim Programmstart.
    std::unique_ptr<OverlayStore> openStore()
    {
        auto store = std::make_unique<OverlayStore>(dataDir());
        QString error;
        if (!store->load(&error))
            qWarning("load: %s", qPrintable(error));
        return store;
    }

    QUuid create(OverlayStore *store, const QString &name, int w = 1920, int h = 1080)
    {
        QString error;
        const QUuid id = store->createOverlay(name, w, h, &error);
        if (id.isNull())
            qWarning("create: %s", qPrintable(error));
        return id;
    }

private slots:
    void initTestCase()
    {
        for (const QString &f : {kImage, kImage2, kVideo, kAlphaVideo, kFont})
            QVERIFY2(QFile::exists(f), qPrintable(f + QStringLiteral(" fehlt; prototypes/messung/media_erzeugen.sh ausführen")));
    }

    void init()
    {
        m_temp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temp->isValid());
    }

    void cleanup() { m_temp.reset(); }

    void defaultDataDir()
    {
        // Linux: AppDataLocation = $XDG_DATA_HOME/<APPNAME> (Standard ~/.local/share/<APPNAME>)
        // https://doc.qt.io/qt-6.8/qstandardpaths.html
        const QByteArray previous = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", dataDir().toUtf8());
        const QString dir = dss::defaultDataDir();
        const QString installed = dss::defaultDataDir(dss::Edition::Installed);
        if (previous.isEmpty())
            qunsetenv("XDG_DATA_HOME");
        else
            qputenv("XDG_DATA_HOME", previous);
        // P10a: in Arbeit der Entwicklungsordner, im Einsatz die echten Daten wie bis P09b
        QCOMPARE(dir, dataDir() + QStringLiteral("/dons-streaming-studio-entwicklung"));
        QCOMPARE(installed, dataDir() + QStringLiteral("/dons-streaming-studio"));
        // Der Programmname wird nur für die Abfrage gesetzt.
        QCOMPARE(QCoreApplication::applicationName(), QStringLiteral("tst_overlaystore"));
    }

    void saveAndLoadGiveSameOverlay()
    {
        QString error;
        auto store = openStore();
        const QUuid id = create(store.get(), QStringLiteral("Überblick Start"));
        QVERIFY(!id.isNull());
        const QUuid image = store->addImageSource(id, kImage, &error);
        QVERIFY2(!image.isNull(), qPrintable(error));
        const QUuid video = store->addVideoSource(id, kVideo, false, &error);
        QVERIFY2(!video.isNull(), qPrintable(error));
        const QUuid alpha = store->addVideoSource(id, kAlphaVideo, true, &error);
        QVERIFY2(!alpha.isNull(), qPrintable(error));
        const QUuid text = store->addTextSource(id, QStringLiteral("Willkommen\nZweite Zeile"), kFont, &error);
        QVERIFY2(!text.isNull(), qPrintable(error));
        QVERIFY(store->setSourceGeometry(id, image, -100, 200, 640, 360, 125, &error));
        QVERIFY(store->setSourceVisible(id, video, false, &error));
        QVERIFY(store->moveSourceForward(id, image, &error));
        QVERIFY(store->moveSourceBackward(id, text, &error));
        const QUuid second = create(store.get(), QStringLiteral("Pause"), 1280, 720);

        const Overlay saved = *store->overlay(id);
        QCOMPARE(saved.sources.size(), 4);
        QCOMPARE(saved.sources[0].id, video);
        QCOMPARE(saved.sources[1].id, image);
        QCOMPARE(saved.sources[2].id, text);
        QCOMPARE(saved.sources[3].id, alpha);
        QCOMPARE(saved.sources[1].name, QStringLiteral("bild1.png")); // ursprünglicher Dateiname
        QCOMPARE(saved.sources[2].name, QStringLiteral("Text: Willkommen"));
        QCOMPARE(saved.sources[2].text.content, QStringLiteral("Willkommen\nZweite Zeile"));
        QCOMPARE(saved.sources[0].loop, false);
        QCOMPARE(saved.sources[3].loop, true);

        auto reloaded = openStore();
        QCOMPARE(reloaded->loadErrors(), QStringList());
        QCOMPARE(reloaded->loadWarnings(), QStringList());
        QCOMPARE(reloaded->overlays().size(), 2);
        QCOMPARE(*reloaded->overlay(id), saved);
        QCOMPARE(*reloaded->overlay(second), *store->overlay(second));
        QCOMPARE(reloaded->token(id), store->token(id));

        // Datei heisst overlays/<Kennung>.json und enthält format_version 3 (seit P09b)
        const QJsonObject json = QJsonDocument::fromJson(readFile(store->overlayFilePath(id))).object();
        QVERIFY(store->overlayFilePath(id).endsWith(
            QStringLiteral("/overlays/") + id.toString(QUuid::WithoutBraces) + QStringLiteral(".json")));
        QCOMPARE(json.value(QStringLiteral("format_version")).toInt(), 3);
    }

    void addressFromNameTwice()
    {
        auto store = openStore();
        const QUuid a = create(store.get(), QStringLiteral("Überblick Start"));
        const QUuid b = create(store.get(), QStringLiteral("Überblick Start"));
        const QUuid c = create(store.get(), QStringLiteral("überblick start!"));
        QCOMPARE(store->overlay(a)->address, QStringLiteral("ueberblick-start"));
        QCOMPARE(store->overlay(b)->address, QStringLiteral("ueberblick-start-2"));
        QCOMPARE(store->overlay(c)->address, QStringLiteral("ueberblick-start-3"));
        QVERIFY(store->overlayByAddress(QStringLiteral("ueberblick-start-2")));
    }

    void renameWithoutAddress()
    {
        QString error;
        auto store = openStore();
        const QUuid id = create(store.get(), QStringLiteral("Überblick Start"));
        QVERIFY(store->renameOverlay(id, QStringLiteral("Pause"), false, &error));
        QCOMPARE(store->overlay(id)->name, QStringLiteral("Pause"));
        QCOMPARE(store->overlay(id)->address, QStringLiteral("ueberblick-start"));
        auto reloaded = openStore();
        QCOMPARE(reloaded->overlay(id)->name, QStringLiteral("Pause"));
        QCOMPARE(reloaded->overlay(id)->address, QStringLiteral("ueberblick-start"));
        QVERIFY(!store->renameOverlay(id, QStringLiteral("  "), false, &error));
        QVERIFY(error.contains(QStringLiteral("nicht leer")));
    }

    void renameWithAddress()
    {
        QString error;
        auto store = openStore();
        const QUuid id = create(store.get(), QStringLiteral("Überblick Start"));
        const QUuid other = create(store.get(), QStringLiteral("Pause"));
        QVERIFY(store->renameOverlay(id, QStringLiteral("Pause"), true, &error));
        QCOMPARE(store->overlay(id)->address, QStringLiteral("pause-2"));
        QCOMPARE(store->overlay(other)->address, QStringLiteral("pause"));
        // Gleicher Name nochmals mit neuer Adresse: die eigene Adresse zählt nicht als vergeben.
        QVERIFY(store->renameOverlay(id, QStringLiteral("Pause"), true, &error));
        QCOMPARE(store->overlay(id)->address, QStringLiteral("pause-2"));
        QVERIFY(store->renameOverlay(id, QStringLiteral("Ende"), true, &error));
        QCOMPARE(store->overlay(id)->address, QStringLiteral("ende"));
        auto reloaded = openStore();
        QCOMPARE(reloaded->overlay(id)->address, QStringLiteral("ende"));
        QCOMPARE(reloaded->overlay(id)->name, QStringLiteral("Ende"));
    }

    void copyOverlay()
    {
        QString error;
        auto store = openStore();
        const QUuid id = create(store.get(), QStringLiteral("Überblick Start"));
        QVERIFY(!store->addImageSource(id, kImage, &error).isNull());
        QVERIFY(!store->addTextSource(id, QStringLiteral("Hallo"), kFont, &error).isNull());
        const QUuid copyId = store->copyOverlay(id, &error);
        QVERIFY2(!copyId.isNull(), qPrintable(error));

        const Overlay original = *store->overlay(id);
        const Overlay copy = *store->overlay(copyId);
        QVERIFY(copy.id != original.id);
        QCOMPARE(copy.name, QStringLiteral("Überblick Start Kopie"));
        QCOMPARE(copy.address, QStringLiteral("ueberblick-start-kopie"));
        QCOMPARE(copy.sources.size(), original.sources.size());
        for (qsizetype i = 0; i < copy.sources.size(); ++i) {
            QVERIFY(copy.sources[i].id != original.sources[i].id);
            Source same = copy.sources[i];
            same.id = original.sources[i].id;
            QCOMPARE(same, original.sources[i]); // sonst gleich, auch derselbe Dateiverweis
        }
        QVERIFY(!store->token(copyId).isEmpty());
        QVERIFY(store->token(copyId) != store->token(id));
        QVERIFY(QFile::exists(store->overlayFilePath(copyId)));
        QCOMPARE(mediaFiles(dataDir()).size(), 2); // keine neuen Kopien der Dateien

        // Zweite Kopie: eigene Adresse
        const QUuid copy2 = store->copyOverlay(id, &error);
        QCOMPARE(store->overlay(copy2)->address, QStringLiteral("ueberblick-start-kopie-2"));
        auto reloaded = openStore();
        QCOMPARE(*reloaded->overlay(copyId), copy);
        QCOMPARE(reloaded->token(copyId), store->token(copyId));
    }

    void removeOverlay()
    {
        QString error;
        auto store = openStore();
        const QUuid id = create(store.get(), QStringLiteral("Überblick Start"));
        const QUuid keep = create(store.get(), QStringLiteral("Pause"));
        const QString file = store->overlayFilePath(id);
        QVERIFY(QFile::exists(file));
        QVERIFY(readTokens(dataDir()).contains(id.toString(QUuid::WithoutBraces)));

        QVERIFY2(store->removeOverlay(id, &error), qPrintable(error));
        QVERIFY(!QFile::exists(file));
        QVERIFY(!store->overlay(id));
        QVERIFY(store->token(id).isEmpty());
        QVERIFY(!readTokens(dataDir()).contains(id.toString(QUuid::WithoutBraces)));
        QVERIFY(readTokens(dataDir()).contains(keep.toString(QUuid::WithoutBraces)));
        QVERIFY(!store->removeOverlay(id, &error));

        auto reloaded = openStore();
        QCOMPARE(reloaded->overlays().size(), 1);
        QCOMPARE(reloaded->overlays().first().id, keep);
    }

    void sameMediaTwiceGivesOneCopy()
    {
        QString error;
        auto store = openStore();
        const QUuid a = create(store.get(), QStringLiteral("A"));
        const QUuid b = create(store.get(), QStringLiteral("B"));
        const QUuid s1 = store->addImageSource(a, kImage, &error);
        const QUuid s2 = store->addImageSource(a, kImage, &error);
        // Dieselbe Datei unter anderem Namen
        const QString renamed = dataDir() + QStringLiteral("/anderer-name.PNG");
        QVERIFY(QFile::copy(kImage, renamed));
        const QUuid s3 = store->addImageSource(b, renamed, &error);
        QVERIFY(!s1.isNull() && !s2.isNull() && !s3.isNull());
        QCOMPARE(mediaFiles(dataDir()).size(), 1);

        const QString ref = store->overlay(a)->sources[0].file;
        QCOMPARE(store->overlay(a)->sources[1].file, ref);
        QCOMPARE(store->overlay(b)->sources[0].file, ref);
        QCOMPARE(store->overlay(b)->sources[0].name, QStringLiteral("anderer-name.PNG"));
        QCOMPARE(readFile(store->mediaFilePath(ref)), readFile(kImage));
        // Name der Kopie: SHA-256 des Inhalts
        QCOMPARE(ref, QStringLiteral("media/") + QString::fromLatin1(QCryptographicHash::hash(
                          readFile(kImage), QCryptographicHash::Sha256).toHex()) + QStringLiteral(".png"));

        QVERIFY(!store->addImageSource(a, kImage2, &error).isNull());
        QCOMPARE(mediaFiles(dataDir()).size(), 2);
    }

    void invalidValuesRejected()
    {
        QString error;
        auto store = openStore();
        QVERIFY(store->createOverlay(QString(), 1920, 1080, &error).isNull());
        QVERIFY(error.contains(QStringLiteral("nicht leer")));
        QVERIFY(store->createOverlay(QStringLiteral("X"), 0, 1080, &error).isNull());
        QVERIFY(store->createOverlay(QStringLiteral("X"), 1920, 4321, &error).isNull());
        QVERIFY(QDir(dataDir() + QStringLiteral("/overlays")).isEmpty());

        const QUuid id = create(store.get(), QStringLiteral("Test"));
        const QUuid image = store->addImageSource(id, kImage, &error);
        const QByteArray before = readFile(store->overlayFilePath(id));
        const Overlay overlayBefore = *store->overlay(id);
        QVERIFY(!store->setSourceGeometry(id, image, 0, 0, 100, 100, 3600, &error));
        QVERIFY(error.contains(QStringLiteral("Drehung 3600")));
        QVERIFY(!store->setSourceGeometry(id, image, 0, 0, 0, 100, 0, &error));
        QVERIFY(error.contains(QStringLiteral("Breite 0")));
        QCOMPARE(readFile(store->overlayFilePath(id)), before);
        QCOMPARE(*store->overlay(id), overlayBefore);

        // Falsche Art der Datei (Endung), fehlende Datei, unbekanntes Overlay
        QVERIFY(store->addImageSource(id, kFont, &error).isNull());
        QVERIFY(error.contains(QStringLiteral("Endung")));
        QVERIFY(store->addVideoSource(id, kImage, false, &error).isNull());
        QVERIFY(store->addTextSource(id, QStringLiteral("x"), kImage, &error).isNull());
        QVERIFY(store->addImageSource(id, dataDir() + QStringLiteral("/fehlt.png"), &error).isNull());
        QVERIFY(store->addImageSource(QUuid::createUuid(), kImage, &error).isNull());
        QVERIFY(error.contains(QStringLiteral("nicht gefunden")));
        QCOMPARE(*store->overlay(id), overlayBefore);
    }

    void brokenFileIsReportedAndKept()
    {
        auto store = openStore();
        const QUuid good = create(store.get(), QStringLiteral("Gut"));
        const QUuid brokenId = QUuid::createUuid();
        const QString brokenPath = store->overlayFilePath(brokenId);
        const QByteArray brokenContent = "{ \"format_version\": 1, \"name\": ";
        QVERIFY(writeFile(brokenPath, brokenContent));

        auto reloaded = openStore();
        QCOMPARE(reloaded->overlays().size(), 1);
        QCOMPARE(reloaded->overlays().first().id, good);
        QCOMPARE(reloaded->loadErrors().size(), 1);
        const QString message = reloaded->loadErrors().first();
        QVERIFY2(message.contains(QStringLiteral("overlays/") + brokenId.toString(QUuid::WithoutBraces)
                                  + QStringLiteral(".json")), qPrintable(message));

        // Weiterarbeiten überschreibt die kaputte Datei nicht.
        QString error;
        create(reloaded.get(), QStringLiteral("Neu"));
        QVERIFY(reloaded->renameOverlay(good, QStringLiteral("Gut 2"), true, &error));
        QCOMPARE(readFile(brokenPath), brokenContent);

        // Gültiges JSON mit falschem Dateinamen ist ebenfalls ein Fehler.
        const QString wrongName = dataDir() + QStringLiteral("/overlays/") +
                                  QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".json");
        QVERIFY(QFile::copy(reloaded->overlayFilePath(good), wrongName));
        auto third = openStore();
        QCOMPARE(third->loadErrors().size(), 2);
        QCOMPARE(third->overlays().size(), 2);
    }

    void higherFormatVersionRejected()
    {
        auto store = openStore();
        const QUuid id = create(store.get(), QStringLiteral("Zukunft"));
        const QUuid other = create(store.get(), QStringLiteral("Heute"));
        QJsonObject json = QJsonDocument::fromJson(readFile(store->overlayFilePath(id))).object();
        json.insert(QStringLiteral("format_version"), 4);
        const QByteArray content = QJsonDocument(json).toJson();
        QVERIFY(writeFile(store->overlayFilePath(id), content));

        auto reloaded = openStore();
        QVERIFY(!reloaded->overlay(id));
        QVERIFY(reloaded->overlay(other));
        QCOMPARE(reloaded->loadErrors().size(), 1);
        const QString message = reloaded->loadErrors().first();
        QVERIFY2(message.contains(id.toString(QUuid::WithoutBraces)) && message.contains(QStringLiteral("neuer")),
                 qPrintable(message));
        QCOMPARE(readFile(store->overlayFilePath(id)), content);
    }

    void missingMediaReportedPerSource()
    {
        QString error;
        auto store = openStore();
        const QUuid id = create(store.get(), QStringLiteral("Medien"));
        const QUuid image = store->addImageSource(id, kImage, &error);
        store->addImageSource(id, kImage, &error); // zweite Quelle, dieselbe Kopie
        store->addTextSource(id, QStringLiteral("Hallo"), kFont, &error);
        const QString ref = store->overlay(id)->sources[0].file;
        QVERIFY(QFile::remove(store->mediaFilePath(ref)));

        auto reloaded = openStore();
        QVERIFY(reloaded->overlay(id)); // trotzdem geladen
        QCOMPARE(reloaded->loadErrors(), QStringList());
        QCOMPARE(reloaded->loadWarnings().size(), 2); // je Quelle eine Meldung
        QVERIFY2(reloaded->loadWarnings().first().contains(image.toString(QUuid::WithoutBraces)),
                 qPrintable(reloaded->loadWarnings().first()));
        QVERIFY(reloaded->loadWarnings().first().contains(ref));
    }

    void tokenFile()
    {
        auto store = openStore();
        const QUuid a = create(store.get(), QStringLiteral("A"));
        const QUuid b = create(store.get(), QStringLiteral("B"));
        const QString tokensPath = dataDir() + QStringLiteral("/tokens.json");

        // Dateirechte 0600, unabhängig von der umask
        QCOMPARE(QFile::permissions(tokensPath) & kUnixBits,
                 QFileDevice::ReadOwner | QFileDevice::WriteOwner);

        // Mindestens 128 Bit, verschieden, nicht in der Overlay-Datei
        const QString tokenA = store->token(a);
        const QByteArray raw = QByteArray::fromBase64(tokenA.toLatin1(), QByteArray::Base64UrlEncoding);
        QCOMPARE(raw.size(), kTokenBytes);
        QVERIFY(raw.size() * 8 >= 128);
        QVERIFY(tokenA != store->token(b));
        QVERIFY(!readFile(store->overlayFilePath(a)).contains(tokenA.toLatin1()));
        QCOMPARE(readTokens(dataDir()).value(a.toString(QUuid::WithoutBraces)).toString(), tokenA);

        // Falsche Rechte werden beim Laden gemeldet und auf 0600 gesetzt.
        QVERIFY(QFile::setPermissions(tokensPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                      | QFileDevice::ReadGroup | QFileDevice::ReadOther));
        auto reloaded = openStore();
        QCOMPARE(reloaded->loadWarnings().size(), 1);
        QVERIFY(reloaded->loadWarnings().first().contains(QStringLiteral("0600")));
        QCOMPARE(QFile::permissions(tokensPath) & kUnixBits,
                 QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        QCOMPARE(reloaded->token(a), tokenA);

        // Nach erneutem Schreiben (neues Overlay) weiterhin 0600
        create(reloaded.get(), QStringLiteral("C"));
        QCOMPARE(QFile::permissions(tokensPath) & kUnixBits,
                 QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    }

    void tokenMissingOrBroken()
    {
        auto store = openStore();
        const QUuid a = create(store.get(), QStringLiteral("A"));
        const QString tokensPath = dataDir() + QStringLiteral("/tokens.json");

        // Ohne tokens.json: neuer Token mit Warnung
        QVERIFY(QFile::remove(tokensPath));
        auto reloaded = openStore();
        QCOMPARE(reloaded->loadWarnings().size(), 1);
        QVERIFY(reloaded->loadWarnings().first().contains(QStringLiteral("neuer Token")));
        QVERIFY(!reloaded->token(a).isEmpty());
        QVERIFY(reloaded->token(a) != store->token(a));

        // Kaputte tokens.json: Laden schlägt fehl, die Datei bleibt unverändert,
        // die Meldung enthält keinen Token.
        const QByteArray broken = "{\"format_version\":1,\"tokens\":{\"" +
                                  a.toString(QUuid::WithoutBraces).toLatin1() + "\":\"zu-kurz\"}}";
        QVERIFY(writeFile(tokensPath, broken));
        QFile::setPermissions(tokensPath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        OverlayStore third(dataDir());
        QString error;
        QVERIFY(!third.load(&error));
        QVERIFY2(error.contains(QStringLiteral("tokens.json")), qPrintable(error));
        QVERIFY(!error.contains(QStringLiteral("zu-kurz")));
        QCOMPARE(readFile(tokensPath), broken);
    }

    // Abnahme 4: Das Beispiel in docs/06-overlay-format.md lässt sich mit dem Modell laden.
    void formatDocExample()
    {
        const QString doc = QString::fromUtf8(readFile(kFormatDoc));
        const qsizetype begin = doc.indexOf(QStringLiteral("<!-- beispiel-anfang -->"));
        const qsizetype end = doc.indexOf(QStringLiteral("<!-- beispiel-ende -->"));
        QVERIFY2(begin >= 0 && end > begin, "Markierungen des Beispiels fehlen");
        QString block = doc.mid(begin, end - begin);
        block = block.section(QStringLiteral("```json"), 1).section(QStringLiteral("```"), 0, 0);
        QVERIFY(!block.trimmed().isEmpty());

        const QString id = QStringLiteral("db7f47a9-dee3-4de5-b056-40b5b3507607");
        QVERIFY(QDir().mkpath(dataDir() + QStringLiteral("/overlays")));
        QVERIFY(writeFile(dataDir() + QStringLiteral("/overlays/") + id + QStringLiteral(".json"),
                          block.toUtf8()));
        // Die Dateien unter media/ unter genau den Namen aus dem Beispiel anlegen (Punkt 33):
        // Der Inhalt kommt von den Testdateien, die Prüfsumme im Namen muss nicht dazu passen.
        // media_erzeugen.sh erzeugt alpha.webm nicht jedes Mal gleich (libvpx), deshalb darf
        // der Test nicht von der Prüfsumme abhängen. Ob ein Verweis die Form
        // media/<SHA-256>.<Endung> hat, prüft das Laden selbst.
        static const QRegularExpression reference(
            QStringLiteral("\"(media/[0-9a-f]{64}\\.([a-z0-9]+))\""));
        const QHash<QString, QString> testFileFor = {
            {QStringLiteral("png"), kImage},
            {QStringLiteral("webm"), kAlphaVideo},
            {QStringLiteral("ttf"), kFont},
        };
        QVERIFY(QDir().mkpath(dataDir() + QStringLiteral("/media")));
        int references = 0;
        for (const QRegularExpressionMatch &m : reference.globalMatch(block)) {
            const QString source = testFileFor.value(m.captured(2));
            QVERIFY2(!source.isEmpty(), qPrintable(m.captured(1)));
            QVERIFY2(QFile::copy(source, dataDir() + QLatin1Char('/') + m.captured(1)),
                     qPrintable(m.captured(1)));
            ++references;
        }
        QCOMPARE(references, 3);
        // Ohne Token in tokens.json bekommt das Overlay beim ersten Laden einen neuen (Warnung).
        {
            auto store = openStore();
            QCOMPARE(store->loadErrors(), QStringList());
            QVERIFY(store->loadWarnings().join(QLatin1Char('\n')).contains(QStringLiteral("neuer Token")));
        }
        auto store = openStore();
        QCOMPARE(store->loadErrors(), QStringList());
        const Overlay *o = store->overlay(QUuid::fromString(id));
        QVERIFY(o);
        QCOMPARE(o->name, QStringLiteral("Überblick Start"));
        QCOMPARE(o->address, QStringLiteral("ueberblick-start"));
        QCOMPARE(o->sources.size(), 3);
        QCOMPARE(o->sources[0].kind, SourceKind::Image);
        QCOMPARE(o->sources[0].rotation, 125);
        QCOMPARE(o->sources[1].kind, SourceKind::Video);
        QCOMPARE(o->sources[1].visible, false);
        QCOMPARE(o->sources[1].loop, false);
        QCOMPARE(o->sources[2].kind, SourceKind::Text);
        QCOMPARE(o->sources[2].text.content, QStringLiteral("Willkommen\nZweite Zeile"));
        QCOMPARE(o->sources[2].text.align, TextAlign::Center);
        // Keine Warnung: Die Dateien zu den Verweisen liegen unter media/, der Token ist da.
        QCOMPARE(store->loadWarnings(), QStringList());
        QVERIFY(!store->token(o->id).isEmpty());
        // Wieder geschrieben ergibt sich dasselbe Overlay.
        Overlay reparsed;
        QString error;
        QVERIFY(parseOverlay(serializeOverlay(*o), &reparsed, &error));
        QCOMPARE(reparsed, *o);
    }

    void generatedTokensDiffer()
    {
        QSet<QString> tokens;
        for (int i = 0; i < 1000; ++i)
            tokens.insert(TokenStore::generateToken());
        QCOMPARE(tokens.size(), 1000);
        for (const QString &t : std::as_const(tokens))
            QCOMPARE(t.size(), 43);
    }
};

QTEST_GUILESS_MAIN(TestOverlayStore)
#include "tst_overlaystore.moc"
