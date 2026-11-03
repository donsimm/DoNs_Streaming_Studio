// Prüft das Prüfen und Umwandeln der Datenformate ohne Qt Gui (P10a, Teil B 4), mit den
// Testdaten tests/daten/formate (Overlay-Dateien im Format 1, 2 und 3) in einer Kopie je Fall,
// und die Optionen --format-pruefen und --umwandeln von dss-core.

#include "dataformat.h"
#include "dataformattesting.h"
#include "datalock.h"
#include "overlayjson.h"
#include "overlaystore.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

using namespace dss;

namespace {

const QString kSource = QStringLiteral(DSS_TEST_FORMAT_DIR);
const QString kFormatOne = QStringLiteral("overlays/f5bb9bc6-06f4-4762-a15b-47c37e24f82c.json");
const QString kFormatTwo = QStringLiteral("overlays/cbf17458-229b-4bd1-8449-f5548a069096.json");
const QString kFormatThree = QStringLiteral("overlays/f3ab0d90-c6d2-40d3-87a3-4251c27d5d51.json");
// P10: Format 4 mit Ausgabe aus (E-24), schon aktuell
const QString kFormatFour = QStringLiteral("overlays/02b9d140-0573-4e87-9958-0a74ee955363.json");

bool copyTree(const QString &from, const QString &to)
{
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString target = to + QLatin1Char('/') + QDir(from).relativeFilePath(file);
        if (!QDir().mkpath(QFileInfo(target).path()) || !QFile::copy(file, target))
            return false;
    }
    return true;
}

// Prüfsumme jeder Datei im Ordner (ohne Sperrdatei)
QMap<QString, QByteArray> snapshot(const QString &dir)
{
    QMap<QString, QByteArray> result;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        if (file.endsWith(QStringLiteral(".lock")))
            continue;
        QFile f(file);
        if (f.open(QIODevice::ReadOnly))
            result.insert(QDir(dir).relativeFilePath(file),
                          QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256));
    }
    return result;
}

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Inhalt jeder Datei im Ordner Byte für Byte (ohne Sperrdatei)
QMap<QString, QByteArray> contents(const QString &dir)
{
    QMap<QString, QByteArray> result;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        if (!file.endsWith(QStringLiteral(".lock")))
            result.insert(QDir(dir).relativeFilePath(file), readAll(file));
    }
    return result;
}

ino_t inode(const QString &path)
{
    struct stat st {};
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? st.st_ino : 0;
}

int formatVersion(const QString &path)
{
    return QJsonDocument::fromJson(readAll(path)).object().value(QStringLiteral("format_version")).toInt();
}

struct Run
{
    int exitCode = -1;
    QString out;
    QString err;
};

Run runCore(const QStringList &args)
{
    QProcess p;
    p.start(QStringLiteral(DSS_CORE_BIN), args);
    Run r;
    if (!p.waitForFinished(30000))
        return r;
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.out = QString::fromUtf8(p.readAllStandardOutput());
    r.err = QString::fromUtf8(p.readAllStandardError());
    return r;
}

} // namespace

class TestDataFormat : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<QTemporaryDir> m_temp;
    QString dataDir() const { return m_temp->path() + QStringLiteral("/daten"); }

private slots:
    void init()
    {
        m_temp = std::make_unique<QTemporaryDir>();
        QVERIFY(m_temp->isValid());
        QVERIFY(copyTree(kSource, dataDir()));
        QFile::remove(dataDir() + QStringLiteral("/LIESMICH.md"));
        // git speichert die Rechte 0600 von tokens.json nicht (frischer Klon: 0644)
        QVERIFY(QFile::setPermissions(dataDir() + QStringLiteral("/tokens.json"),
                                      QFile::ReadOwner | QFile::WriteOwner));
    }

    void cleanup() { m_temp.reset(); }

    void checkFindsOlderFiles()
    {
        const DataFormatStatus s = checkDataFormat(dataDir());
        QVERIFY(s.error.isEmpty());
        QCOMPARE(s.older, (QStringList{kFormatTwo + QStringLiteral(" (Format 2)"),
                                       kFormatThree + QStringLiteral(" (Format 3)"),
                                       kFormatOne + QStringLiteral(" (Format 1)")}));
        QVERIFY(s.newer.isEmpty());
        QVERIFY(s.unreadable.isEmpty());
        QVERIFY(s.needsConversion());
    }

    void convertKeepsContent()
    {
        // Was das Programm aus den alten Dateien liest, muss nach dem Umwandeln gleich sein.
        QMap<QString, QByteArray> before;
        for (const QString &f : {kFormatOne, kFormatTwo, kFormatThree, kFormatFour}) {
            Overlay o;
            QString error;
            QVERIFY2(parseOverlay(readAll(dataDir() + QLatin1Char('/') + f), &o, &error), qPrintable(error));
            before.insert(f, serializeOverlay(o));
        }
        const QMap<QString, QByteArray> filesBefore = snapshot(dataDir());

        QStringList converted;
        QString error;
        QVERIFY2(convertDataFormat(dataDir(), &converted, &error), qPrintable(error));
        QCOMPARE(converted.size(), 3);
        for (const QString &f : {kFormatOne, kFormatTwo, kFormatThree, kFormatFour}) {
            QCOMPARE(readAll(dataDir() + QLatin1Char('/') + f), before.value(f));
            QCOMPARE(formatVersion(dataDir() + QLatin1Char('/') + f), 4);
        }
        // Ausgabe (P10): ältere Formate ein, Format 4 wie gespeichert (aus)
        for (const QString &f : {kFormatOne, kFormatTwo, kFormatThree, kFormatFour}) {
            const QJsonObject o = QJsonDocument::fromJson(readAll(dataDir() + QLatin1Char('/') + f)).object();
            QCOMPARE(o.value(QStringLiteral("output")), QJsonValue(f != kFormatFour));
        }
        // Alles andere unverändert: Format 4, Liste, Token, Medien
        const QMap<QString, QByteArray> filesAfter = snapshot(dataDir());
        QCOMPARE(filesAfter.keys(), filesBefore.keys());
        for (const QString &f : filesBefore.keys()) {
            if (f != kFormatOne && f != kFormatTwo && f != kFormatThree)
                QVERIFY2(filesAfter.value(f) == filesBefore.value(f), qPrintable(f));
        }
        QVERIFY(!checkDataFormat(dataDir()).needsConversion());

        // Das Programm lädt alles ohne Fehler (nur die absichtlich fehlende Schrift als Warnung,
        // je eine in format-drei und format-vier)
        OverlayStore store(dataDir());
        QVERIFY2(store.load(&error, OverlayStore::LoadMode::ReadOnly), qPrintable(error));
        QCOMPARE(store.overlays().size(), 4);
        QVERIFY2(store.loadErrors().isEmpty(), qPrintable(store.loadErrors().join(QLatin1Char('\n'))));
        QCOMPARE(store.loadWarnings().size(), 2);

        // Ein zweites Mal: nichts zu tun
        QVERIFY(convertDataFormat(dataDir(), &converted, &error));
        QVERIFY(converted.isEmpty());
        QCOMPARE(snapshot(dataDir()), filesAfter);
    }

    void newerFormatChangesNothing()
    {
        QString path = dataDir() + QLatin1Char('/') + kFormatFour;
        QByteArray data = readAll(path);
        data.replace("\"format_version\": 4", "\"format_version\": 5");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(data) == data.size());
        f.close();
        const DataFormatStatus s = checkDataFormat(dataDir());
        QCOMPARE(s.newer, QStringList{kFormatFour + QStringLiteral(" (Format 5)")});
        const QMap<QString, QByteArray> before = snapshot(dataDir());
        QStringList converted;
        QString error;
        QVERIFY(!convertDataFormat(dataDir(), &converted, &error));
        QVERIFY2(error.contains(QStringLiteral("neueres Format")), qPrintable(error));
        QVERIFY(converted.isEmpty());
        QCOMPARE(snapshot(dataDir()), before);
    }

    void lockedChangesNothing()
    {
        DataLock lock(dataDir());
        QString error;
        QVERIFY(lock.tryLock(&error));
        const QMap<QString, QByteArray> before = snapshot(dataDir());
        QStringList converted;
        QVERIFY(!convertDataFormat(dataDir(), &converted, &error));
        QVERIFY2(error.startsWith(QStringLiteral("Datenordner gesperrt")), qPrintable(error));
        QCOMPARE(snapshot(dataDir()), before);
    }

    void unreadableFileStays()
    {
        const QString broken = dataDir() + QStringLiteral("/overlays/00000000-0000-0000-0000-000000000001.json");
        QFile f(broken);
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write("{ kaputt") > 0);
        f.close();
        const DataFormatStatus s = checkDataFormat(dataDir());
        QCOMPARE(s.unreadable.size(), 1);
        QVERIFY(s.unreadable.first().startsWith(QStringLiteral("overlays/00000000-")));
        QStringList converted;
        QString error;
        QVERIFY2(convertDataFormat(dataDir(), &converted, &error), qPrintable(error));
        QCOMPARE(converted.size(), 3);
        QCOMPARE(readAll(broken), QByteArray("{ kaputt"));
    }

    // P10-vor, Punkt 70: Scheitert das Schreiben der n-ten Datei, sind danach alle Dateien
    // Byte für Byte wie vorher (die schon geschriebenen zurückgeschrieben).
    void failedWriteRestoresFiles_data()
    {
        QTest::addColumn<int>("failAt");
        QTest::newRow("erste Datei") << 1;
        QTest::newRow("zweite Datei") << 2;
    }

    void failedWriteRestoresFiles()
    {
        QFETCH(int, failAt);
        const QMap<QString, QByteArray> before = contents(dataDir());
        QCOMPARE(checkDataFormat(dataDir()).older.size(), 3);
        // Erste umzuwandelnde Datei (older ist nach Name sortiert: Format 2 vor Format 1)
        const QString first = dataDir() + QLatin1Char('/') + kFormatTwo;
        const ino_t firstInode = inode(first);
        testing::setConvertFailWriteAt(failAt);
        QStringList converted;
        QString error;
        const bool ok = convertDataFormat(dataDir(), &converted, &error);
        testing::setConvertFailWriteAt(0);
        QVERIFY(!ok);
        QVERIFY2(error.contains(QStringLiteral("Testfehler")), qPrintable(error));
        QVERIFY2(!error.contains(QStringLiteral("nicht zurückgeschrieben")), qPrintable(error));
        QVERIFY(converted.isEmpty());
        const QMap<QString, QByteArray> after = contents(dataDir());
        QCOMPARE(after.keys(), before.keys());
        for (auto it = before.cbegin(); it != before.cend(); ++it)
            QVERIFY2(after.value(it.key()) == it.value(), qPrintable(it.key()));
        // Beim Fehler an der zweiten Datei wurde die erste geschrieben und zurückgeschrieben
        // (QSaveFile benennt um: neue Inode), an der ersten gar nichts.
        if (failAt == 2)
            QVERIFY(inode(first) != firstInode);
        else
            QCOMPARE(inode(first), firstInode);
        // Gegenprobe: Beim zweiten Mal (ohne Testfehler) wurde die erste Datei wirklich
        // geschrieben und danach zurückgeschrieben; ohne Testmöglichkeit gelingt das Umwandeln.
        QVERIFY2(convertDataFormat(dataDir(), &converted, &error), qPrintable(error));
        QCOMPARE(converted.size(), 3);
        QVERIFY(contents(dataDir()) != before);
    }

    void missingDataDir()
    {
        const DataFormatStatus s = checkDataFormat(m_temp->path() + QStringLiteral("/fehlt"));
        QVERIFY(s.error.contains(QStringLiteral("fehlt")));
        QStringList converted;
        QString error;
        QVERIFY(!convertDataFormat(m_temp->path() + QStringLiteral("/fehlt"), &converted, &error));
        QVERIFY(!QDir(m_temp->path() + QStringLiteral("/fehlt")).exists());
    }

    // Die Optionen von dss-core, wie scripts/installieren.sh sie aufruft
    void coreOptions()
    {
        const QStringList dir{QStringLiteral("--datenordner"), dataDir()};
        Run r = runCore(dir + QStringList{QStringLiteral("--format-pruefen")});
        QCOMPARE(r.exitCode, 4);
        QVERIFY2(r.out.contains(kFormatOne + QStringLiteral(" (Format 1)")), qPrintable(r.out));

        {
            DataLock lock(dataDir());
            QString error;
            QVERIFY(lock.tryLock(&error));
            r = runCore(dir + QStringList{QStringLiteral("--umwandeln")});
            QCOMPARE(r.exitCode, 3);
            QVERIFY2(r.err.contains(QStringLiteral("Dateien unverändert")), qPrintable(r.err));
        }
        r = runCore(dir + QStringList{QStringLiteral("--umwandeln")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(r.out.contains(QStringLiteral("3 Dateien umgewandelt")), qPrintable(r.out));
        r = runCore(dir + QStringList{QStringLiteral("--format-pruefen")});
        QCOMPARE(r.exitCode, 0);
        QVERIFY2(r.out.startsWith(QStringLiteral("Format aktuell")), qPrintable(r.out));

        const QString path = dataDir() + QLatin1Char('/') + kFormatFour;
        QByteArray data = readAll(path);
        data.replace("\"format_version\": 4", "\"format_version\": 9");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly) && f.write(data) == data.size());
        f.close();
        r = runCore(dir + QStringList{QStringLiteral("--format-pruefen")});
        QCOMPARE(r.exitCode, 5);
        QVERIFY2(r.err.contains(kFormatFour + QStringLiteral(" (Format 9)")), qPrintable(r.err));

        r = runCore({QStringLiteral("--datenordner"), m_temp->path() + QStringLiteral("/fehlt"),
                     QStringLiteral("--format-pruefen")});
        QCOMPARE(r.exitCode, 1);
    }
};

QTEST_GUILESS_MAIN(TestDataFormat)
#include "tst_datenformat.moc"
