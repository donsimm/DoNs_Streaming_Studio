#include "sseparser.h"

namespace dss::camera {

QList<SseEvent> SseParser::feed(const QByteArray &bytes, bool *ok)
{
    QList<SseEvent> out;
    if (m_overflow) {
        if (ok)
            *ok = false;
        return out;
    }
    m_buffer.append(bytes);
    if (!m_started) {
        static const QByteArray bom("\xEF\xBB\xBF");
        if (m_buffer.size() < bom.size() && bom.startsWith(m_buffer)) {
            if (ok)
                *ok = true;
            return out; // Noch zu wenig für die Entscheidung.
        }
        if (m_buffer.startsWith(bom))
            m_buffer.remove(0, bom.size());
        m_started = true;
    }
    qsizetype start = 0;
    for (qsizetype i = 0; i < m_buffer.size(); ++i) {
        const char c = m_buffer.at(i);
        if (m_skipLf) {
            m_skipLf = false;
            if (c == '\n') {
                start = i + 1;
                continue;
            }
        }
        if (c != '\n' && c != '\r')
            continue;
        processLine(m_buffer.mid(start, i - start), out);
        m_skipLf = c == '\r';
        start = i + 1;
    }
    m_buffer.remove(0, start);
    if (m_buffer.size() > kMaxBytes || m_data.size() > kMaxBytes) {
        m_overflow = true;
        m_buffer.clear();
        m_data.clear();
    }
    if (ok)
        *ok = !m_overflow;
    return out;
}

void SseParser::reset()
{
    *this = SseParser();
}

void SseParser::processLine(const QByteArray &line, QList<SseEvent> &out)
{
    if (line.isEmpty()) {
        if (m_hasData) {
            SseEvent event;
            event.id = m_lastId;
            event.name = m_name.isEmpty() ? QStringLiteral("message") : m_name;
            event.data = m_data;
            out.append(event);
        }
        m_data.clear();
        m_name.clear();
        m_hasData = false;
        return;
    }
    if (line.startsWith(':'))
        return; // Kommentar, beim Kamera-Dienst alle 15 s ohne Ereignis
    const qsizetype colon = line.indexOf(':');
    const QByteArray field = colon < 0 ? line : line.left(colon);
    QByteArray value = colon < 0 ? QByteArray() : line.mid(colon + 1);
    if (value.startsWith(' '))
        value.remove(0, 1);
    if (field == "data") {
        if (m_hasData)
            m_data.append('\n');
        m_data.append(value);
        m_hasData = true;
    } else if (field == "event") {
        m_name = QString::fromUtf8(value);
    } else if (field == "id") {
        if (!value.contains('\0'))
            m_lastId = QString::fromUtf8(value);
    }
    // «retry» und unbekannte Felder: nichts.
}

} // namespace dss::camera
