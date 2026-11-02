// Prüft, dass SIGINT und SIGTERM als Signal in der Ereignisschleife ankommen.

#include "unixsignals.h"

#include <QSignalSpy>
#include <QTest>

#include <csignal>

class TestUnixSignals : public QObject
{
    Q_OBJECT

private slots:
    void deliversSignal_data()
    {
        QTest::addColumn<int>("signalNumber");
        QTest::newRow("SIGINT") << SIGINT;
        QTest::newRow("SIGTERM") << SIGTERM;
    }

    void deliversSignal()
    {
        QFETCH(int, signalNumber);
        dss::UnixSignalWatcher watcher;
        QVERIFY(watcher.isValid());
        QSignalSpy spy(&watcher, &dss::UnixSignalWatcher::terminationRequested);
        QCOMPARE(::raise(signalNumber), 0);
        QVERIFY(spy.wait(2000));
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), signalNumber);
    }

    void secondWatcherIsInvalid()
    {
        dss::UnixSignalWatcher first;
        dss::UnixSignalWatcher second;
        QVERIFY(first.isValid());
        QVERIFY(!second.isValid());
    }
};

QTEST_GUILESS_MAIN(TestUnixSignals)
#include "tst_unixsignals.moc"
