#pragma once

#include "actionspage.h"
#include "actionview.h"
#include "canvasedit.h"
#include "commandspage.h"
#include "timerspage.h"
#include "connectionstate.h"
#include "docks.h"
#include "emoteimages.h"
#include "theme.h"
#include "transform.h"
#include "chatbotlive.h"
#include "windowdata.h"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QSet>
#include <QUuid>

#include <functional>
#include <optional>

class CameraPage;
class CanvasWidget;
class ChatBotPage;
class HeaderBar;
class QDockWidget;
class ObsSourceDialog;
class SettingsDialog;
class QueuesDialog;
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
    // P14 Emotes em-7: Emotes im Bereich «Chat» laden (mainwindowemotes.cpp), vor dem Start der
    // Verbindung zum Kern. Ohne Aufruf bleibt jedes Wort Text und das Fenster geht nie ins Netz
    // (Fenstertests, Bildschirmfoto). endpoints: echte Server oder ein lokaler Stub; cacheDir:
    // dss::emoteCacheDir() bzw. ein Testordner. Ohne Kern (client nullptr) passiert nichts.
    void enableEmotes(const dss::EmoteEndpoints &endpoints, const QString &cacheDir);
    // Abstände der stillen Wiederholungen nach «ohne Netz und ohne gespeicherte Listen» in ms
    // (Standard 1, 5 und 15 Min., Entscheid des Nutzers 2026-11-16); Tests verkürzen sie.
    void setEmoteRetryDelays(const QList<int> &delays) { m_emoteRetryDelays = delays; }

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
    // P14 Emotes em-7 (mainwindowemotes.cpp): Änderung nach dss::ChatRows::add zeigen (neue oder
    // durch das Echo geänderte Zeile) und deren Bilder bestellen; Laden beim ersten twitch_status
    // und bei einer anderen Kanal-ID; jedes Bild nur einmal bestellen.
    void showChatChange(const dss::RowsAppend &chat);
    void loadEmotes();
    void requestEmoteImages(const QList<dss::ChatRow> &rows);
    // Ergebnis des Ladens: Meldung (wartet, bis die Statuszeile frei ist) und Wiederholungen
    void onEmoteState(const dss::EmoteLoadState &state);
    void showEmoteStatus(const QString &text);
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
    // P14 e3-6: Unterreiter «Zeitnachrichten» (mainwindowtimers.cpp), wie «Befehle»
    void connectTimers();
    void refreshTimerList();
    void refreshTimerDetail(bool resetFields = false);
    void runTimerEdits(const dss::EditResult &result, const QString &savedName,
                       TimerForm::Section section = TimerForm::NoSection, bool deleted = false);
    void sendNextTimerEdit();
    // Bearbeitung der gezeigten Zeitnachricht: change ändert eine Kopie aus dem Kern (putTimer)
    void editShownTimer(TimerForm::Section section, const std::function<void(dss::actions::Timer &)> &change);
    // Variablen holen, solange «Befehle» (Spalte «Benutzt») oder «Variablen» sichtbar ist
    void requestShownVariables();
    // P14 e3-7: Unterreiter «Variablen» (mainwindowvariables.cpp); Bearbeitungen wie bei «Befehle»,
    // Prüfmeldung unter der Tabelle. area: VariablesPage::Area.
    void connectVariables();
    void refreshVariables();
    void refreshVariableButtons();
    void runVariableEdits(const dss::EditResult &result, int area, const QString &savedName, bool deleted = false);
    void sendNextVariableEdit();
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
    QAction *m_cameraSetupAction = nullptr;    // Werkzeuge → Kamera einrichten … (P15)
    QAction *m_canvasImageAction = nullptr;    // P10-vor, nur mit DSS_APP_CANVAS_IMAGE
    QString m_canvasImageDir;                  // Ablage der Bilder der Leinwand
    SettingsDialog *m_settingsDialog = nullptr;
    QueuesDialog *m_queuesDialog = nullptr; // P14 e3-8
    ObsSourceDialog *m_obsSourceDialog = nullptr;
    HeaderBar *m_header = nullptr;
    QStackedWidget *m_modules = nullptr;
    ChatBotPage *m_chatBot = nullptr;  // P13
    CameraPage *m_cameras = nullptr;   // P15 e2
    dss::EventRows m_eventRows;        // P13: Bereich «Ereignisse»
    qint64 m_actionsEnabledRequest = 0; // P14 e2-11: offene Anfrage «Chat-Bot aktiv»
    dss::RunRows m_runRows;             // P14 e2-12: Bereich «Läufe der Aktionen»
    bool m_runsRequested = false;       // get_action_runs in dieser Verbindung geschickt
    dss::ChatRows m_chatRows;           // P14 e2-13: Bereich «Chat»
    // P14 Emotes em-7: nullptr ohne enableEmotes. m_emoteChannel: Kanal-ID des letzten Ladens
    // (leer: nur global, nullopt: noch nicht geladen); m_emoteRequested: bestellte Schlüssel (chatEmoteImageKey), auch die
    // nicht geladenen, damit ein fehlendes Bild nicht bei jeder Zeile neu angefragt wird.
    dss::EmoteLoader *m_emoteLoader = nullptr;
    dss::EmoteImages m_emoteImages;
    // em-9: GIFs im Chat nur hier im Arbeitsspeicher (eigener LRU, Grösse wie m_emoteImages),
    // Schlüssel ist die Adresse; bestellt werden sie ebenfalls über m_emoteRequested
    dss::EmoteImages m_gifImages;
    std::optional<QString> m_emoteChannel;
    QSet<QString> m_emoteRequested;
    // Statuszeile: zuletzt gezeigte Emote-Meldung (darf ersetzt werden) und die wartende
    QString m_emoteStatusShown;
    QString m_emoteStatusPending;
    // Wiederholungen ohne Netz: Zeitgeber (einmalig), Abstände, nächster Versuch (Index in
    // m_emoteRetryDelays); m_emoteRetrying: das laufende Laden ist eine Wiederholung
    QTimer *m_emoteRetryTimer = nullptr;
    QList<int> m_emoteRetryDelays{60 * 1000, 5 * 60 * 1000, 15 * 60 * 1000};
    int m_emoteRetryNext = 0;
    bool m_emoteRetrying = false;
    qint64 m_chatSendRequest = 0;       // offene Anfrage chat_send (Text leeren bei Erfolg)
    QList<dss::ActionsEdit> m_actionEdits; // P14 e2-14: noch zu schickende Bearbeitungen
    qint64 m_actionEditRequest = 0;     // offene Bearbeitung
    QString m_actionEditSaved;          // Name für «Gespeichert: <Name>» nach der letzten
    QUuid m_actionEditCreated;          // neue Aktion, danach wählen
    // P14 e2-15: Abschnitt der Mitte, aus dem die Bearbeitung kommt (Prüfmeldung am Feld, neue
    // Schritte und Auslöser wählen); nach dem Löschen «Gelöscht: <Name>»
    ActionsPage::Section m_actionEditSection = ActionsPage::NoSection;
    bool m_actionEditDeleted = false;
    // P15 f1: zuletzt gezeigte Namen des Kamera-Dienstes (nur cameraPresets, lightPresets, cameraUnavailable)
    dss::ActionViewNames m_actionCameraNames;
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
    QList<dss::ActionsEdit> m_timerEdits; // P14 e3-6: wie m_commandEdits
    qint64 m_timerEditRequest = 0;
    QString m_timerEditSaved;
    QUuid m_timerEditCreated;
    TimerForm::Section m_timerEditSection = TimerForm::NoSection;
    bool m_timerEditDeleted = false;
    // P14 e3-7: «Variablen»; Zeilen wie in den Tabellen (Kennung → VariableRow)
    QList<dss::VariableRow> m_variableRows[2];
    // Gewählte Zeile je Tabelle (Prüfzeile leeren nur bei echtem Wechsel) und zuletzt gezeigter fileError
    QString m_variableSelection[2];
    QString m_variableFileError;
    QList<dss::ActionsEdit> m_variableEdits;
    qint64 m_variableEditRequest = 0;
    QString m_variableEditSaved;
    int m_variableEditArea = 0;
    bool m_variableEditDeleted = false;
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
