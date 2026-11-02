#pragma once

#include "connectionstate.h"

#include <QFrame>
#include <QHash>

class QButtonGroup;
class QToolButton;

// Kopfbereich: Modul-Reiter, Verbindungsknöpfe und Anzeigen für CPU, RAM, LAN.
// Noch ohne Daten: alle Verbindungen grau (nicht eingerichtet), alle Werte «–».
class HeaderBar : public QFrame
{
    Q_OBJECT

public:
    enum Module { Overlays = 0, ChatBot = 1 };

    explicit HeaderBar(QWidget *parent = nullptr);

    // Zeigt den Zustand einer Verbindung an (Farbe des Rahmens, Tooltip). Setzt später der Kern.
    void setConnectionState(dss::Connection connection, dss::ConnectionState state);

signals:
    void moduleSelected(int module);

private:
    QButtonGroup *m_modules = nullptr;
    QHash<dss::Connection, QToolButton *> m_connections;
};
