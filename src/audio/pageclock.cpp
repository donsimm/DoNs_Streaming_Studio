// Uhr der Seite abbilden und glätten (P12b b4), siehe pageclock.h.
// Quelle der Zeiten der Seite: performance.now() und expectedDisplayTime liegen auf derselben
// Zeitachse (https://wicg.github.io/video-rvfc/, VideoFrameCallbackMetadata; in b4 nicht
// abgerufen, wie im Prototyp prototypes/ton/messseite.html genutzt und in M2 gemessen).
#include "pageclock.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace dss {

void PageClockMapper::add(int64_t arrivalNs, double pageNowMs)
{
    m_samples.emplace_back(arrivalNs, arrivalNs - std::llround(pageNowMs * 1e6));
    while (m_samples.size() > 1 && arrivalNs - m_samples.front().first > kWindowNs)
        m_samples.pop_front();
}

int64_t PageClockMapper::offsetNs() const
{
    if (m_samples.empty())
        return 0;
    return std::min_element(m_samples.cbegin(), m_samples.cend(),
                            [](const auto &a, const auto &b) { return a.second < b.second; })
        ->second;
}

int64_t PageClockMapper::toSteadyNs(double pageMs) const
{
    return std::llround(pageMs * 1e6) + offsetNs();
}

PageClock PageClockSmoother::apply(const PageClock &raw)
{
    if (!m_have || raw.loop != m_last.loop) {
        m_have = true;
        m_last = raw;
        return raw;
    }
    const int64_t predicted = m_last.mediaNs + (raw.steadyNs - m_last.steadyNs);
    const int64_t residual = raw.mediaNs - predicted;
    PageClock out = raw;
    if (std::llabs(residual) <= kSmoothMaxNs)
        out.mediaNs = predicted + residual / kDivisor;
    m_last = out;
    return out;
}

} // namespace dss
