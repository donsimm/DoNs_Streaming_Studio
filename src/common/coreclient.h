#pragma once

#include "protocol.h"

#include <QHash>
#include <QList>
#include <QLocalSocket>
#include <QMap>
#include <QSet>
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
    // Overlays in der Reihenfolge der Liste (seit P08 mit Ordnern, listData()).
    QList<OverlaySummary> overlays() const { return m_list.overlays; }
    // Referenz: Zeiger aus summary() und layout.folder() gelten bis zur nächsten overlay_list.
    const OverlayListData &listData() const { return m_list; }
    // Schnitte zur Auswahl (Teil F) und Meldung, wenn ein Teil fehlt; leer bis zur Antwort auf
    // requestFonts().
    QList<FontFace> fonts() const { return m_fonts; }
    QString fontError() const { return m_fontError; }
    // Ganzes Overlay, wenn der Kern es geschickt hat (Antwort auf requestOverlay oder Änderung).
    std::optional<Overlay> overlay(const QUuid &id) const;
    // Originalgrösse der Dateien aus den Overlay-Nachrichten (P09b, media_sizes).
    const MediaSizes &mediaSizes() const { return m_mediaSizes; }
    std::optional<SystemStats> lastStats() const { return m_lastStats; }
    ConnectionStates connectionStates() const { return m_connectionStates; }

    // Einstellungen des Kerns (P10-vor), wenn er sie geschickt hat (Antwort auf requestSettings
    // oder nach einer Änderung); ohne Verbindung leer.
    std::optional<SettingsInfo> settings() const { return m_settings; }
    void requestSettings();
    // Schickt update_settings; requestId wird hier vergeben. Antwort über changeFinished, neue
    // Werte über settingsChanged. Rückgabe: request_id, 0 ohne Verbindung.
    qint64 sendSettingsUpdate(SettingsUpdate update);
    // P10, Punkt 77: ungültige kern.ini auf Standardwerte zurücksetzen (reset_settings). Antwort
    // wie bei sendSettingsUpdate über settingsUpdateFinished.
    qint64 sendSettingsReset();

    // Adresse der Browserquelle (P10) des Overlays id, mit Token. Der Kern schickt sie danach bei
    // jeder Änderung von selbst neu, auch nach dem Wiederverbinden; null = keine mehr.
    void requestBrowserSource(const QUuid &id);
    // Letzte Antwort zum angefragten Overlay; leer bis dahin und ohne Verbindung.
    std::optional<BrowserSourceInfo> browserSource() const { return m_browserSource; }

    // Zustand des Overlay-Servers (P10), nach hello und bei jeder Änderung; ohne Verbindung leer.
    std::optional<ServerStatusInfo> serverStatus() const { return m_serverStatus; }

    // Einzelheiten der Verbindung zu OBS (P10c), nach hello und bei jeder Änderung; ohne Verbindung
    // leer. Die Farbe des Knopfes steht in connectionStates().
    std::optional<ObsStatusInfo> obsStatus() const { return m_obsStatus; }
    // Quelle in OBS zu einem Overlay (P10c, E-29); ohne Eintrag: keine.
    ObsSourceInfo obsSource(const QUuid &overlayId) const;
    // Schicken obs_create_source bzw. obs_add_to_scene; requestId wird hier vergeben. Antwort über
    // obsRequestFinished. 0 = nicht verbunden.
    qint64 sendObsCreateSource(const QUuid &overlayId, const QString &sceneUuid, const QString &name);
    qint64 sendObsAddToScene(const QUuid &overlayId, const QString &sceneUuid);

    // Fragt ein ganzes Overlay an; die Antwort meldet overlayChanged(id).
    void requestOverlay(const QUuid &id);
    void requestOverlayList();
    // P10b: Medienliste (E-28); der Kern schickt sie nach hello und nach jeder Änderung von selbst.
    const MediaListData &mediaList() const { return m_mediaList; }
    void requestMediaList();
    // P10b (Punkt 73): laufende Übernahmen im Kern mit Fortschritt (auch die anderer Fenster).
    QList<ImportProgressData> imports() const { return m_imports; }
    void requestFonts();

    // Schickt eine Änderung an den Kern (P08, Teil A); request.requestId wird hier vergeben.
    // Den neuen Zustand übernimmt das Fenster nur aus den Nachrichten des Kerns an alle
    // (overlay_changed, overlay_list), nie aus der eigenen Änderung.
    // Mit coalesceKey (zum Beispiel ein Feld einer Quelle) bleibt je Schlüssel höchstens eine
    // Änderung unterwegs: Kommt eine neue, bevor der Kern geantwortet hat, wird sie zurückgehalten
    // und ersetzt eine schon zurückgehaltene; nach der Antwort geht die letzte los. So überlastet
    // eine gedrückte Pfeiltaste den Kern nicht, und der letzte Wert kommt trotzdem an.
    // Rückgabe: request_id, 0 ohne Verbindung, -1 wenn zurückgehalten.
    qint64 sendChange(ChangeRequest request, const QString &coalesceKey = QString());
    // true, solange eine Änderung mit diesem Schlüssel unterwegs oder zurückgehalten ist.
    bool isChangePending(const QString &coalesceKey) const;
    // P10-vor (Punkt 67): Namen der Dateien, die eigene Änderungen gerade übernehmen lassen
    // (Kopie im Kern, bis change_result kommt), in der Reihenfolge des Schickens.
    QStringList pendingImports() const;

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
    void fontsChanged();
    // Antwort auf eine Änderung (auch auf eine, die zurückgehalten war). Nicht für Einstellungen.
    void changeFinished(const dss::ChangeResult &result);
    // P10, Punkt 76: Antwort auf sendSettingsUpdate bzw. sendSettingsReset, nur hier (nicht in
    // changeFinished), damit eine Abweisung nur im Dialog «Einstellungen» erscheint.
    void settingsUpdateFinished(const dss::ChangeResult &result);
    // pendingImports() hat sich geändert.
    void pendingImportsChanged();
    // settings() hat sich geändert (P10-vor).
    void settingsChanged();
    // browserSource() hat sich geändert (P10).
    void browserSourceChanged();
    // serverStatus() hat sich geändert (P10).
    void serverStatusChanged();
    // obsStatus() hat sich geändert (P10c).
    void obsStatusChanged();
    void obsSourcesChanged();
    // Antwort auf sendObsCreateSource und sendObsAddToScene, nur hier (nicht in changeFinished): Das
    // Anlegen-Fenster zeigt eine Abweisung selbst und bleibt offen.
    void obsRequestFinished(const dss::ChangeResult &result);
    // P10b: mediaList() bzw. imports() hat sich geändert.
    void mediaListChanged();
    void importProgressChanged();

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
    qint64 transmit(ChangeRequest request, const QString &coalesceKey);

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
    OverlayListData m_list;
    QHash<QUuid, Overlay> m_overlayCache;
    MediaSizes m_mediaSizes;
    QList<FontFace> m_fonts;
    QString m_fontError;
    qint64 m_nextRequestId = 1;
    QHash<qint64, QString> m_keyOfRequest;     // unterwegs: request_id → Schlüssel
    QHash<QString, qint64> m_requestOfKey;     // unterwegs: Schlüssel → request_id
    QHash<QString, ChangeRequest> m_heldChanges; // zurückgehalten je Schlüssel
    QMap<qint64, QString> m_importNames;       // unterwegs mit Datei: request_id → Dateiname
    QSet<qint64> m_settingsRequests;           // update_settings und reset_settings unterwegs
    std::optional<SystemStats> m_lastStats;
    std::optional<SettingsInfo> m_settings;
    QUuid m_browserSourceWanted;
    std::optional<BrowserSourceInfo> m_browserSource;
    std::optional<ServerStatusInfo> m_serverStatus;
    std::optional<ObsStatusInfo> m_obsStatus;
    QList<ObsSourceInfo> m_obsSources;
    QSet<qint64> m_obsRequests;
    MediaListData m_mediaList;
    QList<ImportProgressData> m_imports;
    ConnectionStates m_connectionStates;
    std::optional<QuitReply> m_quitReply;
};

} // namespace dss
