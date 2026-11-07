// Messung Punkt 66: je Takt neues Bild / kein neues / mehrere, abhängig vom Versatz des Takts
// zum ersten Bild. Takt wie die Leinwand: geplante Zeitpunkte start + Versatz + n/30 s.
#include "videoplayer.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
using namespace dss;
using Clock = VideoPlayer::Clock;
int main(int argc, char **argv)
{
    const char *file = argv[1];
    const int ticks = 60;
    std::vector<double> offsets;
    for (int i = -20; i <= 20; ++i) offsets.push_back(i / 10.0);
    for (int i = 3; i <= 33; i += 1) offsets.push_back(i);
    int badRuns = 0;
    for (double off : offsets) {
        VideoPlayer p(QString::fromLocal8Bit(file), true, false);
        while (!p.currentFrame()) {}
        const Clock::time_point start = Clock::now();
        const auto period = std::chrono::nanoseconds(1'000'000'000 / 30);
        const auto shift = std::chrono::nanoseconds(static_cast<long long>(off * 1e6));
        // Erster Takt nach dem ersten Bild, Versatz off (negativ: vor dem n-ten Zeitpunkt)
        p.frameAt(start + shift + period * 0 > start ? start : start);
        uint64_t last = p.frameSerial();
        int none = 0, one = 0, more = 0;
        for (int n = 1; n <= ticks; ++n) {
            const Clock::time_point due = start + shift + n * period;
            std::this_thread::sleep_until(due + std::chrono::milliseconds(n % 7));
            p.frameAt(due);
            const uint64_t s = p.frameSerial();
            if (s == last) ++none; else if (s == last + 1) ++one; else ++more;
            last = s;
        }
        if (none || more) ++badRuns;
        std::printf("%6.1f ms: %2d neu, %2d kein, %2d mehrere\n", off, one, none, more);
        std::fflush(stdout);
    }
    std::printf("ungleichmaessig: %d von %zu Versaetzen\n", badRuns, offsets.size());
}
