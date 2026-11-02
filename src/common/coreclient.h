#pragma once

#include "protocol.h"

#include <QHash>
#include <QList>
#include <QLocalSocket>
#include <QObject>
#include <QString>
#include <QUuid>

#include <optional>

class QProcess;
class QTimer;

// Verbindung eines Fensters (oder eines Tests) zum Kern (P07, Teile C, D, F). Baut die
// Verbindung auf, startet bei Bedarf den Benutzerdienst, verbindet sich bei Verlust neu und
// hält die Daten, die der Kern geschickt hat. Kein Qt Gui (CLAUDE.md Regel 9): Das Fenster
// zeigt nur an, was hier steht; seine eigene Auswahl hält es selbst.
// Schnittstelle: docs/08-kern-schnittstelle.md

namespace dss {

// Grund eines gescheiterten Verbindungsversuchs auf Deutsch für die Statuszeile.
QString socketErrorText(QLocalSocket::LocalSocketError error);

class CoreClient : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QString socketPath;        // Unix-Socket des Kerns
        QString program;           // Name in der Begrüssung, zum Beispiel «dss-app»
        QString version;           // Version in der Begrüssung
        QString unitName;          // Einheit, die gestartet wird, wenn der Kern nicht läuft
        bool startUnit = false;    // beim ersten Fehlschlag die Einheit starten (nur einmal)
        int reconnectIntervalMs = 2000;
    };

    explicit CoreClient(const Options &options, QObject *parent = nullptr);
    ~CoreClient() override;

    // Erster Verbindungsversuch; danach alle reconnectIntervalMs, solange keine Verbindung
    // besteht.
    void start();
    // Trennt die Verbindung und versucht es nicht mehr.
    void stop();

    // true, sobald die Begrüssung des Kerns mit passender Version da ist.
    bool isConnected() const { return m_connected; }
    Hello coreHello() const { return m_coreHello; }
    QString statusText() const { return m_statusText; }

    // Daten vom Kern; ohne Verbindung leer.
    QList<OverlaySummary> overlays() const { return m_overlays; }
    // Ganzes Overlay, wenn der Kern es geschickt hat (Antwort auf requestOverlay oder Änderung).
    std::optional<Overlay> overlay(const QUuid &id) const;
    std::optional<SystemStats> lastStats() const { return m_lastStats; }
    ConnectionStates connectionStates() const { return m_connectionStates; }

    // Fragt ein ganzes Overlay an; die Antwort meldet overlayChanged(id).
    void requestOverlay(const QUuid &id);
    void requestOverlayList();

    // Bittet den Kern, sich zu beenden (normales Schliessen des Fensters, Entscheid 1). Wartet
    // höchstens timeoutMs auf die Antwort und trennt danach die Verbindung; ohne Verbindung
    // oder ohne Antwort leer. Blockiert (nur beim Schliessen verwendet).
    std::optional<QuitReply> requestQuit(int timeoutMs);

signals:
    void connectedChanged(bool connected);
    void statusTextChanged(const QString &text);
    void overlayListChanged();
    void overlayChanged(const QUuid &id);
    void overlayRemoved(const QUuid &id);
    void statsReceived(const dss::SystemStats &stats);
    void connectionStatesChanged();
    void errorReceived(const dss::ErrorInfo &error);
    void quitReplied(const dss::QuitReply &reply);

private:
    void tryConnect();
    void onConnected();
    void onDisconnected();
    void onSocketError();
    void onReadyRead();
    void handleMessage(const QJsonObject &message);
    void send(const QJsonObject &message);
    void startUnit();
    void setConnected(bool connected);
    void setStatus(const QString &text);
    void clearData();

    Options m_options;
    QLocalSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QProcess *m_starter = nullptr;
    LineSplitter m_lines;
    bool m_stopped = true;
    bool m_unitStartTried = false;
    bool m_keepStartError = false;
    bool m_connected = false;
    bool m_wasConnected = false;
    Hello m_coreHello;
    QString m_statusText;
    QList<OverlaySummary> m_overlays;
    QHash<QUuid, Overlay> m_overlayCache;
    std::optional<SystemStats> m_lastStats;
    ConnectionStates m_connectionStates;
    std::optional<QuitReply> m_quitReply;
};

} // namespace dss
