#pragma once

#include "mediapatch.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QUuid>

#include <functional>
#include <memory>

class QFile;
class QTcpServer;
class QTcpSocket;
class QTimer;

namespace dss {

class OverlayStore;
struct HttpRequest;

// Overlay-Server im Kern (P10, Teil A; Schalter DSS_CORE_OVERLAY_SERVER). Liefert jedem Overlay
// eine Webseite (zweiter Zeichner, web/overlay.html) für die Browserquelle in OBS.
//
// Technik (Begründung im P10-Bericht): eigener kleiner HTTP/1.1-Server auf QTcpServer (Qt
// Network ist schon da, kein neues Paket) und Server-Sent Events für Änderungen (der Kern schickt
// nur zur Seite; EventSource verbindet sich von selbst neu). Qt HttpServer und Qt WebSockets sind
// auf dem Studio-PC nicht installiert (Regel 5).
//
// Lauscht nur auf IPv4: 127.0.0.1 und die Adresse im Heimnetz, je ein QTcpServer mit dieser
// Adresse (nie QHostAddress::Any, das auch IPv6 annimmt).
// https://doc.qt.io/qt-6.8/qtcpserver.html#listen, https://doc.qt.io/qt-6.8/qhostaddress.html
//
// Adressen: /o/<adresse>/<token>/ (Seite), …/overlay.js, …/events (Server-Sent Events),
// …/media/<sha256>.<endung> (nur Dateien dieses Overlays), …/report (Meldungen der Seite).
// Unbekannte Adresse oder falscher Token: leere, durchsichtige Seite (Ereignisse: «gone»),
// Protokoll ohne Token. docs/08-kern-schnittstelle.md, «Overlay-Server».
// Server-Sent Events: https://html.spec.whatwg.org/multipage/server-sent-events.html
class OverlayServer : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        QString webDir;       // Ordner mit overlay.html und overlay.js
        QString installedDir; // Version im Einsatz (Meldung «Port belegt»)
        QString procRoot = QStringLiteral("/proc");
        int retryIntervalMs = 2000; // Adresse wieder da? Port frei?
        int heartbeatMs = 15000;    // Kommentarzeile an die Seiten (tote Verbindungen erkennen)
        int invalidHintMs = 30000;  // so lange zeigt das Fenster den Hinweis «ungültige Adresse»
        // Adressen dieses PCs (Tests: eigene Liste).
        std::function<QList<QHostAddress>()> localAddresses;
        // Ausrichtung eines Videos aus der Display-Matrix (1 bis 8, dss_media); leer = 1.
        std::function<int(const QString &path)> videoOrientation;
    };

    struct Status
    {
        QStringList listening;  // «127.0.0.1:8081», …
        QStringList problems;   // Port belegt, Adresse fehlt, …
        QString invalidUse;     // Hinweis: eine Browserquelle benutzt eine ungültige Adresse
        QHash<QUuid, int> pages; // verbundene Seiten je Overlay
        bool operator==(const Status &) const = default;
    };

    OverlayServer(const OverlayStore *store, const Options &options, QObject *parent = nullptr);
    ~OverlayServer() override;

    // Startet (oder nach einer Änderung neu) auf dem Port, auf 127.0.0.1 und homeAddress (leer =
    // nur 127.0.0.1).
    void configure(int port, const QString &homeAddress);
    void stop();

    // Nach Änderungen im Kern: ganzes Overlay an seine Seiten (K-02); neue Adresse, neuer Token
    // oder gelöschtes Overlay: «gone» an die Seiten der alten Adresse.
    void overlayChanged(const QUuid &overlayId);
    void overlayRemoved(const QUuid &overlayId);
    void tokenChanged(const QUuid &overlayId);
    // Nach dem Neu laden des Datenordners: alle Seiten prüfen.
    void allChanged();

    Status status() const { return m_status; }
    int port() const { return m_port; }

    // Daten für die Seite: Overlay im Format der Datei, Ausrichtung der Bilder und Videos,
    // Zeilenmasse der Schriften (für Tests öffentlich).
    QJsonObject pageData(const QUuid &overlayId);

signals:
    void statusChanged();

private:
    struct Connection;

    void listenAll();
    void retry();
    void updateStatus();
    void onNewConnection(QTcpServer *server);
    void onReadyRead(QTcpSocket *socket);
    void onBytesWritten(QTcpSocket *socket);
    void onDisconnected(QTcpSocket *socket);
    void handle(QTcpSocket *socket, Connection &c, const HttpRequest &request);
    void handleOverlay(QTcpSocket *socket, Connection &c, const HttpRequest &request,
                       const QUuid &overlayId, const QString &rest);
    void handleInvalid(QTcpSocket *socket, Connection &c, const HttpRequest &request,
                       const QString &address, const QString &rest);
    void respond(QTcpSocket *socket, Connection &c, const HttpRequest &request, int status,
                 const QByteArray &type, const QByteArray &body,
                 const QList<std::pair<QByteArray, QByteArray>> &extra = {});
    void sendFile(QTcpSocket *socket, Connection &c, const HttpRequest &request, const QString &path,
                  const BytePatches &patches, bool immutable);
    void continueFile(QTcpSocket *socket, Connection &c);
    void finishResponse(QTcpSocket *socket, Connection &c);
    void startEvents(QTcpSocket *socket, Connection &c, const QUuid &overlayId);
    void sendGone(QTcpSocket *socket);
    void sendOverlayEvent(QTcpSocket *socket, Connection &c);
    void checkStreams(const QUuid &overlayId);
    void heartbeat();
    void noteInvalid(const QTcpSocket *socket, const QString &address);
    QString mediaPath(const QString &reference) const;
    int orientationOf(const QString &reference);
    const BytePatches &patchesOf(const QString &reference);

    const OverlayStore *m_store;
    Options m_options;
    int m_port = 0;
    QString m_homeAddress;
    QList<QTcpServer *> m_servers;
    QStringList m_listenProblems; // aus dem letzten Versuch
    bool m_homeListening = false;
    bool m_loopbackListening = false;
    QTimer *m_retryTimer = nullptr;
    QTimer *m_heartbeatTimer = nullptr;
    QHash<QTcpSocket *, std::shared_ptr<Connection>> m_connections;
    Status m_status;
    // Hinweis «ungültige Adresse»: letzter Zugriff; Protokoll höchstens einmal pro Minute je
    // Gegenstelle und Adresse.
    QElapsedTimer m_lastInvalid;
    QString m_lastInvalidText;
    QHash<QString, QElapsedTimer> m_invalidLogged;
    QElapsedTimer m_reportWindow;
    int m_reportsInWindow = 0;
    QHash<QString, int> m_orientations; // nach Verweis media/…
    QHash<QString, BytePatches> m_patches;
};

} // namespace dss
