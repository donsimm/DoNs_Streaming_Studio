#include "chatmessage.h"

#include "helixclient.h"

#include <QJsonArray>
#include <QJsonObject>

namespace dss::twitch {

void sendChatMessage(HelixClient *helix, TokenSource *token, const ChatMessage &message,
                     std::function<void(const ChatMessageResult &)> done)
{
    QJsonObject body{{QStringLiteral("broadcaster_id"), message.broadcasterId},
                     {QStringLiteral("sender_id"), message.senderId},
                     {QStringLiteral("message"), message.text}};
    if (!message.replyTo.isEmpty())
        body.insert(QStringLiteral("reply_parent_message_id"), message.replyTo);
    helix->post(token, QStringLiteral("/helix/chat/messages"), body, [done](const HelixClient::Reply &reply) {
        ChatMessageResult result;
        if (!reply.ok()) {
            // Fehler von Twitch: status und message im Körper (https://dev.twitch.tv/docs/api/reference/)
            const QString text = reply.body.value(QStringLiteral("message")).toString();
            result.error = !reply.error.isEmpty() ? reply.error
                           : text.isEmpty()       ? QStringLiteral("Twitch antwortet mit %1").arg(reply.status)
                                                  : QStringLiteral("Twitch antwortet mit %1: %2").arg(QString::number(reply.status), text);
            done(result);
            return;
        }
        const QJsonObject data = reply.body.value(QStringLiteral("data")).toArray().first().toObject();
        result.messageId = data.value(QStringLiteral("message_id")).toString();
        result.sent = data.value(QStringLiteral("is_sent")).toBool();
        if (!result.sent) {
            const QJsonObject drop = data.value(QStringLiteral("drop_reason")).toObject();
            result.dropCode = drop.value(QStringLiteral("code")).toString();
            const QString text = drop.value(QStringLiteral("message")).toString();
            result.error = text.isEmpty() ? QStringLiteral("von Twitch nicht gesendet")
                                          : QStringLiteral("von Twitch nicht gesendet: %1").arg(text);
        }
        done(result);
    });
}

} // namespace dss::twitch
