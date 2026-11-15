#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>

#include <functional>

class QTimer;

// Uhr des Aktionssystems (P14 b1, Plan 3.1): Zeitpunkt und einmalige Zeitgeber. Der Kern nimmt
// SystemActionClock, Tests eine eigene Uhr, die sie von Hand weiterstellen. So prüfen die Tests
// Pausen, Abklingzeiten und die Grenze von 15 min ohne zu warten. Nur Qt Core.

namespace dss::actions {

class ActionClock
{
public:
    using TimerId = quint64;

    virtual ~ActionClock() = default;
    // Jetzt, in UTC
    virtual QDateTime now() const = 0;
    // Ruft callback einmal nach ms Millisekunden auf, nie im Aufruf selbst. 0 ist keine gültige
    // Kennung.
    virtual TimerId startTimer(qint64 ms, std::function<void()> callback) = 0;
    // Unbekannte oder abgelaufene Kennungen werden ignoriert.
    virtual void stopTimer(TimerId id) = 0;
};

// Uhr des Systems mit QTimer (Qt::PreciseTimer, damit eine Pause von 1 h nicht um Minuten
// abweicht: https://doc.qt.io/qt-6.8/qt.html#TimerType-enum). Muss im Thread der Läufe leben.
class SystemActionClock : public QObject, public ActionClock
{
    Q_OBJECT
public:
    explicit SystemActionClock(QObject *parent = nullptr);
    ~SystemActionClock() override;

    QDateTime now() const override;
    TimerId startTimer(qint64 ms, std::function<void()> callback) override;
    void stopTimer(TimerId id) override;

private:
    QHash<TimerId, QTimer *> m_timers;
    TimerId m_next = 1;
};

} // namespace dss::actions
