#include "mainwindow.h"

#include <QTimer>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("DoNs Streaming Studio"));
    resize(1280, 720);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    if (m_firstPaintReported)
        return;
    m_firstPaintReported = true;
    // Erst nach dem Zeichnen melden, wie in den Prototypen von P01.
    QTimer::singleShot(0, this, [] { qInfo("Fenster gezeichnet"); });
}
