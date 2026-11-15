#pragma once

#include "chatoutbox.h"

namespace dss::twitch {
class TwitchService;
}

// Chat senden über Twitch im Kern (P14 c1a, Plan 4.1, nur mit DSS_CORE_TWITCH): verbindet
// dss::actions::ChatOutbox mit TwitchService. Bot: Token aus botSendToken() (App-Token für das Abzeichen
// oder das Token des Bots, P13 Plan 2.4), sender_id = Bot. Kanal: Token und ID des Kanals.
// broadcaster_id ist immer der eigene Kanal; ohne angemeldeten Kanal sendet auch der Bot nicht.
class TwitchChatTransport : public dss::actions::ChatTransport
{
public:
    // twitch lebt länger als diese Instanz.
    explicit TwitchChatTransport(dss::twitch::TwitchService *twitch);

    // Twitch-ID des eigenen Kanals; leer, wenn nicht angemeldet.
    QString broadcasterId() const;

    QString unavailableReason(dss::actions::ChatAccount account) const override;
    QString userId(dss::actions::ChatAccount account) const override;
    void post(dss::actions::ChatAccount account, const QString &text, const QString &replyTo,
              std::function<void(const dss::actions::ChatPostResult &)> done) override;

private:
    dss::twitch::TwitchService *m_twitch;
};
