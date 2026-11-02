#pragma once

#include "connectionstate.h"
#include "theme.h"

#include <QMainWindow>

class HeaderBar;
class QMenu;
class QStackedWidget;

// Hauptfenster nach docs/bilder/oberflaeche.png. Dünn, ohne Logik (CLAUDE.md Regel 9):
// baut die Bereiche auf und verbindet sie, mehr nicht. Meldet einmal im Protokoll,
// wenn es zum ersten Mal gezeichnet wurde.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    // keepLayout: Grösse, Lage und Anordnung der Docks beim Start laden und beim
    // Beenden speichern (aus bei --bildschirmfoto).
    explicit MainWindow(const Theme &theme, bool keepLayout, QWidget *parent = nullptr);

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

    Theme m_theme;
    bool m_keepLayout = false;
    QMenu *m_docksMenu = nullptr;
    HeaderBar *m_header = nullptr;
    QStackedWidget *m_modules = nullptr;
    bool m_firstPaintReported = false;
};
