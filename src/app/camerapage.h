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
class LightsDialog;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;
class QSplitter;
class QTimer;
class QToolButton;
class QTreeWidget;
class QVBoxLayout;

// P15 e2: Reiter «Kameras» (Plan P15 Abschnitt 8 mit den Antworten des Nutzers 1, 2 und 5:
// eigener Reiter, Variante 1B «Alles sichtbar»; Checkliste docs/archiv/prompts/P15-e-checkliste.md).
// Oben die Kopfzeile mit Kamera-Auswahl, «Wecken»/«Schlafen» und dem Zustand des Dienstes
// («Kamera einrichten …» steht im Menü «Werkzeuge», Wunsch Gerätetest P15), darunter drei
// Spalten: links «Bewegen», «KI-Verfolgung», «Positionen»; Mitte «Bildwerte» mit festen Zeilen
// (P15 e3) und rechts «Licht» mit den Lampen und «Licht-Presets» (P15 e3). Dünn
// (Regel 9): Texte, Freigaben, Anfragen, Halten und Bündeln kommen aus cameraview.h; die Seite
// schickt die Anfragen über CoreClient::callCamera und zeigt den Stand aus camera_service,
// camera_event und camera_result.
class CameraPage : public QWidget
{
    Q_OBJECT

public:
    explicit CameraPage(const Theme &theme, QWidget *parent = nullptr);

    void setClient(dss::CoreClient *client);
    QString currentCameraId() const { return m_cameraId; }
    // Werkzeuge → «Kamera einrichten …»: Dialog für die hier gewählte Kamera, auch wenn der Reiter
    // nicht offen ist; aktiv wie setupEnabled (cameraview.h)
    bool setupEnabled() const { return m_setupEnabled; }
    void openSetup();

signals:
    // Meldung für die Statuszeile (Gespeichert, Gelöscht, Fehler des Dienstes)
    void statusMessage(const QString &text);
    void setupEnabledChanged(bool enabled);

protected:
    // Pfeiltasten am Steuerkreuz: Drücken und Loslassen wie die Maus (ohne Wiederholung der Taste)
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createHeader();
    QWidget *createMovePanel();
    QWidget *createAiPanel();
    QWidget *createPresetsPanel();
    QWidget *createImagePanel();
    QWidget *createLightPanel();
    QWidget *createLightPresetsSection(QWidget *parent);

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
    // Anfrage aus dem Halten: ein Fehler des Dienstes nur einmal je Halten in die Statuszeile
    void sendHoldCall(const dss::CameraCall &call);

    // Regler: höchstens alle kCameraBundleMs je Pfad (CameraBundler), beim Loslassen alles
    void putBundled(const QString &path, const QJsonObject &values);
    void flushBundled();
    void onBundleTimer();
    void refreshImage(const dss::CameraEntry *camera, const QJsonArray &controls);
    void showControlMenu(const QString &key, const QPoint &globalPos);

    // Bereiche der Lampen nur neu, wenn sich Kennungen oder Namen ändern und der Dialog «Lampen»
    // zu ist (Plan 8)
    void rebuildLights();
    void refreshLights();
    QString selectedLightPresetId() const;
    QString selectedLightPresetName() const;
    void saveLightPreset();
    void renameLightPreset();
    void removeLightPreset();
    void openLights();
    void fetchLightPresets();

    const dss::CameraEntry *currentCamera() const;
    QString selectedPresetId() const;
    QString selectedPresetName() const;
    void recallPreset();
    void savePreset();
    void overwritePreset();
    void renamePreset();
    void removePreset();
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
    bool m_setupEnabled = false;
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

    // Mitte «Bildwerte»: je Schlüssel aus cameraControlSpecs() eine feste Zeile
    struct ControlWidgets
    {
        QLabel *mark = nullptr;  // «•» bei gemerkten Werten
        QLabel *label = nullptr; // leer bei Kästchen
        QSlider *slider = nullptr;
        QLabel *value = nullptr;
        QComboBox *choice = nullptr;
        QCheckBox *check = nullptr;
    };
    QHash<QString, ControlWidgets> m_controlWidgets;
    struct SectionWidgets
    {
        QToolButton *reset = nullptr;
        QLabel *hint = nullptr;
    };
    QHash<QString, SectionWidgets> m_sectionWidgets;
    QList<dss::CameraControlRow> m_controlRows;
    dss::CameraBundler m_bundler;
    QTimer *m_bundleTimer = nullptr;

    // Rechts «Licht»
    struct LightWidgets
    {
        QString id;
        QWidget *section = nullptr;
        QLabel *state = nullptr;
        QCheckBox *on = nullptr;
        QLabel *brightnessLabel = nullptr;
        QLabel *kelvinLabel = nullptr;
        QSlider *brightness = nullptr;
        QLabel *brightnessValue = nullptr;
        QSlider *kelvin = nullptr;
        QLabel *kelvinValue = nullptr;
    };
    QList<LightWidgets> m_lightWidgets;
    QStringList m_lightLayout;
    bool m_lightsBuilt = false;
    QVBoxLayout *m_lightSections = nullptr;
    QPushButton *m_allOn = nullptr;
    QPushButton *m_allOff = nullptr;
    QPushButton *m_lamps = nullptr;
    QTreeWidget *m_lightPresets = nullptr;
    QList<dss::LightPresetRow> m_lightPresetRows;
    QPushButton *m_lightRecall = nullptr;
    QPushButton *m_lightSave = nullptr;
    QPushButton *m_lightOverwrite = nullptr;
    QPushButton *m_lightRename = nullptr;
    QPushButton *m_lightRemove = nullptr;
    QSet<QString> m_lightPresetsFetched;
    QPointer<LightsDialog> m_lightsDialog;
};
