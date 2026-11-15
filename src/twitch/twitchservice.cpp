#include "twitchservice.h"

#include "chatsession.h"
#include "eventsubconnection.h"
#include "helixclient.h"
#include "secretstore.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace dss::twitch {

namespace {

// Längste Wartezeit bis zum Erneuern des App-Tokens (Twitch gibt etwa 60 Tage; QTimer nimmt int ms).
constexpr qint64 kMaxAppTokenWaitMs = 24LL * 60 * 60 * 1000;

const char *stateName(TwitchService::State state)
{
    switch (state) {
    case TwitchService::State::Off:
        return "aus";
    case TwitchService::State::Connecting:
        return "verbindet";
    case TwitchService::State::Connected:
        return "verbunden";
    case TwitchService::State::Error:
        return "Fehler";
    }
    return "?";
}

} // namespace

// --- App-Token -----------------------------------------------------------------------------------

AppToken::AppToken(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent)
    , m_network(network)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, &AppToken::fetch);
}

void AppToken::configure(const QString &clientId, const QString &clientSecret)
{
    if (clientId == m_clientId && clientSecret == m_clientSecret && (isValid() || m_reply || m_timer->isActive()))
        return;
    stop();
    m_clientId = clientId;
    m_clientSecret = clientSecret;
    if (clientId.isEmpty() || clientSecret.isEmpty()) {
        m_problem = clientSecret.isEmpty() ? QStringLiteral("Client Secret fehlt") : QStringLiteral("Client-ID fehlt");
        emit changed();
        return;
    }
    fetch();
    emit changed();
}

void AppToken::stop()
{
    ++m_generation;
    m_timer->stop();
    if (QNetworkReply *reply = std::exchange(m_reply, nullptr))
        reply->abort();
    m_token.clear();
    m_problem.clear();
    m_retryAttempt = 0;
    finish(false);
}

void AppToken::renew(std::function<void(bool)> done)
{
    m_waiting.append(std::move(done));
    m_token.clear();
    if (m_clientId.isEmpty() || m_clientSecret.isEmpty()) {
        finish(false);
        return;
    }
    if (!m_reply)
        fetch();
}

void AppToken::fetch()
{
    m_timer->stop();
    QNetworkRequest request(m_endpoints.idUrl(QStringLiteral("/oauth2/token")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    request.setTransferTimeout(m_timing.requestTimeout);
    const QByteArray body = "client_id=" + QUrl::toPercentEncoding(m_clientId) + "&client_secret="
                            + QUrl::toPercentEncoding(m_clientSecret) + "&grant_type=client_credentials";
    QNetworkReply *reply = m_network->post(request, body);
    m_reply = reply;
    const quint64 generation = m_generation;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] { onReply(reply, generation); });
}

void AppToken::onReply(QNetworkReply *reply, quint64 generation)
{
    reply->deleteLater();
    if (generation != m_generation)
        return;
    m_reply = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0) {
        const int delay = backoffDelay(m_retryAttempt++, m_timing.retryMin, m_timing.retryMax,
                                       QRandomGenerator::global()->generateDouble());
        qWarning("Twitch: App-Token nicht erhalten (Netz), neuer Versuch in %d ms", delay);
        m_problem = QStringLiteral("App-Token nicht erhalten (Netz), neuer Versuch läuft");
        m_timer->start(delay);
        finish(false);
        emit changed();
        return;
    }
    const QJsonObject body = QJsonDocument::fromJson(reply->readAll()).object();
    const QString token = body.value(QStringLiteral("access_token")).toString();
    if (status != 200 || token.isEmpty()) {
        qWarning("Twitch: App-Token abgewiesen (HTTP %d)", status);
        m_token.clear();
        m_problem = QStringLiteral("App-Token abgewiesen (HTTP %1), Client-ID und Client Secret prüfen").arg(status);
        finish(false);
        emit changed();
        return;
    }
    m_token = token;
    m_problem.clear();
    m_retryAttempt = 0;
    const qint64 expiresMs = qint64(body.value(QStringLiteral("expires_in")).toInteger()) * 1000;
    const qint64 wait = std::clamp<qint64>(expiresMs - m_timing.refreshMargin, 1000, kMaxAppTokenWaitMs);
    m_timer->start(int(wait));
    qInfo("Twitch: App-Token erhalten (gültig %lld s)", static_cast<long long>(expiresMs / 1000));
    finish(true);
    emit changed();
}

void AppToken::finish(bool ok)
{
    const QList<std::function<void(bool)>> waiting = std::exchange(m_waiting, {});
    for (const auto &done : waiting)
        done(ok);
}

// --- Dienst --------------------------------------------------------------------------------------

TwitchService::TwitchService(events::EventBus *bus, const Options &options, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_options(options)
    , m_network(new QNetworkAccessManager(this))
{
    const QString dir = options.settingsDir + QLatin1Char('/');
    m_channelStore = std::make_unique<SecretStore>(dir + QStringLiteral("twitch-token.cred"),
                                                   QStringLiteral("dss-twitch-token"),
                                                   SecretStore::Labels{QStringLiteral("Twitch-Anmeldung (Kanal)"),
                                                                       QStringLiteral("Anmeldung")},
                                                   options.credsProgram);
    m_botStore = std::make_unique<SecretStore>(dir + QStringLiteral("twitch-bot-token.cred"),
                                               QStringLiteral("dss-twitch-bot-token"),
                                               SecretStore::Labels{QStringLiteral("Twitch-Anmeldung (Bot)"),
                                                                   QStringLiteral("Anmeldung")},
                                               options.credsProgram);
    m_secretStore = std::make_unique<SecretStore>(dir + QStringLiteral("twitch-client-secret.cred"),
                                                  QStringLiteral("dss-twitch-client-secret"),
                                                  SecretStore::Labels{QStringLiteral("Twitch-Client-Secret"),
                                                                      QStringLiteral("Client Secret")},
                                                  options.credsProgram);

    m_channel = new TwitchAuth(TwitchAuth::Account::Channel, m_network, m_channelStore.get(), this);
    m_bot = new TwitchAuth(TwitchAuth::Account::Bot, m_network, m_botStore.get(), this);
    m_helix = new HelixClient(m_network, this);
    m_connection = new EventSubConnection(bus, m_helix, this);
    m_appToken = new AppToken(m_network, this);
    m_chatSession = new ChatSession(bus, options.dataDir.isEmpty() ? QString()
                                                                   : options.dataDir + QStringLiteral("/twitch-sitzung.json"),
                                    2000, this);

    m_channel->setEndpoints(options.endpoints);
    m_bot->setEndpoints(options.endpoints);
    m_helix->setEndpoints(options.endpoints);
    m_connection->setEndpoints(options.endpoints);
    m_appToken->setEndpoints(options.endpoints);
    m_channel->setRequiredScopes(channelScopes());
    m_bot->setRequiredScopes(botScopes());

    connect(m_channel, &TwitchAuth::changed, this, &TwitchService::update);
    connect(m_bot, &TwitchAuth::changed, this, &TwitchService::update);
    connect(m_connection, &EventSubConnection::changed, this, &TwitchService::update);
    connect(m_appToken, &AppToken::changed, this, &TwitchService::update);
    // Plan 2.5: Freigabe entzogen = Anmeldung des Kanals ungültig (die Verbindung tut das nicht selbst).
    connect(m_connection, &EventSubConnection::authorizationRevoked, this, [this] {
        qWarning("Twitch: Freigabe des Kanals entzogen (authorization_revoked)");
        m_channel->markExpired();
    });
}

TwitchService::~TwitchService()
{
    flush();
    // Zuerst alles, was die Ablagen und Token benutzt; die Ablagen (unique_ptr) danach.
    for (QObject *o : std::initializer_list<QObject *>{m_connection, m_channel, m_bot, m_appToken})
        o->disconnect(this);
    delete m_connection;
    delete m_chatSession;
    delete m_appToken;
    delete m_bot;
    delete m_channel;
    delete m_helix;
}

void TwitchService::configure(bool enabled, const QString &clientId)
{
    if (enabled == m_enabled && clientId == m_clientId)
        return;
    const bool clientIdChanged = clientId != m_clientId;
    m_enabled = enabled;
    m_clientId = clientId;
    const bool run = enabled && !clientId.isEmpty();
    if (m_active && (!run || clientIdChanged))
        deactivate();
    if (run && !m_active)
        activate();
    update();
}

void TwitchService::activate()
{
    m_active = true;
    const quint64 generation = ++m_generation;
    qInfo("Twitch: eingeschaltet");
    const auto start = [this] {
        for (TwitchAuth *auth : {m_channel, m_bot}) {
            auth->setClientId(m_clientId);
            auth->setClientSecret(m_clientSecret);
        }
        m_helix->setClientId(m_clientId);
        m_appToken->configure(m_clientId, m_clientSecret);
        m_channel->load();
        m_bot->load();
        update();
    };
    if (!m_secretStore->isSet()) {
        m_clientSecret.clear();
        start();
        return;
    }
    const QPointer<TwitchService> self(this);
    m_secretStore->read([self, generation, start](const std::optional<QString> &secret) {
        if (!self || generation != self->m_generation)
            return;
        if (!secret)
            qWarning("Twitch: Client Secret nicht lesbar, weiter ohne (kein Abzeichen)");
        self->m_clientSecret = secret.value_or(QString());
        start();
    });
}

void TwitchService::deactivate()
{
    m_active = false;
    ++m_generation;
    m_connectedUserId.clear();
    m_connection->stop();
    m_channel->unload();
    m_bot->unload();
    m_appToken->stop();
    qInfo("Twitch: ausgeschaltet");
}

bool TwitchService::clientSecretSet() const
{
    return m_secretStore->isSet();
}

void TwitchService::writeClientSecret(const QString &secret, std::function<void(const QString &)> done)
{
    const QPointer<TwitchService> self(this);
    m_secretStore->write(secret, [self, secret, done](const QString &error) {
        if (self && error.isEmpty()) {
            qInfo("Twitch: Client Secret verschlüsselt gespeichert");
            self->m_clientSecret = secret;
            for (TwitchAuth *auth : {self->m_channel, self->m_bot})
                auth->setClientSecret(secret);
            if (self->m_active)
                self->m_appToken->configure(self->m_clientId, secret);
            self->update();
        }
        done(error);
    });
}

bool TwitchService::removeClientSecret(QString *error)
{
    if (!m_secretStore->remove(error))
        return false;
    qInfo("Twitch: Client Secret gelöscht");
    m_clientSecret.clear();
    for (TwitchAuth *auth : {m_channel, m_bot})
        auth->setClientSecret(QString());
    if (m_active)
        m_appToken->configure(m_clientId, QString());
    update();
    return true;
}

QString TwitchService::startLogin(TwitchAuth::Account account)
{
    if (!m_active)
        return QStringLiteral("Twitch ist aus oder die Client-ID fehlt.");
    TwitchAuth *auth = account == TwitchAuth::Account::Channel ? m_channel : m_bot;
    qInfo("Twitch (%s): Anmeldung gestartet", account == TwitchAuth::Account::Channel ? "Kanal" : "Bot");
    auth->startLogin();
    return {};
}

QString TwitchService::cancelLogin(TwitchAuth::Account account)
{
    if (!m_active)
        return QStringLiteral("Twitch ist aus oder die Client-ID fehlt.");
    (account == TwitchAuth::Account::Channel ? m_channel : m_bot)->cancelLogin();
    return {};
}

QString TwitchService::logout(TwitchAuth::Account account)
{
    // Auch bei ausgeschaltetem Twitch: Die Datei des Kontos wird gelöscht.
    (account == TwitchAuth::Account::Channel ? m_channel : m_bot)->logout();
    return {};
}

QString TwitchService::message() const
{
    if (!m_enabled)
        return QStringLiteral("Twitch ist aus.");
    if (m_clientId.isEmpty())
        return QStringLiteral("Client-ID fehlt.");
    switch (m_channel->state()) {
    case TwitchAuth::State::Off:
        return m_channel->message().isEmpty()
                   ? QStringLiteral("Kanal nicht angemeldet. Im Dialog «Einstellungen» anmelden.")
                   : m_channel->message();
    case TwitchAuth::State::Checking:
        return QStringLiteral("Anmeldung des Kanals wird geprüft.");
    case TwitchAuth::State::Expired:
    case TwitchAuth::State::Error:
        return m_channel->message();
    case TwitchAuth::State::Ok:
        break;
    }
    if (!m_channel->message().isEmpty())
        return m_channel->message();
    return m_connection->message();
}

QString TwitchService::badgeProblem() const
{
    if (!m_active)
        return {};
    QString reason;
    if (m_bot->state() != TwitchAuth::State::Ok)
        reason = QStringLiteral("Bot nicht angemeldet");
    else if (!m_bot->scopes().contains(QStringLiteral("user:bot")))
        reason = QStringLiteral("Bot ohne Recht user:bot");
    else if (m_clientSecret.isEmpty())
        reason = QStringLiteral("Client Secret fehlt");
    else if (!m_appToken->isValid())
        reason = m_appToken->problem().isEmpty() ? QStringLiteral("App-Token wird geholt") : m_appToken->problem();
    else if (m_channel->state() == TwitchAuth::State::Ok && !m_channel->scopes().contains(QStringLiteral("channel:bot")))
        reason = QStringLiteral("Kanal ohne Recht channel:bot (geht nur, wenn der Bot Moderator ist)");
    return reason.isEmpty() ? QString() : QStringLiteral("Abzeichen nicht aktiv: %1.").arg(reason);
}

TokenSource *TwitchService::botSendToken() const
{
    if (!m_active || m_bot->state() != TwitchAuth::State::Ok)
        return nullptr;
    return badgeProblem().isEmpty() ? static_cast<TokenSource *>(m_appToken) : static_cast<TokenSource *>(m_bot);
}

void TwitchService::flush()
{
    m_chatSession->flush();
}

void TwitchService::update()
{
    const TwitchAuth::State channel = m_channel->state();
    const QString channelId = m_channel->userId();
    // Plan 2.1: Der Bot darf nicht das Kanal-Konto sein.
    m_bot->setRejectedUserId(channelId);
    m_connection->setBotUserId(m_bot->state() == TwitchAuth::State::Ok ? m_bot->userId() : QString());
    // EventSub läuft, solange der Kanal angemeldet ist. Kennung vor start/stop setzen: Beide melden
    // changed() sofort, update() läuft dann noch einmal.
    if (m_active && channel == TwitchAuth::State::Ok && !channelId.isEmpty()) {
        if (m_connectedUserId != channelId) {
            m_connectedUserId = channelId;
            m_connection->start(m_channel, channelId);
        }
    } else if (!m_connectedUserId.isEmpty()
               && (!m_active || channel == TwitchAuth::State::Off || channel == TwitchAuth::State::Expired)) {
        m_connectedUserId.clear();
        m_connection->stop();
    }

    State state = State::Off;
    if (m_active) {
        switch (channel) {
        case TwitchAuth::State::Off:
            state = State::Off;
            break;
        case TwitchAuth::State::Checking:
            state = State::Connecting;
            break;
        case TwitchAuth::State::Expired:
        case TwitchAuth::State::Error:
            state = State::Error;
            break;
        case TwitchAuth::State::Ok:
            if (!m_channel->missingScopes().isEmpty())
                state = State::Error;
            else if (m_connection->state() == EventSubConnection::State::Connected)
                state = m_connection->allSubscriptionsOk() ? State::Connected : State::Error;
            else if (m_connection->state() == EventSubConnection::State::Error)
                state = State::Error;
            else
                state = State::Connecting;
            break;
        }
    }
    if (state != m_state) {
        m_state = state;
        qInfo("Twitch: Kanal %s", stateName(state));
    }
    emit stateChanged();
}

} // namespace dss::twitch
