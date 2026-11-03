// Prüft die Einstellungen des Kerns (P10-vor, Teil A 3) ohne Qt Gui: Standardwerte je Version,
// Prüfung der Werte, Lesen und Schreiben von kern.ini, atomares Schreiben und die Adresse im
// Heimnetz aus der Standardroute. Testdaten: tests/daten/proc/heimnetz/net/route ist eine Kopie
// von /proc/net/route des Studio-PCs vom 2026-11-03 20:52:31 (enp3s0, 192.168.1.9);
// messung1 (enx2aea2d88f6c1) und ohne-route wie in tst_systemstats (CLAUDE.md Regel 7).

#include "coresettings.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <sys/stat.h>

using namespace dss;

namespace {

const QString kData = QStringLiteral(DSS_TEST_PROC_DIR);

QByteArray readFile(const QString &path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

ino_t inode(const QString &path)
{
    struct stat st{};
    return ::stat(QFile::encodeName(path).constData(), &st) == 0 ? st.st_ino : 0;
}

// Adressen wie auf dem Studio-PC am 2026-11-03 (ip -4 addr, ip -6 addr): enp3s0 mit IPv6
// link-local und 192.168.1.9; lo mit 127.0.0.1.
QList<QHostAddress> studioAddresses(const QString &iface)
{
    if (iface == QStringLiteral("enp3s0"))
        return {QHostAddress(QStringLiteral("fe80::1")), QHostAddress(QStringLiteral("192.168.1.9"))};
    if (iface == QStringLiteral("enx2aea2d88f6c1"))
        return {QHostAddress(QStringLiteral("169.254.3.4")), QHostAddress(QStringLiteral("172.20.10.2"))};
    if (iface == QStringLiteral("lo"))
        return {QHostAddress(QStringLiteral("127.0.0.1"))};
    return {};
}

// Zählt die Aufrufe, damit geprüft ist, dass die Adresse nur einmal ermittelt wird.
struct FakeDetector
{
    QString address;
    QString reason = QStringLiteral("keine Standardroute mit IPv4");
    int *calls;

    QString operator()(QString *iface, QString *why) const
    {
        ++*calls;
        if (iface)
            *iface = QStringLiteral("enp3s0");
        if (address.isEmpty() && why)
            *why = reason;
        return address;
    }
};

} // namespace

class TestCoreSettings : public QObject
{
    Q_OBJECT

private slots:
    void defaultsPerEdition()
    {
        QCOMPARE(defaultCoreSettings(Edition::Installed).port, 8080);
        QCOMPARE(defaultCoreSettings(Edition::Development).port, 8081);
        QVERIFY(defaultCoreSettings(Edition::Installed).homeAddress.isEmpty());
        QVERIFY(coreSettingsPath(Edition::Installed).endsWith(QStringLiteral("/dons-streaming-studio/kern.ini")));
        QVERIFY(coreSettingsPath(Edition::Development)
                    .endsWith(QStringLiteral("/dons-streaming-studio-entwicklung/kern.ini")));
    }

    void port_data()
    {
        QTest::addColumn<int>("port");
        QTest::addColumn<bool>("valid");
        QTest::newRow("0") << 0 << false;
        QTest::newRow("80") << 80 << false;
        QTest::newRow("1023") << 1023 << false;
        QTest::newRow("1024") << 1024 << true;
        QTest::newRow("8080") << 8080 << true;
        QTest::newRow("32767") << 32767 << true;
        QTest::newRow("32768") << 32768 << false;
        QTest::newRow("65535") << 65535 << false;
        QTest::newRow("-1") << -1 << false;
    }

    void port()
    {
        QFETCH(int, port);
        QFETCH(bool, valid);
        QCOMPARE(validatePort(port).isEmpty(), valid);
    }

    void address_data()
    {
        QTest::addColumn<QString>("address");
        QTest::addColumn<bool>("valid");
        QTest::newRow("leer") << QString() << true;
        QTest::newRow("192.168.1.9") << QStringLiteral("192.168.1.9") << true;
        QTest::newRow("10.0.0.1") << QStringLiteral("10.0.0.1") << true;
        QTest::newRow("172.20.10.2") << QStringLiteral("172.20.10.2") << true;
        QTest::newRow("223.255.255.255") << QStringLiteral("223.255.255.255") << true;
        QTest::newRow("0.0.0.0") << QStringLiteral("0.0.0.0") << false;
        QTest::newRow("127.0.0.1") << QStringLiteral("127.0.0.1") << false;
        QTest::newRow("224.0.0.1") << QStringLiteral("224.0.0.1") << false;
        QTest::newRow("255.255.255.255") << QStringLiteral("255.255.255.255") << false;
        QTest::newRow("256") << QStringLiteral("192.168.1.256") << false;
        QTest::newRow("führende Null") << QStringLiteral("192.168.01.9") << false;
        QTest::newRow("drei Teile") << QStringLiteral("192.168.1") << false;
        QTest::newRow("eine Zahl") << QStringLiteral("1") << false;
        QTest::newRow("IPv6") << QStringLiteral("fe80::1") << false;
        QTest::newRow("Name") << QStringLiteral("studio.local") << false;
        QTest::newRow("Leerzeichen") << QStringLiteral(" 192.168.1.9") << false;
    }

    void address()
    {
        QFETCH(QString, address);
        QFETCH(bool, valid);
        QCOMPARE(validateHomeAddress(address).isEmpty(), valid);
    }

    void parseValid()
    {
        const CoreSettings defaults{8081, QString()};
        CoreSettings s;
        QString error;
        QVERIFY2(parseCoreSettings(serializeCoreSettings({9000, QStringLiteral("192.168.1.9")}), defaults, &s,
                                   &error),
                 qPrintable(error));
        QCOMPARE(s, (CoreSettings{9000, QStringLiteral("192.168.1.9")}));
        // Fehlende Angaben gelten mit dem Standardwert, Kommentare und Leerzeichen erlaubt.
        QVERIFY(parseCoreSettings("# nur ein Kommentar\n\n[overlay-server]\n  port = 8090 \n", defaults, &s,
                                  &error));
        QCOMPARE(s, (CoreSettings{8090, QString()}));
        QVERIFY(parseCoreSettings("", defaults, &s, &error));
        QCOMPARE(s, defaults);
        QVERIFY(parseCoreSettings("[overlay-server]\nadresse-im-heimnetz=\n", defaults, &s, &error));
        QCOMPARE(s, defaults);
    }

    void parseInvalid_data()
    {
        QTest::addColumn<QByteArray>("text");
        QTest::addColumn<QString>("message");
        QTest::newRow("Port zu klein") << QByteArray("[overlay-server]\nport=80\n")
                                       << QStringLiteral("Zeile 2: Port 80 ungültig");
        QTest::newRow("Port zu gross") << QByteArray("[overlay-server]\nport=40000\n")
                                       << QStringLiteral("Zeile 2: Port 40000 ungültig");
        QTest::newRow("Port Text") << QByteArray("[overlay-server]\nport=acht\n")
                                   << QStringLiteral("keine ganze Zahl");
        QTest::newRow("Adresse") << QByteArray("[overlay-server]\nadresse-im-heimnetz=127.0.0.1\n")
                                 << QStringLiteral("keine Adresse im Heimnetz");
        QTest::newRow("unbekannte Angabe") << QByteArray("[overlay-server]\nhost=1\n")
                                           << QStringLiteral("unbekannte Angabe «host»");
        QTest::newRow("unbekannter Abschnitt") << QByteArray("[server]\nport=8080\n")
                                               << QStringLiteral("unbekannter Abschnitt [server]");
        QTest::newRow("vor dem Abschnitt") << QByteArray("port=8080\n") << QStringLiteral("steht vor");
        QTest::newRow("doppelt") << QByteArray("[overlay-server]\nport=8080\nport=8081\n")
                                 << QStringLiteral("Zeile 3: «port» doppelt");
        QTest::newRow("ohne =") << QByteArray("[overlay-server]\nport\n") << QStringLiteral("Name=Wert");
    }

    void parseInvalid()
    {
        QFETCH(QByteArray, text);
        QFETCH(QString, message);
        CoreSettings s{1, QStringLiteral("x")};
        QString error;
        QVERIFY(!parseCoreSettings(text, {8081, QString()}, &s, &error));
        QVERIFY2(error.contains(message), qPrintable(error));
        QCOMPARE(s, (CoreSettings{1, QStringLiteral("x")})); // bei Fehler unverändert
    }

    void addressFromRoute_data()
    {
        QTest::addColumn<QString>("set");
        QTest::addColumn<QString>("address");
        QTest::addColumn<QString>("iface");
        QTest::addColumn<QString>("reason");
        QTest::newRow("heimnetz") << QStringLiteral("heimnetz") << QStringLiteral("192.168.1.9")
                                  << QStringLiteral("enp3s0") << QString();
        // link-local 169.254.x.x wird übersprungen
        QTest::newRow("messung1") << QStringLiteral("messung1") << QStringLiteral("172.20.10.2")
                                  << QStringLiteral("enx2aea2d88f6c1") << QString();
        QTest::newRow("ohne-route") << QStringLiteral("ohne-route") << QString() << QString()
                                    << QStringLiteral("keine Standardroute mit IPv4");
    }

    void addressFromRoute()
    {
        QFETCH(QString, set);
        QFETCH(QString, address);
        QFETCH(QString, iface);
        QFETCH(QString, reason);
        const QByteArray route = readFile(kData + QLatin1Char('/') + set + QStringLiteral("/net/route"));
        QVERIFY(!route.isEmpty());
        QString gotIface;
        QString gotReason;
        QCOMPARE(homeAddressFromRoute(route, studioAddresses, &gotIface, &gotReason), address);
        QCOMPARE(gotIface, iface);
        QCOMPARE(gotReason, reason);
    }

    void addressFromRouteWithoutIpv4()
    {
        const QByteArray route = readFile(kData + QStringLiteral("/heimnetz/net/route"));
        QString reason;
        const auto onlyV6 = [](const QString &) {
            return QList<QHostAddress>{QHostAddress(QStringLiteral("fe80::1")),
                                       QHostAddress(QStringLiteral("169.254.1.1"))};
        };
        QCOMPARE(homeAddressFromRoute(route, onlyV6, nullptr, &reason), QString());
        QCOMPARE(reason, QStringLiteral("Netzwerkkarte enp3s0 der Standardroute hat keine IPv4-Adresse"));
    }

    // Auf dem Studio-PC am 2026-11-03: Standardroute über enp3s0 mit 192.168.1.9. Auf einem
    // anderen Gerät nur, dass das Ergebnis gültig ist.
    void detectOnThisMachine()
    {
        QString iface;
        QString reason;
        const QString address = detectHomeAddress(&iface, &reason);
        qInfo("Erkannt: %s über %s (%s)", qPrintable(address), qPrintable(iface), qPrintable(reason));
        QVERIFY(validateHomeAddress(address).isEmpty());
        QCOMPARE(address.isEmpty(), !reason.isEmpty());
    }

    void storeCreatesFileAndDetectsOnce()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("ordner/kern.ini")); // Ordner fehlt noch
        int calls = 0;
        CoreSettingsStore store(path, {8081, QString()}, FakeDetector{QStringLiteral("192.168.1.9"), {}, &calls});
        QStringList log;
        QStringList warnings;
        store.load(&log, &warnings);
        QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join(QLatin1Char('\n'))));
        QCOMPARE(calls, 1);
        QCOMPARE(store.settings(), (CoreSettings{8081, QStringLiteral("192.168.1.9")}));
        QCOMPARE(log, (QStringList{
                          QStringLiteral("Einstellungen: Adresse im Heimnetz 192.168.1.9 eingetragen "
                                         "(Netzwerkkarte enp3s0 der Standardroute)"),
                          QStringLiteral("Einstellungen: %1 angelegt").arg(path),
                          QStringLiteral("Einstellungen: Port 8081, Adresse im Heimnetz 192.168.1.9 (%1)").arg(path)}));
        QCOMPARE(readFile(path), serializeCoreSettings(store.settings()));

        // Zweiter Start: Adresse steht in der Datei, nicht neu ermitteln, Datei nicht schreiben.
        const ino_t before = inode(path);
        int calls2 = 0;
        CoreSettingsStore again(path, {8081, QString()}, FakeDetector{QStringLiteral("10.0.0.5"), {}, &calls2});
        log.clear();
        again.load(&log, &warnings);
        QCOMPARE(calls2, 0);
        QCOMPARE(again.settings().homeAddress, QStringLiteral("192.168.1.9"));
        QCOMPARE(inode(path), before);
        QCOMPARE(log.size(), 1);
    }

    void storeWithoutRoute()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("kern.ini"));
        int calls = 0;
        CoreSettingsStore store(path, {8080, QString()}, FakeDetector{QString(), QStringLiteral("keine Standardroute mit IPv4"), &calls});
        QStringList log;
        QStringList warnings;
        store.load(&log, &warnings);
        QCOMPARE(warnings, QStringList{QStringLiteral("Einstellungen: keine Standardroute mit IPv4, Adresse im "
                                                      "Heimnetz bleibt leer")});
        QCOMPARE(store.settings(), (CoreSettings{8080, QString()}));
        // Datei trotzdem angelegt (Port), Adresse leer; beim nächsten Start neuer Versuch.
        QCOMPARE(readFile(path), serializeCoreSettings({8080, QString()}));
        CoreSettingsStore again(path, {8080, QString()}, FakeDetector{QStringLiteral("192.168.1.9"), {}, &calls});
        again.load(&log, &warnings);
        QCOMPARE(calls, 2);
        QCOMPARE(again.settings().homeAddress, QStringLiteral("192.168.1.9"));
    }

    void invalidFileStaysUntouched()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("kern.ini"));
        const QByteArray broken("[overlay-server]\nport=99999\nadresse-im-heimnetz=\n");
        QVERIFY(writeFile(path, broken));
        int calls = 0;
        CoreSettingsStore store(path, {8081, QString()}, FakeDetector{QStringLiteral("192.168.1.9"), {}, &calls});
        QStringList log;
        QStringList warnings;
        store.load(&log, &warnings);
        QCOMPARE(warnings.size(), 1);
        QVERIFY2(warnings.first().contains(QStringLiteral("Port 99999 ungültig")), qPrintable(warnings.first()));
        QVERIFY(!store.fileError().isEmpty());
        QCOMPARE(store.settings(), (CoreSettings{8081, QString()}));
        QCOMPARE(calls, 0); // keine Erkennung, nichts schreiben
        QString error;
        QVERIFY(!store.change(8090, std::nullopt, &error));
        QVERIFY2(error.contains(QStringLiteral("von Hand reparieren")), qPrintable(error));
        QCOMPARE(readFile(path), broken);
    }

    void changeValidatesAndWrites()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("kern.ini"));
        int calls = 0;
        CoreSettingsStore store(path, {8081, QString()}, FakeDetector{QStringLiteral("192.168.1.9"), {}, &calls});
        QStringList log;
        QStringList warnings;
        store.load(&log, &warnings);
        const QByteArray before = readFile(path);
        QString error;
        QVERIFY(!store.change(std::nullopt, std::nullopt, &error));
        QVERIFY(!store.change(80, std::nullopt, &error));
        QVERIFY(error.contains(QStringLiteral("1024 bis 32767")));
        // Ein ungültiger Wert verwirft die ganze Änderung, auch den gültigen Port.
        QVERIFY(!store.change(9000, QStringLiteral("192.168.1"), &error));
        QCOMPARE(store.settings(), (CoreSettings{8081, QStringLiteral("192.168.1.9")}));
        QCOMPARE(readFile(path), before);

        QVERIFY2(store.change(9000, QStringLiteral("10.0.0.2"), &error), qPrintable(error));
        QCOMPARE(store.settings(), (CoreSettings{9000, QStringLiteral("10.0.0.2")}));
        QCOMPARE(readFile(path), serializeCoreSettings(store.settings()));
        // Leere Adresse ist erlaubt.
        QVERIFY(store.change(std::nullopt, QString(), &error));
        QCOMPARE(store.settings(), (CoreSettings{9000, QString()}));
        // Gleicher Wert: nichts schreiben.
        const ino_t same = inode(path);
        QVERIFY(store.change(9000, std::nullopt, &error));
        QCOMPARE(inode(path), same);
    }

    // Atomar: QSaveFile schreibt eine neue Datei und benennt sie um (neue Inode). Ist der Ordner
    // schreibgeschützt, scheitert das, und die alte Datei bleibt ganz und gilt weiter.
    void atomicWrite()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("kern.ini"));
        int calls = 0;
        CoreSettingsStore store(path, {8081, QString()}, FakeDetector{QStringLiteral("192.168.1.9"), {}, &calls});
        QStringList log;
        QStringList warnings;
        store.load(&log, &warnings);
        const ino_t first = inode(path);
        QString error;
        QVERIFY(store.change(9001, std::nullopt, &error));
        QVERIFY(inode(path) != first);

        const QByteArray before = readFile(path);
        QVERIFY(QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        const bool changed = store.change(9002, std::nullopt, &error);
        QFile::setPermissions(dir.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(!changed);
        QVERIFY2(error.contains(QStringLiteral("kann nicht geschrieben werden")), qPrintable(error));
        QCOMPARE(store.settings().port, 9001);
        QCOMPARE(readFile(path), before);
        QCOMPARE(QDir(dir.path()).entryList(QDir::Files | QDir::Hidden), QStringList{QStringLiteral("kern.ini")});
    }
};

QTEST_GUILESS_MAIN(TestCoreSettings)
#include "tst_kerneinstellungen.moc"
