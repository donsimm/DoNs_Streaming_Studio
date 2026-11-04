// Namen und Ordner der Mediendateien (P10b, Teil A 1 und 2) ohne Qt Gui und ohne Kern-Prozess:
// medien.json lesen und schreiben (streng wie liste.json), Abgleich mit media/ (fehlende und neue
// Dateien, Ersatznamen), Ordner (beliebig tief, löschen: Inhalt eine Ebene höher, keine
// Schleifen), «verwendet in», Löschen abgewiesen, ungültige Datei, nur der Kern schreibt, Liste
// der Videoformate. Daten wie im Handtest: Bilder und Videos aus prototypes/media.

#include "medialibrary.h"
#include "overlaystore.h"
#include "videoformat.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

#include <utime.h>

using namespace dss;

namespace {

const QString kMediaDir = QStringLiteral(DSS_TEST_MEDIA_DIR);

QString ref(char c, const char *suffix)
{
    return QStringLiteral("media/") + QString(64, QLatin1Char(c)) + QLatin1Char('.')
           + QLatin1String(suffix);
}

QUuid uuid(int n)
{
    return QUuid::fromString(QStringLiteral("00000000-0000-4000-8000-%1").arg(n, 12, 10, QLatin1Char('0')));
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

// Zeit der letzten Änderung setzen (Datum des Ersatznamens), Sekunden seit 1970.
bool setModified(const QString &path, const QDateTime &when)
{
    utimbuf times{when.toSecsSinceEpoch(), when.toSecsSinceEpoch()};
    return ::utime(QFile::encodeName(path).constData(), &times) == 0;
}

QString sha256Name(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex()) + QLatin1Char('.') + QFileInfo(path).suffix();
}

MediaLibrary sampleLibrary()
{
    MediaLibrary l;
    l.folders = {{uuid(1), QStringLiteral("Logos"), QUuid(), true},
                 {uuid(2), QStringLiteral("Alt"), uuid(1), false},
                 {uuid(3), QStringLiteral("Tief"), uuid(2), true}};
    l.files = {{ref('a', "png"), QStringLiteral("logo.png"), uuid(2)},
               {ref('b', "webm"), QStringLiteral("intro «neu».webm"), QUuid()},
               {ref('c', "ttf"), QStringLiteral("Schrift"), uuid(3)}};
    return l;
}

} // namespace

class TestMedienliste : public QObject
{
    Q_OBJECT

private slots:
    // Beispiel in docs/06-overlay-format.md, Abschnitt «Medienliste»: gültig, wie beschrieben,
    // und so geschrieben, wie der Kern schreibt (Felder alphabetisch, eingerückt).
    void formatDocExample()
    {
        QFile f(QStringLiteral(DSS_FORMAT_DOC));
        QVERIFY(f.open(QIODevice::ReadOnly));
        const QString doc = QString::fromUtf8(f.readAll());
        const qsizetype begin = doc.indexOf(QStringLiteral("<!-- medien-beispiel-anfang -->"));
        const qsizetype end = doc.indexOf(QStringLiteral("<!-- medien-beispiel-ende -->"));
        QVERIFY2(begin >= 0 && end > begin, "Markierungen des Beispiels fehlen");
        const QString block = doc.mid(begin, end - begin).section(QStringLiteral("```json"), 1).section(QStringLiteral("```"), 0, 0);
        MediaLibrary l;
        QString error;
        QVERIFY2(parseMediaLibrary(block.toUtf8(), &l, &error), qPrintable(error));
        QCOMPARE(l.folders.size(), qsizetype(2));
        QCOMPARE(l.files.size(), qsizetype(2));
        QCOMPARE(mediaFolderPath(l, l.files.at(0).folder), QStringLiteral("Logos/Alt"));
        QVERIFY(l.files.at(1).folder.isNull());
        QCOMPARE(QString::fromUtf8(serializeMediaLibrary(l)).trimmed(), block.trimmed());
    }

    void roundTrip()
    {
        const MediaLibrary l = sampleLibrary();
        const QByteArray data = serializeMediaLibrary(l);
        MediaLibrary back;
        QString error;
        QVERIFY2(parseMediaLibrary(data, &back, &error), qPrintable(error));
        QCOMPARE(back, l);
        const QJsonObject o = QJsonDocument::fromJson(data).object();
        QCOMPARE(o.value(QStringLiteral("format_version")).toInt(), kMediaLibraryFormatVersion);
        QVERIFY(o.value(QStringLiteral("files")).toArray().at(1).toObject().value(QStringLiteral("folder")).isNull());
        QVERIFY(o.value(QStringLiteral("folders")).toArray().at(0).toObject().value(QStringLiteral("parent")).isNull());
        // Gleiche Namen sind erlaubt (nur eine Beschriftung)
        MediaLibrary same = l;
        same.files[1].name = same.files[0].name;
        QVERIFY2(parseMediaLibrary(serializeMediaLibrary(same), &back, &error), qPrintable(error));
    }

    void invalid_data()
    {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("message");
        const QByteArray f1 = "{\"id\":\"00000000-0000-4000-8000-000000000001\",\"name\":\"A\",\"parent\":null,\"expanded\":true}";
        const QByteArray file = "{\"file\":\"" + ref('a', "png").toLatin1() + "\",\"name\":\"x\",\"folder\":null}";
        const auto doc = [](const QByteArray &folders, const QByteArray &files, const QByteArray &version = "1") {
            return "{\"format_version\":" + version + ",\"folders\":[" + folders + "],\"files\":[" + files + "]}";
        };
        QTest::newRow("kein JSON") << QByteArray("{ kaputt") << QStringLiteral("kein gültiges JSON");
        QTest::newRow("Version 2") << doc("", "", "2") << QStringLiteral("format_version 2 ist unbekannt");
        QTest::newRow("Version fehlt") << QByteArray("{\"folders\":[],\"files\":[]}") << QStringLiteral("format_version");
        QTest::newRow("unbekanntes Feld") << QByteArray("{\"format_version\":1,\"folders\":[],\"files\":[],\"x\":1}")
                                          << QStringLiteral("«x»: unbekanntes Feld");
        QTest::newRow("Ordner doppelt") << doc(f1 + "," + f1, "") << QStringLiteral("Ordner kommt mehrfach vor");
        QTest::newRow("Eltern fehlen")
            << doc("{\"id\":\"00000000-0000-4000-8000-000000000001\",\"name\":\"A\",\"parent\":\"00000000-0000-4000-8000-000000000009\",\"expanded\":true}", "")
            << QStringLiteral("gibt es nicht");
        QTest::newRow("Schleife")
            << doc("{\"id\":\"00000000-0000-4000-8000-000000000001\",\"name\":\"A\",\"parent\":\"00000000-0000-4000-8000-000000000002\",\"expanded\":true},"
                   "{\"id\":\"00000000-0000-4000-8000-000000000002\",\"name\":\"B\",\"parent\":\"00000000-0000-4000-8000-000000000001\",\"expanded\":true}", "")
            << QStringLiteral("im Kreis");
        QTest::newRow("Ordner sich selbst")
            << doc("{\"id\":\"00000000-0000-4000-8000-000000000001\",\"name\":\"A\",\"parent\":\"00000000-0000-4000-8000-000000000001\",\"expanded\":true}", "")
            << QStringLiteral("im Kreis");
        QTest::newRow("Name leer")
            << doc("{\"id\":\"00000000-0000-4000-8000-000000000001\",\"name\":\"  \",\"parent\":null,\"expanded\":true}", "")
            << QStringLiteral("nicht leerer Text");
        QTest::newRow("Kennung gross")
            << doc("{\"id\":\"0000000A-0000-4000-8000-000000000001\",\"name\":\"A\",\"parent\":null,\"expanded\":true}", "")
            << QStringLiteral("muss eine UUID");
        QTest::newRow("Datei doppelt") << doc("", file + "," + file) << QStringLiteral("Datei kommt mehrfach vor");
        QTest::newRow("Verweis falsch")
            << doc("", "{\"file\":\"media/abc.png\",\"name\":\"x\",\"folder\":null}") << QStringLiteral("Verweis media/");
        QTest::newRow("Endung nicht erlaubt")
            << doc("", "{\"file\":\"" + ref('a', "gif").toLatin1() + "\",\"name\":\"x\",\"folder\":null}")
            << QStringLiteral("Verweis media/");
        QTest::newRow("Ordner der Datei fehlt")
            << doc("", "{\"file\":\"" + ref('a', "png").toLatin1() + "\",\"name\":\"x\",\"folder\":\"00000000-0000-4000-8000-000000000001\"}")
            << QStringLiteral("gibt es nicht");
        QTest::newRow("Feld fehlt") << doc("", "{\"file\":\"" + ref('a', "png").toLatin1() + "\",\"name\":\"x\"}")
                                    << QStringLiteral("«files[0].folder»: fehlt");
    }

    void invalid()
    {
        QFETCH(QByteArray, data);
        QFETCH(QString, message);
        MediaLibrary l;
        QString error;
        QVERIFY(!parseMediaLibrary(data, &l, &error));
        QVERIFY2(error.contains(message), qPrintable(error));
    }

    void reconcile()
    {
        MediaLibrary l = sampleLibrary();
        const QDateTime morning(QDate(2026, 11, 2), QTime(8, 0));
        const QDateTime evening(QDate(2026, 11, 2), QTime(20, 0));
        // a bleibt, b fehlt (fällt weg), c bleibt, d und e neu (am selben Tag), f neu am Tag danach
        const QList<MediaFileInfo> present = {{ref('a', "png"), morning, 10},
                                              {ref('c', "ttf"), morning, 20},
                                              {ref('e', "mp4"), evening, 30},
                                              {ref('d', "webm"), morning, 40},
                                              {ref('f', "jpg"), morning.addDays(1), 50}};
        QStringList added;
        QStringList dropped;
        QVERIFY(reconcileMediaLibrary(&l, present, &added, &dropped));
        QCOMPARE(dropped, QStringList{ref('b', "webm")});
        // Reihenfolge nach Datum: d (8 Uhr), e (20 Uhr), f (Tag danach)
        QCOMPARE(added, (QStringList{ref('d', "webm"), ref('e', "mp4"), ref('f', "jpg")}));
        QCOMPARE(l.entry(ref('d', "webm"))->name, QStringLiteral("Video vom 2026-11-02"));
        QCOMPARE(l.entry(ref('e', "mp4"))->name, QStringLiteral("Video vom 2026-11-02 (2)"));
        QCOMPARE(l.entry(ref('f', "jpg"))->name, QStringLiteral("Bild vom 2026-11-03"));
        QVERIFY(l.entry(ref('f', "jpg"))->folder.isNull());
        // Bestehende Einträge unverändert, Ordner bleiben
        QCOMPARE(l.entry(ref('a', "png"))->name, QStringLiteral("logo.png"));
        QCOMPARE(l.entry(ref('c', "ttf"))->folder, uuid(3));
        QCOMPARE(l.folders.size(), 3);
        // Zweites Mal: nichts zu tun
        QVERIFY(!reconcileMediaLibrary(&l, present));
        // Ersatzname belegt: weiter zählen
        MediaLibrary taken;
        taken.files = {{ref('a', "png"), QStringLiteral("Bild vom 2026-11-02"), QUuid()}};
        QVERIFY(reconcileMediaLibrary(&taken, {{ref('a', "png"), morning, 1}, {ref('g', "png"), morning, 1}}));
        QCOMPARE(taken.entry(ref('g', "png"))->name, QStringLiteral("Bild vom 2026-11-02 (2)"));
        QCOMPARE(substituteMediaName(MediaKind::Font, QDate(2026, 1, 9)), QStringLiteral("Schrift vom 2026-01-09"));
    }

    void folders()
    {
        MediaLibrary l = sampleLibrary();
        // Tiefer anlegen
        QVERIFY(createMediaFolderInLibrary(&l, uuid(4), QStringLiteral("Noch tiefer"), uuid(3)).isEmpty());
        QCOMPARE(mediaFolderPath(l, uuid(4)), QStringLiteral("Logos/Alt/Tief/Noch tiefer"));
        QVERIFY(!createMediaFolderInLibrary(&l, uuid(4), QStringLiteral("doppelt"), QUuid()).isEmpty());
        QVERIFY(!createMediaFolderInLibrary(&l, uuid(5), QStringLiteral(""), QUuid()).isEmpty());
        QVERIFY(!createMediaFolderInLibrary(&l, uuid(5), QStringLiteral("x"), uuid(99)).isEmpty());
        // Verschieben: nicht in sich selbst oder einen Unterordner
        QVERIFY(!moveMediaFolderInLibrary(&l, uuid(1), uuid(1)).isEmpty());
        QVERIFY(!moveMediaFolderInLibrary(&l, uuid(1), uuid(4)).isEmpty());
        QVERIFY(moveMediaFolderInLibrary(&l, uuid(3), QUuid()).isEmpty());
        QCOMPARE(mediaFolderPath(l, uuid(4)), QStringLiteral("Tief/Noch tiefer"));
        QVERIFY(moveMediaFolderInLibrary(&l, uuid(3), uuid(2)).isEmpty());
        // Umbenennen, auf- und zuklappen
        QVERIFY(renameMediaFolderInLibrary(&l, uuid(2), QStringLiteral("Früher")).isEmpty());
        QVERIFY(!renameMediaFolderInLibrary(&l, uuid(2), QStringLiteral(" ")).isEmpty());
        QVERIFY(setMediaFolderExpandedInLibrary(&l, uuid(2), true).isEmpty());
        QVERIFY(l.folder(uuid(2))->expanded);
        // Löschen: Inhalt (Datei a, Ordner Tief) kommt in den Elternordner Logos
        QVERIFY(removeMediaFolderFromLibrary(&l, uuid(2)).isEmpty());
        QVERIFY(!l.folder(uuid(2)));
        QCOMPARE(l.entry(ref('a', "png"))->folder, uuid(1));
        QCOMPARE(l.folder(uuid(3))->parent, uuid(1));
        QCOMPARE(l.files.size(), 3); // keine Datei gelöscht
        QVERIFY(!removeMediaFolderFromLibrary(&l, uuid(2)).isEmpty());
        // Oberster Ordner gelöscht: Inhalt auf die oberste Ebene
        QVERIFY(removeMediaFolderFromLibrary(&l, uuid(1)).isEmpty());
        QVERIFY(l.entry(ref('a', "png"))->folder.isNull());
        QVERIFY(l.folder(uuid(3))->parent.isNull());
        // Gültig wie gespeichert
        MediaLibrary back;
        QString error;
        QVERIFY2(parseMediaLibrary(serializeMediaLibrary(l), &back, &error), qPrintable(error));
        QCOMPARE(back, l);
    }

    void files()
    {
        MediaLibrary l = sampleLibrary();
        // Gleicher Inhalt nochmals: Name und Ordner bleiben
        QVERIFY(addMediaToLibrary(&l, ref('a', "png"), QStringLiteral("anders.png"), QUuid()).isEmpty());
        QCOMPARE(l.entry(ref('a', "png"))->name, QStringLiteral("logo.png"));
        QCOMPARE(l.entry(ref('a', "png"))->folder, uuid(2));
        QVERIFY(addMediaToLibrary(&l, ref('d', "otf"), QStringLiteral("neu.otf"), uuid(1)).isEmpty());
        QCOMPARE(l.entry(ref('d', "otf"))->folder, uuid(1));
        QVERIFY(!addMediaToLibrary(&l, ref('e', "png"), QStringLiteral("x"), uuid(99)).isEmpty());
        QVERIFY(!addMediaToLibrary(&l, QStringLiteral("media/kurz.png"), QStringLiteral("x"), QUuid()).isEmpty());
        // Umbenennen ändert nur den Namen
        QVERIFY(renameMediaInLibrary(&l, ref('a', "png"), QStringLiteral("Logo gross")).isEmpty());
        QCOMPARE(*l.entry(ref('a', "png")), (MediaEntry{ref('a', "png"), QStringLiteral("Logo gross"), uuid(2)}));
        QVERIFY(!renameMediaInLibrary(&l, ref('a', "png"), QString()).isEmpty());
        QVERIFY(!renameMediaInLibrary(&l, ref('z', "png"), QStringLiteral("x")).isEmpty());
        QVERIFY(moveMediaFileInLibrary(&l, ref('a', "png"), QUuid()).isEmpty());
        QVERIFY(l.entry(ref('a', "png"))->folder.isNull());
        QVERIFY(!moveMediaFileInLibrary(&l, ref('a', "png"), uuid(99)).isEmpty());
        QVERIFY(removeMediaFromLibrary(&l, ref('a', "png")).isEmpty());
        QVERIFY(!l.entry(ref('a', "png")));
        QVERIFY(!removeMediaFromLibrary(&l, ref('a', "png")).isEmpty());
    }

    void displayOrderAndLookup()
    {
        MediaLibrary l = sampleLibrary();
        l.files.append({ref('d', "png"), QStringLiteral("Zebra"), QUuid()});
        l.files.append({ref('e', "png"), QStringLiteral("logo.png"), uuid(1)});
        const QList<MediaEntry> order = mediaFilesInOrder(l);
        QStringList names;
        for (const MediaEntry &e : order)
            names.append(mediaFolderPath(l, e.folder) + QLatin1Char('|') + e.name);
        QCOMPARE(names, (QStringList{QStringLiteral("|intro «neu».webm"), QStringLiteral("|Zebra"),
                                     QStringLiteral("Logos|logo.png"), QStringLiteral("Logos/Alt|logo.png"),
                                     QStringLiteral("Logos/Alt/Tief|Schrift")}));
        QString error;
        QCOMPARE(findMediaFile(l, QStringLiteral("2"), &error), ref('d', "png"));
        QCOMPARE(findMediaFile(l, ref('c', "ttf"), &error), ref('c', "ttf"));
        QCOMPARE(findMediaFile(l, QStringLiteral("Zebra"), &error), ref('d', "png"));
        QVERIFY(findMediaFile(l, QStringLiteral("logo.png"), &error).isEmpty());
        QVERIFY2(error.contains(QStringLiteral("2 Dateien")), qPrintable(error));
        QVERIFY(findMediaFile(l, QStringLiteral("6"), &error).isEmpty());
        QVERIFY(findMediaFile(l, QStringLiteral("gibt es nicht"), &error).isEmpty());
        QCOMPARE(findMediaFolder(l, QStringLiteral("Logos/Alt"), &error), std::optional<QUuid>(uuid(2)));
        QCOMPARE(findMediaFolder(l, QStringLiteral("/"), &error), std::optional<QUuid>(QUuid()));
        QCOMPARE(findMediaFolder(l, QString(), &error), std::optional<QUuid>(QUuid()));
        QVERIFY(!findMediaFolder(l, QStringLiteral("Alt"), &error));
        QCOMPARE(mediaSizeText(512), QStringLiteral("512 Byte"));
        QCOMPARE(mediaSizeText(19776), QStringLiteral("19.3 KiB"));
        QCOMPARE(mediaSizeText(qint64(5) << 30), QStringLiteral("5.0 GiB"));
    }

    void usages()
    {
        Overlay a;
        a.id = uuid(10);
        a.name = QStringLiteral("Start");
        Source image;
        image.id = uuid(11);
        image.kind = SourceKind::Image;
        image.name = QStringLiteral("Logo");
        image.file = ref('a', "png");
        Source text;
        text.id = uuid(12);
        text.kind = SourceKind::Text;
        text.name = QStringLiteral("Titel");
        text.text.fontFile = ref('c', "ttf");
        Source copy = image;
        copy.id = uuid(13);
        copy.name = QStringLiteral("Logo 2");
        a.sources = {image, text, copy};
        Overlay b;
        b.id = uuid(20);
        b.name = QStringLiteral("Pause");
        Source other = image;
        other.id = uuid(21);
        b.sources = {other};
        const MediaUsages u = mediaUsages({a, b});
        QCOMPARE(u.value(ref('a', "png")).size(), 3);
        QCOMPARE(u.value(ref('c', "ttf")).size(), 1); // Schrift der Textquelle zählt
        QVERIFY(u.value(ref('b', "webm")).isEmpty());
        QCOMPARE(usageText(u.value(ref('a', "png"))),
                 QStringLiteral("verwendet in Overlay «Start» (Quelle «Logo», «Logo 2»), Overlay «Pause» (Quelle «Logo»)"));
        QCOMPARE(usageText({}), QString());
    }

    void videoFormats()
    {
        // Liste aus dem Bericht P10 (videoformat.h)
        QCOMPARE(browserUnsupportedVideoCodec(QStringLiteral("hevc")), std::optional<QString>(QStringLiteral("HEVC")));
        QCOMPARE(browserUnsupportedVideoCodec(QStringLiteral("mpeg4")), std::optional<QString>(QStringLiteral("MPEG-4 Part 2")));
        QCOMPARE(browserUnsupportedVideoCodec(QStringLiteral("prores")), std::optional<QString>(QStringLiteral("ProRes")));
        for (const char *ok : {"h264", "vp9", "vp8", "av1", ""})
            QVERIFY2(!browserUnsupportedVideoCodec(QLatin1String(ok)), ok);
        QCOMPARE(browserVideoWarning(QStringLiteral("zaehler.mov"), QStringLiteral("ProRes")),
                 QStringLiteral("zaehler.mov (ProRes) wird in der Browserquelle nicht angezeigt. In H.264 oder VP9 umwandeln."));
    }

    // OverlayStore: Laden mit Ersatznamen, nur der Kern schreibt, Umbenennen ändert nur den Namen,
    // Löschen abgewiesen, ungültige medien.json bleibt unverändert.
    void store()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString media = dir.filePath(QStringLiteral("media"));
        QVERIFY(QDir().mkpath(media));
        const QString image = sha256Name(kMediaDir + QStringLiteral("/bild1.png"));
        const QString video = sha256Name(kMediaDir + QStringLiteral("/video.webm"));
        QVERIFY(QFile::copy(kMediaDir + QStringLiteral("/bild1.png"), media + QLatin1Char('/') + image));
        QVERIFY(QFile::copy(kMediaDir + QStringLiteral("/video.webm"), media + QLatin1Char('/') + video));
        QVERIFY(writeFile(media + QStringLiteral("/fremd.txt"), "nicht dabei"));
        QVERIFY(setModified(media + QLatin1Char('/') + image, QDateTime(QDate(2026, 11, 2), QTime(10, 0))));
        QVERIFY(setModified(media + QLatin1Char('/') + video, QDateTime(QDate(2026, 10, 31), QTime(10, 0))));

        OverlayStore store(dir.path());
        QString error;
        QVERIFY2(store.load(&error), qPrintable(error));
        QVERIFY(store.mediaLibraryError().isEmpty());
        QCOMPARE(store.mediaLibrary().files.size(), 2);
        QCOMPARE(store.mediaName(QStringLiteral("media/") + image), QStringLiteral("Bild vom 2026-11-02"));
        QCOMPARE(store.mediaName(QStringLiteral("media/") + video), QStringLiteral("Video vom 2026-10-31"));
        QCOMPARE(store.mediaBytes(QStringLiteral("media/") + image), QFileInfo(kMediaDir + QStringLiteral("/bild1.png")).size());
        // Laden schreibt nichts; ohne setWritesMediaLibrary (dss-cli) auch Änderungen nicht
        QVERIFY(!QFile::exists(store.mediaLibraryFilePath()));
        QVERIFY2(store.renameMedia(QStringLiteral("media/") + image, QStringLiteral("Logo"), &error), qPrintable(error));
        QVERIFY(!QFile::exists(store.mediaLibraryFilePath()));

        // Kern: schreibt bei jeder Änderung, atomar
        store.setWritesMediaLibrary(true);
        const QUuid folder = store.createMediaFolder(QStringLiteral("Logos"), QUuid(), &error);
        QVERIFY2(!folder.isNull(), qPrintable(error));
        QVERIFY(store.moveMediaFile(QStringLiteral("media/") + image, folder, &error));
        MediaLibrary onDisk;
        QVERIFY(parseMediaLibrary(readAll(store.mediaLibraryFilePath()), &onDisk, &error));
        QCOMPARE(onDisk, store.mediaLibrary());
        QCOMPARE(onDisk.entry(QStringLiteral("media/") + image)->name, QStringLiteral("Logo"));

        // Übernehmen: ursprünglicher Dateiname; dieselbe Datei nochmals: Name bleibt
        const QString bild2 = store.importMedia(kMediaDir + QStringLiteral("/bild2.png"), MediaUse::Image, &error, folder);
        QVERIFY2(!bild2.isEmpty(), qPrintable(error));
        QCOMPARE(store.mediaName(bild2), QStringLiteral("bild2.png"));
        QCOMPARE(store.mediaLibrary().entry(bild2)->folder, folder);
        QVERIFY(store.renameMedia(bild2, QStringLiteral("Zweites Bild"), &error));
        QCOMPARE(store.importMedia(kMediaDir + QStringLiteral("/bild2.png"), MediaUse::Image, &error), bild2);
        QCOMPARE(store.mediaName(bild2), QStringLiteral("Zweites Bild"));
        QCOMPARE(store.mediaLibrary().entry(bild2)->folder, folder);

        // Verwendet: Löschen abgewiesen; Umbenennen ändert den Verweis der Quelle nicht
        const QUuid overlay = store.createOverlay(QStringLiteral("Start"), 1920, 1080, &error);
        const QUuid source = store.addImageSource(overlay, bild2, &error);
        QVERIFY2(!source.isNull(), qPrintable(error));
        QVERIFY(store.renameMedia(bild2, QStringLiteral("Drittes"), &error));
        QCOMPARE(store.overlay(overlay)->sources.first().file, bild2);
        QCOMPARE(store.mediaUsages().value(bild2).size(), 1);
        QVERIFY(!store.removeMedia(bild2, &error));
        QVERIFY2(error.contains(QStringLiteral("verwendet in Overlay «Start»")), qPrintable(error));
        QVERIFY(QFile::exists(store.mediaFilePath(bild2)));
        // Ungenutzt: Datei und Eintrag weg
        QVERIFY2(store.removeMedia(QStringLiteral("media/") + video, &error), qPrintable(error));
        QVERIFY(!QFile::exists(media + QLatin1Char('/') + video));
        QVERIFY(!store.mediaLibrary().entry(QStringLiteral("media/") + video));
        QVERIFY(QFile::exists(media + QStringLiteral("/fremd.txt")));

        // Nicht lesbare Overlay-Datei mit dem Verweis: Löschen abgewiesen (könnte ihn nutzen)
        QVERIFY(writeFile(dir.filePath(QStringLiteral("overlays/00000000-0000-4000-8000-000000000099.json")),
                          "{ kaputt \"" + (QStringLiteral("media/") + image).toLatin1() + "\""));
        QVERIFY(store.load(&error));
        QVERIFY(!store.removeMedia(QStringLiteral("media/") + image, &error));
        QVERIFY2(error.contains(QStringLiteral("nicht lesbar")), qPrintable(error));

        // Wieder geladen: alles aus medien.json; fehlende Datei (von aussen gelöscht) fällt weg,
        // neue (zum Beispiel mit dss-cli übernommen) bekommt einen Ersatznamen
        QVERIFY(QFile::remove(store.mediaFilePath(QStringLiteral("media/") + image)));
        const QString cliCopy = sha256Name(kMediaDir + QStringLiteral("/bild3.png"));
        QVERIFY(QFile::copy(kMediaDir + QStringLiteral("/bild3.png"), media + QLatin1Char('/') + cliCopy));
        QVERIFY(setModified(media + QLatin1Char('/') + cliCopy, QDateTime(QDate(2026, 11, 4), QTime(9, 0))));
        OverlayStore again(dir.path());
        QVERIFY(again.load(&error));
        QCOMPARE(again.mediaName(bild2), QStringLiteral("Drittes"));
        QVERIFY(!again.mediaLibrary().entry(QStringLiteral("media/") + image));
        QCOMPARE(again.mediaName(QStringLiteral("media/") + cliCopy), QStringLiteral("Bild vom 2026-11-04"));
        QVERIFY(again.mediaLibrary().folder(folder));
    }

    // Ungültige medien.json: Meldung, Ersatznamen, Umbenennen und Ordner abgewiesen, Übernehmen
    // geht weiter (nur im Speicher), die Datei bleibt Byte für Byte.
    void invalidFile()
    {
        QTemporaryDir dir;
        const QString media = dir.filePath(QStringLiteral("media"));
        QVERIFY(QDir().mkpath(media));
        const QString image = sha256Name(kMediaDir + QStringLiteral("/bild1.png"));
        QVERIFY(QFile::copy(kMediaDir + QStringLiteral("/bild1.png"), media + QLatin1Char('/') + image));
        const QByteArray broken = "{\"format_version\": 1, \"folders\": [], \"files\": 7}";
        QVERIFY(writeFile(dir.filePath(QStringLiteral("medien.json")), broken));
        OverlayStore store(dir.path());
        store.setWritesMediaLibrary(true);
        QString error;
        QVERIFY(store.load(&error));
        QVERIFY(store.mediaLibraryError().startsWith(QStringLiteral("medien.json: ")));
        QVERIFY(store.loadErrors().join(QLatin1Char('\n')).contains(QStringLiteral("medien.json")));
        QCOMPARE(store.mediaLibrary().files.size(), 1);
        QVERIFY(store.mediaName(QStringLiteral("media/") + image).contains(QStringLiteral("Bild vom ")));
        QVERIFY(!store.renameMedia(QStringLiteral("media/") + image, QStringLiteral("x"), &error));
        QVERIFY2(error.contains(QStringLiteral("nicht überschrieben")), qPrintable(error));
        QVERIFY(store.createMediaFolder(QStringLiteral("x"), QUuid(), &error).isNull());
        const QString bild2 = store.importMedia(kMediaDir + QStringLiteral("/bild2.png"), MediaUse::Image, &error);
        QVERIFY(!bild2.isEmpty());
        QCOMPARE(store.mediaName(bild2), QStringLiteral("bild2.png"));
        QVERIFY(store.removeMedia(bild2, &error));
        QCOMPARE(readAll(dir.filePath(QStringLiteral("medien.json"))), broken);
    }
};

QTEST_GUILESS_MAIN(TestMedienliste)
#include "tst_medienliste.moc"
