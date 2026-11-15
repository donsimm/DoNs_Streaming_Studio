#pragma once

#include "twitchauth.h"
#include "twitchendpoints.h"

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace dss {
class SecretStore;
}
namespace dss::events {
class EventBus;
}

// Anbindung an Twitch im Kern (P13, Plan 2.6 und 2.7): hält Anmeldung (Kanal und Bot), Helix,
// EventSub und die Sitzung «erste Nachricht» zusammen. Die Ereignisse gehen über EventSubConnection
// und ChatSession auf den Fluss (Regel 14). Kennt weder den Kern noch protocol.h (Regel 9); der Kern
// übersetzt Zustand und Anfragen in die Schnittstelle (twitch_status, twitch_login_*).
//
// - Ein- und ausschalten mit configure(enabled, clientId) aus kern.ini [twitch]. Aus oder ohne
//   Client-ID: alles angehalten, die Dateien der Konten bleiben.
// - Ablage (Plan 2.2, Regel 12): twitch-token.cred (Kanal), twitch-bot-token.cred (Bot) und
//   twitch-client-secret.cred neben kern.ini, über dss::SecretStore (systemd-creds).
// - EventSub läuft, solange der Kanal angemeldet ist (TwitchAuth Ok). revocation mit
//   authorization_revoked: Anmeldung des Kanals abgelaufen. Abmelden des Kanals trennt.
// - Bot = Kanal wird abgewiesen (Plan 2.1, TwitchAuth::setRejectedUserId).
// - Abzeichen «Chat Bot» (Plan 2.4, Frage 2): App-Token mit Client Credentials (Client-ID und
//   Secret), nur im Speicher, erneuert 10 min vor Ablauf und nach 401. Aktiv nur mit App-Token,
//   user:bot beim Bot und channel:bot beim Kanal; sonst nennt badgeProblem() den Grund, und P14 sendet
//   «als Bot» mit dem User-Token des Bots (botSendToken()).
// - Zustand des Kanals für den Knopf (Plan 2.7): Off (aus, ohne Client-ID, nicht angemeldet),
//   Connecting (Prüfung oder Verbindung läuft), Connected (alle Abos aktiv), Error (Anmeldung
//   abgelaufen oder ungültig, Rechte fehlen, Abo abgewiesen, Netz). Der Bot ändert ihn nicht.
// - Nie im Protokoll: Token, Secret, device_code, user_code.
//
// Quellen:
//   https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/#client-credentials-grant-flow
//     (grant_type client_credentials mit client_id und client_secret; access_token, expires_in, kein
//     Refresh-Token)
//   https://dev.twitch.tv/docs/chat/send-receive-chat/ und
//   https://dev.twitch.tv/docs/api/reference/#send-chat-message (Abzeichen «Chat Bot»: App-Token,
//     user:bot vom Bot, channel:bot vom Kanal oder Bot als Moderator)
//   Bericht Schritt 0: docs/archiv/recherchen/2026-11-15-schritt0-twitch-streamerbot.md

namespace dss::twitch {

class ChatSession;
class EventSubConnection;
class HelixClient;

// App-Token (Client Credentials) für das Abzeichen; nur im Speicher.
class AppToken : public QObject, public TokenSource
{
    Q_OBJECT

public:
    struct Timing
    {
        int refreshMargin = 10 * 60 * 1000;
        int retryMin = 2000;
        int retryMax = 60000;
        int requestTimeout = 15000;
    };

    AppToken(QNetworkAccessManager *network, QObject *parent = nullptr);

    void setEndpoints(const Endpoints &endpoints) { m_endpoints = endpoints; }
    // Leere Client-ID oder leeres Secret: kein App-Token (problem() nennt den Grund).
    void configure(const QString &clientId, const QString &clientSecret);
    void stop();

    bool isValid() const { return !m_token.isEmpty(); }
    // Grund ohne App-Token auf Deutsch; leer, wenn es eines gibt oder es gerade geholt wird.
    QString problem() const { return m_problem; }

    // TokenSource
    QString accessToken() const override { return m_token; }
    void renew(std::function<void(bool)> done) override;

signals:
    void changed();

private:
    void fetch();
    void onReply(QNetworkReply *reply, quint64 generation);
    void finish(bool ok);

    QNetworkAccessManager *m_network;
    Timing m_timing;
    Endpoints m_endpoints = Endpoints::twitch();
    QString m_clientId;
    QString m_clientSecret;
    QString m_token;
    QString m_problem;
    QTimer *m_timer;
    QPointer<QNetworkReply> m_reply;
    int m_retryAttempt = 0;
    quint64 m_generation = 0;
    QList<std::function<void(bool)>> m_waiting;
};

class TwitchService : public QObject
{
    Q_OBJECT

public:
    enum class State { Off, Connecting, Connected, Error };

    struct Options
    {
        QString settingsDir;   // Ordner von kern.ini: dort liegen die .cred-Dateien
        QString dataDir;       // Datenordner: twitch-sitzung.json
        Endpoints endpoints = Endpoints::twitch();
        QString credsProgram = QStringLiteral("systemd-creds");
    };

    TwitchService(events::EventBus *bus, const Options &options, QObject *parent = nullptr);
    ~TwitchService() override;

    // Werte aus kern.ini [twitch]. Neue Client-ID: neu laden (laufende Anmeldungen enden).
    void configure(bool enabled, const QString &clientId);
    bool isActive() const { return m_active; }

    // Client Secret (Plan 2.2): nur «gesetzt ja/nein» geht hinaus.
    bool clientSecretSet() const;
    // done(Meldung): leer = verschlüsselt gespeichert.
    void writeClientSecret(const QString &secret, std::function<void(const QString &)> done);
    bool removeClientSecret(QString *error);

    // Anfragen des Fensters; leer = angenommen, sonst die Meldung auf Deutsch.
    QString startLogin(TwitchAuth::Account account);
    QString cancelLogin(TwitchAuth::Account account);
    QString logout(TwitchAuth::Account account);

    State state() const { return m_state; }
    // Zustand des Kanals und von EventSub auf Deutsch; leer, wenn verbunden.
    QString message() const;
    QString badgeProblem() const;

    TwitchAuth *channel() const { return m_channel; }
    TwitchAuth *bot() const { return m_bot; }
    EventSubConnection *connection() const { return m_connection; }
    HelixClient *helix() const { return m_helix; }
    // Für P14: Senden «als Bot» mit App-Token (Abzeichen) oder dem Token des Bots; nullptr ohne Bot.
    TokenSource *botSendToken() const;

    // Liste der Zuschauer sofort schreiben (Beenden des Kerns).
    void flush();

signals:
    // Zustand, Meldung, Konten, Abos, Stream oder Abzeichen haben sich geändert.
    void stateChanged();

private:
    void activate();
    void deactivate();
    void update();

    events::EventBus *m_bus;
    Options m_options;
    QNetworkAccessManager *m_network;
    std::unique_ptr<SecretStore> m_channelStore;
    std::unique_ptr<SecretStore> m_botStore;
    std::unique_ptr<SecretStore> m_secretStore;
    TwitchAuth *m_channel;
    TwitchAuth *m_bot;
    HelixClient *m_helix;
    EventSubConnection *m_connection;
    ChatSession *m_chatSession;
    AppToken *m_appToken;

    bool m_enabled = false;
    QString m_clientId;
    QString m_clientSecret;
    bool m_active = false;
    quint64 m_generation = 0;
    QString m_connectedUserId; // Kanal, für den EventSub läuft
    State m_state = State::Off;
};

} // namespace dss::twitch
