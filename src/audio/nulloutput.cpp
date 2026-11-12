// Ersatzausgang ohne Gerät (P12b b4), siehe nulloutput.h.
#include "nulloutput.h"

#include "mixer.h"

#include <chrono>
#include <vector>

namespace dss {

namespace {

constexpr int64_t kPeriodNs = int64_t(NullOutput::kPeriodFrames) * 1'000'000'000 / Mixer::kRate;
constexpr int64_t kResyncNs = 100'000'000;

int64_t steadyNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

NullOutput::NullOutput(Mixer *mixer) : m_mixer(mixer) {}

NullOutput::~NullOutput()
{
    m_stop.store(true);
    if (m_thread.joinable())
        m_thread.join();
}

bool NullOutput::start(QString *)
{
    if (!m_thread.joinable())
        m_thread = std::thread([this] { run(); });
    return true;
}

void NullOutput::setTarget(const QString &nodeName)
{
    State state;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_target = nodeName;
        state = nodeName.isEmpty()                         ? State::NoTarget
                : nodeName == QLatin1String(kDeviceName) ? State::Playing
                                                           : State::Missing;
        if (state == m_state)
            return;
        m_state = state;
        m_playing.store(state == State::Playing);
    }
    if (onLog)
        onLog(QStringLiteral("Ersatzausgang: Zustand %1").arg(QLatin1String(stateName(state))));
    if (onChange)
        onChange();
}

QString NullOutput::target() const
{
    std::lock_guard<std::mutex> l(m_mutex);
    return m_target;
}

QList<AudioDevice> NullOutput::devices() const
{
    return {{QString::fromLatin1(kDeviceName), QStringLiteral("Ersatzausgang ohne Gerät (Test)"), true}};
}

AudioOutput::State NullOutput::state() const
{
    std::lock_guard<std::mutex> l(m_mutex);
    return m_state;
}

void NullOutput::run()
{
    std::vector<float> buffer(size_t(kPeriodFrames) * Mixer::kChannels);
    int64_t cycle = steadyNs();
    while (!m_stop.load()) {
        if (m_playing.load()) {
            OutputTiming t;
            t.callNs = cycle;
            t.size = kPeriodFrames;
            t.rateNum = 1;
            t.rateDenom = Mixer::kRate;
            m_mixer->render(t, buffer.data(), kPeriodFrames);
            m_pulls.fetch_add(1, std::memory_order_relaxed);
        }
        cycle += kPeriodNs;
        const int64_t now = steadyNs();
        if (now - cycle > kResyncNs)
            cycle = now;
        std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(cycle)));
    }
}

} // namespace dss
