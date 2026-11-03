// Zusammenspiel Fenster–Kern beim Bearbeiten (P08, Tests): dss-core läuft als eigener Prozess mit
// eigenem Datenordner und eigener Verbindung (wie tst_kernverbindung). Zwei Verbindungen wie im
// Fenster (CoreClient) sind gleichzeitig verbunden; jede Art von Änderung wird einmal geschickt,
// beide müssen dasselbe Ergebnis bekommen. Danach wird der Kern beendet bzw. mit kill -9
// abgebrochen und neu gestartet; alles muss wie vorher sein. Nur Qt Core, Network und Test.
// Daten wie im Handtest (CLAUDE.md Regel 7): Bild und Video aus prototypes/media, Schriften vom
// Studio-PC (DejaVu Sans) und die mitgelieferte Be Vietnam Pro.

#include "coreclient.h"
#include "fontcatalog.h"
#include "overlaystore.h"
#include "prozesshilfe.h"
#include "version.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace dss;
using namespace testhelp;

namespace {

const QString kMediaDir = QStringLiteral(DSS_TEST_MEDIA_DIR);
const QString kImage = kMediaDir + QStringLiteral("/bild1.png");
const QString kImage2 = kMediaDir + QStringLiteral("/bild2.png");
const QString kVideo = kMediaDir + QStringLiteral("/alpha.webm");
const QString kFont = QStringLiteral(DSS_TEST_FONT); // DejaVu Sans
const QString kFontBold = QStringLiteral("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");

QString sha256Reference(const QString &path, const QString &suffix)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QStringLiteral("media/")
           + QString::fromLatin1(QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex())
           + QLatin1Char('.') + suffix;
}

// Eine einzelne Änderung an einer Quelle (ohne -Wmissing-field-initializers).
template <typename T>
SourceChanges only(std::optional<T> SourceChanges::*field, T value)
{
    SourceChanges c;
    c.*field = std::move(value);
    return c;
}

ChangeRequest request(const char *type)
{
    ChangeRequest r;
    r.type = QString::fromLatin1(type);
    return r;
}

} // namespace

class TestBearbeiten : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_data;
    std::unique_ptr<QTemporaryDir> m_run;
    QString m_socket;
    std::unique_ptr<QProcess> m_core;
    QString m_coreLog;
    std::unique_ptr<CoreClient> m_a; // Fenster, das ändert
    std::unique_ptr<CoreClient> m_b; // zweites Fenster, ändert nichts
    QUuid m_overview;
    QUuid m_chat;

    std::unique_ptr<CoreClient> client(const QString &program)
    {
        CoreClient::Options o;
        o.socketPath = m_socket;
        o.program = program;
        o.version = version();
        o.reconnectIntervalMs = 200;
        auto c = std::make_unique<CoreClient>(o);
        c->start();
        return c;
    }

    bool startCore()
    {
        m_core = testhelp::startCore(m_data.path(), m_socket);
        m_coreLog.clear();
        return waitForLog(m_core.get(), QStringLiteral("Verbindung offen"), &m_coreLog);
    }

    // Schickt eine Änderung über m_a und wartet auf die Antwort.
    ChangeResult change(ChangeRequest r)
    {
        QSignalSpy spy(m_a.get(), &CoreClient::changeFinished);
        const qint64 id = m_a->sendChange(r);
        if (id <= 0)
            return {};
        for (int i = 0; i < 50; ++i) {
            for (const QList<QVariant> &args : std::as_const(spy)) {
                const ChangeResult result = args.at(0).value<ChangeResult>();
                if (result.requestId == id)
                    return result;
            }
            spy.wait(100);
        }
        return {};
    }

    // Beide Fenster haben dasselbe Overlay (nach dem Verteilen an alle, K-02).
    bool sameInBoth(const QUuid &overlayId)
    {
        const auto same = [&] {
            return m_a->overlay(overlayId) && m_b->overlay(overlayId)
                   && *m_a->overlay(overlayId) == *m_b->overlay(overlayId);
        };
        for (int i = 0; i < 30 && !same(); ++i)
            QTest::qWait(100);
        return same();
    }

    Overlay current(const QUuid &overlayId)
    {
        const std::optional<Overlay> o = m_a->overlay(overlayId);
        return o ? *o : Overlay();
    }

    Source currentSource(const QUuid &overlayId, const QUuid &sourceId)
    {
        const Overlay o = current(overlayId);
        const int i = sourceIndex(o, sourceId);
        return i < 0 ? Source() : o.sources.at(i);
    }

    // Zustand, wie ihn ein neu verbundenes Fenster sieht: Liste und alle Overlays.
    struct Snapshot
    {
        OverlayListData list;
        QList<Overlay> overlays;
        bool operator==(const Snapshot &) const = default;
    };
    Snapshot snapshot(CoreClient *c)
    {
        Snapshot s;
        s.list = c->listData();
        for (const OverlaySummary &o : s.list.overlays) {
            QSignalSpy spy(c, &CoreClient::overlayChanged);
            c->requestOverlay(o.id);
            for (int i = 0; i < 20 && !spy.contains(QList<QVariant>{QVariant::fromValue(o.id)}); ++i)
                spy.wait(100);
            if (c->overlay(o.id))
                s.overlays.append(*c->overlay(o.id));
            else
                qWarning("Overlay %s nicht erhalten", qPrintable(o.id.toString()));
        }
        return s;
    }

    Overlay storedOverlay(const QUuid &id)
    {
        OverlayStore store(m_data.path());
        QString error;
        store.load(&error, OverlayStore::LoadMode::ReadOnly);
        return store.overlay(id) ? *store.overlay(id) : Overlay();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        m_run = runtimeDir();
        QVERIFY(m_run->isValid());
        m_socket = m_run->filePath(QStringLiteral("kern.sock"));
        {
            OverlayStore store(m_data.path());
            QString error;
            QVERIFY(store.load(&error));
            m_overview = store.createOverlay(QStringLiteral("Überblick Start"), 1920, 1080, &error);
            m_chat = store.createOverlay(QStringLiteral("Chat Ecke"), 800, 600, &error);
            QVERIFY2(!m_chat.isNull(), qPrintable(error));
            QVERIFY(!store.addImageSource(m_overview, kImage, &error).isNull());
            QVERIFY(!store.addTextSource(m_overview, QStringLiteral("Willkommen"), kFont, &error).isNull());
        }
        QVERIFY2(startCore(), qPrintable(m_coreLog));
        m_a = client(QStringLiteral("tst_bearbeiten_a"));
        m_b = client(QStringLiteral("tst_bearbeiten_b"));
        QTRY_VERIFY_WITH_TIMEOUT(m_a->isConnected() && m_b->isConnected(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(m_b->overlays().size(), 2, 3000);
        m_a->requestOverlay(m_overview);
        m_b->requestOverlay(m_overview);
        QVERIFY(sameInBoth(m_overview));
    }

    void cleanupTestCase()
    {
        m_a.reset();
        m_b.reset();
        if (m_core && m_core->state() != QProcess::NotRunning) {
            m_core->terminate();
            m_core->waitForFinished(5000);
        }
    }

    void overlays()
    {
        // anlegen: ans Ende der Liste, beide Fenster sehen es
        ChangeRequest r = request(msg::kCreateOverlay);
        r.name = QStringLiteral("Neu Eins");
        r.width = 1280;
        r.height = 720;
        ChangeResult res = change(r);
        QVERIFY2(res.ok, qPrintable(res.message));
        const QUuid created = res.id;
        QVERIFY(!created.isNull());
        QTRY_COMPARE_WITH_TIMEOUT(m_b->overlays().size(), 3, 3000);
        QCOMPARE(m_b->overlays().last().id, created);
        QCOMPARE(m_b->overlays().last().address, QStringLiteral("neu-eins"));
        QCOMPARE(m_b->overlays().last().width, 1280);
        QVERIFY(sameInBoth(created));

        // umbenennen ohne und mit neuer Adresse
        r = request(msg::kRenameOverlay);
        r.overlayId = created;
        r.name = QStringLiteral("Neu Zwei");
        QVERIFY(change(r).ok);
        QVERIFY(sameInBoth(created));
        QCOMPARE(current(created).address, QStringLiteral("neu-eins"));
        QCOMPARE(m_b->listData().summary(created)->name, QStringLiteral("Neu Zwei"));
        r.changeAddress = true;
        QVERIFY(change(r).ok);
        QTRY_COMPARE_WITH_TIMEOUT(m_b->overlay(created)->address, QStringLiteral("neu-zwei"), 3000);

        // kopieren: direkt nach dem Original
        r = request(msg::kCopyOverlay);
        r.overlayId = m_overview;
        res = change(r);
        QVERIFY(res.ok);
        QTRY_COMPARE_WITH_TIMEOUT(m_b->overlays().size(), 4, 3000);
        QCOMPARE(m_b->overlays().at(1).id, res.id);
        QCOMPARE(m_b->overlays().at(1).name, QStringLiteral("Überblick Start Kopie"));
        QCOMPARE(m_b->overlays().at(1).sourceCount, 2);

        // löschen: beide bekommen overlay_removed
        QSignalSpy removedB(m_b.get(), &CoreClient::overlayRemoved);
        r = request(msg::kRemoveOverlay);
        r.overlayId = res.id;
        QVERIFY(change(r).ok);
        QTRY_COMPARE_WITH_TIMEOUT(removedB.count(), 1, 3000);
        QCOMPARE(m_b->overlays().size(), 3);

        // ungültig: Grösse 0, unbekanntes Overlay, leerer Name
        r = request(msg::kCreateOverlay);
        r.name = QStringLiteral("Falsch");
        r.width = 0;
        r.height = 720;
        res = change(r);
        QVERIFY(!res.ok);
        QCOMPARE(res.code, QStringLiteral("rejected"));
        QVERIFY2(res.message.startsWith(QStringLiteral("Breite 0 ungültig")), qPrintable(res.message));
        r = request(msg::kRenameOverlay);
        r.overlayId = QUuid::createUuid();
        r.name = QStringLiteral("X");
        QCOMPARE(change(r).code, QStringLiteral("unknown_overlay"));
        r.overlayId = created;
        r.name = QStringLiteral("  ");
        res = change(r);
        QCOMPARE(res.code, QStringLiteral("rejected"));
        QCOMPARE(res.message, QStringLiteral("Der Name des Overlays darf nicht leer sein."));
        QCOMPARE(m_b->overlays().size(), 3);
    }

    // Ausgabe ein/aus (E-24, P10): beide Fenster sehen es, die Datei behält es.
    void overlayOutput()
    {
        QVERIFY(current(m_chat).output);
        ChangeRequest r = request(msg::kSetOverlayOutput);
        r.overlayId = m_chat;
        r.output = false;
        QVERIFY(change(r).ok);
        QVERIFY(sameInBoth(m_chat));
        QVERIFY(!current(m_chat).output);
        QTRY_VERIFY_WITH_TIMEOUT(!m_b->listData().summary(m_chat)->output, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!storedOverlay(m_chat).output, 3000);

        r.output = true;
        QVERIFY(change(r).ok);
        QTRY_VERIFY_WITH_TIMEOUT(m_b->listData().summary(m_chat)->output, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(storedOverlay(m_chat).output, 3000);

        r.overlayId = QUuid::createUuid();
        QCOMPARE(change(r).code, QStringLiteral("unknown_overlay"));
    }

    void folders()
    {
        ChangeRequest r = request(msg::kCreateFolder);
        r.name = QStringLiteral("Szenen");
        ChangeResult res = change(r);
        QVERIFY(res.ok);
        const QUuid folder = res.id;
        QTRY_VERIFY_WITH_TIMEOUT(m_b->listData().layout.folder(folder), 3000);
        QCOMPARE(m_b->listData().layout.folder(folder)->expanded, true);
        // Overlay hinein, Ordner nach oben, zuklappen, umbenennen
        r = request(msg::kMoveOverlay);
        r.overlayId = m_chat;
        r.folderId = folder;
        r.index = 0;
        QVERIFY(change(r).ok);
        r = request(msg::kMoveFolder);
        r.folderId = folder;
        r.index = 0;
        QVERIFY(change(r).ok);
        r = request(msg::kSetFolderExpanded);
        r.folderId = folder;
        r.expanded = false;
        QVERIFY(change(r).ok);
        r = request(msg::kRenameFolder);
        r.folderId = folder;
        r.name = QStringLiteral("Szenen neu");
        QVERIFY(change(r).ok);
        QTRY_VERIFY_WITH_TIMEOUT(m_b->listData().layout.folder(folder)
                                     && m_b->listData().layout.folder(folder)->name == QStringLiteral("Szenen neu"),
                                 3000);
        const OverlayLayout l = m_b->listData().layout;
        QVERIFY(l.entries.at(0).isFolder);
        QCOMPARE(l.entries.at(0).folder.overlays, QList<QUuid>{m_chat});
        QCOMPARE(l.entries.at(0).folder.expanded, false);
        QCOMPARE(m_b->overlays().first().id, m_chat);
        QCOMPARE(m_a->listData(), m_b->listData());
        // unbekannter Ordner, ungültige Stelle
        r = request(msg::kMoveOverlay);
        r.overlayId = m_chat;
        r.folderId = QUuid::createUuid();
        QCOMPARE(change(r).code, QStringLiteral("unknown_folder"));
        r.folderId = QUuid();
        r.index = 99;
        QCOMPARE(change(r).code, QStringLiteral("rejected"));
        // Ordner löschen: Overlay kommt auf die oberste Ebene, bleibt erhalten
        r = request(msg::kRemoveFolder);
        r.folderId = folder;
        QVERIFY(change(r).ok);
        QTRY_VERIFY_WITH_TIMEOUT(!m_b->listData().layout.folder(folder), 3000);
        QCOMPARE(m_b->listData().layout.entries.at(0).overlayId, m_chat);
        QCOMPARE(m_b->overlays().size(), 3);
        // liste.json im Datenordner
        QVERIFY(QFile::exists(m_data.filePath(QStringLiteral("liste.json"))));
    }

    void sources()
    {
        // hinzufügen: Bild, Video, Text (Inhalt «Text», Be Vietnam Pro)
        ChangeRequest r = request(msg::kAddImageSource);
        r.overlayId = m_chat;
        r.file = kImage;
        ChangeResult res = change(r);
        QVERIFY2(res.ok, qPrintable(res.message));
        const QUuid image = res.id;
        r = request(msg::kAddVideoSource);
        r.overlayId = m_chat;
        r.file = kVideo;
        res = change(r);
        QVERIFY(res.ok);
        const QUuid video = res.id;
        r = request(msg::kAddTextSource);
        r.overlayId = m_chat;
        res = change(r);
        QVERIFY(res.ok);
        const QUuid text = res.id;
        QVERIFY(sameInBoth(m_chat));
        Overlay o = current(m_chat);
        QCOMPARE(o.sources.size(), 3);
        QCOMPARE(o.sources.last().id, text); // neue Quelle liegt vorne
        QCOMPARE(currentSource(m_chat, video).loop, false);
        QCOMPARE(currentSource(m_chat, image).file, sha256Reference(kImage, QStringLiteral("png")));
        const Source t = currentSource(m_chat, text);
        QCOMPARE(t.text.content, QStringLiteral("Text"));
        QCOMPARE(t.text.weight, 400);
        QCOMPARE(t.text.fontFile, sha256Reference(defaultTextFontFile(), QStringLiteral("ttf")));
        QVERIFY(QFile::exists(m_data.filePath(t.text.fontFile)));
        QCOMPARE(t.width, 800); // Grösse des Overlays (Punkt 20)

        // Reihenfolge: Text ganz nach hinten
        r = request(msg::kMoveSource);
        r.overlayId = m_chat;
        r.sourceId = text;
        r.index = 0;
        QVERIFY(change(r).ok);
        QVERIFY(sameInBoth(m_chat));
        QCOMPARE(current(m_chat).sources.first().id, text);
        r.index = 3;
        QCOMPARE(change(r).code, QStringLiteral("rejected"));

        // ausblenden, Lage, Grösse, Drehung, Name
        r = request(msg::kUpdateSource);
        r.overlayId = m_chat;
        r.sourceId = image;
        r.changes.visible = false;
        r.changes.x = -50;
        r.changes.y = 40;
        r.changes.width = 320;
        r.changes.height = 180;
        r.changes.rotation = 3599;
        r.changes.name = QStringLiteral("Logo");
        QVERIFY(change(r).ok);
        QVERIFY(sameInBoth(m_chat));
        Source s = currentSource(m_chat, image);
        QCOMPARE(s.visible, false);
        QCOMPARE(s.x, -50);
        QCOMPARE(s.width, 320);
        QCOMPARE(s.rotation, 3599);
        QCOMPARE(s.name, QStringLiteral("Logo"));
        // ungültige Werte werden abgewiesen und ändern nichts
        for (const auto &[changes, expected] :
             {std::pair{only(&SourceChanges::rotation, 3600), QStringLiteral("Drehung 3600 ungültig")},
              std::pair{only(&SourceChanges::width, 0), QStringLiteral("Breite 0 ungültig")},
              std::pair{only(&SourceChanges::name, QString()), QStringLiteral("Der Name der Quelle darf nicht leer sein.")},
              std::pair{only(&SourceChanges::loop, true), QStringLiteral("Wiederholen gibt es nur")},
              std::pair{only(&SourceChanges::content, QStringLiteral("x")), QStringLiteral("Inhalt, Schriftgrösse")}}) {
            r.changes = changes;
            res = change(r);
            QVERIFY(!res.ok);
            QCOMPARE(res.code, QStringLiteral("rejected"));
            QVERIFY2(res.message.startsWith(expected), qPrintable(res.message));
        }
        QCOMPARE(currentSource(m_chat, image), s);
        r.sourceId = QUuid::createUuid();
        r.changes = only(&SourceChanges::x, 1);
        QCOMPARE(change(r).code, QStringLiteral("unknown_source"));

        // Text: Inhalt mehrzeilig, Grösse, Farbe mit Deckkraft, Ausrichtung
        r.sourceId = text;
        r.changes = SourceChanges{};
        r.changes.content = QStringLiteral("Zeile 1\nZeile 2");
        r.changes.fontSize = 72;
        r.changes.color = QStringLiteral("#FF800080");
        r.changes.align = TextAlign::Right;
        QVERIFY(change(r).ok);
        r.changes = only(&SourceChanges::color, QStringLiteral("rot"));
        QCOMPARE(change(r).code, QStringLiteral("rejected"));
        r.changes = only(&SourceChanges::fontSize, 0);
        QCOMPARE(change(r).code, QStringLiteral("rejected"));
        // Video: wiederholen
        r.sourceId = video;
        r.changes = only(&SourceChanges::loop, true);
        QVERIFY(change(r).ok);
        QVERIFY(sameInBoth(m_chat));
        QCOMPARE(currentSource(m_chat, text).text.content, QStringLiteral("Zeile 1\nZeile 2"));
        QCOMPARE(currentSource(m_chat, text).text.color, QStringLiteral("#FF800080"));
        QCOMPARE(currentSource(m_chat, text).text.align, TextAlign::Right);
        QCOMPARE(currentSource(m_chat, text).text.fontSize, 72);
        QCOMPARE(currentSource(m_chat, video).loop, true);

        // Schrift: installierte Datei mit Schnitt; eigene Datei ohne Schnitt (aus der Datei)
        r = request(msg::kSetTextFont);
        r.overlayId = m_chat;
        r.sourceId = text;
        r.file = kFontBold;
        r.weight = 700;
        r.italic = false;
        QVERIFY(change(r).ok);
        QVERIFY(sameInBoth(m_chat));
        QCOMPARE(currentSource(m_chat, text).text.fontFile, sha256Reference(kFontBold, QStringLiteral("ttf")));
        QCOMPARE(currentSource(m_chat, text).text.weight, 700);
        r.file = builtinFontDir() + QStringLiteral("/BeVietnamPro-SemiBoldItalic.ttf");
        r.weight.reset();
        r.italic.reset();
        QVERIFY(change(r).ok);
        QVERIFY(sameInBoth(m_chat));
        QCOMPARE(currentSource(m_chat, text).text.weight, 600);
        QCOMPARE(currentSource(m_chat, text).text.italic, true);
        // Verweis auf eine schon vorhandene Kopie
        r.file = sha256Reference(kFontBold, QStringLiteral("ttf"));
        r.weight = 700;
        r.italic = false;
        QVERIFY(change(r).ok);
        // gleiche Datei zweimal → eine Kopie
        const int mediaBefore = int(QDir(m_data.filePath(QStringLiteral("media"))).entryList(QDir::Files).size());
        r.file = kFontBold;
        QVERIFY(change(r).ok);
        QCOMPARE(QDir(m_data.filePath(QStringLiteral("media"))).entryList(QDir::Files).size(), mediaBefore);
        // keine Schrift, falsche Endung, Schrift bei einem Bild
        r.file = kImage;
        QCOMPARE(change(r).code, QStringLiteral("rejected"));
        r.sourceId = image;
        r.file = kFontBold;
        res = change(r);
        QCOMPARE(res.code, QStringLiteral("rejected"));
        QCOMPARE(res.message, QStringLiteral("Eine Schrift gibt es nur bei Quellen der Art Text."));

        // Bild ersetzen
        r = request(msg::kReplaceImageFile);
        r.overlayId = m_chat;
        r.sourceId = image;
        r.file = kImage2;
        QVERIFY(change(r).ok);
        QVERIFY(sameInBoth(m_chat));
        QCOMPARE(currentSource(m_chat, image).file, sha256Reference(kImage2, QStringLiteral("png")));
        r.sourceId = video;
        QCOMPARE(change(r).code, QStringLiteral("rejected"));

        // entfernen
        r = request(msg::kRemoveSource);
        r.overlayId = m_chat;
        r.sourceId = video;
        QVERIFY(change(r).ok);
        QVERIFY(sameInBoth(m_chat));
        QCOMPARE(current(m_chat).sources.size(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(m_b->listData().summary(m_chat)->sourceCount, 2, 3000);
    }

    void fontList()
    {
        QSignalSpy spy(m_a.get(), &CoreClient::fontsChanged);
        m_a->requestFonts();
        QVERIFY(spy.wait(5000));
        const QList<FontFace> faces = m_a->fonts();
        QCOMPARE(m_a->fontError(), QString());
        QCOMPARE(faces.first().family, QStringLiteral("Be Vietnam Pro"));
        QCOMPARE(faces.first().origin, FontOrigin::Builtin);
        QVERIFY(fontFaceForFile(faces, kFontBold));
        // Kopien im Datenordner gehören dazu (als Verweis)
        const FontFace *media = fontFaceForFile(faces, sha256Reference(kFontBold, QStringLiteral("ttf")));
        QVERIFY(media);
        QCOMPARE(media->origin, FontOrigin::Media);
        QCOMPARE(media->family, QStringLiteral("DejaVu Sans"));
        // Eine neue Kopie kommt als font_list an alle, auch an das zweite Fenster.
        QSignalSpy spyB(m_b.get(), &CoreClient::fontsChanged);
        m_b->requestFonts();
        QVERIFY(spyB.wait(5000));
        spyB.clear();
        ChangeRequest r = request(msg::kSetTextFont);
        r.overlayId = m_overview;
        r.sourceId = current(m_overview).sources.last().id;
        r.file = QStringLiteral("/usr/share/fonts/truetype/lato/Lato-Light.ttf");
        r.weight = 300;
        r.italic = false;
        QVERIFY(change(r).ok);
        QTRY_VERIFY_WITH_TIMEOUT(spyB.count() >= 1, 3000);
        const FontFace *lato = fontFaceForFile(m_b->fonts(), sha256Reference(r.file, QStringLiteral("ttf")));
        QVERIFY(lato);
        QCOMPARE(lato->family, QStringLiteral("Lato"));
    }

    void invalidMessages()
    {
        // Rohe Zeilen: Antwort change_result mit Fehler bzw. error ohne request_id
        QLocalSocket raw;
        raw.connectToServer(m_socket);
        QVERIFY(raw.waitForConnected(3000));
        raw.write(encodeMessage(helloMessage({QStringLiteral("tst"), QStringLiteral("0"), kProtocolVersion})));
        raw.write("{\"type\":\"update_source\",\"request_id\":5,\"overlay_id\":\""
                  + m_chat.toString(QUuid::WithoutBraces).toLatin1()
                  + "\",\"source_id\":\"" + current(m_chat).sources.first().id.toString(QUuid::WithoutBraces).toLatin1()
                  + "\",\"changes\":{\"breite\":5}}\n");
        raw.write("{\"type\":\"remove_overlay\"}\n");
        raw.waitForBytesWritten(1000);
        QByteArray all;
        LineSplitter lines;
        QJsonObject result;
        QJsonObject error;
        for (int i = 0; i < 30 && (result.isEmpty() || error.isEmpty()); ++i) {
            if (raw.waitForReadyRead(100))
                lines.append(raw.readAll());
            QByteArray line;
            while (lines.next(&line) == LineSplitter::Result::Line) {
                QJsonObject o;
                QString e;
                if (!decodeMessage(line, &o, &e))
                    continue;
                if (messageType(o) == QLatin1String("change_result"))
                    result = o;
                if (messageType(o) == QLatin1String("error"))
                    error = o;
            }
        }
        QCOMPARE(result.value(QStringLiteral("request_id")).toInt(), 5);
        QCOMPARE(result.value(QStringLiteral("ok")).toBool(), false);
        QCOMPARE(result.value(QStringLiteral("code")).toString(), QStringLiteral("invalid_message"));
        QVERIFY(result.value(QStringLiteral("message")).toString().contains(QStringLiteral("changes.breite")));
        QCOMPARE(error.value(QStringLiteral("code")).toString(), QStringLiteral("invalid_message"));
        QCOMPARE(error.value(QStringLiteral("request")).toString(), QStringLiteral("remove_overlay"));
    }

    void frequentChangesAreCombined()
    {
        // 200 Schritte wie mit gedrückter Pfeiltaste, über denselben Schlüssel: höchstens eine
        // Änderung je Feld unterwegs, der letzte Wert kommt an.
        const QUuid sourceId = current(m_chat).sources.first().id;
        const QString file = m_data.filePath(QStringLiteral("overlays/") + m_chat.toString(QUuid::WithoutBraces)
                                             + QStringLiteral(".json"));
        QTest::qWait(1500); // was vorher geändert wurde, ist gespeichert
        const QByteArray before = [&] { QFile f(file); f.open(QIODevice::ReadOnly); return f.readAll(); }();
        QSignalSpy finished(m_a.get(), &CoreClient::changeFinished);
        int sent = 0;
        for (int x = 1; x <= 200; ++x) {
            ChangeRequest r = request(msg::kUpdateSource);
            r.overlayId = m_chat;
            r.sourceId = sourceId;
            r.changes.x = x;
            if (m_a->sendChange(r, QStringLiteral("x")) > 0)
                ++sent;
            if (x % 10 == 0)
                QCoreApplication::processEvents(); // Antworten zwischendurch, wie im Fenster
        }
        QTRY_VERIFY_WITH_TIMEOUT(!m_a->isChangePending(QStringLiteral("x")), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(m_b->overlay(m_chat)->sources.first().x, 200, 3000);
        // sendChange gibt -1 zurück, wenn die Änderung zurückgehalten wird; sie geht nach der
        // Antwort auf die vorige weg (oder wird durch eine neuere ersetzt). Jede geschickte
        // Änderung bekommt genau eine Antwort.
        qInfo("200 Schritte: %d sofort geschickt, %lld geschickt insgesamt (= Antworten)", sent,
              static_cast<long long>(finished.size()));
        QVERIFY(sent >= 1);
        QVERIFY(finished.size() < 20);
        // Die Datei ist nicht bei jedem Schritt geschrieben: direkt danach noch der alte Stand …
        const QByteArray right = [&] { QFile f(file); f.open(QIODevice::ReadOnly); return f.readAll(); }();
        QCOMPARE(right, before);
        // … spätestens nach 1 s der neue.
        QTRY_COMPARE_WITH_TIMEOUT(storedOverlay(m_chat).sources.first().x, 200, 1500);
    }

    void restartKeepsEverything()
    {
        // Zustand vor dem Neustart, wie ihn ein neues Fenster sieht
        auto c = client(QStringLiteral("tst_bearbeiten_c"));
        QTRY_VERIFY_WITH_TIMEOUT(c->isConnected() && !c->overlays().isEmpty(), 3000);
        const Snapshot before = snapshot(c.get());
        QCOMPARE(before.overlays.size(), 3);
        // eine Änderung kurz vor dem Beenden: wird beim Beenden gespeichert (SIGTERM)
        ChangeRequest r = request(msg::kUpdateSource);
        r.overlayId = m_overview;
        r.sourceId = current(m_overview).sources.first().id;
        r.changes.y = 777;
        QVERIFY(change(r).ok);
        const Snapshot changed = snapshot(c.get());
        QVERIFY(!(changed == before));
        m_core->terminate();
        QVERIFY(m_core->waitForFinished(5000));
        QCOMPARE(m_core->exitCode(), 0);
        // Erst wenn das Fenster die Trennung bemerkt hat, gilt «verbunden» wieder für den neuen Kern.
        QTRY_VERIFY_WITH_TIMEOUT(!c->isConnected() && !m_a->isConnected(), 3000);
        QVERIFY2(startCore(), qPrintable(m_coreLog));
        QTRY_VERIFY_WITH_TIMEOUT(c->isConnected() && c->overlays().size() == 3, 5000);
        const Snapshot restarted = snapshot(c.get());
        QCOMPARE(restarted.list, changed.list);
        QCOMPARE(restarted.overlays.size(), changed.overlays.size());
        for (qsizetype i = 0; i < changed.overlays.size(); ++i)
            QCOMPARE(restarted.overlays.at(i), changed.overlays.at(i));
        QVERIFY(m_coreLog.contains(QStringLiteral("3 Overlays geladen")));
        QVERIFY(!m_coreLog.contains(QStringLiteral("Fehler beim Laden")));

        // kill -9 nach mehr als 1 s: die Änderung ist schon gespeichert
        QTRY_VERIFY_WITH_TIMEOUT(m_a->isConnected(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(m_a->overlay(m_overview) || (m_a->requestOverlay(m_overview), false), 3000);
        r.changes.y = 888;
        QVERIFY(change(r).ok);
        QTest::qWait(1300);
        m_core->kill();
        QVERIFY(m_core->waitForFinished(5000));
        QTRY_VERIFY_WITH_TIMEOUT(!c->isConnected(), 3000);
        QVERIFY2(startCore(), qPrintable(m_coreLog));
        QTRY_VERIFY_WITH_TIMEOUT(c->isConnected() && c->overlays().size() == 3, 5000);
        const Snapshot after = snapshot(c.get());
        QCOMPARE(after.list, changed.list);
        QCOMPARE(storedOverlay(m_overview).sources.first().y, 888);
    }
};

QTEST_GUILESS_MAIN(TestBearbeiten)
#include "tst_bearbeiten.moc"
