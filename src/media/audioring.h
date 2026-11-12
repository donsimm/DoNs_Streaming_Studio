#pragma once

// Ring für Ton zwischen Decoder und Mischer (P12b b2): ein Schreiber (Thread des Decoders), ein
// Leser (Echtzeit-Abruf des Ausgangs), ohne Sperren und ohne Speicher anzulegen nach dem Bau.
// Proben float, 48 kHz, Stereo verschränkt (L R L R …), gezählt in Rahmen (eine Probe je Kanal).
//
// Durchgänge: Jeder Durchgang ist der Ton der Datei von vorne (Start, Neubeginn, Wiederholen). Der
// Schreiber setzt am Anfang jedes Durchgangs eine Marke mit dessen Nummer an die Schreibstelle; die
// Lage im Durchgang zählt ab der Marke von 0. Die Nummern vergibt der Ring (allocatePass), damit
// sie für eine Stelle des Mischers auch über mehrere Decoder hinweg steigen. Der Leser verwirft
// Durchgänge, die nicht mehr gelten, und braucht dafür keinen Befehl an den Schreiber.
//
// Ordnung: Die Marke wird vor den Proben an ihrer Stelle veröffentlicht (release auf m_markWrite,
// danach release auf m_write). Sieht der Leser die Proben (acquire auf m_write), sieht er auch die
// Marke. Quelle: https://en.cppreference.com/w/cpp/atomic/memory_order (Release-Acquire ordering).

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>

namespace dss {

class AudioRing
{
public:
    static constexpr int kChannels = 2;
    static constexpr size_t kMaxMarks = 64;

    struct Mark
    {
        uint64_t frame = 0; // Schreibstelle (fortlaufend, nicht umgebrochen)
        uint32_t pass = 0;
    };

    // Platz für mindestens minFrames Rahmen, aufgerundet auf eine Zweierpotenz.
    explicit AudioRing(size_t minFrames)
    {
        size_t c = 1;
        while (c < minFrames)
            c <<= 1;
        m_capacity = c;
        m_mask = c - 1;
        m_data = std::make_unique<float[]>(c * kChannels);
    }
    AudioRing(const AudioRing &) = delete;
    AudioRing &operator=(const AudioRing &) = delete;

    size_t capacity() const { return m_capacity; }

    // Nummer für einen neuen Durchgang, aus jedem Thread.
    uint32_t allocatePass() { return m_nextPass.fetch_add(1, std::memory_order_relaxed); }
    // Nummer, die der nächste Durchgang bekäme; alle bisherigen sind kleiner.
    uint32_t nextPass() const { return m_nextPass.load(std::memory_order_relaxed); }

    // --- Schreiber ---
    size_t writable() const
    {
        return m_capacity - size_t(m_write.load(std::memory_order_relaxed) - m_read.load(std::memory_order_acquire));
    }
    bool markWritable() const
    {
        return m_markWrite.load(std::memory_order_relaxed) - m_markRead.load(std::memory_order_acquire) < kMaxMarks;
    }
    // Marke an der jetzigen Schreibstelle; vorher markWritable() prüfen.
    void markPass(uint32_t pass)
    {
        const uint64_t mw = m_markWrite.load(std::memory_order_relaxed);
        m_marks[mw % kMaxMarks] = Mark{m_write.load(std::memory_order_relaxed), pass};
        m_markWrite.store(mw + 1, std::memory_order_release);
    }
    // n Rahmen schreiben (frames == nullptr: Stille); n <= writable().
    void write(const float *frames, size_t n)
    {
        const uint64_t w = m_write.load(std::memory_order_relaxed);
        const size_t at = size_t(w & m_mask);
        const size_t first = std::min(n, m_capacity - at);
        copyIn(at, frames, first);
        copyIn(0, frames ? frames + first * kChannels : nullptr, n - first);
        m_write.store(w + n, std::memory_order_release);
    }

    // --- Leser ---
    size_t readable() const
    {
        return size_t(m_write.load(std::memory_order_acquire) - m_read.load(std::memory_order_relaxed));
    }
    uint64_t readPosition() const { return m_read.load(std::memory_order_relaxed); }
    // i-ter lesbarer Rahmen (kChannels Werte); i < readable().
    const float *frame(size_t i) const
    {
        return m_data.get() + size_t((m_read.load(std::memory_order_relaxed) + i) & m_mask) * kChannels;
    }
    void consume(size_t n) { m_read.store(m_read.load(std::memory_order_relaxed) + n, std::memory_order_release); }
    // Vorderste ungelesene Marke.
    bool frontMark(Mark *mark) const
    {
        const uint64_t mr = m_markRead.load(std::memory_order_relaxed);
        if (m_markWrite.load(std::memory_order_acquire) == mr)
            return false;
        *mark = m_marks[mr % kMaxMarks];
        return true;
    }
    void popMark() { m_markRead.store(m_markRead.load(std::memory_order_relaxed) + 1, std::memory_order_release); }

private:
    void copyIn(size_t at, const float *src, size_t n)
    {
        if (n == 0)
            return;
        float *dst = m_data.get() + at * kChannels;
        if (src)
            std::memcpy(dst, src, n * kChannels * sizeof(float));
        else
            std::fill(dst, dst + n * kChannels, 0.0f);
    }

    size_t m_capacity = 0;
    size_t m_mask = 0;
    std::unique_ptr<float[]> m_data;
    std::atomic<uint64_t> m_write{0};
    std::atomic<uint64_t> m_read{0};
    std::array<Mark, kMaxMarks> m_marks{};
    std::atomic<uint64_t> m_markWrite{0};
    std::atomic<uint64_t> m_markRead{0};
    std::atomic<uint32_t> m_nextPass{0};
};

} // namespace dss
