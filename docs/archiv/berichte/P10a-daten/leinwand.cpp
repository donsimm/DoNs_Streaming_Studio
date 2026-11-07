// Messung Punkt 66 wie die Leinwand (SourceRenderer): Takt ab einem beliebigen Zeitpunkt
// (Start + Versatz), jedes frameAt mit dem geplanten Zeitpunkt des Takts, auch das erste.
// Gezählt wird ab dem ersten gezeigten Bild: je Takt neues Bild, kein neues, mehrere.
#include "videoplayer.h"
#include <cstdio>
#include <thread>
using namespace dss;
using Clock = VideoPlayer::Clock;
int main(int argc, char **argv)
{
    const int ticks = 45;
    int bad = 0, runs = 0, badTicks = 0;
    for (int i = 0; i < 334; ++i) {
        const double off = i / 10.0; // 0 bis 33.3 ms
        const auto period = std::chrono::nanoseconds(1'000'000'000 / 30);
        VideoPlayer p(QString::fromLocal8Bit(argv[1]), true, false);
        const Clock::time_point start = Clock::now() + std::chrono::nanoseconds(long(off * 1e6));
        uint64_t last = 0;
        bool seen = false;
        int none = 0, one = 0, more = 0, n = 0;
        while (one + none + more < ticks) {
            ++n;
            const Clock::time_point due = start + n * period;
            std::this_thread::sleep_until(due + std::chrono::milliseconds(n % 7));
            const bool has = p.frameAt(due) != nullptr;
            const uint64_t s = p.frameSerial();
            if (seen) {
                if (s == last) ++none; else if (s == last + 1) ++one; else ++more;
            }
            seen = seen || has;
            last = s;
        }
        ++runs;
        if (none || more) { ++bad; badTicks += none + more; }
        std::printf("%5.1f ms: %2d neu, %2d kein, %2d mehrere\n", off, one, none, more);
        std::fflush(stdout);
    }
    std::printf("ungleichmaessig: %d von %d Laeufen, %d von %d Takten\n", bad, runs, badTicks, runs * ticks);
}
