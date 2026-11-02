// Prüft die Zustände der Verbindungsknöpfe ohne Fenster (Auftrag P06, Punkt 27).

#include "connectionstate.h"

#include <QSet>
#include <QTest>

using namespace dss;

namespace {
const QList<Connection> kConnections = {Connection::Twitch, Connection::YouTube, Connection::Obs,
                                        Connection::Ndi};
const QList<ConnectionState> kStates = {ConnectionState::Off, ConnectionState::Error,
                                        ConnectionState::Connecting, ConnectionState::Connected};
} // namespace

class TestConnectionState : public QObject
{
    Q_OBJECT

private slots:
    void names()
    {
        QCOMPARE(connectionName(Connection::Twitch), QStringLiteral("Twitch"));
        QCOMPARE(connectionName(Connection::YouTube), QStringLiteral("YouTube"));
        QCOMPARE(connectionName(Connection::Obs), QStringLiteral("OBS"));
        QCOMPARE(connectionName(Connection::Ndi), QStringLiteral("NDI"));
    }

    void keysMatchStyleSheet()
    {
        // Diese Werte stehen im Stylesheet (src/app/theme.cpp).
        QCOMPARE(connectionStateKey(ConnectionState::Off), QStringLiteral("off"));
        QCOMPARE(connectionStateKey(ConnectionState::Error), QStringLiteral("error"));
        QCOMPARE(connectionStateKey(ConnectionState::Connecting), QStringLiteral("connecting"));
        QCOMPARE(connectionStateKey(ConnectionState::Connected), QStringLiteral("connected"));
    }

    void texts()
    {
        QCOMPARE(connectionStateText(Connection::Twitch, ConnectionState::Off),
                 QStringLiteral("Twitch: nicht eingerichtet oder abgeschaltet"));
        QCOMPARE(connectionStateText(Connection::Obs, ConnectionState::Error),
                 QStringLiteral("OBS: eingerichtet, aber nicht verbunden"));
        QCOMPARE(connectionStateText(Connection::YouTube, ConnectionState::Connecting),
                 QStringLiteral("YouTube: Verbindungsversuch läuft"));
        QCOMPARE(connectionStateText(Connection::Twitch, ConnectionState::Connected),
                 QStringLiteral("Twitch: verbunden"));
        QCOMPARE(connectionStateText(Connection::Ndi, ConnectionState::Connected),
                 QStringLiteral("NDI: Ausgabe läuft"));
    }

    void textsDistinct()
    {
        // Jede Verbindung hat in jedem Zustand einen eigenen Text.
        QSet<QString> seen;
        for (Connection c : kConnections) {
            for (ConnectionState s : kStates)
                seen.insert(connectionStateText(c, s));
        }
        QCOMPARE(seen.size(), kConnections.size() * kStates.size());
    }

    void shownStatesAreAllFour()
    {
        // --zustaende-zeigen: Twitch grün, YouTube orange, OBS rot, NDI grau.
        QCOMPARE(shownState(Connection::Twitch), ConnectionState::Connected);
        QCOMPARE(shownState(Connection::YouTube), ConnectionState::Connecting);
        QCOMPARE(shownState(Connection::Obs), ConnectionState::Error);
        QCOMPARE(shownState(Connection::Ndi), ConnectionState::Off);
    }
};

QTEST_GUILESS_MAIN(TestConnectionState)
#include "tst_connectionstate.moc"
