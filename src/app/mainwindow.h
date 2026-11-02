#pragma once

#include "connectionstate.h"
#include "docks.h"
#include "theme.h"
#include "windowdata.h"

#include <QMainWindow>
#include <QUuid>

class CanvasWidget;
class HeaderBar;
class QLabel;
class QListWidgetItem;
class QMenu;
class QStackedWidget;

namespace dss {
class CoreClient;
struct SystemStats;
}

// Hauptfenster nach docs/bilder/oberflaeche.png. Dünn, ohne Logik (CLAUDE.md Regel 9):
// baut die Bereiche auf und verbindet sie, mehr nicht. Meldet einmal im Protokoll,
// wenn es zum ersten Mal gezeichnet wurde.
// Seit P07 zeigt es die Daten vom Kern (dss::CoreClient). Eigener Zustand ist nur die Auswahl
// (gewähltes Overlay, gewählte Quelle) und der Verlauf der Leistungswerte für die Anzeige.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // keepLayout: Grösse, Lage und Anordnung der Docks beim Start laden und beim
    // Beenden speichern (aus bei --bildschirmfoto).
    // client darf nullptr sein (dann ohne Kern, zum Beispiel für Tests des Aussehens).
    explicit MainWindow(const Theme &theme, bool keepLayout, dss::CoreClient *client,
                        QWidget *parent = nullptr);

    // Für --bildschirmfoto mit --verbindung: Zeile der Liste wählen wie mit der Maus.
    void selectOverlayRow(int row);
    void selectSourceRow(int row);

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
    void onOverlayItemChanged(QListWidgetItem *current);
    void onSourceItemChanged(QListWidgetItem *current);

    Theme m_theme;
    bool m_keepLayout = false;
    QMenu *m_docksMenu = nullptr;
    HeaderBar *m_header = nullptr;
    QStackedWidget *m_modules = nullptr;
    CanvasWidget *m_canvas = nullptr;
    OverlaysDock m_overlaysDock;
    SourcesDock m_sourcesDock;
    PropertiesDock m_propertiesDock;
    QLabel *m_statusMessage = nullptr;
    QLabel *m_coreStatus = nullptr;
    bool m_firstPaintReported = false;

    dss::CoreClient *m_client = nullptr;
    // Zustand des Fensters (Regel 9): Auswahl und Verlauf der Anzeigen.
    QUuid m_selectedOverlay;
    QUuid m_selectedSource;
    bool m_filling = false; // Listen werden gerade neu gefüllt, Auswahl-Signale übergehen
    dss::ValueHistory m_cpuHistory;
    dss::ValueHistory m_ramHistory;
    dss::ValueHistory m_lanHistory;
};
