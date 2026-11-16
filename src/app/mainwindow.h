#pragma once

#include "actionspage.h"
#include "actionview.h"
#include "canvasedit.h"
#include "commandspage.h"
#include "connectionstate.h"
#include "docks.h"
#include "theme.h"
#include "transform.h"
#include "chatbotlive.h"
#include "windowdata.h"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QUuid>

#include <functional>
#include <optional>

class CanvasWidget;
class ChatBotPage;
class HeaderBar;
class QDockWidget;
class ObsSourceDialog;
class SettingsDialog;
class SourceRenderer;
class QAction;
class QLabel;
class QListWidgetItem;
class QMenu;
class QMenuBar;
class QStackedWidget;
class QTimer;
class QTreeWidgetItem;

namespace dss {
class CoreClient;
class MediaPreviewLoader;
enum class DropPosition;
struct ChangeRequest;
struct ChangeResult;
struct ErrorInfo;
struct SourceChanges;
struct SystemStats;
}

// Hauptfenster nach docs/bilder/oberflaeche.png. Dünn, ohne Logik (CLAUDE.md Regel 9):
// baut die Bereiche auf und verbindet sie, mehr nicht. Meldet einmal im Protokoll,
// wenn es zum ersten Mal gezeichnet wurde.
// Seit P07 zeigt es die Daten vom Kern (dss::CoreClient). Eigener Zustand ist nur die Auswahl
// (gewähltes Overlay, Ordner, gewählte Quelle) und der Verlauf der Leistungswerte für die Anzeige.
// Seit P08 schickt es Änderungen an den Kern (mainwindowedit.cpp, Schalter DSS_APP_EDITING); den
// neuen Stand zeigt es erst, wenn der Kern ihn an alle schickt.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // keepLayout: Grösse, Lage und Anordnung der Docks beim Start laden und beim
    // Beenden speichern (aus bei --bildschirmfoto).
    // client darf nullptr sein (dann ohne Kern, zum Beispiel für Tests des Aussehens).
    explicit MainWindow(const Theme &theme, bool keepLayout, dss::CoreClient *client,
                        QWidget *parent = nullptr);

    // Für --bildschirmfoto mit --verbindung: wählen wie mit der Maus.
    void selectFirstOverlay();
    void selectSourceRow(int row);
    void scrollPropertiesToKindPage();
    // P09: Zeichner der Leinwand (nullptr ohne DSS_APP_RENDERER) und die Leinwand, für
    // Bildschirmfotos, Prüfungen und die Leistungsmessung.
    SourceRenderer *renderer() const { return m_renderer; }
    CanvasWidget *canvas() const { return m_canvas; }
    // P10-vor: Ordner für «Leinwand als Bild speichern» (Standard: dss::canvasImageDir()); Tests
    // setzen einen eigenen, damit nichts in ~/Bilder landet.
    void setCanvasImageDir(const QString &dir) { m_canvasImageDir = dir; }

    // Zustand eines Verbindungsknopfes im Kopfbereich.
    void setConnectionState(dss::Connection connection, dss::ConnectionState state);

signals:
    // Nach dem ersten Zeichnen, einmal (für --bildschirmfoto).
    void firstPainted();

protected:
    void paintEvent(QPaintEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void createMenus();
    void createHeader();
    void createCentralArea();
    void createDocks();
    void createStatusBar();
    void restoreLayout();
    void saveLayout();
    void showAbout();
    void showSettings(); // P10-vor: Werkzeuge → Einstellungen …
    // P13: Reiter wechseln (Docks des Editors nur bei «Overlays»), Reiter «Chat-Bot» nachführen
    void onModuleSelected(int module);
    void showTwitchSettings(); // Klick auf den Twitch-Knopf: Dialog beim Bereich «Twitch»
    QList<QDockWidget *> editorDocks() const;
    void refreshChatBotHeader();
    void connectChatBot();
    void requestActionRuns();
    void refreshChatSend(); // P14 e2-13: Eingabezeile «Chat» aus dss::chatSendView
    // P14 e2-14: Unterreiter «Aktionen», links. Bearbeitungen gehen nacheinander an den Kern
    // (runActionEdits); «Gespeichert: <Name>» und Fehler in der Statuszeile.
    void connectActions();
    void refreshActionList();
    void refreshActionButtons();
    // section: Abschnitt der Mitte (Prüfmeldung am Feld statt in der Statuszeile); deleted:
    // «Gelöscht: <Name>» statt «Gespeichert: <Name>».
    void runActionEdits(const dss::EditResult &result, const QString &savedName,
                        ActionsPage::Section section = ActionsPage::NoSection, bool deleted = false);
    void sendNextActionEdit();
    // P14 e2-15, Mitte: Abschnitte aus dem Stand des Kerns, Freigaben der Schritte und von «▶ Testen».
    // resetFields: Felder zeigen wieder den Wert des Kerns (nach Prüfmeldung oder Ablehnung).
    void refreshActionDetail(bool resetFields = false);
    void refreshStepButtons();
    void refreshActionTest();
    // P14 e2-16, rechts: Seite und Werte zur Auswahl der Mitte
    void refreshActionFields(bool resetFields = false);
    dss::ActionViewNames actionViewNames() const;
    void connectActionDetail();
    // Bearbeitung der gezeigten Aktion: fn bekommt die Aktion des Kerns und liefert die Bearbeitung
    void editDetailAction(ActionsPage::Section section,
                          const std::function<dss::EditResult(const dss::actions::ActionsConfig &,
                                                              const dss::actions::Action &)> &fn);
    void startActionTest();
    void askRenameActionGroup(const QString &group);
    void askRemoveActionGroup(const QString &group);
    // Unterreiter «Aktionen» mit der Aktion gewählt (steht sie wegen der Suche nicht da, wird die
    // Suche geleert); aus «Läufe der Aktionen» und «Befehle».
    void openAction(const QUuid &actionId);
    // P14 e3-5: Unterreiter «Befehle» (mainwindowcommands.cpp). Bearbeitungen gehen nacheinander an
    // den Kern wie bei «Aktionen»; Prüfmeldungen unter dem Abschnitt des Formulars.
    void connectCommands();
    void refreshCommandList();
    // resetFields wie refreshActionDetail
    void refreshCommandDetail(bool resetFields = false);
    void refreshCommandTest();
    void runCommandEdits(const dss::EditResult &result, const QString &savedName,
                         CommandForm::Section section = CommandForm::NoSection, bool deleted = false);
    void sendNextCommandEdit();
    // Bearbeitung des gezeigten Befehls: change ändert eine Kopie des Befehls aus dem Kern (putCommand)
    void editShownCommand(CommandForm::Section section, const std::function<void(dss::actions::Command &)> &change);
    void startCommandTest(int userIndex);
    void sendNextCommandTest();
    // Variablen holen, solange «Befehle» sichtbar ist (Spalte «Benutzt»)
    void requestShownVariables();
    void saveCanvasImage(); // P10-vor: Datei → Leinwand als Bild speichern
    void connectCore();
    void onCoreConnectedChanged(bool connected);
    void onStats(const dss::SystemStats &stats);
    void refreshGauges(const dss::SystemStats &stats);
    void refreshConnections();
    void refreshOverlayList();
    void refreshOverlay();
    void refreshProperties();
    void refreshFonts();
    void refreshActions();
    void onOverlayItemChanged(QTreeWidgetItem *current);
    void onSourceItemChanged(QListWidgetItem *current);
    void onCoreError(const dss::ErrorInfo &error);
    void showStatus(const QString &text);
    // P10, Teil C: Browserquelle des gewählten Overlays und Meldungen des Overlay-Servers
    void refreshBrowserSource();
    void refreshServerStatus();
    void copyBrowserSource();
    // P10c, E-29: Zeile «OBS» im Dock «Steuerung»
    void refreshObsSource();
    // P12b b5: Zeile «Ton Videos» im Dock «Steuerung» aus Einstellungen und Ausgängen des Kerns;
    // Änderungen gehen mit update_settings an den Kern. P14 t: dazu die Zeile «Ton Chat-Bot».
    void refreshAudioRoute();
    void sendAudioRoute(const QString &purpose, const dss::AudioRouteChange &change);
    void showObsSourceDialog();
    std::optional<dss::Overlay> selectedOverlay() const;
    // P14 d: gewähltes Overlay mit dem Laufzustand darüber (für Leinwand und Zeichner, K-07)
    std::optional<dss::Overlay> canvasOverlay() const;
    // P14 d: Bereich «Chat-Bot» im Dock «Eigenschaften»; zählt die Restdauer mit m_liveTimer herunter
    void refreshLiveRow();
    void hideLive();

    // Bearbeiten (mainwindowedit.cpp)
    bool editingAllowed() const;
    void connectEditing();
    qint64 sendChange(const dss::ChangeRequest &request, const QString &coalesceKey = QString());
    void onChangeFinished(const dss::ChangeResult &result);
    void selectOverlay(const QUuid &id);
    void addOverlay();
    void renameOverlay(const QUuid &id);
    void copyOverlay(const QUuid &id);
    void removeOverlay(const QUuid &id);
    void createFolder();
    void renameFolder(const QUuid &id);
    void removeFolder(const QUuid &id);
    void showOverlayMenu(const QPoint &position);
    void onOverlayDropped(const QUuid &id, bool isFolder, const QUuid &target, bool targetIsFolder,
                          dss::DropPosition position);
    void onFolderExpanded(QTreeWidgetItem *item, bool expanded);
    void addMediaSource(bool video);
    void addTextSource();
    void removeSource(const QUuid &id);
    void renameSource(const QUuid &id);
    void stepSource(const QUuid &id, int step);
    void toggleVisibility(const QUuid &id);
    void toggleOutput(const QUuid &overlayId); // P10 (E-24)
    void newToken();                           // P10, Teil A 8
    void showSourceMenu(const QPoint &position);
    // Einträge für eine Quelle (Dock «Quellen» und Leinwand), danach die zum Hinzufügen.
    void fillSourceMenu(QMenu *menu, const dss::Overlay &overlay, const QUuid &id);
    // P09b (E-25): Befehle «Transformieren» an der gewählten Quelle (Schalter DSS_APP_TRANSFORM)
    void createTransformMenu(QMenu *edit);
    // P10a: Tastenkürzel an Leinwand und Dock «Quellen», Knöpfe im Bereich «Transformation»
    void connectTransformShortcuts();
    void showCanvasMenu(const QUuid &id, const QPoint &globalPosition);
    void runTransform(dss::TransformCommand command);
    void copyTransform();
    void pasteTransform();
    void onSourceDropped(const QUuid &id, int insertRow);
    void updateSelectedSource(const dss::SourceChanges &changes, const QString &field);
    void onNameEdited();
    void onOverlaySizeEdited();
    // Leinwand (P09, Teil C)
    void onCanvasSourceClicked(const QUuid &id);
    void onCanvasGeometryEdited(const QUuid &id, const dss::SourceGeometry &geometry, bool finished);
    void clearFinishedPreview();
    void onFamilyChosen(int index);
    void onFontStyleChosen();
    void chooseColor();
    void replaceImage();
    QString askMediaFile(const QString &title, const QString &filter);
    // Dock «Medien» (P10b, E-28, mainwindowmedia.cpp, Schalter DSS_APP_MEDIA_DOCK)
    void setupMediaDock();
    void connectMedia();
    void refreshMedia();
    void refreshMediaImports();
    void refreshMediaSelection();
    void onMediaPreview(const QString &reference);
    void onMediaItemChanged(QTreeWidgetItem *current);
    QUuid mediaTargetFolder() const;
    void addMediaFiles();
    void createMediaFolder();
    void renameMediaItem(QTreeWidgetItem *item);
    void removeMediaItem();
    void showMediaMenu(const QPoint &position);
    void onMediaDropped(const QUuid &draggedFolder, const QString &draggedFile, bool targetIsFolder,
                        const QUuid &targetFolder, const QString &targetFile, dss::DropPosition position);
    void onMediaFolderExpanded(QTreeWidgetItem *item, bool expanded);
    void addSourceFromMedia(const QString &reference, const QPoint &center, std::optional<int> insertIndex);

    Theme m_theme;
    bool m_keepLayout = false;
    QMenu *m_docksMenu = nullptr;
    QMenu *m_transformMenu = nullptr;      // P09b, nur mit DSS_APP_TRANSFORM
    QAction *m_copyTransform = nullptr;
    QAction *m_pasteTransform = nullptr;
    QAction *m_resetTransform = nullptr;   // P10a: auch Knopf im Bereich «Transformation»
    QList<QAction *> m_transformActions;   // alle Befehle ausser Kopieren und Einfügen
    QMenuBar *m_menuBar = nullptr;
    QAction *m_settingsAction = nullptr;       // P10-vor, nur mit DSS_APP_SETTINGS_DIALOG
    QAction *m_canvasImageAction = nullptr;    // P10-vor, nur mit DSS_APP_CANVAS_IMAGE
    QString m_canvasImageDir;                  // Ablage der Bilder der Leinwand
    SettingsDialog *m_settingsDialog = nullptr;
    ObsSourceDialog *m_obsSourceDialog = nullptr;
    HeaderBar *m_header = nullptr;
    QStackedWidget *m_modules = nullptr;
    ChatBotPage *m_chatBot = nullptr;  // P13
    dss::EventRows m_eventRows;        // P13: Bereich «Ereignisse»
    qint64 m_actionsEnabledRequest = 0; // P14 e2-11: offene Anfrage «Chat-Bot aktiv»
    dss::RunRows m_runRows;             // P14 e2-12: Bereich «Läufe der Aktionen»
    bool m_runsRequested = false;       // get_action_runs in dieser Verbindung geschickt
    dss::ChatRows m_chatRows;           // P14 e2-13: Bereich «Chat»
    qint64 m_chatSendRequest = 0;       // offene Anfrage chat_send (Text leeren bei Erfolg)
    QList<dss::ActionsEdit> m_actionEdits; // P14 e2-14: noch zu schickende Bearbeitungen
    qint64 m_actionEditRequest = 0;     // offene Bearbeitung
    QString m_actionEditSaved;          // Name für «Gespeichert: <Name>» nach der letzten
    QUuid m_actionEditCreated;          // neue Aktion, danach wählen
    // P14 e2-15: Abschnitt der Mitte, aus dem die Bearbeitung kommt (Prüfmeldung am Feld, neue
    // Schritte und Auslöser wählen); nach dem Löschen «Gelöscht: <Name>»
    ActionsPage::Section m_actionEditSection = ActionsPage::NoSection;
    bool m_actionEditDeleted = false;
    qint64 m_actionTestRequest = 0;     // offene Anfrage action_test
    QString m_actionTestName;
    bool m_actionTestReal = false;
    // P14 e3-5: «Befehle», wie die Bearbeitungen der Aktionen
    QList<dss::ActionsEdit> m_commandEdits;
    qint64 m_commandEditRequest = 0;
    QString m_commandEditSaved;
    QUuid m_commandEditCreated;
    CommandForm::Section m_commandEditSection = CommandForm::NoSection;
    bool m_commandEditDeleted = false;
    QList<dss::ActionsEdit> m_commandTests; // noch zu schickende action_test von «▶ Testen als …»
    qint64 m_commandTestRequest = 0;
    QString m_commandTestName;
    dss::ModuleDocks m_moduleDocks;    // P13: Docks je Reiter
    QByteArray m_editorLayout;         // P13: Anordnung beim Verlassen von «Overlays» (saveLayout)
    CanvasWidget *m_canvas = nullptr;
    SourceRenderer *m_renderer = nullptr; // P09, nur mit DSS_APP_RENDERER
    OverlaysDock m_overlaysDock;
    SourcesDock m_sourcesDock;
    PropertiesDock m_propertiesDock;
    ControlDock m_controlDock;
    MediaDock m_mediaDock;                              // P10b; dock nullptr ohne DSS_APP_MEDIA_DOCK
    dss::MediaPreviewLoader *m_mediaPreviews = nullptr; // P10b, nur mit DSS_APP_RENDERER
    QLabel *m_statusMessage = nullptr;
    QLabel *m_importStatus = nullptr; // P10-vor: «Übernehme …», solange der Kern Dateien kopiert
    QLabel *m_coreStatus = nullptr;
    QLabel *m_serverStatus = nullptr; // P10: Meldungen des Overlay-Servers, leer ohne Meldung
    bool m_firstPaintReported = false;

    dss::CoreClient *m_client = nullptr;
    QTimer *m_liveTimer = nullptr; // P14 d: nächste Anzeige der Restdauer im Bereich «Chat-Bot»
    // Zustand des Fensters (Regel 9): Auswahl und Verlauf der Anzeigen.
    QUuid m_selectedOverlay;
    QUuid m_selectedFolder;
    QUuid m_selectedSource;
    // Lage einer Quelle beim Ziehen auf der Leinwand (P09, Teil C): gilt, bis der Kern die letzte
    // Änderung beantwortet hat, damit Leinwand und «Eigenschaften» nicht zurückspringen.
    struct GeometryPreview
    {
        QUuid overlayId;
        QUuid sourceId;
        dss::SourceGeometry geometry;
        bool dragging = false;
    };
    std::optional<GeometryPreview> m_geometryPreview;
    // Kopierte Transformation (P09b, E-25): Zwischenablage des Fensters, nicht im Kern; gilt auch
    // beim Einfügen in eine Quelle eines anderen Overlays.
    std::optional<dss::SourceGeometry> m_copiedTransform;
    // Nach der Antwort auf diese Änderungen das neue Overlay bzw. die neue Quelle wählen.
    enum class SelectNew { Overlay, Source };
    QHash<qint64, SelectNew> m_selectOnResult;
    QList<qint64> m_audioRouteRequests; // P12b b5: offene update_settings der Zeile «Ton Videos»
    QString m_lastListError;
    // Auswahl im Dock «Medien» (P10b): höchstens eines davon
    QString m_selectedMediaFile;
    QUuid m_selectedMediaFolder;
    qint64 m_selectedImport = 0;
    QList<qint64> m_shownImports; // Übernahmen im Baum (nur Prozent nachführen, wenn gleich)
    QString m_lastMediaError;
    QString m_lastMediaDir;
    bool m_filling = false; // Listen werden gerade neu gefüllt, Auswahl-Signale übergehen
    dss::ValueHistory m_cpuHistory;
    dss::ValueHistory m_ramHistory;
    dss::ValueHistory m_lanHistory;
};
