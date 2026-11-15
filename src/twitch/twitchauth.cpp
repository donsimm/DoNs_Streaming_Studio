#include "twitchauth.h"

#include "secretstore.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace dss::twitch {

namespace {

constexpr int kDefaultPollMs = 5000;

int httpStatus(QNetworkReply *reply)
{
    return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
}

QJsonObject jsonBody(QNetworkReply *reply)
{
    return QJsonDocument::fromJson(reply->readAll()).object();
}

QStringList stringList(const QJsonValue &value)
{
    QStringList result;
    for (const QJsonValue &item : value.toArray())
        result.append(item.toString());
    return result;
}

} // namespace

QStringList channelScopes()
{
    // https://dev.twitch.tv/docs/authentication/scopes/ (Schritt 0, B 15, gelesen 2026-11-15)
    return {
        QStringLiteral("analytics:read:extensions"), QStringLiteral("analytics:read:games"),
        QStringLiteral("bits:read"),
        QStringLiteral("channel:bot"), QStringLiteral("channel:edit:commercial"), QStringLiteral("channel:manage:ads"),
        QStringLiteral("channel:manage:broadcast"), QStringLiteral("channel:manage:clips"),
        QStringLiteral("channel:manage:extensions"), QStringLiteral("channel:manage:guest_star"),
        QStringLiteral("channel:manage:moderators"), QStringLiteral("channel:manage:polls"),
        QStringLiteral("channel:manage:predictions"), QStringLiteral("channel:manage:raids"),
        QStringLiteral("channel:manage:redemptions"), QStringLiteral("channel:manage:schedule"),
        QStringLiteral("channel:manage:videos"), QStringLiteral("channel:manage:vips"), QStringLiteral("channel:moderate"),
        QStringLiteral("channel:read:ads"), QStringLiteral("channel:read:charity"), QStringLiteral("channel:read:editors"),
        QStringLiteral("channel:read:goals"), QStringLiteral("channel:read:guest_star"),
        QStringLiteral("channel:read:hype_train"), QStringLiteral("channel:read:polls"),
        QStringLiteral("channel:read:predictions"), QStringLiteral("channel:read:redemptions"),
        QStringLiteral("channel:read:stream_key"), QStringLiteral("channel:read:subscriptions"),
        QStringLiteral("channel:read:vips"),
        QStringLiteral("clips:edit"), QStringLiteral("editor:manage:clips"),
        QStringLiteral("moderation:read"),
        QStringLiteral("moderator:manage:announcements"), QStringLiteral("moderator:manage:automod"),
        QStringLiteral("moderator:manage:automod_settings"), QStringLiteral("moderator:manage:banned_users"),
        QStringLiteral("moderator:manage:blocked_terms"), QStringLiteral("moderator:manage:chat_messages"),
        QStringLiteral("moderator:manage:chat_settings"), QStringLiteral("moderator:manage:guest_star"),
        QStringLiteral("moderator:manage:shield_mode"), QStringLiteral("moderator:manage:shoutouts"),
        QStringLiteral("moderator:manage:suspicious_users"), QStringLiteral("moderator:manage:unban_requests"),
        QStringLiteral("moderator:manage:warnings"),
        QStringLiteral("moderator:read:automod_settings"), QStringLiteral("moderator:read:banned_users"),
        QStringLiteral("moderator:read:blocked_terms"), QStringLiteral("moderator:read:chat_messages"),
        QStringLiteral("moderator:read:chat_settings"), QStringLiteral("moderator:read:chatters"),
        QStringLiteral("moderator:read:followers"), QStringLiteral("moderator:read:guest_star"),
        QStringLiteral("moderator:read:moderators"), QStringLiteral("moderator:read:shield_mode"),
        QStringLiteral("moderator:read:shoutouts"), QStringLiteral("moderator:read:suspicious_users"),
        QStringLiteral("moderator:read:unban_requests"), QStringLiteral("moderator:read:vips"),
        QStringLiteral("moderator:read:warnings"),
        QStringLiteral("user:bot"), QStringLiteral("user:edit"), QStringLiteral("user:edit:broadcast"),
        QStringLiteral("user:manage:blocked_users"), QStringLiteral("user:manage:chat_color"),
        QStringLiteral("user:manage:whispers"), QStringLiteral("user:read:blocked_users"),
        QStringLiteral("user:read:broadcast"), QStringLiteral("user:read:chat"), QStringLiteral("user:read:email"),
        QStringLiteral("user:read:emotes"), QStringLiteral("user:read:follows"),
        QStringLiteral("user:read:moderated_channels"), QStringLiteral("user:read:subscriptions"),
        QStringLiteral("user:read:whispers"), QStringLiteral("user:write:chat"),
    };
}

QStringList botScopes()
{
    return {QStringLiteral("user:write:chat"), QStringLiteral("user:bot"),
            QStringLiteral("moderator:manage:announcements")};
}

TwitchAuth::TwitchAuth(Account account, QNetworkAccessManager *network, SecretStore *store, QObject *parent)
    : TwitchAuth(account, network, store, Timing(), parent)
{
}

TwitchAuth::TwitchAuth(Account account, QNetworkAccessManager *network, SecretStore *store, const Timing &timing,
                       QObject *parent)
    : QObject(parent)
    , m_account(account)
    , m_network(network)
    , m_store(store)
    , m_timing(timing)
    , m_pollTimer(new QTimer(this))
    , m_codeTimer(new QTimer(this))
    , m_refreshTimer(new QTimer(this))
    , m_validateTimer(new QTimer(this))
    , m_retryTimer(new QTimer(this))
{
    m_pollTimer->setSingleShot(true);
    m_codeTimer->setSingleShot(true);
    m_refreshTimer->setSingleShot(true);
    m_retryTimer->setSingleShot(true);
    m_validateTimer->setInterval(m_timing.validateInterval);
    connect(m_pollTimer, &QTimer::timeout, this, &TwitchAuth::poll);
    connect(m_codeTimer, &QTimer::timeout, this, &TwitchAuth::loginExpired);
    connect(m_refreshTimer, &QTimer::timeout, this, &TwitchAuth::refresh);
    connect(m_validateTimer, &QTimer::timeout, this, [this] { validate(); });
    connect(m_retryTimer, &QTimer::timeout, this, [this] {
        const std::function<void()> action = std::move(m_retryAction);
        m_retryAction = nullptr;
        if (action)
            action();
    });
}

TwitchAuth::~TwitchAuth() = default;

QString TwitchAuth::label() const
{
    return m_account == Account::Channel ? QStringLiteral("Kanal") : QStringLiteral("Bot");
}

void TwitchAuth::setState(State state, const QString &message)
{
    if (state == m_state && message == m_message)
        return;
    m_state = state;
    m_message = message;
    emit changed();
}

QStringList TwitchAuth::missingScopes() const
{
    QStringList missing;
    for (const QString &scope : m_requiredScopes) {
        if (!m_token.scopes.contains(scope))
            missing.append(scope);
    }
    return missing;
}

QString TwitchAuth::scopeMessage() const
{
    const QStringList missing = missingScopes();
    if (missing.isEmpty())
        return {};
    return QStringLiteral("Rechte fehlen: %1. Neu anmelden.").arg(missing.join(QStringLiteral(", ")));
}

QString TwitchAuth::accessToken() const
{
    return m_state == State::Expired ? QString() : m_token.accessToken;
}

QNetworkReply *TwitchAuth::postForm(const QString &path, const QList<std::pair<QString, QString>> &fields)
{
    QNetworkRequest request(m_endpoints.idUrl(path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(m_timing.requestTimeout);
    QByteArray body;
    for (const auto &[name, value] : fields) {
        if (!body.isEmpty())
            body += '&';
        body += QUrl::toPercentEncoding(name) + '=' + QUrl::toPercentEncoding(value);
    }
    return m_network->post(request, body);
}

void TwitchAuth::stopTimers()
{
    m_refreshTimer->stop();
    m_validateTimer->stop();
    m_retryTimer->stop();
    m_retryAction = nullptr;
}

void TwitchAuth::scheduleTimers()
{
    if (m_token.accessToken.isEmpty())
        return;
    const qint64 untilRefresh = QDateTime::currentDateTimeUtc().msecsTo(m_token.expiresAt) - m_timing.refreshMargin;
    m_refreshTimer->start(int(std::clamp<qint64>(untilRefresh, 0, std::numeric_limits<int>::max())));
    if (!m_validateTimer->isActive())
        m_validateTimer->start();
}

void TwitchAuth::scheduleRetry(std::function<void()> action)
{
    const double base = std::min<double>(m_timing.retryMax, m_timing.retryMin * std::pow(2.0, m_retryAttempt));
    const double jitter = 0.8 + 0.4 * QRandomGenerator::global()->generateDouble();
    const int delay = int(std::min<double>(m_timing.retryMax, base * jitter));
    if (m_retryAttempt < 30)
        ++m_retryAttempt;
    m_retryAction = std::move(action);
    m_retryTimer->start(delay);
}

void TwitchAuth::networkProblem(QNetworkReply *reply, std::function<void()> retry)
{
    const int status = httpStatus(reply);
    const QString cause = status == 0 ? reply->errorString() : QStringLiteral("HTTP %1").arg(status);
    scheduleRetry(std::move(retry));
    const int seconds = (m_retryTimer->remainingTime() + 999) / 1000;
    qWarning("Twitch (%s): nicht erreichbar (%s), neuer Versuch in %d s", qPrintable(label()), qPrintable(cause), seconds);
    setState(State::Error, QStringLiteral("Twitch nicht erreichbar (%1). Neuer Versuch in %2 s.").arg(cause).arg(seconds));
}

// --- Laden ---------------------------------------------------------------------------------------

void TwitchAuth::load()
{
    if (!m_store->isSet()) {
        setState(State::Off, QString());
        return;
    }
    setState(State::Checking, QString());
    const quint64 generation = m_generation;
    const QPointer<TwitchAuth> self(this);
    m_store->read([self, generation](const std::optional<QString> &value) {
        if (!self || generation != self->m_generation)
            return;
        const std::optional<Token> token = value ? tokenFromJson(value->toUtf8()) : std::nullopt;
        if (value && value->isEmpty()) {
            self->setState(State::Off, QString());
            return;
        }
        if (!token) {
            qWarning("Twitch (%s): gespeicherte Anmeldung nicht lesbar", qPrintable(self->label()));
            self->setState(State::Error, QStringLiteral("Gespeicherte Anmeldung bei Twitch (%1) nicht lesbar. "
                                                        "Im Dialog «Einstellungen» neu anmelden.")
                                             .arg(self->label()));
            return;
        }
        self->m_token = *token;
        emit self->changed();
        if (QDateTime::currentDateTimeUtc().msecsTo(self->m_token.expiresAt) <= self->m_timing.refreshMargin) {
            self->renew([self](bool ok) {
                if (self && ok)
                    self->validate(true);
            });
        } else {
            self->validate();
        }
    });
}

// --- Gerätefluss ---------------------------------------------------------------------------------

void TwitchAuth::stopLogin()
{
    m_pollTimer->stop();
    m_codeTimer->stop();
    if (m_loginReply) {
        QNetworkReply *reply = m_loginReply;
        m_loginReply = nullptr;
        reply->abort();
    }
    m_pending.reset();
    m_deviceCode.clear();
    m_newToken.reset();
}

void TwitchAuth::startLogin()
{
    stopLogin();
    if (m_clientId.isEmpty()) {
        setState(m_token.accessToken.isEmpty() ? State::Error : m_state,
                 QStringLiteral("Client-ID fehlt. Zuerst die Client-ID eintragen."));
        return;
    }
    m_requestedScopes = m_requiredScopes;
    QNetworkReply *reply = postForm(QStringLiteral("/oauth2/device"),
                                    {{QStringLiteral("client_id"), m_clientId},
                                     {QStringLiteral("scopes"), m_requestedScopes.join(QLatin1Char(' '))}});
    m_loginReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onDeviceReply(reply); });
}

void TwitchAuth::cancelLogin()
{
    if (!m_pending && !m_loginReply)
        return;
    stopLogin();
    emit changed();
}

void TwitchAuth::onDeviceReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_loginReply)
        return; // abgebrochen
    m_loginReply = nullptr;
    const int status = httpStatus(reply);
    const QJsonObject body = jsonBody(reply);
    if (status != 200) {
        const QString cause = status == 0 ? reply->errorString()
                                          : QStringLiteral("%1, %2").arg(status).arg(body.value(QStringLiteral("message")).toString());
        qWarning("Twitch (%s): Anmeldung nicht gestartet (%s)", qPrintable(label()), qPrintable(cause));
        setState(m_token.accessToken.isEmpty() ? State::Error : m_state,
                 QStringLiteral("Anmeldung bei Twitch nicht gestartet (%1).").arg(cause));
        return;
    }
    const QString deviceCode = body.value(QStringLiteral("device_code")).toString();
    const QString userCode = body.value(QStringLiteral("user_code")).toString();
    const QUrl uri(body.value(QStringLiteral("verification_uri")).toString());
    const int expiresIn = body.value(QStringLiteral("expires_in")).toInt();
    const int interval = body.value(QStringLiteral("interval")).toInt();
    if (deviceCode.isEmpty() || userCode.isEmpty() || !uri.isValid() || expiresIn <= 0) {
        setState(m_token.accessToken.isEmpty() ? State::Error : m_state,
                 QStringLiteral("Antwort von Twitch unvollständig, Anmeldung nicht gestartet."));
        return;
    }
    m_deviceCode = deviceCode;
    m_pending = Pending{userCode, uri, QDateTime::currentDateTimeUtc().addSecs(expiresIn)};
    m_pollInterval = m_timing.pollInterval > 0 ? m_timing.pollInterval : interval > 0 ? interval * 1000 : kDefaultPollMs;
    m_codeTimer->start(expiresIn * 1000);
    m_pollTimer->start(m_pollInterval);
    qInfo("Twitch (%s): Anmeldung gestartet, Code gültig %d s", qPrintable(label()), expiresIn);
    setState(m_token.accessToken.isEmpty() ? State::Off : m_state, QString());
    emit changed();
}

void TwitchAuth::poll()
{
    if (!m_pending)
        return;
    QList<std::pair<QString, QString>> fields{{QStringLiteral("client_id"), m_clientId}};
    if (!m_clientSecret.isEmpty())
        fields.append({QStringLiteral("client_secret"), m_clientSecret});
    fields.append({QStringLiteral("device_code"), m_deviceCode});
    fields.append({QStringLiteral("scopes"), m_requestedScopes.join(QLatin1Char(' '))});
    fields.append({QStringLiteral("grant_type"), QStringLiteral("urn:ietf:params:oauth:grant-type:device_code")});
    QNetworkReply *reply = postForm(QStringLiteral("/oauth2/token"), fields);
    m_loginReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onPollReply(reply); });
}

void TwitchAuth::onPollReply(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_loginReply)
        return;
    m_loginReply = nullptr;
    const int status = httpStatus(reply);
    const QJsonObject body = jsonBody(reply);
    if (status == 200) {
        Token token;
        token.accessToken = body.value(QStringLiteral("access_token")).toString();
        token.refreshToken = body.value(QStringLiteral("refresh_token")).toString();
        token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(body.value(QStringLiteral("expires_in")).toInt());
        token.scopes = stringList(body.value(QStringLiteral("scope")));
        m_pollTimer->stop();
        m_codeTimer->stop();
        m_pending.reset();
        m_deviceCode.clear();
        if (token.accessToken.isEmpty()) {
            setState(m_token.accessToken.isEmpty() ? State::Error : m_state,
                     QStringLiteral("Antwort von Twitch ohne Token, Anmeldung nicht abgeschlossen."));
            return;
        }
        m_newToken = token;
        emit changed();
        checkNewToken();
        return;
    }
    const QString reason = body.value(QStringLiteral("message")).toString();
    if (status == 400 && reason == QLatin1String("authorization_pending")) {
        m_pollTimer->start(m_pollInterval);
        return;
    }
    if (status == 400 && reason == QLatin1String("invalid device code")) {
        loginExpired();
        return;
    }
    if (status == 0 || status >= 500) {
        m_pollTimer->start(m_pollInterval); // Netz: weiter versuchen, bis der Code abläuft
        return;
    }
    stopLogin();
    qWarning("Twitch (%s): Anmeldung abgelehnt (%d, %s)", qPrintable(label()), status, qPrintable(reason));
    setState(m_token.accessToken.isEmpty() ? State::Error : m_state,
             QStringLiteral("Anmeldung bei Twitch abgelehnt (%1).").arg(reason.isEmpty() ? QString::number(status) : reason));
}

void TwitchAuth::loginExpired()
{
    stopLogin();
    qInfo("Twitch (%s): Code der Anmeldung abgelaufen", qPrintable(label()));
    setState(m_token.accessToken.isEmpty() ? State::Off : m_state, QStringLiteral("Code abgelaufen, neu anmelden."));
    emit changed();
}

// Neuer Token aus dem Gerätefluss: Konto und Rechte prüfen, dann ablegen.
void TwitchAuth::checkNewToken()
{
    QNetworkRequest request(m_endpoints.idUrl(QStringLiteral("/oauth2/validate")));
    request.setRawHeader("Authorization", "OAuth " + m_newToken->accessToken.toUtf8());
    request.setTransferTimeout(m_timing.requestTimeout);
    QNetworkReply *reply = m_network->get(request);
    m_loginReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply != m_loginReply || !m_newToken)
            return;
        m_loginReply = nullptr;
        const int status = httpStatus(reply);
        const QJsonObject body = jsonBody(reply);
        if (status != 200) {
            rejectNewToken(QStringLiteral("Anmeldung bei Twitch nicht prüfbar (%1). Neu anmelden.")
                               .arg(status == 0 ? reply->errorString() : QString::number(status)));
            return;
        }
        Token token = *m_newToken;
        token.userId = body.value(QStringLiteral("user_id")).toString();
        token.login = body.value(QStringLiteral("login")).toString();
        token.scopes = stringList(body.value(QStringLiteral("scopes")));
        if (!m_rejectedUserId.isEmpty() && token.userId == m_rejectedUserId) {
            rejectNewToken(QStringLiteral("Das Bot-Konto ist das Kanal-Konto. Im Browser mit dem Bot-Konto anmelden."));
            return;
        }
        m_newToken = token;
        const QPointer<TwitchAuth> self(this);
        save(token, [self, token](bool ok) {
            if (!self || !self->m_newToken)
                return;
            self->m_newToken.reset();
            if (!ok) {
                self->setState(self->m_token.accessToken.isEmpty() ? State::Error : self->m_state,
                               QStringLiteral("Anmeldung bei Twitch (%1) nicht gespeichert (systemd-creds, siehe "
                                              "Protokoll des Kerns).").arg(self->label()));
                return;
            }
            ++self->m_generation; // Antworten zum alten Token zählen nicht mehr
            self->stopTimers();
            self->m_refreshing = false;
            self->finishRenewals(false);
            self->m_token = token;
            self->m_retryAttempt = 0;
            qInfo("Twitch (%s): angemeldet als %s", qPrintable(self->label()), qPrintable(token.login));
            self->setState(State::Ok, self->scopeMessage());
            self->scheduleTimers();
            emit self->changed();
        });
    });
}

void TwitchAuth::rejectNewToken(const QString &message)
{
    // Das Token wird nicht gebraucht: bei Twitch zurückgeben, Antwort egal.
    QNetworkReply *revoke = postForm(QStringLiteral("/oauth2/revoke"), {{QStringLiteral("client_id"), m_clientId},
                                                                         {QStringLiteral("token"), m_newToken->accessToken}});
    connect(revoke, &QNetworkReply::finished, revoke, &QObject::deleteLater);
    m_newToken.reset();
    qWarning("Twitch (%s): Anmeldung abgewiesen: %s", qPrintable(label()), qPrintable(message));
    setState(m_token.accessToken.isEmpty() ? State::Error : m_state, message);
    emit changed();
}

// --- Erneuern ------------------------------------------------------------------------------------

void TwitchAuth::renew(std::function<void(bool)> done)
{
    if (m_state == State::Expired || m_token.refreshToken.isEmpty()) {
        done(false);
        return;
    }
    m_renewals.append(std::move(done));
    refresh();
}

void TwitchAuth::refresh()
{
    if (m_refreshing)
        return;
    if (m_token.refreshToken.isEmpty()) {
        expire();
        return;
    }
    m_refreshing = true;
    m_refreshTimer->stop();
    QList<std::pair<QString, QString>> fields{{QStringLiteral("client_id"), m_clientId}};
    if (!m_clientSecret.isEmpty())
        fields.append({QStringLiteral("client_secret"), m_clientSecret});
    fields.append({QStringLiteral("grant_type"), QStringLiteral("refresh_token")});
    fields.append({QStringLiteral("refresh_token"), m_token.refreshToken});
    QNetworkReply *reply = postForm(QStringLiteral("/oauth2/token"), fields);
    const quint64 generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        onRefreshReply(reply);
    });
}

void TwitchAuth::onRefreshReply(QNetworkReply *reply)
{
    m_refreshing = false;
    const int status = httpStatus(reply);
    const QJsonObject body = jsonBody(reply);
    if (status == 200 && !body.value(QStringLiteral("access_token")).toString().isEmpty()) {
        m_token.accessToken = body.value(QStringLiteral("access_token")).toString();
        m_token.refreshToken = body.value(QStringLiteral("refresh_token")).toString();
        m_token.expiresAt = QDateTime::currentDateTimeUtc().addSecs(body.value(QStringLiteral("expires_in")).toInt());
        if (body.contains(QStringLiteral("scope")))
            m_token.scopes = stringList(body.value(QStringLiteral("scope")));
        m_retryAttempt = 0;
        // Sofort ablegen: das alte Refresh-Token gilt nicht mehr (Plan 2.3).
        const QPointer<TwitchAuth> self(this);
        save(m_token, [self](bool ok) {
            if (self && !ok)
                self->setState(self->m_state, QStringLiteral("Erneuerte Anmeldung bei Twitch (%1) nicht gespeichert. "
                                                             "Nach einem Neustart neu anmelden.").arg(self->label()));
        });
        qInfo("Twitch (%s): Anmeldung erneuert", qPrintable(label()));
        if (m_state != State::Checking)
            setState(State::Ok, scopeMessage());
        scheduleTimers();
        emit changed();
        finishRenewals(true);
        return;
    }
    if (status == 400 || status == 401) {
        expire();
        return;
    }
    finishRenewals(false);
    networkProblem(reply, [this] { refresh(); });
}

void TwitchAuth::expire()
{
    stopTimers();
    m_token.accessToken.clear();
    m_token.refreshToken.clear();
    qWarning("Twitch (%s): Anmeldung abgelaufen", qPrintable(label()));
    setState(State::Expired, QStringLiteral("Anmeldung bei Twitch abgelaufen (%1). Im Dialog «Einstellungen» neu anmelden.")
                                 .arg(label()));
    finishRenewals(false);
}

void TwitchAuth::finishRenewals(bool ok)
{
    const QList<std::function<void(bool)>> renewals = std::exchange(m_renewals, {});
    for (const auto &done : renewals)
        done(ok);
}

// --- Prüfen --------------------------------------------------------------------------------------

void TwitchAuth::validate(bool retried)
{
    if (m_token.accessToken.isEmpty())
        return;
    QNetworkRequest request(m_endpoints.idUrl(QStringLiteral("/oauth2/validate")));
    request.setRawHeader("Authorization", "OAuth " + m_token.accessToken.toUtf8());
    request.setTransferTimeout(m_timing.requestTimeout);
    QNetworkReply *reply = m_network->get(request);
    const quint64 generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, retried] {
        reply->deleteLater();
        if (generation != m_generation)
            return;
        onValidateReply(reply, retried);
    });
}

void TwitchAuth::onValidateReply(QNetworkReply *reply, bool retried)
{
    const int status = httpStatus(reply);
    const QJsonObject body = jsonBody(reply);
    if (status == 200) {
        const QString userId = body.value(QStringLiteral("user_id")).toString();
        const QString login = body.value(QStringLiteral("login")).toString();
        const QStringList scopes = stringList(body.value(QStringLiteral("scopes")));
        const bool changedToken = userId != m_token.userId || login != m_token.login || scopes != m_token.scopes;
        m_token.userId = userId;
        m_token.login = login;
        m_token.scopes = scopes;
        if (changedToken)
            save(m_token, [](bool) {});
        m_retryAttempt = 0;
        const QStringList missing = missingScopes();
        if (!missing.isEmpty())
            qWarning("Twitch (%s): %lld Rechte fehlen", qPrintable(label()), qlonglong(missing.size()));
        setState(State::Ok, scopeMessage());
        scheduleTimers();
        if (changedToken)
            emit changed();
        return;
    }
    if (status == 401) {
        if (retried) {
            expire();
            return;
        }
        const QPointer<TwitchAuth> self(this);
        renew([self](bool ok) {
            if (self && ok)
                self->validate(true);
        });
        return;
    }
    networkProblem(reply, [this, retried] { validate(retried); });
}

// --- Abmelden ------------------------------------------------------------------------------------

void TwitchAuth::logout()
{
    ++m_generation;
    stopLogin();
    stopTimers();
    m_refreshing = false;
    finishRenewals(false);
    if (!m_token.accessToken.isEmpty()) {
        QNetworkReply *revoke = postForm(QStringLiteral("/oauth2/revoke"), {{QStringLiteral("client_id"), m_clientId},
                                                                             {QStringLiteral("token"), m_token.accessToken}});
        connect(revoke, &QNetworkReply::finished, revoke, &QObject::deleteLater);
    }
    m_token = Token();
    QString error;
    if (!m_store->remove(&error))
        qWarning("Twitch (%s): %s", qPrintable(label()), qPrintable(error));
    qInfo("Twitch (%s): abgemeldet", qPrintable(label()));
    setState(State::Off, error);
    emit changed();
}

void TwitchAuth::unload()
{
    ++m_generation;
    stopLogin();
    stopTimers();
    m_refreshing = false;
    m_newToken.reset();
    finishRenewals(false);
    m_token = Token();
    setState(State::Off, QString());
    emit changed();
}

// --- Ablage --------------------------------------------------------------------------------------

void TwitchAuth::save(const Token &token, std::function<void(bool)> done)
{
    const QPointer<TwitchAuth> self(this);
    m_store->write(QString::fromUtf8(tokenToJson(token)), [self, done](const QString &error) {
        if (!self)
            return;
        if (!error.isEmpty())
            qWarning("Twitch (%s): %s", qPrintable(self->label()), qPrintable(error));
        done(error.isEmpty());
    });
}

std::optional<TwitchAuth::Token> TwitchAuth::tokenFromJson(const QByteArray &json)
{
    const QJsonObject object = QJsonDocument::fromJson(json).object();
    Token token;
    token.accessToken = object.value(QStringLiteral("access_token")).toString();
    token.refreshToken = object.value(QStringLiteral("refresh_token")).toString();
    token.expiresAt = QDateTime::fromString(object.value(QStringLiteral("expires_at")).toString(), Qt::ISODateWithMs);
    token.userId = object.value(QStringLiteral("user_id")).toString();
    token.login = object.value(QStringLiteral("login")).toString();
    token.scopes = stringList(object.value(QStringLiteral("scopes")));
    if (token.accessToken.isEmpty() || token.refreshToken.isEmpty() || !token.expiresAt.isValid())
        return std::nullopt;
    token.expiresAt = token.expiresAt.toUTC();
    return token;
}

QByteArray TwitchAuth::tokenToJson(const Token &token)
{
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("access_token"), token.accessToken},
                             {QStringLiteral("refresh_token"), token.refreshToken},
                             {QStringLiteral("expires_at"), token.expiresAt.toUTC().toString(Qt::ISODateWithMs)},
                             {QStringLiteral("user_id"), token.userId},
                             {QStringLiteral("login"), token.login},
                             {QStringLiteral("scopes"), QJsonArray::fromStringList(token.scopes)},
                         })
        .toJson(QJsonDocument::Compact);
}

} // namespace dss::twitch
