#pragma once

#include <QString>
#include <QUrl>

// Adressen von Twitch (P13, Plan 2.6). Für Tests lassen sie sich durch einen Testserver ersetzen;
// die Prüfung «nur 127.0.0.1» macht der Kern bei --twitch-testserver (src/core/main.cpp).
//
// Quellen: https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/ (id.twitch.tv/oauth2/…),
// https://dev.twitch.tv/docs/api/reference/ (api.twitch.tv/helix/…),
// https://dev.twitch.tv/docs/eventsub/handling-websocket-events/ (eventsub.wss.twitch.tv/ws)

namespace dss::twitch {

struct Endpoints
{
    QUrl id;       // Anmeldung: <id>/oauth2/device, /token, /validate, /revoke
    QUrl api;      // Helix: <api>/helix/…
    QUrl eventsub; // WebSocket von EventSub (vollständige Adresse)

    static Endpoints twitch()
    {
        return {QUrl(QStringLiteral("https://id.twitch.tv")), QUrl(QStringLiteral("https://api.twitch.tv")),
                QUrl(QStringLiteral("wss://eventsub.wss.twitch.tv/ws"))};
    }
    // Ein Testserver für alles: HTTP unter base, WebSocket unter ws://<host>:<port>/ws.
    static Endpoints testServer(const QUrl &base)
    {
        QUrl socket = base;
        socket.setScheme(QStringLiteral("ws"));
        socket.setPath(QStringLiteral("/ws"));
        return {base, base, socket};
    }

    QUrl idUrl(const QString &path) const { return withPath(id, path); }
    QUrl apiUrl(const QString &path) const { return withPath(api, path); }

private:
    static QUrl withPath(QUrl base, const QString &path)
    {
        base.setPath(path);
        return base;
    }
};

} // namespace dss::twitch
