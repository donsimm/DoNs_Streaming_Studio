#pragma once

#include "sseparser.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

// Verbindung zum Kamera-Dienst auf dem Streaming-PC (P15 d1, Plan 7.): Anfragen unter /api/v1/,
// Ereignisstrom (GET events, Server-Sent Events) mit Wiederverbinden nach 2 s, Zustand und letzter
// Stand von GET status. Kennt weder den Kern noch das Fenster noch die Schnittstelle zwischen beiden
// (kein protocol.h); die Liste erlaubter Pfade führt der Kern (d2). Prüfen tut der Dienst.
//
// Ablauf eines Versuchs: GET status (Version der Schnittstelle prüfen), danach GET events; sobald
// der Strom mit 200 und text/event-stream antwortet, gilt die Verbindung als hergestellt. Der Dienst
// schickt beim Anmelden den Stand von status, presets und lights; diese Ereignisse und alle späteren
// führen status() nach. Endet der Strom, gilt die Verbindung als verloren: Error, neuer Versuch nach
// 2 s. Connecting nur beim ersten Versuch nach configure(), danach Error, bis es klappt (wie OBS,
// Entscheid F9, docs/01-festlegungen.md).
//
// Quellen:
//   Schnittstelle des Dienstes: docs/09-kamera-dienst.md
//   Server-Sent Events: https://html.spec.whatwg.org/multipage/server-sent-events.html
//   QNetworkAccessManager::sendCustomRequest, QNetworkRequest::setTransferTimeout,
//   QNetworkReply::metaDataChanged: https://doc.qt.io/qt-6.8/qnetworkaccessmanager.html,
//   https://doc.qt.io/qt-6.8/qnetworkrequest.html, https://doc.qt.io/qt-6.8/qnetworkreply.html

namespace dss::camera {

inline constexpr int kDefaultPort = 8093;
inline constexpr int kApiVersion = 1;

struct Config
{
    bool enabled = false;
    QString host;
    int port = kDefaultPort;

    bool operator==(const Config &) const = default;
};

// Antwort auf request(). status 0: keine Antwort des Dienstes (nicht verbunden, Zeitgrenze,
// ungültiger Pfad); dann steht in error eine deutsche Beschreibung und body ist leer.
struct Reply
{
    int status = 0;
    QJsonObject body;
    QString error;
};

class CameraServiceClient : public QObject
{
    Q_OBJECT

public:
    enum class State { Off, Connecting, Connected, Error };
    Q_ENUM(State)

    // Zeiten in ms. Die Tests kürzen sie bei Bedarf.
    struct Timing
    {
        int reconnect = 2000;       // neuer Versuch nach einem Fehlschlag (Plan 7.)
        int connectTimeout = 5000;  // GET status und Kopf von GET events
        int requestTimeout = 15000; // Wecken der Tiny 2 dauert bis 10 s, erste Anfrage nach dem Erscheinen 3 s
        int streamIdle = 40000;     // ohne Daten: Strom gilt als tot (Dienst sendet alle 15 s einen Kommentar)
    };

    using ReplyHandler = std::function<void(const Reply &)>;

    explicit CameraServiceClient(QObject *parent = nullptr);
    CameraServiceClient(const Timing &timing, QObject *parent = nullptr);
    ~CameraServiceClient() override;

    // Neue Einstellungen. Bei gleichen Werten geschieht nichts. Laufende Anfragen enden normal.
    void configure(const Config &config);
    Config config() const { return m_config; }

    State state() const { return m_state; }
    // Beschreibung des Zustands auf Deutsch; leer, wenn verbunden.
    QString message() const { return m_message; }
    // Letzter Stand von GET status, nachgeführt mit den Ereignissen status, presets und lights
    // (Presets dort wie in GET status nur mit id und name). Bleibt bei Error erhalten, leer nach
    // configure() mit anderen Werten und bei Off.
    QJsonObject status() const { return m_status; }

    // Anfrage an den Dienst. path ist relativ zu /api/v1/ («camera-presets/totale/recall»); body
    // undefiniert oder null: ohne Inhalt, sonst als JSON. handler kommt immer, auch bei einem Fehler,
    // und nie vor der Rückkehr von request(). Geht in jedem Zustand ausser Off; bei Error versucht
    // die Anfrage es trotzdem (der Dienst kann schon wieder da sein).
    void request(const QByteArray &method, const QString &path, const QJsonValue &body, ReplyHandler handler);

    // Pfad relativ zu /api/v1/, ohne «..», Abfrage und Leerraum. Für request() und den Kern (d2).
    static bool isValidPath(const QString &path);

signals:
    // Zustand oder message() haben sich geändert.
    void stateChanged(dss::camera::CameraServiceClient::State state);
    // status() hat sich geändert.
    void statusChanged();
    // Ein Ereignis des Dienstes (status, config, controls, presets, lights) mit dem Inhalt von data.
    void eventReceived(const QString &name, const QJsonObject &data);

private:
    QString endpoint() const; // «127.0.0.1:8093»
    QUrl url(const QString &path) const;
    void setStatus(State state, const QString &message);
    void startAttempt();
    void onStatusReply(QNetworkReply *reply);
    void openStream();
    void onStreamHeaders();
    void onStreamData();
    void onStreamFinished();
    void failAttempt(const QString &message);
    void dropReplies();
    void handleEvent(const SseEvent &event);
    void setStatusPart(const QString &key, const QJsonValue &value);

    Timing m_timing;
    Config m_config;
    QNetworkAccessManager *m_network = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QTimer *m_attemptTimer = nullptr; // Zeitgrenze für den Kopf von GET events
    QTimer *m_idleTimer = nullptr;
    QNetworkReply *m_statusReply = nullptr;
    QNetworkReply *m_streamReply = nullptr;
    QSet<QNetworkReply *> m_requests;
    SseParser m_parser;
    State m_state = State::Off;
    QString m_message;
    QJsonObject m_status;
    bool m_streamOpen = false;
};

} // namespace dss::camera
