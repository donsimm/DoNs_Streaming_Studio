#pragma once

#include "canvasedit.h"
#include "connectionstate.h"
#include "docks.h"
#include "theme.h"
#include "windowdata.h"

#include <QHash>
#include <QMainWindow>
#include <QUuid>

#include <optional>

class CanvasWidget;
class HeaderBar;
class SourceRenderer;
class QLabel;
class QListWidgetItem;
class QMenu;
class QMenuBar;
class QStackedWidget;
class QTreeWidgetItem;

namespace dss {
class CoreClient;
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
    std::optional<dss::Overlay> selectedOverlay() const;

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
    void showSourceMenu(const QPoint &position);
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

    Theme m_theme;
    bool m_keepLayout = false;
    QMenu *m_docksMenu = nullptr;
    QMenuBar *m_menuBar = nullptr;
    HeaderBar *m_header = nullptr;
    QStackedWidget *m_modules = nullptr;
    CanvasWidget *m_canvas = nullptr;
    SourceRenderer *m_renderer = nullptr; // P09, nur mit DSS_APP_RENDERER
    OverlaysDock m_overlaysDock;
    SourcesDock m_sourcesDock;
    PropertiesDock m_propertiesDock;
    ControlDock m_controlDock;
    QLabel *m_statusMessage = nullptr;
    QLabel *m_coreStatus = nullptr;
    bool m_firstPaintReported = false;

    dss::CoreClient *m_client = nullptr;
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
    // Nach der Antwort auf diese Änderungen das neue Overlay bzw. die neue Quelle wählen.
    enum class SelectNew { Overlay, Source };
    QHash<qint64, SelectNew> m_selectOnResult;
    QString m_lastListError;
    QString m_lastMediaDir;
    bool m_filling = false; // Listen werden gerade neu gefüllt, Auswahl-Signale übergehen
    dss::ValueHistory m_cpuHistory;
    dss::ValueHistory m_ramHistory;
    dss::ValueHistory m_lanHistory;
};
