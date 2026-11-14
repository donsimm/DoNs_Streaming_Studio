#pragma once

#include "clockreport.h"
#include "pageclock.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUuid>

#include <memory>

class QTimer;

namespace dss {

class AudioDecoder;
class Mixer;
class OverlayStore;
struct Overlay;
struct Source;

// Ton der Videos im Kern (P12b b4, Commit 3, Plan P12 Abschnitte «Umfang» und «b4»): Meldungen der
// Seiten (…/clock, clockreport.h) an den Mischer, Stopp-Regeln. Nur Qt Core (CLAUDE.md Regel 9).
//
// - Je Videoquelle eine Stimme: Stelle im Mischer und Decoder (AudioDecoder) mit Vorrat. Sobald
//   eine Seite des Overlays verbunden ist (…/events/<page>), werden die Stimmen der sichtbaren
//   Videos vorbereitet, damit der Ton beim Start sofort da ist. Ablauf nach einem Stopp:
//   Mixer::stop → AudioDecoder::restart → Mixer::prepare; der nächste Start spielt diesen Vorrat
//   (Mixer::start mit dem neuen Durchgang).
// - Start: Meldung «start» der Seite. Kommt eine Lage («frame», «loop») für ein Video, das nicht
//   spielt, steigt der Ton dort ein (Einstieg), etwa nach 3 s ohne Meldung, nach dem Neustart des
//   Kerns oder wenn die Quelle wieder sichtbar ist. Nicht nach «end» oder «stop» desselben Laufs.
// - Mehrere Seiten auf dasselbe Overlay: Ton einmal je Videoquelle, nach der ersten Seite, die
//   startet; die anderen werden nicht beachtet, solange sie spielt.
// - Lage: Zeit der Seite über PageClockMapper auf steady_clock, geglättet (PageClockSmoother),
//   Länge aus duration_s an Mischer und Decoder (Wiederholen).
// - Stopp sofort bei: «stop» oder «end» der Seite, Ausgabe des Overlays aus, Quelle verborgen,
//   Quelle oder Overlay gelöscht, Quelle geändert (Datei oder Wiederholen), Ende der Ereignisse der
//   Seite (SSE), 3 s ohne Meldung.
// - Protokoll: «Ton: Start …», «Ton: Einstieg …», «Ton: Stopp …: <Grund> (…)», «Ton: Nachführen …»
//   (höchstens einmal je Sekunde, wenn der Mischer nachgeführt hat) und alle 10 s «Ton: Lage …».
class VideoAudio : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 kTimeoutNs = 3'000'000'000;  // Plan: 3 s ohne Meldung
    static constexpr int kCheckIntervalMs = 100;
    static constexpr qint64 kStatusIntervalNs = 10'000'000'000;
    static constexpr qint64 kCorrectionLogNs = 1'000'000'000;
    static constexpr qint64 kReleaseNs = 10'000'000'000; // Stimme ohne verbundene Seite freigeben
    static constexpr qint64 kPageIdleNs = 60'000'000'000;
    static constexpr int kMaxPages = 64;

    VideoAudio(const OverlayStore *store, Mixer *mixer, QObject *parent = nullptr);
    ~VideoAudio() override;

    void clockReported(const QUuid &overlayId, const ClockReport &report);
    void pageOpened(const QUuid &overlayId, const QString &page);
    void pageClosed(const QUuid &overlayId, const QString &page);
    // Nach jeder Änderung an Overlays und nach dem Neu laden: Stopp-Regeln prüfen.
    void overlaysChanged();
    // Beim Beenden, solange Mischer und Ausgang noch leben: alle Stimmen stoppen, Decoder beenden
    // und Stellen freigeben. Danach nimmt das Objekt nichts mehr an.
    void shutdown();

private:
    struct Page
    {
        QUuid overlayId;
        QString id;
        bool connected = false; // Ereignisse mit Kennung offen
        bool closed = false;    // Ereignisse beendet: nichts annehmen, bis wieder verbunden
        int run = -1;
        qint64 lastSeq = -1;
        qint64 lastNs = 0;
        bool stopped = false;     // «stop» ohne Quelle im laufenden Lauf
        QSet<QUuid> ended;        // «end» bzw. «stop» je Quelle im laufenden Lauf
        PageClockMapper mapper;
    };
    struct Voice;

    static QString key(const QUuid &overlayId, const QString &second);
    Page *page(const QUuid &overlayId, const QString &id, qint64 nowNs);
    Voice *ensureVoice(const Overlay &overlay, const Source &source);
    void startVoice(Voice &v, const Page &p, const ClockReport &report, const PageClock &clock);
    void stopVoice(Voice &v, const QString &reason);
    void removeVoice(const QString &key, const QString &reason);
    void setLength(Voice &v, qint64 durationNs);
    // Lautstärke und Stumm der Quelle an den Mischer (Format 5, P12b b5), bei Änderung ins Protokoll
    void applyVolume(Voice &v, const Source &source);
    void prepareVoices(const QUuid &overlayId);
    bool overlayConnected(const QUuid &overlayId) const;
    QString label(const Voice &v) const;
    void check();

    const OverlayStore *m_store;
    Mixer *m_mixer;
    QTimer *m_timer;
    QHash<QString, Page> m_pages;                    // key(Overlay, Seite)
    QHash<QString, std::shared_ptr<Voice>> m_voices; // key(Overlay, Quelle)
    bool m_noRoomLogged = false;
    bool m_shutdown = false;
};

} // namespace dss
