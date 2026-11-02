#pragma once

#include <QFrame>

class QButtonGroup;

// Kopfbereich: Modul-Reiter, Verbindungsknöpfe und Anzeigen für CPU, RAM, LAN.
// In P05 ohne Daten: alle Verbindungen «nicht verbunden», alle Werte «–».
class HeaderBar : public QFrame
{
    Q_OBJECT

public:
    enum Module { Overlays = 0, ChatBot = 1 };

    explicit HeaderBar(QWidget *parent = nullptr);

signals:
    void moduleSelected(int module);

private:
    QButtonGroup *m_modules = nullptr;
};
