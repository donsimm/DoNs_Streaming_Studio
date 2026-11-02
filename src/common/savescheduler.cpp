#include "savescheduler.h"

#include <QTimer>

namespace dss {

SaveScheduler::SaveScheduler(QObject *parent, int idleMs, int maxDelayMs)
    : QObject(parent), m_idle(new QTimer(this)), m_max(new QTimer(this))
{
    // https://doc.qt.io/qt-6.8/qtimer.html (singleShot, start() startet einen laufenden Timer neu)
    for (QTimer *t : {m_idle, m_max}) {
        t->setSingleShot(true);
        connect(t, &QTimer::timeout, this, &SaveScheduler::fire);
    }
    m_idle->setInterval(idleMs);
    m_max->setInterval(maxDelayMs);
}

void SaveScheduler::changed()
{
    m_idle->start(); // neu ab der letzten Änderung
    if (!m_max->isActive())
        m_max->start(); // läuft ab der ersten offenen Änderung
}

bool SaveScheduler::isPending() const
{
    return m_idle->isActive() || m_max->isActive();
}

void SaveScheduler::saveNow()
{
    if (isPending())
        fire();
}

void SaveScheduler::fire()
{
    m_idle->stop();
    m_max->stop();
    emit saveRequested();
}

} // namespace dss
