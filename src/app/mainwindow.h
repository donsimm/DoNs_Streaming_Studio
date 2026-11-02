#pragma once

#include "theme.h"

#include <QMainWindow>

class QMenu;
class QStackedWidget;

// Hauptfenster nach docs/bilder/oberflaeche.png. Dünn, ohne Logik (CLAUDE.md Regel 9):
// baut die Bereiche auf und verbindet sie, mehr nicht. Meldet einmal im Protokoll,
// wenn es zum ersten Mal gezeichnet wurde.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const Theme &theme, QWidget *parent = nullptr);

signals:
    // Nach dem ersten Zeichnen, einmal (für --bildschirmfoto).
    void firstPainted();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    void createMenus();
    void createCentralArea();
    void showAbout();

    Theme m_theme;
    QMenu *m_docksMenu = nullptr;
    QStackedWidget *m_modules = nullptr;
    bool m_firstPaintReported = false;
};
