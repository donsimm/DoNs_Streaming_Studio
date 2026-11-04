// Übernehmen von Dateien im eigenen Thread (P10-vor, Punkt 67) und Aufräumen der vorläufigen
// Dateien von QSaveFile beim Start (Punkt 68). Ohne Qt Gui. Teil 1 prüft importMediaFile,
// MediaImporter und removeSaveFileLeftovers direkt, Teil 2 den Kern als eigenen Prozess (wie
// tst_bearbeiten): Antwortzeit und Leistungswerte während des Übernehmens, zwei Übernahmen
// gleichzeitig (auch dieselbe Datei), Overlay bzw. Quelle während des Übernehmens gelöscht,
// SIGTERM während des Übernehmens.
// Daten: Bild aus prototypes/media (wie im Handtest), grosse Dateien mit Zufallsinhalt (wie die
// Messung in P10a), Name mit Endung .png; ihre Grösse ist nicht lesbar, die Quelle bekommt dann
// die Grösse des Overlays mit Hinweis.

#include "coreclient.h"
#include "mediaimport.h"
#include "prozesshilfe.h"
#include "version.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <csignal>
#include <memory>

using namespace dss;
using namespace testhelp;

namespace {

const QString kImage = QStringLiteral(DSS_TEST_MEDIA_DIR) + QStringLiteral("/bild1.png");
constexpr qint64 kMiB = 1 << 20;

QString sha256Hex(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

// Datei mit Zufallsinhalt (nicht komprimierbar), mib MiB gross.
bool writeRandomFile(const QString &path, int mib)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QByteArray chunk(kMiB, Qt::Uninitialized);
    auto *words = reinterpret_cast<quint32 *>(chunk.data());
    for (int i = 0; i < mib; ++i) {
        QRandomGenerator::global()->fillRange(words, chunk.size() / 4);
        if (f.write(chunk) != chunk.size())
            return false;
    }
    return true;
}

// Alle Einträge in media/ einschliesslich versteckter.
QStringList mediaEntries(const QString &dataDir)
{
    return QDir(dataDir + QStringLiteral("/media"))
        .entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
}

ChangeRequest request(const char *type, const QUuid &overlay = QUuid())
{
    ChangeRequest r;
    r.type = QString::fromLatin1(type);
    r.overlayId = overlay;
    return r;
}

} // namespace

class TestUebernehmen : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_files; // grosse Testdateien, einmal für alle Fälle
    QString m_big;         // 256 MiB
    QString m_big2;        // 256 MiB, anderer Inhalt
    std::unique_ptr<QTemporaryDir> m_data;
    std::unique_ptr<QTemporaryDir> m_run;
    QString m_socket;
    std::unique_ptr<QProcess> m_core;
    QString m_coreLog;
    std::unique_ptr<CoreClient> m_client;
    QUuid m_overlay;

    QString dataDir() const { return m_data->path(); }

    bool startCore()
    {
        m_core = testhelp::startCore(dataDir(), m_socket);
        m_coreLog.clear();
        return waitForLog(m_core.get(), QStringLiteral("Verbindung offen"), &m_coreLog);
    }

    bool connectClient()
    {
        CoreClient::Options o;
        o.socketPath = m_socket;
        o.program = QStringLiteral("tst_uebernehmen");
        o.version = version();
        o.reconnectIntervalMs = 200;
        m_client = std::make_unique<CoreClient>(o);
        QSignalSpy spy(m_client.get(), &CoreClient::overlayListChanged);
        m_client->start();
        return spy.wait(5000);
    }

    // Antwort auf die Änderung mit dieser request_id (wartet höchstens timeoutMs).
    std::optional<ChangeResult> waitResult(QSignalSpy *spy, qint64 id, int timeoutMs = 60000)
    {
        QDeadlineTimer deadline(timeoutMs);
        for (;;) {
            for (const QList<QVariant> &args : std::as_const(*spy)) {
                const ChangeResult r = args.at(0).value<ChangeResult>();
                if (r.requestId == id)
                    return r;
            }
            if (deadline.hasExpired())
                return std::nullopt;
            spy->wait(50);
        }
    }

    ChangeResult change(const ChangeRequest &r)
    {
        QSignalSpy spy(m_client.get(), &CoreClient::changeFinished);
        const qint64 id = m_client->sendChange(r);
        return waitResult(&spy, id).value_or(ChangeResult{});
    }

    QUuid createOverlay(const QString &name)
    {
        ChangeRequest r = request(msg::kCreateOverlay);
        r.name = name;
        r.width = 1280;
        r.height = 720;
        const ChangeResult result = change(r);
        return result.ok ? result.id : QUuid();
    }

    void stopCore()
    {
        if (m_client)
            m_client->stop();
        m_client.reset();
        if (m_core && m_core->state() != QProcess::NotRunning) {
            m_core->terminate();
            m_core->waitForFinished(10000);
        }
        m_core.reset();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_files.isValid());
        QVERIFY(QFile::exists(kImage));
        m_big = m_files.filePath(QStringLiteral("gross.png"));
        m_big2 = m_files.filePath(QStringLiteral("gross2.png"));
        QVERIFY(writeRandomFile(m_big, 256));
        QVERIFY(writeRandomFile(m_big2, 256));
    }

    void init()
    {
        m_data = std::make_unique<QTemporaryDir>();
        QVERIFY(m_data->isValid());
    }

    void cleanup()
    {
        stopCore();
        m_data.reset();
        m_run.reset();
    }

    // --- Teil 1: ohne Kern ---

    void importCopiesOnce()
    {
        const QString media = dataDir() + QStringLiteral("/media");
        const MediaImportResult first = importMediaFile(media, kImage, MediaUse::Image);
        QVERIFY2(!first.reference.isEmpty(), qPrintable(first.error));
        QCOMPARE(first.reference, QStringLiteral("media/") + sha256Hex(kImage) + QStringLiteral(".png"));
        QVERIFY(!first.existed);
        QCOMPARE(sha256Hex(dataDir() + QLatin1Char('/') + first.reference), sha256Hex(kImage));
        const MediaImportResult second = importMediaFile(media, kImage, MediaUse::Image);
        QCOMPARE(second.reference, first.reference);
        QVERIFY(second.existed);
        QCOMPARE(mediaEntries(dataDir()).size(), 1);
        const MediaImportResult wrong = importMediaFile(media, kImage, MediaUse::Video);
        QVERIFY(wrong.reference.isEmpty());
        QVERIFY2(wrong.error.contains(QStringLiteral("nicht erlaubt")), qPrintable(wrong.error));
    }

    // Abbruch mitten im Kopieren: weder die Datei noch eine vorläufige Datei bleibt.
    void cancelLeavesNothing()
    {
        const QString media = dataDir() + QStringLiteral("/media");
        std::atomic<bool> cancel(false);
        int chunks = 0;
        // 256 Stück zum Hashen, dann Kopie; Abbruch nach 300 Stück, also beim Kopieren.
        const MediaImportResult r = importMediaFile(media, m_big, MediaUse::Image, &cancel,
                                                    [&] {
                                                        if (++chunks == 300) {
                                                            cancel = true;
                                                            QVERIFY(QDir(media).exists());
                                                        }
                                                    });
        QVERIFY(r.canceled);
        QVERIFY(r.reference.isEmpty());
        QCOMPARE(chunks, 300);
        QCOMPARE(mediaEntries(dataDir()), QStringList());
    }

    // Dieselbe Datei zweimal gleichzeitig: eine Kopie, beide Aufträge mit demselben Verweis.
    void sameFileTwiceInParallel()
    {
        MediaImporter importer(dataDir() + QStringLiteral("/media"), 2);
        QSignalSpy spy(&importer, &MediaImporter::finished);
        const quint64 a = importer.start(m_big, MediaUse::Image);
        const quint64 b = importer.start(m_big, MediaUse::Image);
        const quint64 c = importer.start(m_big2, MediaUse::Image);
        QCOMPARE(importer.pending(), 3);
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 3, 60000);
        QHash<quint64, MediaImportResult> results;
        for (const QList<QVariant> &args : std::as_const(spy))
            results.insert(args.at(0).toULongLong(), args.at(1).value<MediaImportResult>());
        QVERIFY2(!results.value(a).reference.isEmpty(), qPrintable(results.value(a).error));
        QCOMPARE(results.value(b).reference, results.value(a).reference);
        // Genau einer der beiden hat kopiert, der andere fand die fertige Kopie.
        QVERIFY(results.value(a).existed != results.value(b).existed);
        QVERIFY(!results.value(c).reference.isEmpty());
        QVERIFY(results.value(c).reference != results.value(a).reference);
        QCOMPARE(mediaEntries(dataDir()).size(), 2);
        QCOMPARE(sha256Hex(dataDir() + QLatin1Char('/') + results.value(a).reference), sha256Hex(m_big));
        QCOMPARE(importer.pending(), 0);
    }

    // cancelAll wartet auf die Threads; danach kommt kein finished() mehr, und media/ ist leer.
    void cancelAllStopsThreads()
    {
        MediaImporter importer(dataDir() + QStringLiteral("/media"), 2);
        QSignalSpy spy(&importer, &MediaImporter::finished);
        importer.start(m_big, MediaUse::Image);
        importer.start(m_big2, MediaUse::Image);
        QThread::msleep(300);
        QElapsedTimer t;
        t.start();
        QCOMPARE(importer.cancelAll(), 2);
        qInfo("cancelAll nach 300 ms: %lld ms", static_cast<long long>(t.elapsed()));
        QVERIFY(t.elapsed() < 1000);
        QTest::qWait(200);
        QCOMPARE(spy.size(), 0);
        QCOMPARE(mediaEntries(dataDir()), QStringList());
    }

    void leftoverPattern_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<bool>("leftover");
        const QString sha = QStringLiteral("4cbc5e0e173bf2171e32ced41e534938ee98c5adfaabe60fc8668517bd3a5560");
        QTest::newRow("png") << sha + QStringLiteral(".png.AbCdEf") << true;
        QTest::newRow("jpeg") << sha + QStringLiteral(".jpeg.zzzzzz") << true;
        QTest::newRow("mp4") << sha + QStringLiteral(".mp4.QWERTZ") << true;
        QTest::newRow("otf") << sha + QStringLiteral(".otf.aBcDeF") << true;
        QTest::newRow("fertige Kopie") << sha + QStringLiteral(".png") << false;
        QTest::newRow("Ziffer im Zusatz") << sha + QStringLiteral(".png.AbCdE1") << false;
        QTest::newRow("Zusatz zu kurz") << sha + QStringLiteral(".png.AbCdE") << false;
        QTest::newRow("Zusatz zu lang") << sha + QStringLiteral(".png.AbCdEfG") << false;
        QTest::newRow("Endung gross") << sha + QStringLiteral(".PNG.AbCdEf") << false;
        QTest::newRow("Endung nicht erlaubt") << sha + QStringLiteral(".gif.AbCdEf") << false;
        QTest::newRow("Prüfsumme gross") << sha.toUpper() + QStringLiteral(".png.AbCdEf") << false;
        QTest::newRow("Prüfsumme zu kurz") << sha.left(63) + QStringLiteral(".png.AbCdEf") << false;
        QTest::newRow("anderer Name") << QStringLiteral("bild.png.AbCdEf") << false;
        QTest::newRow("Zeilenende") << sha + QStringLiteral(".png.AbCdEf\n") << false;
    }

    void leftoverPattern()
    {
        QFETCH(QString, name);
        QFETCH(bool, leftover);
        QCOMPARE(isSaveFileLeftover(name), leftover);
    }

    // Nur passende reguläre Dateien werden gelöscht, mit Anzahl und Grösse.
    void removeLeftoversOnlyMatching()
    {
        const QString media = dataDir() + QStringLiteral("/media");
        QVERIFY(QDir().mkpath(media));
        const QString sha = sha256Hex(kImage);
        const auto put = [&](const QString &name, qint64 size) {
            QFile f(media + QLatin1Char('/') + name);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QVERIFY(f.write(QByteArray(size, 'x')) == size);
        };
        put(sha + QStringLiteral(".png.AbCdEf"), 1000);
        put(sha + QStringLiteral(".mp4.zyXWvu"), 234);
        put(sha + QStringLiteral(".png"), 10);
        put(sha + QStringLiteral(".png.AbCdE1"), 10);
        put(QStringLiteral("liesmich.txt"), 10);
        QVERIFY(QDir(media).mkdir(sha + QStringLiteral(".jpg.Ordner")));
        QVERIFY(QFile::link(media + QLatin1Char('/') + sha + QStringLiteral(".png"),
                            media + QLatin1Char('/') + sha + QStringLiteral(".jpg.Verwei")));
        const LeftoverCleanup c = removeSaveFileLeftovers(media);
        QCOMPARE(c.count, 2);
        QCOMPARE(c.bytes, qint64(1234));
        QVERIFY(c.failed.isEmpty());
        QCOMPARE(mediaEntries(dataDir()),
                 (QStringList{sha + QStringLiteral(".jpg.Ordner"), sha + QStringLiteral(".jpg.Verwei"),
                              sha + QStringLiteral(".png"), sha + QStringLiteral(".png.AbCdE1"),
                              QStringLiteral("liesmich.txt")}));
        // Fehlender Ordner: nichts zu tun
        const LeftoverCleanup none = removeSaveFileLeftovers(dataDir() + QStringLiteral("/gibt-es-nicht"));
        QCOMPARE(none.count, 0);
    }

    // --- Teil 2: Kern als eigener Prozess ---

    // Der Kern räumt beim Start auf und schreibt Anzahl und Grösse ins Protokoll.
    void coreCleansAtStart()
    {
        const QString media = dataDir() + QStringLiteral("/media");
        QVERIFY(QDir().mkpath(media));
        const QString sha = sha256Hex(kImage);
        QVERIFY(QFile::copy(kImage, media + QLatin1Char('/') + sha + QStringLiteral(".png")));
        QFile rest(media + QLatin1Char('/') + sha + QStringLiteral(".png.KlMnOp"));
        QVERIFY(rest.open(QIODevice::WriteOnly));
        QVERIFY(rest.write(QByteArray(4096, 'y')) == 4096);
        rest.close();
        m_run = runtimeDir();
        m_socket = m_run->filePath(QStringLiteral("k.sock"));
        QVERIFY2(startCore(), qPrintable(m_coreLog));
        QVERIFY2(m_coreLog.contains(QStringLiteral(
                     "Aufräumen media/: 1 vorläufige Dateien von QSaveFile gelöscht (4096 Byte)")),
                 qPrintable(m_coreLog));
        QCOMPARE(mediaEntries(dataDir()), QStringList{sha + QStringLiteral(".png")});
        stopCore();
        // Zweiter Start: nichts mehr zu tun
        QVERIFY2(startCore(), qPrintable(m_coreLog));
        QVERIFY2(m_coreLog.contains(QStringLiteral(
                     "Aufräumen media/: 0 vorläufige Dateien von QSaveFile gelöscht (0 Byte)")),
                 qPrintable(m_coreLog));
    }

    // Während eine grosse Datei übernommen wird, antwortet der Kern und schickt Leistungswerte;
    // zwei Übernahmen gleichzeitig, davon eine mit derselben Datei.
    void respondsWhileImporting()
    {
        m_run = runtimeDir();
        m_socket = m_run->filePath(QStringLiteral("k.sock"));
        QVERIFY2(startCore(), qPrintable(m_coreLog));
        QVERIFY(connectClient());
        m_overlay = createOverlay(QStringLiteral("Übernehmen"));
        QVERIFY(!m_overlay.isNull());

        QSignalSpy results(m_client.get(), &CoreClient::changeFinished);
        QSignalSpy stats(m_client.get(), &CoreClient::statsReceived);
        QSignalSpy lists(m_client.get(), &CoreClient::overlayListChanged);
        ChangeRequest add = request(msg::kAddImageSource, m_overlay);
        add.file = m_big;
        QElapsedTimer t;
        t.start();
        const qint64 first = m_client->sendChange(add);
        const qint64 second = m_client->sendChange(add);
        add.file = kImage;
        const qint64 small = m_client->sendChange(add);
        // Zwei Threads: die beiden grossen laufen, die kleine wartet, bis einer frei ist.
        QList<qint64> rounds;
        while (!waitResult(&results, first, 0)) {
            QElapsedTimer r;
            r.start();
            m_client->requestOverlayList();
            // wait() kehrt zurück, sobald die Antwort da ist (overlay_list)
            QVERIFY(lists.wait(2000));
            rounds.append(r.elapsed());
            QTest::qWait(100);
            QVERIFY2(t.elapsed() < 60000, "Übernehmen dauert zu lange");
        }
        const qint64 importMs = t.elapsed();
        std::sort(rounds.begin(), rounds.end());
        QVERIFY2(rounds.size() >= 5, qPrintable(QStringLiteral("nur %1 Anfragen").arg(rounds.size())));
        qInfo("Übernehmen 256 MiB: %lld ms; %lld Anfragen get_overlay_list, Antwort min %lld / Median %lld / max %lld ms; %lld stats",
              static_cast<long long>(importMs), static_cast<long long>(rounds.size()),
              static_cast<long long>(rounds.first()), static_cast<long long>(rounds.at(rounds.size() / 2)),
              static_cast<long long>(rounds.last()), static_cast<long long>(stats.size()));
        QVERIFY2(rounds.last() < 200, qPrintable(QStringLiteral("Antwort nach %1 ms").arg(rounds.last())));
        // Leistungswerte laufen weiter: eine Meldung je Sekunde (mit Spielraum).
        QVERIFY2(stats.size() >= importMs / 1000 - 1,
                 qPrintable(QStringLiteral("%1 stats in %2 ms").arg(stats.size()).arg(importMs)));
        const std::optional<ChangeResult> r1 = waitResult(&results, first);
        const std::optional<ChangeResult> r2 = waitResult(&results, second);
        const std::optional<ChangeResult> r3 = waitResult(&results, small);
        QVERIFY(r1 && r1->ok && r2 && r2->ok && r3 && r3->ok);
        // Drei Quellen, zwei Kopien (die grosse nur einmal), Namen wie die Dateien
        const std::optional<Overlay> o = m_client->overlay(m_overlay);
        QVERIFY(o);
        QCOMPARE(o->sources.size(), 3);
        QCOMPARE(mediaEntries(dataDir()).size(), 2);
        int bigSources = 0;
        for (const Source &s : o->sources) {
            if (s.file == QStringLiteral("media/") + sha256Hex(m_big) + QStringLiteral(".png")) {
                ++bigSources;
                QCOMPARE(s.name, QStringLiteral("gross.png"));
            }
        }
        QCOMPARE(bigSources, 2);
        QVERIFY(waitForLog(m_core.get(), QStringLiteral("war schon vorhanden"), &m_coreLog));
    }

    // P10b, Punkt 73: Eine Datei in Übernahme erscheint mit Fortschritt in Prozent bei allen
    // Fenstern (höchstens alle 200 ms); Abbrechen: change_result «canceled», nichts bleibt in media/
    // (weder Kopie noch vorläufige Datei) und nichts in der Medienliste.
    void progressAndCancel()
    {
        m_run = runtimeDir();
        m_socket = m_run->filePath(QStringLiteral("k.sock"));
        QVERIFY2(startCore(), qPrintable(m_coreLog));
        QVERIFY(connectClient());
        ChangeRequest folder = request(msg::kCreateMediaFolder);
        folder.name = QStringLiteral("Neu");
        const ChangeResult created = change(folder);
        QVERIFY2(created.ok, qPrintable(created.message));

        QSignalSpy progress(m_client.get(), &CoreClient::importProgressChanged);
        QSignalSpy results(m_client.get(), &CoreClient::changeFinished);
        ChangeRequest add = request(msg::kAddMedia);
        add.file = m_big;
        add.folderId = created.id;
        QElapsedTimer t;
        t.start();
        const qint64 addId = m_client->sendChange(add);
        QList<int> percents;
        QList<qint64> times;
        qint64 importId = 0;
        while (percents.isEmpty() || percents.last() < 25) {
            QVERIFY2(progress.wait(3000), "kein Fortschritt");
            const QList<ImportProgressData> imports = m_client->imports();
            QVERIFY2(!waitResult(&results, addId, 0), "Übernehmen war schon fertig; Datei zu klein");
            if (imports.isEmpty())
                continue;
            QCOMPARE(imports.size(), 1);
            QCOMPARE(imports.first().name, QStringLiteral("gross.png"));
            QCOMPARE(imports.first().folder, created.id);
            importId = imports.first().importId;
            percents.append(imports.first().percent);
            times.append(t.elapsed());
        }
        qInfo("Fortschritt: %s nach %s ms",
              qPrintable(QStringList([&] { QStringList l; for (int p : percents) l.append(QString::number(p)); return l; }()).join(QLatin1Char(' '))),
              qPrintable(QStringList([&] { QStringList l; for (qint64 v : times) l.append(QString::number(v)); return l; }()).join(QLatin1Char(' '))));
        QVERIFY(std::is_sorted(percents.begin(), percents.end()));
        QVERIFY(percents.last() < 100);
        // Zwischen zwei Meldungen mindestens 200 ms (Zeitgeber), ausser der ersten beim Start
        for (qsizetype i = 2; i < times.size(); ++i)
            QVERIFY2(times.at(i) - times.at(i - 1) >= 150,
                     qPrintable(QStringLiteral("%1 ms").arg(times.at(i) - times.at(i - 1))));

        ChangeRequest cancel = request(msg::kCancelImport);
        cancel.importId = importId;
        const ChangeResult canceled = change(cancel);
        QVERIFY2(canceled.ok, qPrintable(canceled.message));
        const std::optional<ChangeResult> r = waitResult(&results, addId, 10000);
        QVERIFY(r);
        QVERIFY(!r->ok);
        QCOMPARE(r->code, QString::fromLatin1(err::kCanceled));
        QCOMPARE(r->message, QStringLiteral("Übernehmen von gross.png abgebrochen."));
        QTRY_VERIFY_WITH_TIMEOUT(m_client->imports().isEmpty(), 2000);
        QCOMPARE(mediaEntries(dataDir()), QStringList());
        QVERIFY(m_client->mediaList().files.isEmpty());
        QVERIFY(waitForLog(m_core.get(), QStringLiteral("Übernehmen von %1 abgebrochen nach").arg(m_big), &m_coreLog));
        // Noch einmal abbrechen: gibt es nicht mehr
        const ChangeResult again = change(cancel);
        QVERIFY(!again.ok);
        QVERIFY2(again.message.contains(QStringLiteral("Keine laufende Übernahme")), qPrintable(again.message));

        // Danach geht Übernehmen normal: kleine Datei in den Ordner, Fortschritt am Ende leer
        add.file = kImage;
        const ChangeResult small = change(add);
        QVERIFY2(small.ok, qPrintable(small.message));
        QTRY_COMPARE_WITH_TIMEOUT(m_client->mediaList().files.size(), qsizetype(1), 2000);
        QCOMPARE(m_client->mediaList().files.first().entry.name, QStringLiteral("bild1.png"));
        QCOMPARE(m_client->mediaList().files.first().entry.folder, created.id);
        QTRY_VERIFY_WITH_TIMEOUT(m_client->imports().isEmpty(), 2000);
        QCOMPARE(mediaEntries(dataDir()).size(), 1);
    }

    // P10b, Teil A 1: Hinzufügen ohne Quelle hält den ursprünglichen Dateinamen fest; dieselbe Datei
    // nochmals behält Name und Ordner (mit Hinweis). Die Quelle aus der Medienliste bekommt deren Namen.
    void addMediaKeepsName()
    {
        m_run = runtimeDir();
        m_socket = m_run->filePath(QStringLiteral("k.sock"));
        QVERIFY2(startCore(), qPrintable(m_coreLog));
        QVERIFY(connectClient());
        ChangeRequest add = request(msg::kAddMedia);
        add.file = kImage;
        QVERIFY(change(add).ok);
        QTRY_COMPARE_WITH_TIMEOUT(m_client->mediaList().files.size(), qsizetype(1), 2000);
        const QString file = m_client->mediaList().files.first().entry.file;
        ChangeRequest rename = request(msg::kRenameMedia);
        rename.file = file;
        rename.name = QStringLiteral("Logo");
        QVERIFY(change(rename).ok);
        const ChangeResult again = change(add);
        QVERIFY(again.ok);
        QCOMPARE(again.warning, QStringLiteral("bild1.png ist schon in der Medienliste (als «Logo»), Name und "
                                               "Ordner bleiben."));
        QCOMPARE(m_client->mediaList().files.size(), qsizetype(1));
        QCOMPARE(m_client->mediaList().files.first().entry.name, QStringLiteral("Logo"));
        // Unbekannte Endung: abgewiesen, ohne Übernehmen
        add.file = m_files.filePath(QStringLiteral("text.gif"));
        const ChangeResult gif = change(add);
        QVERIFY(!gif.ok);
        QVERIFY2(gif.message.contains(QStringLiteral("png, jpg, jpeg, webm, mp4, ttf, otf")), qPrintable(gif.message));
        // Quelle aus der Medienliste: Name aus der Liste, keine neue Kopie
        m_overlay = createOverlay(QStringLiteral("Liste"));
        ChangeRequest source = request(msg::kAddImageSource, m_overlay);
        source.file = file;
        const ChangeResult added = change(source);
        QVERIFY2(added.ok, qPrintable(added.message));
        QCOMPARE(m_client->overlay(m_overlay)->sources.first().name, QStringLiteral("Logo"));
        QCOMPARE(mediaEntries(dataDir()).size(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(m_client->mediaList().files.first().usedIn.size(), qsizetype(1), 2000);
        // Verwendet: Löschen abgewiesen mit Code in_use
        ChangeRequest remove = request(msg::kRemoveMedia);
        remove.file = file;
        const ChangeResult refused = change(remove);
        QVERIFY(!refused.ok);
        QCOMPARE(refused.code, QString::fromLatin1(err::kInUse));
        QCOMPARE(refused.message, QStringLiteral("«Logo» lässt sich nicht löschen, verwendet in Overlay «Liste» "
                                                 "(Quelle «Logo»)."));
        QCOMPARE(mediaEntries(dataDir()).size(), 1);
    }

    // Overlay bzw. Quelle während des Übernehmens gelöscht: Die Änderung wird abgewiesen, die
    // Kopie bleibt in media/ (ungenutzte Kopien werden nicht gelöscht, Punkt 24).
    void removedWhileImporting()
    {
        m_run = runtimeDir();
        m_socket = m_run->filePath(QStringLiteral("k.sock"));
        QVERIFY2(startCore(), qPrintable(m_coreLog));
        QVERIFY(connectClient());
        const QUuid gone = createOverlay(QStringLiteral("Wird gelöscht"));
        const QUuid stays = createOverlay(QStringLiteral("Bleibt"));
        ChangeRequest addSmall = request(msg::kAddImageSource, stays);
        addSmall.file = kImage;
        const ChangeResult added = change(addSmall);
        QVERIFY(added.ok);

        QSignalSpy results(m_client.get(), &CoreClient::changeFinished);
        ChangeRequest add = request(msg::kAddImageSource, gone);
        add.file = m_big;
        const qint64 addId = m_client->sendChange(add);
        ChangeRequest replace = request(msg::kReplaceImageFile, stays);
        replace.sourceId = added.id;
        replace.file = m_big2;
        const qint64 replaceId = m_client->sendChange(replace);
        // Während beide laufen: Overlay und Quelle löschen (in der Hauptschleife sofort)
        QTest::qWait(200);
        ChangeRequest removeOverlay = request(msg::kRemoveOverlay, gone);
        QVERIFY(change(removeOverlay).ok);
        ChangeRequest removeSource = request(msg::kRemoveSource, stays);
        removeSource.sourceId = added.id;
        QVERIFY(change(removeSource).ok);
        QVERIFY2(!waitResult(&results, addId, 0), "Übernehmen war schon fertig; Datei zu klein");

        const std::optional<ChangeResult> r1 = waitResult(&results, addId);
        const std::optional<ChangeResult> r2 = waitResult(&results, replaceId);
        QVERIFY(r1 && r2);
        QVERIFY(!r1->ok);
        QCOMPARE(r1->code, QString::fromLatin1(err::kUnknownOverlay));
        QVERIFY(!r2->ok);
        QCOMPARE(r2->code, QString::fromLatin1(err::kUnknownSource));
        QVERIFY(!m_client->overlay(gone));
        QCOMPARE(m_client->overlay(stays)->sources.size(), 0);
        // bild1.png und die zwei grossen Kopien
        QCOMPARE(mediaEntries(dataDir()).size(), 3);
    }

    // SIGTERM während des Übernehmens: sauberes Ende (Exit-Code 0), nichts unter dem endgültigen
    // Namen, keine vorläufige Datei; Zeit bis zum Ende.
    void sigtermWhileImporting_data()
    {
        QTest::addColumn<int>("delayMs");
        // Beim Hashen (vor der Kopie) und beim Kopieren
        QTest::newRow("beim Hashen") << 300;
        QTest::newRow("beim Kopieren") << 2500;
    }

    void sigtermWhileImporting()
    {
        QFETCH(int, delayMs);
        m_run = runtimeDir();
        m_socket = m_run->filePath(QStringLiteral("k.sock"));
        QVERIFY2(startCore(), qPrintable(m_coreLog));
        QVERIFY(connectClient());
        m_overlay = createOverlay(QStringLiteral("Abbruch"));
        QSignalSpy results(m_client.get(), &CoreClient::changeFinished);
        ChangeRequest add = request(msg::kAddImageSource, m_overlay);
        add.file = m_big;
        const qint64 a = m_client->sendChange(add);
        add.file = m_big2;
        const qint64 b = m_client->sendChange(add);
        QTest::qWait(delayMs);
        QVERIFY2(!waitResult(&results, a, 0) && !waitResult(&results, b, 0),
                 "Übernehmen war schon fertig; Datei zu klein");
        QElapsedTimer t;
        t.start();
        QCOMPARE(::kill(pid_t(m_core->processId()), SIGTERM), 0);
        QVERIFY(m_core->waitForFinished(10000));
        const qint64 ms = t.elapsed();
        m_coreLog += QString::fromUtf8(m_core->readAllStandardError());
        qInfo("SIGTERM nach %d ms Übernehmen: Ende nach %lld ms", delayMs, static_cast<long long>(ms));
        QCOMPARE(m_core->exitStatus(), QProcess::NormalExit);
        QCOMPARE(m_core->exitCode(), 0);
        QVERIFY2(m_coreLog.contains(QStringLiteral("2 Übernahmen abgebrochen")), qPrintable(m_coreLog));
        QVERIFY(ms < 2000);
        QCOMPARE(mediaEntries(dataDir()), QStringList());
    }
};

QTEST_GUILESS_MAIN(TestUebernehmen)
#include "tst_uebernehmen.moc"
