#pragma once

#include "actionclock.h"
#include "actionservices.h"

#include <QDateTime>
#include <QList>
#include <QString>

#include <array>
#include <functional>
#include <memory>

// Schritt «Chat senden» im Kern (P14 c1a, Plan 4.1): Wahl des Kontos, Grenze je Konto, Warteschlange,
// Kürzen und Schutz vor Schleifen. Nur Qt Core, kein Netz (Regel 9): Das Senden selbst geht über
// ChatTransport, das der Kern mit Twitch verbindet und die Tests ersetzen. Die Uhr lässt sich ersetzen.
//
// - Konto: use_bot = Bot; kann der Bot nicht senden und ist fallback an, sendet der Kanal, sonst
//   Fehler im Lauf. Gewählt wird beim Eintreffen; fällt das Konto bis zum Senden aus, wird verworfen.
// - Grenze je Konto: höchstens 1 Nachricht je Sekunde und 20 je 30 s (gezählt beim Absenden). Je Konto
//   geht eine Nachricht nach der anderen hinaus (Reihenfolge bleibt).
// - Warteschlange je Konto: 10; ist sie voll, wird die neue Nachricht verworfen. Was beim Senden älter
//   als 30 s ist, wird verworfen und im Lauf genannt.
// - Länge: nach dem Einsetzen auf 500 UTF-16-Einheiten gekürzt, nur an Grenzen von Zeichen (Graphem,
//   QTextBoundaryFinder). Wie Twitch zählt, ist nicht belegt ([OFFEN], Bericht c1a).
// - Schleifen: Eigene Nachrichten erkennt isInternal() an der message_id aus der Antwort (letzte 100)
//   und an Absender und gleichem Text innerhalb von 10 s nach dem Absenden.
// - Ergebnis: Der Lauf bekommt es über StepDone, der Kern über den ResultHandler (twitch.chat_sent,
//   twitch.chat_dropped).
//
// Quellen: https://doc.qt.io/qt-6.8/qtextboundaryfinder.html (Grapheme),
// https://dev.twitch.tv/docs/api/reference/#send-chat-message (500 Zeichen, reply_parent_message_id)

namespace dss::actions {

enum class ChatAccount { Bot, Broadcaster };

// «bot», «broadcaster» (wie accounts in actions_status)
QString chatAccountKey(ChatAccount account);

// Kürzt auf höchstens maxUnits UTF-16-Einheiten, ohne ein Zeichen (Graphem) zu zerschneiden.
QString truncateChatText(const QString &text, int maxUnits);

struct ChatPostResult
{
    bool sent = false;
    QString messageId;
    QString code;    // Grund von Twitch (drop_reason.code), sonst leer
    QString message; // Grund auf Deutsch, wenn nicht gesendet
};

class ChatTransport
{
public:
    virtual ~ChatTransport() = default;
    // Leer, wenn das Konto senden kann; sonst der Grund auf Deutsch.
    virtual QString unavailableReason(ChatAccount account) const = 0;
    // Twitch-ID des Kontos (Absender); leer, wenn nicht angemeldet.
    virtual QString userId(ChatAccount account) const = 0;
    // Genau eine Nachricht; done genau einmal, auch im Aufruf selbst erlaubt.
    virtual void post(ChatAccount account, const QString &text, const QString &replyTo,
                      std::function<void(const ChatPostResult &)> done) = 0;
};

struct ChatOutcome
{
    bool sent = false;
    ChatAccount account = ChatAccount::Bot;
    QString senderId;
    QString messageId;
    QString text;      // wie gesendet (gekürzt)
    QString replyTo;
    bool truncated = false;
    QString code;      // verworfen: unavailable, queue_full, expired, failed oder drop_reason.code
    QString message;   // verworfen: Grund auf Deutsch
};

class ChatOutbox : public ChatSender
{
public:
    struct Limits
    {
        qint64 minIntervalMs = 1000;
        qint64 windowMs = 30000;
        int perWindow = 20;
        int queueSize = 10;
        qint64 maxAgeMs = 30000;
        int maxLength = 500;
        qint64 echoMs = 10000;
        int rememberedIds = 100;
    };

    // clock und transport leben länger als diese Instanz; transport darf nullptr sein (Twitch nicht
    // eingebaut).
    ChatOutbox(ActionClock *clock, ChatTransport *transport, const Limits &limits);
    ChatOutbox(ActionClock *clock, ChatTransport *transport);
    ~ChatOutbox() override;

    void setResultHandler(std::function<void(const ChatOutcome &)> handler) { m_handler = std::move(handler); }

    // ChatSender
    void sendChat(const ChatRequest &request, StepDone done) override;

    // Leer, wenn das Konto senden kann; sonst der Grund (accounts in actions_status).
    QString unavailableReason(ChatAccount account) const;
    // Vom Studio selbst gesendet (Plan 4.1): message_id oder Absender und Text innerhalb von 10 s.
    bool isInternal(const QString &messageId, const QString &senderId, const QString &text) const;
    qsizetype waiting(ChatAccount account) const;

private:
    struct Pending
    {
        QString text;
        QString replyTo;
        bool truncated = false;
        QDateTime queuedAt;
        StepDone done;
    };
    struct Lane
    {
        QList<Pending> queue;
        QList<QDateTime> sentAt; // Absenden in den letzten windowMs
        bool inFlight = false;
        ActionClock::TimerId timer = 0;
        bool pumping = false;
        bool again = false;
    };
    struct Echo
    {
        QString senderId;
        QString text;
        QDateTime at;
    };

    Lane &lane(ChatAccount account) { return m_lanes[account == ChatAccount::Bot ? 0 : 1]; }
    void pump(ChatAccount account);
    void pumpOnce(ChatAccount account);
    void finish(ChatAccount account, const Pending &pending, const ChatOutcome &outcome);
    void drop(ChatAccount account, const Pending &pending, const QString &code, const QString &message);

    ActionClock *m_clock;
    ChatTransport *m_transport;
    Limits m_limits;
    std::array<Lane, 2> m_lanes;
    std::function<void(const ChatOutcome &)> m_handler;
    QList<QString> m_sentIds; // message_id der letzten gesendeten Nachrichten, älteste zuerst
    QList<Echo> m_echoes;     // Absender und Text, beim Absenden; ältere als echoMs fallen in pump weg
    // Antworten nach dem Ende dieser Instanz (Beenden des Kerns) werden ignoriert.
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace dss::actions
