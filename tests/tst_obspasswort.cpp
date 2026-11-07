// Passwort von OBS verschlüsselt ablegen (P10c, Regel 12): mit dem echten systemd-creds --user, nur
// mit zufälligen Testwerten, und mit einem Ersatzprogramm für Fehlerfälle und die Befehlszeile.

#include "obssecretstore.h"

#include <QFile>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using dss::obs::ObsSecretStore;

namespace {

QString randomPassword()
{
    return QStringLiteral("test-ä-") + QString::number(QRandomGenerator::system()->generate64(), 36);
}

constexpr int kWait = 15000;

QByteArray readAll(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// Ersatz für systemd-creds: schreibt Befehlszeile und stdin in Dateien neben sich.
QString fakeProgram(const QString &dir, const QString &outputLine, int exitCode)
{
    const QString path = dir + QStringLiteral("/ersatz.sh");
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return {};
    f.write(QStringLiteral("#!/bin/sh\necho \"$@\" > '%1/befehlszeile'\ncat > '%1/eingabe'\n"
                           "printf '%2'\nexit %3\n").arg(dir, outputLine).arg(exitCode).toUtf8());
    f.close();
    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    return path;
}

} // namespace

class TestObsSecretStore : public QObject
{
    Q_OBJECT

private slots:
    void realToolRoundTrip()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("systemd-creds")).isEmpty())
            QSKIP("systemd-creds fehlt");
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/unterordner/obs-passwort.cred");
        ObsSecretStore store(path);
        QVERIFY(!store.isSet());
        const QString password = randomPassword();
        std::optional<QString> writeResult;
        store.write(password, [&writeResult](const QString &error) { writeResult = error; });
        QTRY_VERIFY_WITH_TIMEOUT(writeResult.has_value(), kWait);
        QVERIFY2(writeResult->isEmpty(), qPrintable(*writeResult));
        QVERIFY(store.isSet());
        // Kein Klartext in der Datei, Rechte 0600
        const QByteArray content = readAll(path);
        QVERIFY(!content.isEmpty());
        QVERIFY(!content.contains(password.toUtf8()));
        QCOMPARE(QFile::permissions(path) & (QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                             | QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
                                             | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther),
                 QFile::ReadOwner | QFile::WriteOwner);
        // Ein neuer Speicher liest denselben Wert (Neustart des Kerns).
        ObsSecretStore again(path);
        std::optional<std::optional<QString>> readResult;
        again.read([&readResult](const std::optional<QString> &value) { readResult = value; });
        QTRY_VERIFY_WITH_TIMEOUT(readResult.has_value(), kWait);
        QVERIFY(readResult->has_value());
        QCOMPARE(**readResult, password);
        // Löschen
        QString error;
        QVERIFY(again.remove(&error));
        QVERIFY(!again.isSet());
        QVERIFY(again.remove(&error)); // schon weg: kein Fehler
    }

    void realToolRejectsDamagedFile()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("systemd-creds")).isEmpty())
            QSKIP("systemd-creds fehlt");
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/obs-passwort.cred");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("das ist kein verschluesselter Inhalt");
        f.close();
        ObsSecretStore store(path);
        std::optional<std::optional<QString>> readResult;
        store.read([&readResult](const std::optional<QString> &value) { readResult = value; });
        QTRY_VERIFY_WITH_TIMEOUT(readResult.has_value(), kWait);
        QVERIFY(!readResult->has_value());
    }

    void realToolJobsRunInOrder()
    {
        if (QStandardPaths::findExecutable(QStringLiteral("systemd-creds")).isEmpty())
            QSKIP("systemd-creds fehlt");
        QTemporaryDir dir;
        ObsSecretStore store(dir.path() + QStringLiteral("/obs-passwort.cred"));
        const QString first = randomPassword();
        const QString second = randomPassword();
        int written = 0;
        std::optional<QString> read;
        const auto onWritten = [&written](const QString &error) {
            QVERIFY2(error.isEmpty(), qPrintable(error));
            ++written;
        };
        store.write(first, onWritten);
        store.write(second, onWritten);
        store.read([&read](const std::optional<QString> &value) { read = value; });
        QTRY_VERIFY_WITH_TIMEOUT(read.has_value(), kWait);
        QCOMPARE(written, 2);
        QCOMPARE(*read, second);
    }

    void withoutFileReadGivesEmpty()
    {
        QTemporaryDir dir;
        ObsSecretStore store(dir.path() + QStringLiteral("/fehlt.cred"), QStringLiteral("/gibt/es/nicht"));
        std::optional<std::optional<QString>> result;
        store.read([&result](const std::optional<QString> &value) { result = value; });
        QVERIFY(result.has_value());
        QVERIFY(result->has_value());
        QVERIFY((*result)->isEmpty());
    }

    void missingProgramFails()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/obs-passwort.cred");
        ObsSecretStore store(path, QStringLiteral("/gibt/es/nicht"));
        std::optional<QString> writeResult;
        store.write(randomPassword(), [&writeResult](const QString &error) { writeResult = error; });
        QTRY_VERIFY_WITH_TIMEOUT(writeResult.has_value(), kWait);
        QVERIFY(!writeResult->isEmpty());
        QVERIFY(!QFile::exists(path));
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        std::optional<std::optional<QString>> readResult;
        store.read([&readResult](const std::optional<QString> &value) { readResult = value; });
        QTRY_VERIFY_WITH_TIMEOUT(readResult.has_value(), kWait);
        QVERIFY(!readResult->has_value());
    }

    void failureKeepsOldFile()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/obs-passwort.cred");
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("alt");
        f.close();
        ObsSecretStore store(path, fakeProgram(dir.path(), QStringLiteral("neu"), 1));
        std::optional<QString> result;
        store.write(randomPassword(), [&result](const QString &error) { result = error; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), kWait);
        QVERIFY(!result->isEmpty());
        QCOMPARE(readAll(path), QByteArray("alt"));
    }

    void passwordOnlyOnStdin()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/obs-passwort.cred");
        ObsSecretStore store(path, fakeProgram(dir.path(), QStringLiteral("verschluesselt"), 0));
        const QString password = randomPassword();
        std::optional<QString> result;
        store.write(password, [&result](const QString &error) { result = error; });
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), kWait);
        QVERIFY2(result->isEmpty(), qPrintable(*result));
        const QString commandLine = QString::fromUtf8(readAll(dir.path() + QStringLiteral("/befehlszeile")));
        QVERIFY(commandLine.contains(QStringLiteral("--user")));
        QVERIFY(commandLine.contains(QStringLiteral("encrypt - -")));
        QVERIFY(!commandLine.contains(password));
        QCOMPARE(QString::fromUtf8(readAll(dir.path() + QStringLiteral("/eingabe"))), password);
        QCOMPARE(readAll(path), QByteArray("verschluesselt"));
    }
};

QTEST_GUILESS_MAIN(TestObsSecretStore)
#include "tst_obspasswort.moc"
