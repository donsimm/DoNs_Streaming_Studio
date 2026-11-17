#pragma once

#include <QString>

#include <functional>

// «Send Chat Message» über Helix (P14 c1a, Plan 4.1): POST /helix/chat/messages mit broadcaster_id,
// sender_id (Konto des Tokens; beim App-Token der Bot), message und wahlweise reply_parent_message_id.
// Die Antwort nennt message_id, is_sent und drop_reason (code, message). Grenzen, Warteschlange und
// Kürzen macht der Aufrufer (dss::actions::ChatOutbox); hier geht genau eine Nachricht hinaus.
// Nie im Protokoll: Token und Text.
//
// Quellen: https://dev.twitch.tv/docs/api/reference/#send-chat-message,
// docs/archiv/recherchen/2026-11-15-schritt0-twitch-streamerbot.md (Parameter und Antwort)

namespace dss::twitch {

class HelixClient;
class TokenSource;

struct ChatMessage
{
    QString broadcasterId;
    QString senderId;
    QString text;
    QString replyTo; // reply_parent_message_id; leer = keine Antwort
};

struct ChatMessageResult
{
    bool sent = false;
    QString messageId;
    QString dropCode;  // drop_reason.code, wenn Twitch die Nachricht nicht gesendet hat
    QString error;     // Grund auf Deutsch, wenn nicht gesendet
};

// token muss leben, bis done aufgerufen ist (HelixClient).
void sendChatMessage(HelixClient *helix, TokenSource *token, const ChatMessage &message,
                     std::function<void(const ChatMessageResult &)> done);

} // namespace dss::twitch
