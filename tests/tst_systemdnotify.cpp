// Prüft die Meldungen an systemd ohne systemd (P07, Teil C; Watchdog seit P10a, Punkt 41):
// ein eigener Datagramm-Socket steht für NOTIFY_SOCKET.

#include "systemdnotify.h"

#include <QTemporaryDir>
#include <QTest>

#include <cstring>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace dss;
using std::chrono::microseconds;

class TestSystemdNotify : public QObject
{
    Q_OBJECT

private slots:
    void watchdogTime()
    {
        // Wie sd_watchdog_enabled(3): WATCHDOG_PID fehlt oder ist die eigene PID
        QCOMPARE(watchdogTimeout("10000000", "", 42), microseconds(10'000'000));
        QCOMPARE(watchdogTimeout("10000000", "42", 42), microseconds(10'000'000));
        QCOMPARE(watchdogTimeout("10000000", "43", 42), microseconds(0)); // für einen anderen Prozess
        QCOMPARE(watchdogTimeout("", "", 42), microseconds(0));
        QCOMPARE(watchdogTimeout("0", "", 42), microseconds(0));
        QCOMPARE(watchdogTimeout("-5", "", 42), microseconds(0));
        QCOMPARE(watchdogTimeout("abc", "", 42), microseconds(0));
        QCOMPARE(watchdogTimeout("1000", "x", 42), microseconds(0));
    }

    void watchdogMessage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray path = dir.filePath(QStringLiteral("notify")).toLocal8Bit();
        const int fd = ::socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
        QVERIFY(fd >= 0);
        sockaddr_un address = {};
        address.sun_family = AF_UNIX;
        std::memcpy(address.sun_path, path.constData(), size_t(path.size()));
        QCOMPARE(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
        qputenv("NOTIFY_SOCKET", path);
        QVERIFY(notifySystemd("WATCHDOG=1"));
        qunsetenv("NOTIFY_SOCKET");
        char buffer[64] = {};
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        ::close(fd);
        QCOMPARE(QByteArray(buffer, int(n)), QByteArray("WATCHDOG=1"));
        // Ohne NOTIFY_SOCKET: nichts gesendet
        QVERIFY(!notifySystemd("WATCHDOG=1"));
    }
};

QTEST_GUILESS_MAIN(TestSystemdNotify)
#include "tst_systemdnotify.moc"
