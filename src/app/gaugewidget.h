#pragma once

#include <QColor>
#include <QFrame>
#include <QList>

#include <optional>

// Verlaufsfläche einer Anzeige im Kopfbereich (CPU, RAM, LAN): zeichnet die letzten Werte
// als Linie, wie auf docs/bilder/oberflaeche.png. Zeichnet nur; die Werte und den oberen Rand
// rechnet dss_common (windowdata.h).
class GaugeGraph : public QFrame
{
    Q_OBJECT

public:
    explicit GaugeGraph(const QColor &line, QWidget *parent = nullptr);

    // values: älteste zuerst, capacity Plätze von links nach rechts (rechts = neuester Wert).
    void setHistory(const QList<std::optional<double>> &values, int capacity, double maximum);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QColor m_line;
    QList<std::optional<double>> m_values;
    int m_capacity = 60;
    double m_maximum = 1.0;
};
