#pragma once

#include "obsprotocol.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

class QTimer;
class QWebSocket;
class TestObsConnection; // tests/tst_obsverbindung.cpp

// Verbindung zu OBS über obs-websocket (P10c, T-05): Anmeldung, aktuelle Szene, Wiederverbinden,
// Ping. Kennt weder den Kern noch das Fenster noch die Schnittstelle zwischen beiden (Plan P10c,
// Abschnitt 1.5a). Nach aussen gibt es nur Zustand und aktuelle Szene mit eigenen Werten, kein
// JSON von OBS; den Ereignisfluss (Regel 14) baut erst die Twitch-Verbindung (Punkt 92).
//
// Farben (Entscheid F9, docs/01-festlegungen.md): Connecting nur beim ersten Versuch nach dem
// Start oder nach neuen Einstellungen, danach Error, bis es klappt. So flackert der Knopf nicht
// alle 2 s.
//
// Quellen:
//   Protokoll: siehe obsprotocol.h
//   QWebSocket: https://doc.qt.io/qt-6.8/qwebsocket.html (open mit QWebSocketHandshakeOptions, ping,
//     pong, closeCode), https://doc.qt.io/qt-6.8/qwebsockethandshakeoptions.html (setSubprotocols)

namespace dss::obs {

class ObsSources;

struct Config
{
    bool enabled = false;
    QString host;
    int port = kDefaultPort;

    bool operator==(const Config &) const = default;
};

// Für ObsSources (innerhalb von src/obs): Ereignisse, die den Abgleich der Quellen betreffen.
class ConnectionListener
{
public:
    virtual ~ConnectionListener() = default;
    virtual void onObsReady() = 0;    // verbunden, Anfragen möglich
    virtual void onObsLost() = 0;     // Verbindung weg, offene Anfragen sind schon beantwortet
    virtual void onObsEvent(const Event &event) = 0;
};

enum class RequestFailure {
    None,          // Antwort von OBS, auch mit einem Fehlercode
    NotConnected,  // nicht geschickt
    Disconnected,  // Verbindung während der Anfrage verloren: Ausgang unbekannt
    Timeout,       // keine Antwort, oder OBS blieb beschäftigt (207)
};

class ObsConnection : public QObject
{
    Q_OBJECT

public:
    enum class State { Off, Connecting, Connected, Error };

    // Zeiten in ms. Die Tests kürzen sie.
    struct Timing
    {
        int reconnect = 2000;       // neuer Versuch nach einem Fehlschlag
        int connectTimeout = 5000;  // Verbinden bis Identified (Firewall: sonst minutenlang)
        int ping = 5000;
        int pongTimeout = 10000;    // ohne Pong: Verbindung gilt als tot
        int requestTimeout = 5000;
        int busyRetry = 1000;       // OBS beschäftigt (207)
        int busyRetries = 5;
    };

    // Liest das gespeicherte Passwort und ruft done auf, auch später (systemd-creds läuft als
    // eigener Prozess). std::nullopt: nicht lesbar. Leer: keines gespeichert.
    using PasswordDone = std::function<void(const std::optional<QString> &)>;
    using PasswordProvider = std::function<void(const PasswordDone &)>;

    explicit ObsConnection(PasswordProvider passwordProvider, QObject *parent = nullptr);
    ObsConnection(PasswordProvider passwordProvider, const Timing &timing, QObject *parent = nullptr);
    ~ObsConnection() override;

    // Neue Einstellungen. Bei gleichen Werten geschieht nichts, ausser die Verbindung wartet auf
    // das Eingreifen des Nutzers (falsches Passwort, 4010, 4011, Passwort nicht lesbar): dann ein
    // neuer Versuch. passwordChanged: Passwort neu lesen und neu verbinden.
    void configure(const Config &config, bool passwordChanged = false);

    State state() const { return m_state; }
    // Zuletzt bekannte aktuelle Szene; nach neuen Einstellungen leer.
    Scene currentScene() const { return m_currentScene; }
    // Beschreibung des Zustands auf Deutsch; leer, wenn verbunden.
    QString message() const { return m_message; }
    QString obsVersion() const { return m_obsVersion; }
    QString websocketVersion() const { return m_websocketVersion; }

signals:
    // Zustand oder message() haben sich geändert.
    void stateChanged(dss::obs::ObsConnection::State state);
    // Echter Wechsel der aktuellen Szene, auch wenn der Abgleich nach dem Wiederverbinden einen
    // Unterschied zeigt. Gleiche Szene (auch umbenannt): kein Signal.
    void currentSceneChanged(const dss::obs::Scene &scene);

private:
    // Nur für ObsSources: Anfragen und Ereignisse bleiben innerhalb von src/obs (Abschnitt 1.5a).
    friend class ObsSources;
    friend class ::TestObsConnection;
    using ResponseHandler = std::function<void(const Response &, RequestFailure)>;
    void setListener(ConnectionListener *listener) { m_listener = listener; }
    bool isReady() const { return m_phase == Phase::Ready && !m_exiting; }
    // Antwort immer über handler, auch sofort, wenn nicht verbunden. Während eines Wechsels der
    // Szenensammlung wartet die Anfrage: Die Doku nennt Anfragen dann «undefined behavior».
    void request(const QString &type, const QJsonObject &fields, ResponseHandler handler);

    enum class Phase { Idle, Opening, WaitHello, WaitIdentified, Syncing, Ready };
    struct Pending
    {
        QString type;
        QJsonObject fields;
        ResponseHandler handler;
        QTimer *timeout = nullptr;
        int busyRetries = 0;
    };

    QString endpoint() const; // «192.168.1.116:4455»
    void setStatus(State state, const QString &message);
    void startConnecting();
    void startAttempt();
    void dropSocket();
    void onSocketClosed();
    void onTextMessage(const QString &text);
    void onHello(const QJsonObject &data);
    void onIdentified();
    void onResponse(const QJsonObject &data);
    void onEvent(const QJsonObject &data);
    void becomeReady();
    void updateCurrentScene(const Scene &scene);
    void sendRequest(const QString &id);
    void finishRequest(const QString &id, const Response &response, RequestFailure failure);
    void failAllRequests(RequestFailure failure);

    PasswordProvider m_passwordProvider;
    Timing m_timing;
    Config m_config;
    QWebSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_attemptTimer = nullptr;
    QTimer *m_pingTimer = nullptr;
    QTimer *m_pongTimer = nullptr;
    ConnectionListener *m_listener = nullptr;
    Phase m_phase = Phase::Idle;
    State m_state = State::Off;
    QString m_message;
    QString m_password; // nur im Speicher
    Hello m_hello;
    QString m_obsVersion;
    QString m_websocketVersion;
    Scene m_currentScene;
    bool m_waitsForUser = false;       // kein neuer Versuch ohne configure()
    bool m_collectionChanging = false; // zwischen …Changing und …Changed: keine Anfragen schicken
    bool m_exiting = false;            // ExitStarted: OBS beendet sich gerade
    quint64 m_passwordGeneration = 0;  // verwirft die Antwort auf veraltete Leseaufträge
    quint64 m_nextRequest = 1;
    QHash<QString, Pending> m_pending;
    QList<QString> m_held; // wartet auf das Ende des Wechsels der Szenensammlung
};

} // namespace dss::obs
