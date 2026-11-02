#pragma once

#include "connectionstate.h"

#include <QFrame>
#include <QHash>
#include <QList>

#include <optional>

class GaugeGraph;
class QButtonGroup;
class QLabel;
class QToolButton;
struct Theme;

// Kopfbereich: Modul-Reiter, Verbindungsknöpfe und Anzeigen für CPU, RAM, LAN.
// Werte und Zustände kommen vom Kern (P07); ohne Verbindung alle Werte «–», alle grau.
class HeaderBar : public QFrame
{
    Q_OBJECT

public:
    enum Module { Overlays = 0, ChatBot = 1 };
    enum Gauge { Cpu = 0, Ram = 1, Lan = 2 };

    explicit HeaderBar(const Theme &theme, QWidget *parent = nullptr);

    // Wert, Tooltip und Verlauf einer Anzeige (älteste Werte zuerst).
    void setGauge(Gauge gauge, const QString &text, const QString &toolTip,
                  const QList<std::optional<double>> &history, int capacity, double maximum);

    // Zeigt den Zustand einer Verbindung an (Farbe des Rahmens, Tooltip). Setzt später der Kern.
    void setConnectionState(dss::Connection connection, dss::ConnectionState state);

signals:
    void moduleSelected(int module);

private:
    QButtonGroup *m_modules = nullptr;
    QHash<dss::Connection, QToolButton *> m_connections;
    QList<QLabel *> m_gaugeValues;
    QList<GaugeGraph *> m_gaugeGraphs;
};
