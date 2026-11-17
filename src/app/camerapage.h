#pragma once

#include "cameraview.h"
#include "theme.h"

#include <QElapsedTimer>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QWidget>

#include <functional>

namespace dss {
class CoreClient;
}
class CameraSetupDialog;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QSplitter;
class QTimer;
class QToolButton;
class QTreeWidget;

// P15 e2: Reiter «Kameras» (Plan P15 Abschnitt 8 mit den Antworten des Nutzers 1, 2 und 5:
// eigener Reiter, Variante 1B «Alles sichtbar»; Checkliste docs/archiv/prompts/P15-e-checkliste.md).
// Oben die Kopfzeile mit Kamera-Auswahl, «Wecken»/«Schlafen», «Einrichten …» und dem Zustand des
// Dienstes, darunter drei Spalten: links «Bewegen», «KI-Verfolgung», «Positionen»; Mitte «Bildwerte»
// und rechts «Licht» (Inhalt ab e3). Dünn (Regel 9): Texte, Freigaben, Anfragen und Halten kommen
// aus cameraview.h; die Seite schickt die Anfragen über CoreClient::callCamera und zeigt den Stand
// aus camera_service, camera_event und camera_result.
class CameraPage : public QWidget
{
    Q_OBJECT

public:
    explicit CameraPage(const Theme &theme, QWidget *parent = nullptr);

    void setClient(dss::CoreClient *client);
    QString currentCameraId() const { return m_cameraId; }

signals:
    // Meldung für die Statuszeile (Gespeichert, Gelöscht, Fehler des Dienstes)
    void statusMessage(const QString &text);

protected:
    // Pfeiltasten am Steuerkreuz: Drücken und Loslassen wie die Maus (ohne Wiederholung der Taste)
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createHeader();
    QWidget *createMovePanel();
    QWidget *createAiPanel();
    QWidget *createPresetsPanel();

    void onServiceChanged();
    void onEvent(const dss::CameraEventInfo &event);
    void onResult(const dss::CameraCall &call, const dss::CameraResult &result);
    void applyParts(int parts);
    void refreshChoice();
    void switchCamera(const QString &cameraId);
    void refresh();
    void fetchCamera();
    void fetchPresets();
    // Schickt eine Anfrage; done bekommt die Antwort (ohne done: Fehler in die Statuszeile)
    void send(const dss::CameraCall &call, std::function<void(const dss::CameraResult &)> done = {});

    void pressDirection(int pan, int tilt, int zoom);
    void releaseDirection();
    void stopHolding(bool sendStop);
    void scheduleHold();
    void onHoldTimer();

    const dss::CameraEntry *currentCamera() const;
    QString selectedPresetId() const;
    QString selectedPresetName() const;
    void recallPreset();
    void savePreset();
    void overwritePreset();
    void renamePreset();
    void removePreset();
    void openSetup();
    void reportResult(const dss::CameraResult &result, const QString &okText);

    Theme m_theme;
    dss::CoreClient *m_client = nullptr;
    dss::CameraData m_data;
    dss::CameraServiceView m_service;
    dss::CameraControlView m_view;
    QString m_cameraId;
    QSet<QString> m_presetsFetched;
    QHash<qint64, std::function<void(const dss::CameraResult &)>> m_pending;
    dss::CameraHold m_hold;
    QTimer *m_holdTimer = nullptr;
    QElapsedTimer m_clock;
    QList<dss::CameraPresetRow> m_presetRows;
    QList<std::pair<int, QString>> m_aiMenu;
    QPointer<CameraSetupDialog> m_setupDialog;

    QComboBox *m_choice = nullptr;
    QPushButton *m_wake = nullptr;
    QPushButton *m_setup = nullptr;
    QLabel *m_state = nullptr;
    QLabel *m_stateDetail = nullptr;
    QLabel *m_blockedHint = nullptr;
    QSplitter *m_columns = nullptr;

    QLabel *m_moveHint = nullptr;
    QPushButton *m_hintWake = nullptr;
    QList<QToolButton *> m_crossButtons; // 8 Richtungen, «Mitte», Zoom «−»/«+»
    QToolButton *m_home = nullptr;
    QLabel *m_zoomText = nullptr;
    QSpinBox *m_speed = nullptr;
    QDoubleSpinBox *m_step = nullptr;

    QLabel *m_aiHint = nullptr;
    QComboBox *m_ai = nullptr;

    QTreeWidget *m_presets = nullptr;
    QPushButton *m_recall = nullptr;
    QPushButton *m_save = nullptr;
    QPushButton *m_overwrite = nullptr;
    QPushButton *m_rename = nullptr;
    QPushButton *m_remove = nullptr;
    QLabel *m_recallHint = nullptr;

    QLabel *m_imageTitle = nullptr;
};
