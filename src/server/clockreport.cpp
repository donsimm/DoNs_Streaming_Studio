#include "clockreport.h"

#include "overlay.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace dss {

namespace {

// Obergrenzen gegen unsinnige Werte: Zeiten der Seite (performance.now) bis 30 Tage, Lage und
// Länge eines Videos bis 24 h, Zähler bis 2^31 − 1 bzw. 2^53 (genau als double in JSON).
constexpr double kMaxPageMs = 30.0 * 24 * 3600 * 1000;
constexpr double kMaxMediaS = 24.0 * 3600;
constexpr double kMaxSeq = 9007199254740991.0;
constexpr double kMaxCount = 2147483647.0;

struct EventName
{
    const char *name;
    ClockReport::Event event;
};
constexpr EventName kEvents[] = {
    {"start", ClockReport::Event::Start}, {"frame", ClockReport::Event::Frame},
    {"loop", ClockReport::Event::Loop},   {"end", ClockReport::Event::End},
    {"stop", ClockReport::Event::Stop},
};

// Ganze Zahl von 0 bis max (JSON kennt nur double)
bool readCount(const QJsonObject &o, const char *key, double max, qint64 *value, QString *error)
{
    const QJsonValue v = o.value(QLatin1String(key));
    const double d = v.toDouble(-1.0);
    if (!v.isDouble() || !std::isfinite(d) || d < 0 || d > max || d != std::floor(d)) {
        *error = QStringLiteral("«%1» fehlt oder ist keine ganze Zahl von 0 bis %2")
                     .arg(QLatin1String(key)).arg(max, 0, 'f', 0);
        return false;
    }
    *value = qint64(d);
    return true;
}

bool readNumber(const QJsonObject &o, const char *key, double max, double *value, QString *error)
{
    const QJsonValue v = o.value(QLatin1String(key));
    const double d = v.toDouble(-1.0);
    if (!v.isDouble() || !std::isfinite(d) || d < 0 || d > max) {
        *error = QStringLiteral("«%1» fehlt oder liegt nicht zwischen 0 und %2")
                     .arg(QLatin1String(key)).arg(max, 0, 'f', 0);
        return false;
    }
    *value = d;
    return true;
}

} // namespace

const char *clockEventName(ClockReport::Event event)
{
    for (const EventName &e : kEvents) {
        if (e.event == event)
            return e.name;
    }
    return "?";
}

bool validClockPage(const QString &page)
{
    static const QRegularExpression pattern(QStringLiteral("^[a-z0-9]{8,32}$"));
    return pattern.match(page).hasMatch();
}

bool parseClockReport(const QByteArray &body, ClockReport *report, QString *error)
{
    if (body.size() > kMaxClockBytes) {
        *error = QStringLiteral("Meldung zu gross");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (!doc.isObject()) {
        *error = QStringLiteral("kein JSON-Objekt");
        return false;
    }
    const QJsonObject o = doc.object();
    ClockReport r;
    if (o.value(QStringLiteral("v")).toInt(-1) != kClockVersion) {
        *error = QStringLiteral("Version «v» fehlt oder ist nicht %1").arg(kClockVersion);
        return false;
    }
    r.page = o.value(QStringLiteral("page")).toString();
    if (!validClockPage(r.page)) {
        *error = QStringLiteral("«page» fehlt oder ist ungültig");
        return false;
    }
    const QString eventName = o.value(QStringLiteral("event")).toString();
    const auto event = std::find_if(std::begin(kEvents), std::end(kEvents),
                                    [&eventName](const EventName &e) { return eventName == QLatin1String(e.name); });
    if (event == std::end(kEvents)) {
        *error = QStringLiteral("«event» fehlt oder ist unbekannt");
        return false;
    }
    r.event = event->event;
    qint64 count = 0;
    if (!readCount(o, "seq", kMaxSeq, &r.seq, error) || !readCount(o, "run", kMaxCount, &count, error))
        return false;
    r.run = int(count);
    if (!readNumber(o, "now_ms", kMaxPageMs, &r.nowMs, error))
        return false;

    const QJsonValue source = o.value(QStringLiteral("source"));
    if (!source.isUndefined()) {
        r.source = QUuid::fromString(source.toString());
        if (r.source.isNull()) {
            *error = QStringLiteral("«source» ist keine Kennung");
            return false;
        }
    } else if (r.event != ClockReport::Event::Stop) {
        *error = QStringLiteral("«source» fehlt");
        return false;
    }

    if (r.hasPosition()) {
        r.file = o.value(QStringLiteral("file")).toString();
        if (!validateMediaReference(r.file).isEmpty()) {
            *error = QStringLiteral("«file» fehlt oder ist kein Verweis media/…");
            return false;
        }
        double mediaS = 0.0;
        if (!readCount(o, "loop", kMaxCount, &count, error) || !readNumber(o, "media_s", kMaxMediaS, &mediaS, error)
            || !readNumber(o, "display_ms", kMaxPageMs, &r.displayMs, error))
            return false;
        r.loop = int(count);
        r.mediaNs = std::llround(mediaS * 1e9);
    }

    const QJsonValue duration = o.value(QStringLiteral("duration_s"));
    if (!duration.isUndefined() && !duration.isNull()) {
        double d = 0.0;
        if (!readNumber(o, "duration_s", kMaxMediaS, &d, error))
            return false;
        r.durationNs = std::llround(d * 1e9);
    }
    *report = r;
    return true;
}

bool ClockLimiter::allow(const QString &page, qint64 nowNs)
{
    // Ruhende Seiten entfernen (selten: nur wenn die Liste voll ist)
    if (!m_pages.contains(page) && m_pages.size() >= kMaxPages) {
        for (auto it = m_pages.begin(); it != m_pages.end();) {
            if (nowNs - it->lastNs > kIdleNs)
                it = m_pages.erase(it);
            else
                ++it;
        }
        if (m_pages.size() >= kMaxPages)
            return false;
    }
    auto it = m_pages.find(page);
    if (it == m_pages.end()) {
        it = m_pages.insert(page, Bucket());
        it->lastNs = nowNs;
    }
    Bucket &b = *it;
    b.tokens = std::min<double>(kBurst, b.tokens + double(nowNs - b.lastNs) * 1e-9 * kRatePerSecond);
    b.lastNs = nowNs;
    if (b.tokens < 1.0)
        return false;
    b.tokens -= 1.0;
    return true;
}

} // namespace dss
