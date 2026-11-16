#include "actionclock.h"

#include <QTimer>

#include <algorithm>
#include <limits>

namespace dss::actions {

SystemActionClock::SystemActionClock(QObject *parent)
    : QObject(parent)
{
}

SystemActionClock::~SystemActionClock()
{
    qDeleteAll(m_timers);
}

QDateTime SystemActionClock::now() const
{
    return QDateTime::currentDateTimeUtc();
}

ActionClock::TimerId SystemActionClock::startTimer(qint64 ms, std::function<void()> callback)
{
    const TimerId id = m_next++;
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setTimerType(Qt::PreciseTimer);
    // QTimer nimmt int; längere Zeiten kommen im Aktionssystem nicht vor (Pause höchstens 1 h)
    timer->setInterval(int(std::clamp<qint64>(ms, 0, std::numeric_limits<int>::max())));
    connect(timer, &QTimer::timeout, this, [this, id, callback = std::move(callback)] {
        QTimer *t = m_timers.take(id);
        if (!t)
            return;
        t->deleteLater();
        callback();
    });
    m_timers.insert(id, timer);
    timer->start();
    return id;
}

void SystemActionClock::stopTimer(TimerId id)
{
    if (QTimer *t = m_timers.take(id)) {
        t->stop();
        t->deleteLater();
    }
}

} // namespace dss::actions
