#pragma once

// Uhr für die Tests des Aktionssystems (P14 b1, b2): der Test stellt sie von Hand weiter,
// Zeitgeber laufen in der Reihenfolge ihrer Fälligkeit. So prüfen die Tests Pausen,
// Abklingzeiten, Zeitnachrichten und die Grenze von 15 min, ohne zu warten.

#include "actionclock.h"

#include <QDateTime>
#include <QList>
#include <QTimeZone>

#include <algorithm>
#include <functional>

class FakeActionClock : public dss::actions::ActionClock
{
public:
    QDateTime now() const override { return m_start.addMSecs(m_elapsed); }
    TimerId startTimer(qint64 ms, std::function<void()> callback) override
    {
        const TimerId id = m_next++;
        m_timers.append({m_elapsed + std::max<qint64>(0, ms), id, std::move(callback)});
        return id;
    }
    void stopTimer(TimerId id) override
    {
        m_timers.removeIf([id](const Timer &t) { return t.id == id; });
    }
    void advance(qint64 ms)
    {
        const qint64 end = m_elapsed + ms;
        for (;;) {
            auto next = std::min_element(m_timers.begin(), m_timers.end(), [](const Timer &a, const Timer &b) {
                return a.due != b.due ? a.due < b.due : a.id < b.id;
            });
            if (next == m_timers.end() || next->due > end)
                break;
            const Timer t = *next;
            m_timers.erase(next);
            m_elapsed = t.due;
            t.callback();
        }
        m_elapsed = end;
    }
    int pendingTimers() const { return int(m_timers.size()); }

private:
    struct Timer
    {
        qint64 due;
        TimerId id;
        std::function<void()> callback;
    };
    QDateTime m_start{QDate(2026, 11, 15), QTime(12, 0), QTimeZone::UTC};
    qint64 m_elapsed = 0;
    TimerId m_next = 1;
    QList<Timer> m_timers;
};
