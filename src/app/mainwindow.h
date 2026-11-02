#pragma once

#include <QMainWindow>

// Leeres Hauptfenster. Meldet einmal im Protokoll, wenn es zum ersten Mal gezeichnet wurde.
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool m_firstPaintReported = false;
};
