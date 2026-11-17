#include "helixactions.h"

#include "helixclient.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrlQuery>

namespace dss::twitch {

namespace {

HelixCallResult callResult(const HelixClient::Reply &reply)
{
    return {reply.ok(), reply.status, reply.ok() ? QString() : reply.errorText()};
}

QJsonObject firstData(const HelixClient::Reply &reply)
{
    return reply.body.value(QStringLiteral("data")).toArray().first().toObject();
}

} // namespace

void getUser(HelixClient *helix, TokenSource *token, const QString &login,
             std::function<void(const UserInfoResult &)> done)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("login"), login);
    helix->get(token, QStringLiteral("/helix/users"), query, [helix, token, done](const HelixClient::Reply &reply) {
        UserInfoResult result;
        if (!reply.ok()) {
            result.error = reply.errorText();
            done(result);
            return;
        }
        const QJsonObject user = firstData(reply);
        result.user.id = user.value(QStringLiteral("id")).toString();
        if (result.user.id.isEmpty()) {
            done(result); // unbekannter Name
            return;
        }
        result.found = true;
        result.user.login = user.value(QStringLiteral("login")).toString();
        result.user.displayName = user.value(QStringLiteral("display_name")).toString();
        result.user.description = user.value(QStringLiteral("description")).toString();
        result.user.profileImageUrl = user.value(QStringLiteral("profile_image_url")).toString();
        QUrlQuery channel;
        channel.addQueryItem(QStringLiteral("broadcaster_id"), result.user.id);
        helix->get(token, QStringLiteral("/helix/channels"), channel, [result, done](const HelixClient::Reply &reply) mutable {
            if (reply.ok())
                result.game = firstData(reply).value(QStringLiteral("game_name")).toString();
            else
                result.gameError = reply.errorText();
            done(result);
        });
    });
}

void sendShoutout(HelixClient *helix, TokenSource *token, const QString &fromId, const QString &toId,
                  const QString &moderatorId, std::function<void(const HelixCallResult &)> done)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("from_broadcaster_id"), fromId);
    query.addQueryItem(QStringLiteral("to_broadcaster_id"), toId);
    query.addQueryItem(QStringLiteral("moderator_id"), moderatorId);
    helix->post(token, QStringLiteral("/helix/chat/shoutouts"), {},
                [done](const HelixClient::Reply &reply) { done(callResult(reply)); }, query);
}

void sendAnnouncement(HelixClient *helix, TokenSource *token, const QString &broadcasterId,
                      const QString &moderatorId, const QString &text, const QString &color,
                      std::function<void(const HelixCallResult &)> done)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("broadcaster_id"), broadcasterId);
    query.addQueryItem(QStringLiteral("moderator_id"), moderatorId);
    QJsonObject body{{QStringLiteral("message"), text}};
    if (!color.isEmpty())
        body.insert(QStringLiteral("color"), color);
    helix->post(token, QStringLiteral("/helix/chat/announcements"), body,
                [done](const HelixClient::Reply &reply) { done(callResult(reply)); }, query);
}

} // namespace dss::twitch
