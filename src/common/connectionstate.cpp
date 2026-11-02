#include "connectionstate.h"

namespace dss {

QString connectionName(Connection connection)
{
    switch (connection) {
    case Connection::Twitch:
        return QStringLiteral("Twitch");
    case Connection::YouTube:
        return QStringLiteral("YouTube");
    case Connection::Obs:
        return QStringLiteral("OBS");
    case Connection::Ndi:
        break;
    }
    return QStringLiteral("NDI");
}

QString connectionStateKey(ConnectionState state)
{
    switch (state) {
    case ConnectionState::Off:
        return QStringLiteral("off");
    case ConnectionState::Error:
        return QStringLiteral("error");
    case ConnectionState::Connecting:
        return QStringLiteral("connecting");
    case ConnectionState::Connected:
        break;
    }
    return QStringLiteral("connected");
}

bool connectionStateFromKey(const QString &key, ConnectionState *state)
{
    for (ConnectionState s : {ConnectionState::Off, ConnectionState::Error,
                              ConnectionState::Connecting, ConnectionState::Connected}) {
        if (connectionStateKey(s) == key) {
            *state = s;
            return true;
        }
    }
    return false;
}

QString connectionKey(Connection connection)
{
    return connectionName(connection).toLower();
}

QString connectionStateText(Connection connection, ConnectionState state)
{
    QString text;
    switch (state) {
    case ConnectionState::Off:
        text = QStringLiteral("nicht eingerichtet oder abgeschaltet");
        break;
    case ConnectionState::Error:
        text = QStringLiteral("eingerichtet, aber nicht verbunden");
        break;
    case ConnectionState::Connecting:
        text = QStringLiteral("Verbindungsversuch läuft");
        break;
    case ConnectionState::Connected:
        // NDI ist keine Verbindung, sondern eine Ausgabe.
        text = connection == Connection::Ndi ? QStringLiteral("Ausgabe läuft")
                                             : QStringLiteral("verbunden");
        break;
    }
    return connectionName(connection) + QStringLiteral(": ") + text;
}

ConnectionState shownState(Connection connection)
{
    switch (connection) {
    case Connection::Twitch:
        return ConnectionState::Connected;
    case Connection::YouTube:
        return ConnectionState::Connecting;
    case Connection::Obs:
        return ConnectionState::Error;
    case Connection::Ndi:
        break;
    }
    return ConnectionState::Off;
}

} // namespace dss
