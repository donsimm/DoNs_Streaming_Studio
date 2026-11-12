#pragma once

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QUuid>

// Meldungen der Seite zur Lage ihrer Videos (P12b b4, Plan P12 Abschnitt «b4», Commit 1): POST
// auf …/clock, JSON, nur mit gültigem Token. Ohne Verbindung und ohne Qt Gui, testbar.
//
// Format (Version 1), ein Objekt je Meldung und Video:
//   {"v": 1, "page": "k3j4h5g6a1b2", "seq": 17, "event": "frame", "run": 2,
//    "source": "<uuid der Quelle>", "file": "media/<sha256>.mp4", "loop": 0,
//    "media_s": 12.345, "display_ms": 81234.5, "now_ms": 81236.0, "duration_s": 60.0}
// - page: Kennung der Seite, beim Laden zufällig gewählt, [a-z0-9]{8,32}. Dieselbe Kennung steht
//   in der Adresse der Ereignisse (…/events/<page>), damit der Kern das Ende der Verbindung der
//   Seite zuordnen kann.
// - seq: laufende Nummer je Seite (ältere und doppelte Meldungen verwirft der Kern).
// - event: "start" (Video beginnt von vorne, erstes Bild), "frame" (Lage, etwa jede Sekunde),
//   "loop" (erstes Bild nach dem Wiederholen), "end" (Ende ohne Wiederholen), "stop" (Seite
//   inaktiv; ohne source alle Videos der Seite).
// - run: Lauf der Seite, zählt bei jedem Start durch «aktiv» hoch.
// - source, file: Quelle und Datei, wie die Seite sie spielt. source fehlt nur bei "stop".
// - loop: Durchgang seit dem Start (0 = erster); media_s: mediaTime des Bildes, display_ms:
//   expectedDisplayTime (requestVideoFrameCallback, https://wicg.github.io/video-rvfc/); beide
//   nur bei start, frame, loop.
// - now_ms: performance.now() beim Absenden (alle Meldungen). Gleiche Zeitachse wie display_ms.
// - duration_s: Länge des Videos (HTMLMediaElement.duration), freiwillig; fehlt oder 0 = unbekannt.
// Zeiten der Seite (display_ms, now_ms) sind performance.now() der Seite; der Kern bildet sie
// mit dem Eintreffen (arrivalNs) auf seine Uhr ab (src/audio/pageclock.h).

namespace dss {

inline constexpr int kClockVersion = 1;
// Grösster Inhalt einer Meldung (ein Objekt mit kurzen Feldern braucht etwa 300 Byte).
inline constexpr qsizetype kMaxClockBytes = 2048;

struct ClockReport
{
    enum class Event { Start, Frame, Loop, End, Stop };

    QString page;
    qint64 seq = 0;
    Event event = Event::Frame;
    int run = 0;
    QUuid source;   // null nur bei Stop (alle Videos der Seite)
    QString file;   // bei Start, Frame, Loop
    int loop = 0;
    qint64 mediaNs = 0;
    double displayMs = 0.0;
    double nowMs = 0.0;
    qint64 durationNs = 0; // 0 = unbekannt
    qint64 arrivalNs = 0;  // steady_clock beim Eintreffen (Server), nicht aus der Meldung

    // Meldung mit Lage des Videos (media_s, display_ms)
    bool hasPosition() const { return event == Event::Start || event == Event::Frame || event == Event::Loop; }
};

// "start", "frame", … (Protokoll)
const char *clockEventName(ClockReport::Event event);
bool validClockPage(const QString &page);

// Liest eine Meldung; false mit Grund (deutsch, ohne den Inhalt zu wiederholen).
bool parseClockReport(const QByteArray &body, ClockReport *report, QString *error);

// Grenze je Seite (Eimer mit Marken): höchstens kBurst Meldungen am Stück, danach kRatePerSecond
// je Sekunde. Höchstens kMaxPages Seiten gleichzeitig; eine Seite ohne Meldung seit kIdleNs fällt
// heraus.
class ClockLimiter
{
public:
    static constexpr int kBurst = 40;
    static constexpr int kRatePerSecond = 20;
    static constexpr int kMaxPages = 64;
    static constexpr qint64 kIdleNs = 60'000'000'000;

    // true: Meldung annehmen. nowNs: steady_clock.
    bool allow(const QString &page, qint64 nowNs);

private:
    struct Bucket
    {
        double tokens = kBurst;
        qint64 lastNs = 0;
    };
    QHash<QString, Bucket> m_pages;
};

} // namespace dss
