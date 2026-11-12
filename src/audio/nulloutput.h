#pragma once

#include "audiooutput.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>

// Ersatzausgang ohne Gerät (P12b b4, Plan P12: «Ersatzausgang ohne Gerät in tst_kernton»). Ruft den
// Mischer in einem eigenen Thread alle 10 ms mit 480 Rahmen ab, wie ein Gerät mit fester Uhr: Die
// Abrufzeit ist die gedachte Zeit des Zyklus (steady_clock), size 480, delay, queued und buffered 0.
// Die Proben werden verworfen. Kein PipeWire, kein Ton.
//
// Der Kern nimmt ihn mit der Testoption --ersatzausgang statt PwOutput. Es gibt ein Gerät
// kDeviceName; nur wenn es gewählt ist, ruft der Thread den Mischer ab (Zustand «playing»), ein
// anderer Name gibt «missing», ein leerer «no_target». Hängt der Thread mehr als 100 ms nach, springt
// die gedachte Uhr auf die Zeit von jetzt (wie ein Unterlauf eines Geräts; der Mischer sieht einen
// Sprung des Ausgangs).

namespace dss {

class Mixer;

class NullOutput : public AudioOutput
{
public:
    static constexpr char kDeviceName[] = "dss_ersatzausgang";
    static constexpr uint32_t kPeriodFrames = 480; // 10 ms bei 48 kHz

    explicit NullOutput(Mixer *mixer);
    ~NullOutput() override;
    NullOutput(const NullOutput &) = delete;
    NullOutput &operator=(const NullOutput &) = delete;

    bool start(QString *error) override;
    void setTarget(const QString &nodeName) override;
    QString target() const override;
    QList<AudioDevice> devices() const override;
    State state() const override;
    QString stateDetail() const override { return {}; }

    // Zahl der Abrufe des Mischers (Tests)
    uint64_t pulls() const { return m_pulls.load(std::memory_order_relaxed); }

private:
    void run();

    Mixer *m_mixer;
    mutable std::mutex m_mutex;
    QString m_target;
    State m_state = State::NoTarget;
    std::atomic<bool> m_playing{false};
    std::atomic<bool> m_stop{false};
    std::atomic<uint64_t> m_pulls{0};
    std::thread m_thread;
};

} // namespace dss
