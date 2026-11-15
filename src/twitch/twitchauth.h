#pragma once

#include "twitchendpoints.h"

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace dss {
class SecretStore;
}

// Anmeldung bei Twitch für ein Konto (P13, Plan 2.1 bis 2.3): Gerätefluss («Device Code Flow»),
// Ablage der Token mit SecretStore (F1, Regel 12), Erneuern, stündliche Prüfung, Abmelden. Eine
// Instanz je Konto (Kanal, Bot); beide nutzen dieselbe Client-ID.
//
// - Gerätefluss: POST /oauth2/device, danach POST /oauth2/token mit grant_type device_code im Abstand
//   interval. 400 «authorization_pending»: weiter warten. Ablauf (expires_in oder «invalid device
//   code»): «Code abgelaufen, neu anmelden». Nach dem Tausch prüft validate Konto und Rechte; erst
//   dann wird abgelegt.
// - Client Secret (vertrauliche App, Entscheid 2026-11-15): Ob der Tausch des Gerätecodes es
//   verlangt, ist laut Schritt 0 nicht belegt. Ist eines gesetzt, geht es beim Tausch und beim
//   Erneuern mit (Erneuern: Pflicht für vertrauliche Apps). [OFFEN] bis zur ersten echten Anmeldung.
// - Erneuern: 10 min vor expires_at und auf Wunsch (401 bei Helix, renew). Das Refresh-Token gilt nur
//   einmal: Die Antwort wird sofort abgelegt. Abgewiesen (400, 401): Zustand Expired, kein weiterer
//   Versuch, bis der Nutzer sich neu anmeldet. Netzfehler: neuer Versuch nach 2 s, danach doppelt so
//   lang bis 60 s, ±20 % Zufall.
// - Prüfen: validate beim Start und danach stündlich (Pflicht laut Twitch); vergleicht die Rechte
//   mit den nötigen. 401: einmal erneuern und erneut prüfen, sonst Expired.
// - Abmelden: POST /oauth2/revoke, Datei des Kontos löschen.
// - Nie im Protokoll oder in Meldungen: Access- und Refresh-Token, device_code, user_code, Secret.
//
// Quellen:
//   https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/ (Device Code Grant Flow,
//     authorization_pending, invalid device code, Antwortfelder; Client Credentials)
//   https://dev.twitch.tv/docs/authentication/refresh-tokens/ (grant_type refresh_token, client_secret,
//     einmal verwendbar, 400/401 bei ungültigem Refresh-Token)
//   https://dev.twitch.tv/docs/authentication/validate-tokens/ (Authorization: OAuth, stündlich)
//   https://dev.twitch.tv/docs/authentication/revoke-tokens/ (client_id, token)
//   https://dev.twitch.tv/docs/authentication/scopes/ (Namen der Rechte)
//   Bericht Schritt 0: docs/archiv/recherchen/2026-11-15-schritt0-twitch-streamerbot.md (B 12, 15)

namespace dss::twitch {

// Quelle eines Bearer-Tokens für Helix: Konto (TwitchAuth), später das App-Token (Commit 7).
class TokenSource
{
public:
    virtual ~TokenSource() = default;
    virtual QString accessToken() const = 0;
    // Neues Token holen (nach 401); done(true), wenn es danach ein gültiges gibt.
    virtual void renew(std::function<void(bool)> done) = 0;
};

// Rechte (Plan 2.4, Frage 1 entschieden 2026-11-15: alles). Kanal: alle 78 Namen der Seite «scopes»
// ausser den zwei nur für IRC (chat:read, chat:edit; der Chat läuft über EventSub und Helix).
QStringList channelScopes();
// Bot: Chat schreiben, Abzeichen «Chat Bot», Ansage als Bot.
QStringList botScopes();

class TwitchAuth : public QObject, public TokenSource
{
    Q_OBJECT

public:
    enum class Account { Channel, Bot };
    // Off: nicht angemeldet. Checking: Token gelesen, Prüfung läuft. Ok: angemeldet (fehlende Rechte
    // stehen in missingScopes). Expired: Anmeldung abgelaufen, wartet auf den Nutzer. Error: Fehler
    // (Netz, Datei, abgewiesene Anmeldung); bei Netzfehlern läuft ein neuer Versuch.
    enum class State { Off, Checking, Ok, Expired, Error };

    // Zeiten in ms; die Tests kürzen sie.
    struct Timing
    {
        int refreshMargin = 10 * 60 * 1000;
        int validateInterval = 60 * 60 * 1000;
        int retryMin = 2000;
        int retryMax = 60000;
        int requestTimeout = 15000;
        int pollInterval = 0; // 0: interval aus der Antwort von Twitch
    };

    struct Pending
    {
        QString userCode;      // nur für das Fenster, nie ins Protokoll
        QUrl verificationUri;
        QDateTime expiresAt;   // UTC
    };

    // store gehört dem Aufrufer und lebt länger als diese Instanz.
    TwitchAuth(Account account, QNetworkAccessManager *network, SecretStore *store, QObject *parent = nullptr);
    TwitchAuth(Account account, QNetworkAccessManager *network, SecretStore *store, const Timing &timing,
               QObject *parent = nullptr);
    ~TwitchAuth() override;

    void setEndpoints(const Endpoints &endpoints) { m_endpoints = endpoints; }
    void setClientId(const QString &clientId) { m_clientId = clientId; }
    // Leer: öffentliche App, kein Secret.
    void setClientSecret(const QString &secret) { m_clientSecret = secret; }
    void setRequiredScopes(const QStringList &scopes) { m_requiredScopes = scopes; }
    // Anmeldung mit diesem Konto abweisen (Bot = Kanal, Plan 2.1). Leer: keine Prüfung.
    void setRejectedUserId(const QString &userId) { m_rejectedUserId = userId; }

    // Gespeichertes Token lesen und prüfen (Start des Kerns).
    void load();
    void startLogin();
    void cancelLogin();
    void logout();
    // Commit 7: Twitch aus oder neue Client-ID: alles anhalten und das Token aus dem Speicher nehmen,
    // die Datei bleibt (Zustand Off). load() liest sie wieder.
    void unload();
    // Commit 7: Die Freigabe wurde bei Twitch entzogen (revocation authorization_revoked): wie ein
    // abgewiesenes Erneuern, Zustand Expired.
    void markExpired() { expire(); }

    Account account() const { return m_account; }
    State state() const { return m_state; }
    // Beschreibung auf Deutsch; leer, wenn alles in Ordnung ist.
    QString message() const { return m_message; }
    std::optional<Pending> pending() const { return m_pending; }
    QString userId() const { return m_token.userId; }
    QString login() const { return m_token.login; }
    QStringList scopes() const { return m_token.scopes; }
    QStringList missingScopes() const;
    QDateTime expiresAt() const { return m_token.expiresAt; }

    // TokenSource
    QString accessToken() const override;
    void renew(std::function<void(bool)> done) override;

signals:
    // Zustand, Meldung, Konto, Rechte oder laufende Anmeldung haben sich geändert.
    void changed();

private:
    struct Token
    {
        QString accessToken;
        QString refreshToken;
        QDateTime expiresAt;
        QString userId;
        QString login;
        QStringList scopes;
    };

    QString label() const;
    QString scopeMessage() const;
    void setState(State state, const QString &message);
    QNetworkReply *postForm(const QString &path, const QList<std::pair<QString, QString>> &fields);
    void stopTimers();
    void scheduleTimers();
    void scheduleRetry(std::function<void()> action);

    void stopLogin();
    void onDeviceReply(QNetworkReply *reply);
    void poll();
    void onPollReply(QNetworkReply *reply);
    void loginExpired();

    void checkNewToken();
    void rejectNewToken(const QString &message);

    void refresh();
    void onRefreshReply(QNetworkReply *reply);
    void expire();
    void finishRenewals(bool ok);

    // retried: nach einem 401 schon einmal erneuert
    void validate(bool retried = false);
    void onValidateReply(QNetworkReply *reply, bool retried);
    void networkProblem(QNetworkReply *reply, std::function<void()> retry);

    void save(const Token &token, std::function<void(bool)> done);
    static std::optional<Token> tokenFromJson(const QByteArray &json);
    static QByteArray tokenToJson(const Token &token);

    Account m_account;
    QNetworkAccessManager *m_network;
    SecretStore *m_store;
    Timing m_timing;
    Endpoints m_endpoints = Endpoints::twitch();
    QString m_clientId;
    QString m_clientSecret;
    QStringList m_requiredScopes;
    QString m_rejectedUserId;

    State m_state = State::Off;
    QString m_message;
    Token m_token;

    // Gerätefluss
    std::optional<Pending> m_pending;
    QString m_deviceCode;
    QStringList m_requestedScopes;
    int m_pollInterval = 5000;
    QTimer *m_pollTimer;
    QTimer *m_codeTimer;
    QPointer<QNetworkReply> m_loginReply;
    // Neuer Token aus dem Gerätefluss, bis validate und Ablage durch sind.
    std::optional<Token> m_newToken;

    QTimer *m_refreshTimer;
    QTimer *m_validateTimer;
    QTimer *m_retryTimer;
    std::function<void()> m_retryAction;
    int m_retryAttempt = 0;
    bool m_refreshing = false;
    QList<std::function<void(bool)>> m_renewals;
    // Zählt Aufträge; eine Antwort zu einem älteren Auftrag (vor logout oder neuer Anmeldung) zählt
    // nicht mehr.
    quint64 m_generation = 0;
};

} // namespace dss::twitch
