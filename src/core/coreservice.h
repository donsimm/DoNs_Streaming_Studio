#pragma once

#include "fontcatalog.h"
#include "overlaystore.h"
#include "protocol.h"
#include "savescheduler.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>

#include <optional>

class QLocalServer;
class QLocalSocket;
class QTimer;

// Dienst des Kerns (P07, Teile C bis E): hält die Overlays aus dem Datenordner, nimmt
// Verbindungen von Fenstern über einen Unix-Socket an und verteilt Overlays, Leistungswerte
// und Zustände der Verbindungen. Seit P08 nimmt er Änderungen der Fenster an, prüft sie mit dem
// Modell, verteilt das Ergebnis an alle Fenster (K-02) und speichert zusammengefasst
// (SaveScheduler). Schnittstelle: docs/08-kern-schnittstelle.md.
class CoreService : public QObject
{
    Q_OBJECT

public:
    // Name in der Begrüssung, an dem der Kern Fenster erkennt (Beenden nur ohne andere Fenster).
    static constexpr char kWindowProgram[] = "dss-app";

    CoreService(dss::OverlayStore *store, const QString &socketPath, QObject *parent = nullptr);
    ~CoreService() override;

    enum class ListenResult { Ok, InUse, Failed };
    // Legt den Ordner des Sockets an (Rechte 0700) und nimmt Verbindungen an. InUse: Unter
    // diesem Pfad antwortet schon ein anderer Kern.
    ListenResult listen(QString *error);

    // Lädt den Datenordner neu (SIGHUP) und schickt jedes geänderte Overlay ganz an alle
    // Fenster (K-02), entfernte als overlay_removed, danach die neue Liste.
    void reload();

    int clientCount() const { return int(m_clients.size()); }

    // Schreibt alle Änderungen, die noch nicht gespeichert sind (vor dem Beenden, vor dem Neu
    // laden). false mit Meldung, wenn etwas nicht gespeichert werden konnte.
    bool savePending(QString *error);

signals:
    // Ein Fenster hat um das Beenden gebeten und kein anderes Fenster ist verbunden.
    void quitRequested();

private:
    struct Client
    {
        dss::LineSplitter lines;
        bool greeted = false;
        dss::Hello hello;
    };

    void onNewConnection();
    void onReadyRead(QLocalSocket *socket);
    void onDisconnected(QLocalSocket *socket);
    void handleMessage(QLocalSocket *socket, Client &client, const QJsonObject &message);
    void send(QLocalSocket *socket, const QJsonObject &message);
    void sendError(QLocalSocket *socket, const char *code, const QString &text,
                   const QString &request);
    void broadcast(const QJsonObject &message);
    void sampleStats();
    QList<dss::OverlaySummary> summaries() const;
    QJsonObject listMessage() const;
    // Schickt jedes neue oder geänderte Overlay ganz (overlay_changed), entfernte als
    // overlay_removed und, wenn sich die Liste geändert hat, overlay_list an alle.
    // Gibt die Zahl der geänderten und entfernten Overlays zurück.
    std::pair<int, int> broadcastDifferences(const QList<dss::Overlay> &before,
                                             const QJsonObject &listBefore);
    void handleChange(QLocalSocket *socket, const QJsonObject &message);
    dss::ChangeResult applyChange(const dss::ChangeRequest &request);
    void onSaveRequested();
    // Schriften (Teil F): beim ersten get_fonts geladen, danach aus dem Zwischenspeicher.
    void handleGetFonts(QLocalSocket *socket);
    void startFontLoading();
    void onFontsLoaded(const QList<dss::FontFace> &faces, const QString &error);
    // Nach dem Kopieren einer Schriftdatei: Schnitt der neuen Kopie in die Liste aufnehmen und
    // die Liste an alle schicken.
    void addMediaFont(const QString &reference);

    dss::OverlayStore *m_store;
    QString m_socketPath;
    QLocalServer *m_server = nullptr;
    QHash<QLocalSocket *, Client> m_clients;
    QTimer *m_statsTimer = nullptr;
    QElapsedTimer m_sinceLastSample;
    dss::StatsSampler m_sampler;
    std::optional<dss::SystemStats> m_lastStats;
    dss::ConnectionStates m_connectionStates;
    dss::SaveScheduler *m_saver = nullptr;
    dss::FontCatalogLoader *m_fontLoader = nullptr;
    bool m_fontsLoaded = false;
    bool m_broadcastFonts = false; // nach dem Laden an alle (Neu laden des Datenordners)
    QList<dss::FontFace> m_fonts;
    QString m_fontError;
    QList<QPointer<QLocalSocket>> m_fontWaiters;
};
