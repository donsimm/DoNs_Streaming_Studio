// Prüft die Sperre im Datenordner ohne Qt Gui (P07, Teil C, Entscheid 2): Solange ein Kern
// läuft, startet kein zweiter am selben Datenordner, und dss-cli ändert nichts. Startet
// dss-core und dss-cli als eigene Prozesse mit eigenen Ordnern.

#include "datalock.h"
#include "overlayjson.h"
#include "overlaystore.h"
#include "prozesshilfe.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>
#include <sys/types.h>

using namespace dss;
using namespace testhelp;

namespace {

constexpr int kExitAlreadyRunning = 3;

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

} // namespace

class TestSperre : public QObject
{
    Q_OBJECT

private slots:
    void secondLockInSameProcessFails()
    {
        QTemporaryDir dir;
        DataLock a(dir.path());
        QString error;
        QVERIFY2(a.tryLock(&error), qPrintable(error));
        QVERIFY(QFileInfo::exists(lockFilePath(dir.path())));
        DataLock b(dir.path());
        qint64 pid = 0;
        QVERIFY(!b.tryLock(&error, &pid));
        QCOMPARE(pid, QCoreApplication::applicationPid());
        QVERIFY(error.contains(QStringLiteral("gesperrt")));
        a.unlock();
        QVERIFY(!QFileInfo::exists(lockFilePath(dir.path())));
        QVERIFY(b.tryLock(&error));
    }

    void lockCreatesDataDir()
    {
        QTemporaryDir dir;
        const QString data = dir.path() + QStringLiteral("/neu/daten");
        DataLock a(data);
        QString error;
        QVERIFY(a.tryLock(&error));
        QVERIFY(QFileInfo(data).isDir());
    }

    void oldLockOfRunningProcessStaysValid()
    {
        // Der Kern läuft lange: Eine Sperre bleibt gültig, auch wenn die Datei alt ist
        // (staleLockTime 0, https://doc.qt.io/qt-6.8/qlockfile.html#setStaleLockTime).
        QTemporaryDir dir;
        DataLock a(dir.path());
        QString error;
        QVERIFY(a.tryLock(&error));
        QFile lockFile(lockFilePath(dir.path()));
        QVERIFY(lockFile.open(QIODevice::ReadWrite));
        QVERIFY(lockFile.setFileTime(QDateTime::currentDateTime().addSecs(-7200),
                                     QFileDevice::FileModificationTime));
        lockFile.close();
        DataLock b(dir.path());
        QVERIFY(!b.tryLock(&error));
    }

    void readOnlyLoadWritesNothing()
    {
        // dss-cli bei laufendem Kern: Laden ohne Ordner anzulegen, ohne Token zu erzeugen und
        // ohne Dateirechte zu ändern.
        QTemporaryDir dir;
        const QString id = QStringLiteral("db7f47a9-dee3-4de5-b056-40b5b3507607");
        QVERIFY(QDir().mkpath(dir.path() + QStringLiteral("/overlays")));
        Overlay o;
        o.id = QUuid::fromString(id);
        o.name = QStringLiteral("Ohne Token");
        o.address = QStringLiteral("ohne-token");
        QFile f(dir.path() + QStringLiteral("/overlays/") + id + QStringLiteral(".json"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(serializeOverlay(o));
        f.close();
        QFile tokens(dir.path() + QStringLiteral("/tokens.json"));
        QVERIFY(tokens.open(QIODevice::WriteOnly));
        tokens.write("{\"format_version\": 1, \"tokens\": {}}");
        tokens.close();
        QVERIFY(tokens.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup
                                      | QFile::ReadOther)); // 0644
        const QByteArray before = readFile(tokens.fileName());
        const QFile::Permissions permsBefore = tokens.permissions();

        OverlayStore store(dir.path());
        QString error;
        QVERIFY2(store.load(&error, OverlayStore::LoadMode::ReadOnly), qPrintable(error));
        QCOMPARE(store.overlays().size(), 1);
        QVERIFY(store.loadWarnings().join(QLatin1Char('\n')).contains(QStringLiteral("nur gelesen")));
        QCOMPARE(readFile(tokens.fileName()), before);
        QCOMPARE(QFile(tokens.fileName()).permissions(), permsBefore);
        QVERIFY(!QFileInfo::exists(dir.path() + QStringLiteral("/media")));
    }

    void secondCoreDoesNotStart()
    {
        QTemporaryDir data;
        auto run = runtimeDir();
        QVERIFY(run->isValid());
        auto first = startCore(data.path(), run->filePath(QStringLiteral("a.sock")));
        QString log;
        QVERIFY2(waitForLog(first.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));

        // Gleicher Datenordner, andere Verbindung: Sperre belegt.
        auto second = startCore(data.path(), run->filePath(QStringLiteral("b.sock")));
        QVERIFY(second->waitForFinished(5000));
        QCOMPARE(second->exitCode(), kExitAlreadyRunning);
        QVERIFY(QString::fromUtf8(second->readAllStandardError()).contains(QStringLiteral("gesperrt")));

        // Anderer Datenordner, gleiche Verbindung: Socket belegt.
        QTemporaryDir otherData;
        auto third = startCore(otherData.path(), run->filePath(QStringLiteral("a.sock")));
        QVERIFY(third->waitForFinished(5000));
        QCOMPARE(third->exitCode(), kExitAlreadyRunning);
        QVERIFY(QString::fromUtf8(third->readAllStandardError())
                    .contains(QStringLiteral("antwortet schon ein Kern")));

        first->terminate(); // SIGTERM
        QVERIFY(first->waitForFinished(5000));
        QCOMPARE(first->exitCode(), 0);
        QVERIFY(!QFileInfo::exists(lockFilePath(data.path())));
    }

    void socketPathTooLong()
    {
        QTemporaryDir data;
        auto core = startCore(data.path(), QStringLiteral("/tmp/") + QString(110, QLatin1Char('x')));
        QVERIFY(core->waitForFinished(5000));
        QCOMPARE(core->exitCode(), 1);
        QVERIFY(QString::fromUtf8(core->readAllStandardError()).contains(QStringLiteral("zu lang")));
    }

    void killedCoreLeavesNoLock()
    {
        // Nach kill -9 bleibt die Sperrdatei liegen, gilt aber als verwaist (Prozess läuft
        // nicht mehr): Ein neuer Kern startet (so wie nach dem Neustart durch systemd).
        QTemporaryDir data;
        auto run = runtimeDir();
        auto core = startCore(data.path(), run->filePath(QStringLiteral("k.sock")));
        QString log;
        QVERIFY(waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log));
        core->kill(); // SIGKILL
        QVERIFY(core->waitForFinished(5000));
        QVERIFY(QFileInfo::exists(lockFilePath(data.path())));
        QVERIFY(QFileInfo::exists(run->filePath(QStringLiteral("k.sock")))); // Socket bleibt liegen
        auto again = startCore(data.path(), run->filePath(QStringLiteral("k.sock")));
        log.clear();
        QVERIFY2(waitForLog(again.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        again->terminate();
        QVERIFY(again->waitForFinished(5000));
        QCOMPARE(again->exitCode(), 0);
    }

#ifdef DSS_CLI_BIN
    void cliOnlyReadsWhileCoreRuns()
    {
        QTemporaryDir data;
        QCOMPARE(runCli(data.path(), {QStringLiteral("overlay-neu"), QStringLiteral("Vorher"),
                                      QStringLiteral("1280"), QStringLiteral("720")}).exitCode, 0);
        auto run = runtimeDir();
        auto core = startCore(data.path(), run->filePath(QStringLiteral("c.sock")));
        QString log;
        QVERIFY(waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log));
        const QStringList filesBefore =
            QDir(data.path() + QStringLiteral("/overlays")).entryList(QDir::Files);
        const QByteArray tokensBefore = readFile(data.path() + QStringLiteral("/tokens.json"));

        // Anzeigen geht.
        CliResult r = runCli(data.path(), {QStringLiteral("overlay-liste")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY(r.out.contains(QStringLiteral("Vorher")));
        QCOMPARE(runCli(data.path(), {QStringLiteral("quelle-liste"), QStringLiteral("vorher")}).exitCode, 0);

        // Ändern bricht ab, mit Hinweis zum Anhalten.
        for (const QStringList &args : {QStringList{QStringLiteral("overlay-neu"), QStringLiteral("Neu"),
                                                    QStringLiteral("10"), QStringLiteral("10")},
                                        QStringList{QStringLiteral("overlay-loeschen"), QStringLiteral("vorher")},
                                        QStringList{QStringLiteral("overlay-umbenennen"), QStringLiteral("vorher"),
                                                    QStringLiteral("X")}}) {
            r = runCli(data.path(), args);
            QCOMPARE(r.exitCode, 3);
            QVERIFY2(r.err.contains(QStringLiteral("systemctl --user stop dss-core-entwicklung.service")),
                     qPrintable(r.err));
            QVERIFY(r.err.contains(QStringLiteral("gesperrt von dss-core")));
        }
        QCOMPARE(QDir(data.path() + QStringLiteral("/overlays")).entryList(QDir::Files), filesBefore);
        QCOMPARE(readFile(data.path() + QStringLiteral("/tokens.json")), tokensBefore);

        core->terminate();
        QVERIFY(core->waitForFinished(5000));
        // Kern angehalten: Ändern geht wieder.
        r = runCli(data.path(), {QStringLiteral("overlay-neu"), QStringLiteral("Neu"),
                                 QStringLiteral("10"), QStringLiteral("10")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY(!QFileInfo::exists(lockFilePath(data.path())));
    }
#endif
};

QTEST_GUILESS_MAIN(TestSperre)
#include "tst_sperre.moc"
