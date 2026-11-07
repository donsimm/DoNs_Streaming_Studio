#pragma once

#include "obsconnection.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QUuid>

#include <functional>

class QTimer;

// Quellen der Overlays in OBS (P10c, E-29): Zuordnung Overlay → Quelle in obs-quellen.json über die
// Kennung inputUuid, die beim Umbenennen bleibt, ohne Token. Gleicht beim Verbinden und bei
// Ereignissen ab, legt Quellen an, fügt sie in weitere Szenen ein. URL und Token kommen nie ins
// Protokoll (Regel 12). Löscht nichts in OBS (Entscheid F8).
//
// Quellen:
//   Protokoll: siehe obsprotocol.h
//   CreateInput, GetInputList, GetInputSettings (ohne Standardwerte), SetInputSettings (overlay),
//   CreateSceneItem, Ereignisse InputCreated, InputRemoved, InputNameChanged, SceneListChanged,
//   SceneNameChanged, CurrentSceneCollectionChanging, CurrentSceneCollectionChanged:
//     https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md
//   Felder der Listen: https://raw.githubusercontent.com/obsproject/obs-websocket/master/src/utils/Obs_ArrayHelper.cpp
//   QSaveFile: https://doc.qt.io/qt-6.8/qsavefile.html

namespace dss::obs {

class ObsSources : public QObject, private ConnectionListener
{
    Q_OBJECT

public:
    enum class SourceState {
        None,    // keine Zuordnung
        Present, // Quelle da, oder zuletzt bekannt da (Verbindung getrennt)
        Missing, // Zuordnung da, Quelle in OBS nicht: gelöscht oder andere Szenensammlung (F7)
    };
    struct Entry
    {
        QUuid overlayId;
        SourceState state = SourceState::None;
        QString inputName;

        bool operator==(const Entry &) const = default;
    };
    // Sollzustand je Overlay. url leer: im Moment keine Adresse möglich, die Quelle bleibt, wie sie ist.
    struct Desired
    {
        QUuid overlayId;
        QString url;
        int width = 0;
        int height = 0;

        bool operator==(const Desired &) const = default;
    };
    enum class Failure {
        None,
        NotConnected,  // nicht verbunden, oder Verbindung während der Anfrage verloren
        NameTaken,     // 601
        SceneMissing,  // 600 beim Anlegen
        SourceMissing, // 600 beim Einfügen
        Timeout,       // keine Antwort, oder OBS blieb beschäftigt
        Rejected,      // sonst abgewiesen
    };
    struct Result
    {
        Failure failure = Failure::None;
        QString message; // auf Deutsch
    };
    using Done = std::function<void(const Result &)>;

    // filePath: <Datenordner>/obs-quellen.json. Liest sie gleich; eine ungültige Datei wird umbenannt
    // (der Abgleich baut die Zuordnung über die Übernahme nach F10 wieder auf).
    ObsSources(ObsConnection *connection, const QString &filePath, QObject *parent = nullptr);
    ~ObsSources() override;

    QString filePath() const { return m_filePath; }
    // Szenen aus der letzten Abfrage; Reihenfolge wie in OBS.
    QList<Scene> scenes() const { return m_scenes; }
    // Ein Eintrag je Overlay aus setDesired.
    QList<Entry> entries() const;
    // Name der zugeordneten Quelle; leer ohne Zuordnung.
    QString inputNameOf(const QUuid &overlayId) const;

    // Alle Overlays. Zuordnungen von Overlays, die es nicht mehr gibt, fallen weg: Die Quelle bleibt
    // in OBS und ist leer (F8).
    void setDesired(const QList<Desired> &desired);

    void createSource(const QUuid &overlayId, const QString &sceneUuid, const QString &name, const Done &done);
    void addToScene(const QUuid &overlayId, const QString &sceneUuid, const Done &done);

signals:
    void scenesChanged();
    void entriesChanged();

private:
    struct Mapping
    {
        QString inputUuid;
        QString inputName;
    };
    struct KnownInput
    {
        QString name;
        QString url;
        int width = 0;  // 0: nicht in den Einstellungen (GetInputSettings liefert keine Standardwerte)
        int height = 0;
    };

    void onObsReady() override;
    void onObsLost() override;
    void onObsEvent(const Event &event) override;

    void load();
    void save() const;
    void refreshScenes();
    void startSync();
    void finishSync(const QHash<QString, KnownInput> &known);
    // Zuordnung mit dem Bekannten vergleichen, Quellen übernehmen (F10).
    void reconcile();
    void publish();
    SourceState stateOf(const QUuid &overlayId) const;
    const Desired *desiredOf(const QUuid &overlayId) const;

    ObsConnection *m_connection;
    QString m_filePath;
    QHash<QUuid, Mapping> m_mappings;
    QList<Desired> m_desired;
    QHash<QString, KnownInput> m_known; // Browserquellen in OBS, nach Kennung
    bool m_synced = false;              // m_known kommt aus einem Abgleich mit OBS
    bool m_syncing = false;
    bool m_collectionChanging = false;
    quint64 m_syncGeneration = 0;       // verwirft Antworten eines veralteten Abgleichs
    QList<Scene> m_scenes;
    QList<Entry> m_lastEntries;
    QTimer *m_resyncTimer = nullptr;
};

} // namespace dss::obs
