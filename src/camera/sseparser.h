#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Leser für Server-Sent Events (GET events des Kamera-Dienstes, docs/09-kamera-dienst.md,
// «Ereignisse»). Ohne Netz und ohne Qt Network, damit er für sich getestet werden kann.
//
// Nach der Spezifikation: Zeilen enden mit CRLF, LF oder CR; eine leere Zeile schliesst ein
// Ereignis ab; Zeilen mit «:» am Anfang sind Kommentare; nach dem ersten «:» fällt ein Leerzeichen
// weg; mehrere «data»-Zeilen werden mit LF verbunden; ohne «data» kein Ereignis; ohne «event» heisst
// das Ereignis «message»; ein BOM am Anfang des Stroms fällt weg. «retry» wird nicht beachtet: Der
// Client verbindet immer nach 2 s neu (Plan P15, 7.).
//
// Quelle: https://html.spec.whatwg.org/multipage/server-sent-events.html#event-stream-interpretation

namespace dss::camera {

struct SseEvent
{
    QString id;   // letzte Kennung, auch von früheren Ereignissen
    QString name; // Feld «event», sonst «message»
    QByteArray data;

    bool operator==(const SseEvent &) const = default;
};

class SseParser
{
public:
    // Obergrenze einer Zeile und der Daten eines Ereignisses (Schutz vor einem Strom ohne Ende).
    static constexpr qsizetype kMaxBytes = 4 * 1024 * 1024;

    // Neue Bytes; gibt die damit fertigen Ereignisse zurück. false in ok: Grenze überschritten,
    // der Strom ist nicht mehr lesbar (neu verbinden).
    QList<SseEvent> feed(const QByteArray &bytes, bool *ok = nullptr);
    // Für einen neuen Strom.
    void reset();

private:
    void processLine(const QByteArray &line, QList<SseEvent> &out);

    QByteArray m_buffer;
    QByteArray m_data;
    QString m_name;
    QString m_lastId;
    bool m_hasData = false;
    bool m_started = false;   // BOM geprüft
    bool m_skipLf = false;    // letztes Zeichen war CR: ein folgendes LF gehört dazu
    bool m_overflow = false;
};

} // namespace dss::camera
