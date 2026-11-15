#include "chatoutbox.h"

#include <QTextBoundaryFinder>

#include <algorithm>

namespace dss::actions {

namespace {

QString accountLabel(ChatAccount account)
{
    return account == ChatAccount::Bot ? QStringLiteral("Bot") : QStringLiteral("Kanal");
}

} // namespace

QString chatAccountKey(ChatAccount account)
{
    return account == ChatAccount::Bot ? QStringLiteral("bot") : QStringLiteral("broadcaster");
}

QString truncateChatText(const QString &text, int maxUnits)
{
    if (text.size() <= maxUnits)
        return text;
    // Letzte Grenze eines Graphems an oder vor maxUnits: Ersatzpaare, Emoji mit Hautton oder ZWJ und
    // Zeichen mit kombinierenden Akzenten bleiben ganz.
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    finder.setPosition(maxUnits);
    const qsizetype end = finder.isAtBoundary() ? maxUnits : finder.toPreviousBoundary();
    return text.left(std::max<qsizetype>(end, 0));
}

ChatOutbox::ChatOutbox(ActionClock *clock, ChatTransport *transport, const Limits &limits)
    : m_clock(clock)
    , m_transport(transport)
    , m_limits(limits)
{
}

ChatOutbox::ChatOutbox(ActionClock *clock, ChatTransport *transport)
    : ChatOutbox(clock, transport, Limits())
{
}

ChatOutbox::~ChatOutbox()
{
    *m_alive = false;
    for (Lane &l : m_lanes) {
        if (l.timer)
            m_clock->stopTimer(l.timer);
    }
}

QString ChatOutbox::unavailableReason(ChatAccount account) const
{
    if (!m_transport)
        return QStringLiteral("Twitch nicht verbunden");
    return m_transport->unavailableReason(account);
}

qsizetype ChatOutbox::waiting(ChatAccount account) const
{
    return m_lanes[account == ChatAccount::Bot ? 0 : 1].queue.size();
}

bool ChatOutbox::isInternal(const QString &messageId, const QString &senderId, const QString &text) const
{
    if (!messageId.isEmpty() && m_sentIds.contains(messageId))
        return true;
    if (senderId.isEmpty())
        return false;
    const QDateTime now = m_clock->now();
    return std::any_of(m_echoes.cbegin(), m_echoes.cend(), [&](const Echo &e) {
        return e.senderId == senderId && e.text == text && e.at.msecsTo(now) <= m_limits.echoMs;
    });
}

void ChatOutbox::sendChat(const ChatRequest &request, StepDone done)
{
    ChatAccount account = request.useBot ? ChatAccount::Bot : ChatAccount::Broadcaster;
    QString reason = unavailableReason(account);
    if (!reason.isEmpty() && request.useBot && request.fallback) {
        const QString channelReason = unavailableReason(ChatAccount::Broadcaster);
        if (channelReason.isEmpty())
            account = ChatAccount::Broadcaster;
        // Gleicher Grund (Twitch aus, Kanal nicht angemeldet) nur einmal
        if (channelReason.isEmpty())
            reason.clear();
        else if (channelReason != reason)
            reason += QStringLiteral("; Ersatz Kanal: ") + channelReason;
    }
    Pending pending;
    pending.text = truncateChatText(request.text, m_limits.maxLength);
    pending.truncated = pending.text.size() != request.text.size();
    pending.replyTo = request.replyTo;
    pending.queuedAt = m_clock->now();
    pending.done = std::move(done);
    if (!reason.isEmpty()) {
        drop(account, pending, QStringLiteral("unavailable"), reason);
        return;
    }
    Lane &l = lane(account);
    if (l.queue.size() >= m_limits.queueSize) {
        drop(account, pending, QStringLiteral("queue_full"),
             QStringLiteral("Chat-Warteschlange %1 voll (%2), verworfen").arg(accountLabel(account)).arg(m_limits.queueSize));
        return;
    }
    l.queue.append(std::move(pending));
    pump(account);
}

void ChatOutbox::pump(ChatAccount account)
{
    // Rückrufe (Lauf, Ergebnis, Antwort von Twitch) dürfen im Aufruf selbst kommen und wieder senden:
    // dann läuft pumpOnce nach dem laufenden Durchgang noch einmal, nie verschachtelt.
    Lane &l = lane(account);
    if (l.pumping) {
        l.again = true;
        return;
    }
    l.pumping = true;
    do {
        l.again = false;
        pumpOnce(account);
    } while (l.again);
    l.pumping = false;
}

void ChatOutbox::pumpOnce(ChatAccount account)
{
    Lane &l = lane(account);
    if (l.inFlight || l.timer)
        return;
    const QDateTime now = m_clock->now();
    m_echoes.removeIf([&](const Echo &e) { return e.at.msecsTo(now) > m_limits.echoMs; });
    while (!l.queue.isEmpty()) {
        const Pending &first = l.queue.first();
        if (first.queuedAt.msecsTo(now) > m_limits.maxAgeMs) {
            const Pending old = l.queue.takeFirst();
            drop(account, old, QStringLiteral("expired"),
                 QStringLiteral("verworfen: wartete länger als %1 s").arg(m_limits.maxAgeMs / 1000));
            continue;
        }
        const QString reason = unavailableReason(account);
        if (!reason.isEmpty()) {
            const Pending old = l.queue.takeFirst();
            drop(account, old, QStringLiteral("unavailable"), reason);
            continue;
        }
        break;
    }
    if (l.queue.isEmpty())
        return;

    // Grenze: 1 je Sekunde und perWindow je windowMs, gezählt beim Absenden
    l.sentAt.removeIf([&](const QDateTime &t) { return t.msecsTo(now) >= m_limits.windowMs; });
    qint64 wait = 0;
    if (!l.sentAt.isEmpty())
        wait = std::max(wait, m_limits.minIntervalMs - l.sentAt.last().msecsTo(now));
    if (l.sentAt.size() >= m_limits.perWindow)
        wait = std::max(wait, m_limits.windowMs - l.sentAt[l.sentAt.size() - m_limits.perWindow].msecsTo(now));
    if (wait > 0) {
        l.timer = m_clock->startTimer(wait, [this, account] {
            lane(account).timer = 0;
            pump(account);
        });
        return;
    }

    const Pending pending = l.queue.takeFirst();
    l.sentAt.append(now);
    l.inFlight = true;
    const QString senderId = m_transport->userId(account);
    m_echoes.append({senderId, pending.text, now});
    const std::weak_ptr<bool> alive = m_alive;
    m_transport->post(account, pending.text, pending.replyTo,
                      [this, alive, account, pending, senderId](const ChatPostResult &r) {
                          if (alive.expired() || !*alive.lock())
                              return;
                          lane(account).inFlight = false;
                          if (!r.messageId.isEmpty()) {
                              m_sentIds.append(r.messageId);
                              while (m_sentIds.size() > m_limits.rememberedIds)
                                  m_sentIds.removeFirst();
                          }
                          if (r.sent) {
                              ChatOutcome outcome;
                              outcome.sent = true;
                              outcome.account = account;
                              outcome.senderId = senderId;
                              outcome.messageId = r.messageId;
                              outcome.text = pending.text;
                              outcome.replyTo = pending.replyTo;
                              outcome.truncated = pending.truncated;
                              finish(account, pending, outcome);
                          } else {
                              drop(account, pending, r.code.isEmpty() ? QStringLiteral("failed") : r.code, r.message);
                          }
                          pump(account);
                      });
}

void ChatOutbox::finish(ChatAccount account, const Pending &pending, const ChatOutcome &outcome)
{
    if (m_handler)
        m_handler(outcome);
    if (!pending.done)
        return;
    if (outcome.sent) {
        QString message = QStringLiteral("gesendet als %1").arg(accountLabel(account));
        if (pending.truncated)
            message += QStringLiteral(", gekürzt auf %1 Zeichen").arg(m_limits.maxLength);
        pending.done({true, message, {}});
    } else {
        pending.done(StepResult::failure(outcome.message));
    }
}

void ChatOutbox::drop(ChatAccount account, const Pending &pending, const QString &code, const QString &message)
{
    ChatOutcome outcome;
    outcome.account = account;
    outcome.senderId = m_transport ? m_transport->userId(account) : QString();
    outcome.text = pending.text;
    outcome.replyTo = pending.replyTo;
    outcome.truncated = pending.truncated;
    outcome.code = code;
    outcome.message = message;
    finish(account, pending, outcome);
}

} // namespace dss::actions
