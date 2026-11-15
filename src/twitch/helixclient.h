#pragma once

#include "twitchendpoints.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrlQuery>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

// Aufrufe der Twitch API (Helix) mit Client-Id und Bearer-Token des gewählten Kontos (P13, Plan 2.6).
// P14 baut darauf die schreibenden Aufrufe (Chat senden, Shoutout, Ansage).
//
// - 401: einmal neues Token holen (TokenSource::renew), dann dieselbe Anfrage noch einmal.
// - 429: warten bis Ratelimit-Reset (Unix-Zeit in Sekunden), höchstens maxRateLimitWait, dann einmal
//   wiederholen.
// - Netzfehler und andere Antworten gehen an den Aufrufer; er entscheidet über neue Versuche.
// - Token und Antworten kommen nie ins Protokoll.
//
// Quellen: https://dev.twitch.tv/docs/api/reference/ (Client-Id, Authorization: Bearer),
// https://dev.twitch.tv/docs/api/guide/#twitch-rate-limits (Ratelimit-Limit, Ratelimit-Remaining,
// Ratelimit-Reset)

namespace dss::twitch {

class TokenSource;

class HelixClient : public QObject
{
    Q_OBJECT

public:
    struct Reply
    {
        int status = 0;     // HTTP-Status; 0 ohne Antwort
        QJsonObject body;   // Antwort als JSON (leer bei 204 oder ohne JSON)
        QString error;      // Netzfehler auf Deutsch; leer mit Antwort
        bool ok() const { return status >= 200 && status < 300; }
    };
    using Done = std::function<void(const Reply &)>;

    explicit HelixClient(QNetworkAccessManager *network, QObject *parent = nullptr);

    void setEndpoints(const Endpoints &endpoints) { m_endpoints = endpoints; }
    void setClientId(const QString &clientId) { m_clientId = clientId; }
    void setMaxRateLimitWait(int ms) { m_maxRateLimitWait = ms; }
    void setRequestTimeout(int ms) { m_requestTimeout = ms; }

    // path beginnt mit /helix/. token muss leben, bis done aufgerufen ist.
    void get(TokenSource *token, const QString &path, const QUrlQuery &query, Done done);
    void post(TokenSource *token, const QString &path, const QJsonObject &body, Done done,
              const QUrlQuery &query = {});
    void deleteResource(TokenSource *token, const QString &path, const QUrlQuery &query, Done done);

private:
    struct Call
    {
        TokenSource *token = nullptr;
        QByteArray verb;
        QString path;
        QUrlQuery query;
        std::optional<QJsonObject> body;
        Done done;
        bool renewed = false;
        bool waitedForRateLimit = false;
    };

    void send(Call call);
    void onReply(QNetworkReply *reply, Call call);

    QNetworkAccessManager *m_network;
    Endpoints m_endpoints = Endpoints::twitch();
    QString m_clientId;
    int m_maxRateLimitWait = 60000;
    int m_requestTimeout = 15000;
};

} // namespace dss::twitch
