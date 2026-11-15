#pragma once

#include "twitchendpoints.h"

#include <QList>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>

class QTimer;
class QWebSocket;
class TestTwitchConnection; // tests/tst_twitchverbindung.cpp

namespace dss::events {
class EventBus;
}

// Verbindung zu EventSub über WebSocket (P13, Plan 2.5): Begrüssung, Abos, Keepalive, Wechsel nach
// session_reconnect, revocation, Close-Codes, neue Versuche. Die Benachrichtigungen gehen als
// Ereignisse direkt auf den Fluss (Regel 14). Kennt weder den Kern noch protocol.h (Regel 9).
//
// - Verbinden: <eventsub>?keepalive_timeout_seconds=10. Nach session_welcome gehen alle Abos aus
//   kSubscriptions parallel hinaus (Frist laut Twitch 10 s, sonst Close 4003). Ein abgewiesenes Abo
//   hält die anderen nicht auf. Danach GET /helix/streams: Läuft der Stream schon, einmal
//   twitch.stream_online mit at_start true.
// - Keine Nachricht innerhalb von Keepalive + 5 s: Verbindung gilt als verloren; neu verbinden und
//   die Abos neu anlegen (verlorene Ereignisse liefert Twitch nicht nach).
// - session_reconnect: neue Verbindung zu reconnect_url (unverändert), die alte erst nach deren
//   session_welcome schliessen; die Abos wandern mit, keine neuen. Ohne Welcome innerhalb von 30 s:
//   wie verloren.
// - revocation: Das Abo gilt als weg, die Meldung nennt den Status. authorization_revoked meldet
//   authorizationRevoked() (die Anmeldung des Kanals gilt dann als ungültig).
// - Close-Codes 4000 bis 4007: immer neu verbinden. 4003 und 4007 sind Fehler im eigenen Ablauf und
//   stehen als kritisch im Protokoll.
// - Doppelte message_id (Zustellung «at least once»): die letzten 500 werden gemerkt.
// - Neue Versuche: 2 s, danach doppelt so lang bis 60 s, ±20 % Zufall (wie TwitchAuth).
// - Genau eine Verbindung (Grenze 3 je Client-ID und Nutzer), Abos mit User-Token kosten 0.
// - TLS: Prüfung über die Zertifikate des Systems; ohne TLS-Unterstützung (QSslSocket::supportsSsl)
//   keine Verbindung zu wss://, Meldung «TLS fehlt».
// - Chat-Texte, Token und Rohdaten kommen nie ins Protokoll, nur Typen, Anzahlen und Fehler.
//
// Quellen:
//   https://dev.twitch.tv/docs/eventsub/handling-websocket-events/ (Welcome, 10 s, Keepalive,
//     Reconnect, Close-Codes 4000 bis 4007, kein Nachliefern)
//   https://dev.twitch.tv/docs/eventsub/websocket-reference/ (Nachrichten, revocation-Status)
//   https://dev.twitch.tv/docs/eventsub/manage-subscriptions/ (Abos über WebSocket nur mit
//     User-Token, 3 Verbindungen, 300 Abos, Kosten)
//   https://dev.twitch.tv/docs/api/reference/streams#get-streams (Stream-Zustand)
//   https://doc.qt.io/qt-6.8/qwebsocket.html, https://doc.qt.io/qt-6.8/qsslsocket.html#supportsSsl
//   Bericht Schritt 0: docs/archiv/recherchen/2026-11-15-schritt0-twitch-streamerbot.md (A 10, 11)

namespace dss::twitch {

class HelixClient;
class TokenSource;

// Wartezeit vor dem Versuch Nummer attempt (ab 0): minMs · 2^attempt, höchstens maxMs, mal 0,8 bis
// 1,2 (random 0 bis 1), wieder höchstens maxMs.
int backoffDelay(int attempt, int minMs, int maxMs, double random);

class EventSubConnection : public QObject
{
    Q_OBJECT

public:
    // Off: aus. Connecting: erster Versuch nach start(). Connected: verbunden, alle Abos beantwortet
    // (abgewiesene stehen in message() und subscriptions()). Error: Verbindung weg, neuer Versuch
    // läuft, oder TLS fehlt.
    enum class State { Off, Connecting, Connected, Error };

    // Zeiten; die Tests kürzen sie.
    struct Timing
    {
        int keepaliveSeconds = 10;    // Wunsch an Twitch (10 bis 600)
        int keepaliveGrace = 5000;    // ms über der Keepalive-Zeit aus dem Welcome
        int welcomeTimeout = 15000;   // ms vom Öffnen bis zum Welcome
        int reconnectTimeout = 30000; // ms von session_reconnect bis zum Welcome der neuen Verbindung
        int retryMin = 2000;
        int retryMax = 60000;
    };

    struct Subscription
    {
        QString type;
        QString version;
        bool ok = false;
        QString problem; // leer, wenn aktiv
    };

    EventSubConnection(events::EventBus *bus, HelixClient *helix, QObject *parent = nullptr);
    EventSubConnection(events::EventBus *bus, HelixClient *helix, const Timing &timing, QObject *parent = nullptr);
    ~EventSubConnection() override;

    void setEndpoints(const Endpoints &endpoints) { m_endpoints = endpoints; }
    // Für is_bot_account in twitch.chat_message; leer ohne Bot.
    void setBotUserId(const QString &userId) { m_botUserId = userId; }

    // token: User-Token des Kanals, lebt länger als die Verbindung. Baut neu auf, wenn schon verbunden.
    void start(TokenSource *token, const QString &broadcasterUserId);
    void stop();

    State state() const { return m_state; }
    // Beschreibung auf Deutsch; leer, wenn verbunden und alle Abos aktiv.
    QString message() const { return m_message; }
    bool isLive() const { return m_live; }
    QList<Subscription> subscriptions() const { return m_subscriptions; }
    bool allSubscriptionsOk() const;
    QString sessionId() const { return m_sessionId; }

    static bool tlsSupported();

signals:
    // Zustand, Meldung, Abos oder Stream-Zustand haben sich geändert.
    void changed();
    // revocation mit authorization_revoked: Der Kanal hat die Freigabe entzogen.
    void authorizationRevoked();

private:
    friend class ::TestTwitchConnection;

    enum class Phase { Idle, Opening, Subscribing, Ready };

    void setState(State state, const QString &message);
    QWebSocket *createSocket();
    void dropSocket(QWebSocket *&socket);
    void openFresh();
    void lose(const QString &reason);
    void startKeepalive(int seconds);

    void onText(QWebSocket *socket, const QString &text);
    void onClosed(QWebSocket *socket);
    void onWelcome(QWebSocket *socket, const QString &sessionId, int keepaliveSeconds);
    void onReconnect(const QString &url);
    void onRevocation(const QString &type, const QString &status);
    void subscribeAll();
    void onSubscribed(qsizetype index, int status, const QString &problem);
    void checkStream();
    void updateState();
    bool remember(const QString &messageId);

    events::EventBus *m_bus;
    HelixClient *m_helix;
    Timing m_timing;
    Endpoints m_endpoints = Endpoints::twitch();
    TokenSource *m_token = nullptr;
    QString m_broadcasterUserId;
    QString m_botUserId;
    bool m_tlsSupported;

    State m_state = State::Off;
    QString m_message;
    Phase m_phase = Phase::Idle;
    QWebSocket *m_socket = nullptr;     // aktive Verbindung
    QWebSocket *m_nextSocket = nullptr; // neue Verbindung nach session_reconnect, bis zum Welcome
    QString m_sessionId;
    QList<Subscription> m_subscriptions;
    qsizetype m_openSubscriptions = 0;
    bool m_live = false;

    QTimer *m_keepaliveTimer;
    QTimer *m_welcomeTimer;
    QTimer *m_reconnectTimer;
    QTimer *m_retryTimer;
    int m_retryAttempt = 0;
    // Zählt Verbindungen; Antworten von Helix zu einer älteren zählen nicht mehr.
    quint64 m_generation = 0;

    QSet<QString> m_seenIds;
    QQueue<QString> m_seenOrder;
};

} // namespace dss::twitch
