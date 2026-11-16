#include "twitchchat.h"

#include "chatmessage.h"
#include "helixactions.h"
#include "twitchservice.h"

#include <QPointer>
#include <QTimer>

using dss::actions::ChatAccount;
using dss::actions::StepResult;
using dss::twitch::TwitchAuth;

namespace {

// Recht zum Senden mit User-Token (https://dev.twitch.tv/docs/api/reference/#send-chat-message)
const QString kWriteScope = QStringLiteral("user:write:chat");
// Rechte für Shoutout und Ansage (https://dev.twitch.tv/docs/api/reference/chat, Schritt 0 Abschnitt 17)
const QString kShoutoutScope = QStringLiteral("moderator:manage:shoutouts");
const QString kAnnounceScope = QStringLiteral("moderator:manage:announcements");

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
        return QStringLiteral("Twitch nicht verbunden"); // wie TwitchActions::channelProblem
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

// --- Benutzerinfo, Shoutout, Ansage (c1b) ---

TwitchActions::TwitchActions(dss::twitch::TwitchService *twitch, QObject *parent)
    : QObject(parent)
    , m_twitch(twitch)
    , m_announceTimer(new QTimer(this))
{
    m_announceTimer->setSingleShot(true);
    connect(m_announceTimer, &QTimer::timeout, this, &TwitchActions::nextAnnounce);
}

QString TwitchActions::channelProblem(const QString &scope) const
{
    if (!m_twitch->isActive())
        return QStringLiteral("Twitch nicht verbunden");
    if (m_twitch->channel()->state() != TwitchAuth::State::Ok)
        return QStringLiteral("Kanal nicht angemeldet");
    if (!scope.isEmpty() && m_twitch->channel()->missingScopes().contains(scope))
        return QStringLiteral("Kanal: Recht %1 fehlt").arg(scope);
    return {};
}

void TwitchActions::userInfo(const QString &login, dss::actions::StepDone done)
{
    const QString problem = channelProblem();
    if (!problem.isEmpty()) {
        done(StepResult::failure(problem));
        return;
    }
    // Twitch speichert Logins klein (Get Users: login)
    dss::twitch::getUser(m_twitch->helix(), m_twitch->channel(), login.toLower(),
                         [done](const dss::twitch::UserInfoResult &r) {
        if (!r.error.isEmpty()) {
            done(StepResult::failure(r.error));
            return;
        }
        StepResult result;
        if (!r.found) {
            result.message = QStringLiteral("Benutzer nicht gefunden");
            result.arguments.insert(QStringLiteral("targetUserFound"), false);
            done(result);
            return;
        }
        result.message = r.gameError.isEmpty() ? QStringLiteral("Benutzer gefunden")
                                               : QStringLiteral("Benutzer gefunden, Kategorie nicht geholt: %1").arg(r.gameError);
        result.arguments = {{QStringLiteral("targetUserFound"), true},
                            {QStringLiteral("targetUser"), r.user.displayName.isEmpty() ? r.user.login : r.user.displayName},
                            {QStringLiteral("targetUserName"), r.user.login},
                            {QStringLiteral("targetUserId"), r.user.id},
                            {QStringLiteral("targetUserProfileImageUrl"), r.user.profileImageUrl},
                            {QStringLiteral("targetDescription"), r.user.description},
                            {QStringLiteral("game"), r.game}};
        done(result);
    });
}

void TwitchActions::shoutout(const QString &login, dss::actions::StepDone done)
{
    const QString problem = channelProblem(kShoutoutScope);
    if (!problem.isEmpty()) {
        done(StepResult::failure(problem));
        return;
    }
    // Shoutout braucht die ID des Ziels: zuerst Get Users
    QPointer<TwitchActions> self(this);
    dss::twitch::getUser(m_twitch->helix(), m_twitch->channel(), login.toLower(),
                         [self, done](const dss::twitch::UserInfoResult &r) {
        if (!self)
            return;
        if (!r.error.isEmpty()) {
            done(StepResult::failure(r.error));
            return;
        }
        if (!r.found) {
            done(StepResult::failure(QStringLiteral("Benutzer nicht gefunden")));
            return;
        }
        const QString problem = self->channelProblem(kShoutoutScope);
        if (!problem.isEmpty()) {
            done(StepResult::failure(problem));
            return;
        }
        const QString channelId = self->m_twitch->channel()->userId();
        dss::twitch::sendShoutout(self->m_twitch->helix(), self->m_twitch->channel(), channelId, r.user.id, channelId,
                                  [done](const dss::twitch::HelixCallResult &s) {
            done(s.ok ? StepResult{true, QStringLiteral("Shoutout gegeben"), {}}
                      : StepResult::failure(QStringLiteral("Shoutout abgelehnt: %1").arg(s.error)));
        });
    });
}

void TwitchActions::announce(const dss::actions::AnnounceRequest &request, dss::actions::StepDone done)
{
    // Farben laut Schritt 0 (Abschnitt 17); leer = Standard von Twitch (primary)
    static const QStringList colors{QStringLiteral("blue"), QStringLiteral("green"), QStringLiteral("orange"),
                                    QStringLiteral("purple"), QStringLiteral("primary")};
    dss::actions::AnnounceRequest checked = request;
    checked.color = request.color.trimmed().toLower();
    if (!checked.color.isEmpty() && !colors.contains(checked.color)) {
        done(StepResult::failure(QStringLiteral("Farbe «%1» unbekannt (blue, green, orange, purple, primary)").arg(request.color)));
        return;
    }
    m_announceQueue.append({checked, std::move(done)});
    if (!m_announceBusy && !m_announceTimer->isActive())
        nextAnnounce();
}

void TwitchActions::nextAnnounce()
{
    if (m_announceBusy || m_announceQueue.isEmpty())
        return;
    if (m_lastAnnounce.isValid() && m_lastAnnounce.elapsed() < kAnnounceIntervalMs) {
        m_announceTimer->start(int(kAnnounceIntervalMs - m_lastAnnounce.elapsed()));
        return;
    }
    const PendingAnnounce pending = m_announceQueue.takeFirst();
    const QString problem = channelProblem();
    if (!problem.isEmpty()) {
        pending.done(StepResult::failure(problem));
        nextAnnounce();
        return;
    }
    const TwitchAuth *bot = m_twitch->bot();
    const bool botReady = bot->state() == TwitchAuth::State::Ok && !bot->missingScopes().contains(kAnnounceScope);
    QString note;
    if (pending.request.useBot && !botReady)
        note = bot->state() == TwitchAuth::State::Ok ? QStringLiteral("Bot: Recht %1 fehlt").arg(kAnnounceScope)
                                                      : QStringLiteral("Bot nicht angemeldet");
    m_announceBusy = true;
    m_lastAnnounce.start();
    postAnnounce(pending, pending.request.useBot && botReady, note);
}

void TwitchActions::postAnnounce(const PendingAnnounce &pending, bool asBot, const QString &note)
{
    if (!asBot) {
        const QString problem = channelProblem(kAnnounceScope);
        if (!problem.isEmpty()) {
            m_announceBusy = false;
            pending.done(StepResult::failure(note.isEmpty() ? problem : QStringLiteral("%1; %2").arg(note, problem)));
            nextAnnounce();
            return;
        }
    }
    TwitchAuth *account = asBot ? m_twitch->bot() : m_twitch->channel();
    QPointer<TwitchActions> self(this);
    dss::twitch::sendAnnouncement(
        m_twitch->helix(), account, m_twitch->channel()->userId(), account->userId(), pending.request.text,
        pending.request.color, [self, pending, asBot, note](const dss::twitch::HelixCallResult &r) {
            if (!self)
                return;
            // 403: Bot moderiert nicht (Schritt 0 Abschnitt 17), dann sendet der Kanal
            if (asBot && r.status == 403) {
                self->postAnnounce(pending, false, QStringLiteral("Bot ist kein Moderator"));
                return;
            }
            self->m_announceBusy = false;
            const QString who = asBot ? QStringLiteral("als Bot") : QStringLiteral("als Kanal");
            if (r.ok)
                pending.done({true, note.isEmpty() ? QStringLiteral("Ansage gesendet %1").arg(who)
                                                   : QStringLiteral("Ansage gesendet %1 (%2)").arg(who, note), {}});
            else
                pending.done(StepResult::failure(QStringLiteral("Ansage %1 abgelehnt: %2").arg(who, r.error)));
            self->nextAnnounce();
        });
}
