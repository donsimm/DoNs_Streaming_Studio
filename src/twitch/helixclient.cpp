#include "helixclient.h"

#include "twitchauth.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

#include <algorithm>

namespace dss::twitch {

HelixClient::HelixClient(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
{
}

void HelixClient::get(TokenSource *token, const QString &path, const QUrlQuery &query, Done done)
{
    send(Call{token, QByteArrayLiteral("GET"), path, query, std::nullopt, std::move(done)});
}

void HelixClient::post(TokenSource *token, const QString &path, const QJsonObject &body, Done done,
                       const QUrlQuery &query)
{
    send(Call{token, QByteArrayLiteral("POST"), path, query, body, std::move(done)});
}

void HelixClient::deleteResource(TokenSource *token, const QString &path, const QUrlQuery &query, Done done)
{
    send(Call{token, QByteArrayLiteral("DELETE"), path, query, std::nullopt, std::move(done)});
}

void HelixClient::send(Call call)
{
    QUrl url = m_endpoints.apiUrl(call.path);
    url.setQuery(call.query);
    QNetworkRequest request(url);
    request.setRawHeader("Client-Id", m_clientId.toUtf8());
    request.setRawHeader("Authorization", "Bearer " + call.token->accessToken().toUtf8());
    request.setTransferTimeout(m_requestTimeout);
    QNetworkReply *reply = nullptr;
    if (call.body) {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/json"));
        reply = m_network->sendCustomRequest(request, call.verb, QJsonDocument(*call.body).toJson(QJsonDocument::Compact));
    } else {
        reply = m_network->sendCustomRequest(request, call.verb);
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, call] {
        reply->deleteLater();
        onReply(reply, call);
    });
}

void HelixClient::onReply(QNetworkReply *reply, Call call)
{
    Reply result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = QJsonDocument::fromJson(reply->readAll()).object();
    if (result.status == 0)
        result.error = QStringLiteral("Twitch nicht erreichbar (%1)").arg(reply->errorString());

    if (result.status == 401 && !call.renewed) {
        call.renewed = true;
        const QPointer<HelixClient> self(this);
        TokenSource *token = call.token;
        token->renew([self, call, result](bool ok) {
            if (!self)
                return;
            if (ok)
                self->send(call);
            else
                call.done(result);
        });
        return;
    }
    if (result.status == 429 && !call.waitedForRateLimit) {
        call.waitedForRateLimit = true;
        const qint64 reset = reply->rawHeader("Ratelimit-Reset").toLongLong();
        const qint64 wait = reset > 0 ? reset * 1000 - QDateTime::currentMSecsSinceEpoch() : 1000;
        const int delay = int(std::clamp<qint64>(wait, 0, m_maxRateLimitWait));
        qWarning("Twitch: Grenze der API erreicht (%s), neuer Versuch in %d ms", qPrintable(call.path), delay);
        QTimer::singleShot(delay, this, [this, call] { send(call); });
        return;
    }
    call.done(result);
}

} // namespace dss::twitch
