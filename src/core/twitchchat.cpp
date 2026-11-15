#include "twitchchat.h"

#include "chatmessage.h"
#include "twitchservice.h"

using dss::actions::ChatAccount;
using dss::twitch::TwitchAuth;

namespace {

// Recht zum Senden mit User-Token (https://dev.twitch.tv/docs/api/reference/#send-chat-message)
const QString kWriteScope = QStringLiteral("user:write:chat");

} // namespace

TwitchChatTransport::TwitchChatTransport(dss::twitch::TwitchService *twitch)
    : m_twitch(twitch)
{
}

QString TwitchChatTransport::broadcasterId() const
{
    if (!m_twitch->isActive() || m_twitch->channel()->state() != TwitchAuth::State::Ok)
        return {};
    return m_twitch->channel()->userId();
}

QString TwitchChatTransport::unavailableReason(ChatAccount account) const
{
    if (!m_twitch->isActive())
        return QStringLiteral("Twitch nicht verbunden"); // wie Benutzerinfo, Shoutout, Ansage bis c1b
    if (m_twitch->channel()->state() != TwitchAuth::State::Ok)
        return QStringLiteral("Kanal nicht angemeldet");
    if (account == ChatAccount::Broadcaster) {
        if (m_twitch->channel()->missingScopes().contains(kWriteScope))
            return QStringLiteral("Kanal: Recht %1 fehlt").arg(kWriteScope);
        return {};
    }
    if (!m_twitch->botSendToken())
        return QStringLiteral("Bot nicht angemeldet");
    if (m_twitch->bot()->missingScopes().contains(kWriteScope))
        return QStringLiteral("Bot: Recht %1 fehlt").arg(kWriteScope);
    return {};
}

QString TwitchChatTransport::userId(ChatAccount account) const
{
    const TwitchAuth *auth = account == ChatAccount::Bot ? m_twitch->bot() : m_twitch->channel();
    return auth->state() == TwitchAuth::State::Ok ? auth->userId() : QString();
}

void TwitchChatTransport::post(ChatAccount account, const QString &text, const QString &replyTo,
                               std::function<void(const dss::actions::ChatPostResult &)> done)
{
    const QString reason = unavailableReason(account);
    if (!reason.isEmpty()) {
        done({false, {}, {}, reason});
        return;
    }
    dss::twitch::TokenSource *token =
        account == ChatAccount::Bot ? m_twitch->botSendToken() : static_cast<dss::twitch::TokenSource *>(m_twitch->channel());
    const dss::twitch::ChatMessage message{broadcasterId(), userId(account), text, replyTo};
    dss::twitch::sendChatMessage(m_twitch->helix(), token, message, [done](const dss::twitch::ChatMessageResult &r) {
        done({r.sent, r.messageId, r.dropCode, r.error});
    });
}
