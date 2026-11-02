#include "protocol.h"

#include "overlayjson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>

#include <cmath>
#include <limits>

namespace dss {

namespace {

const QString kType = QStringLiteral("type");

QJsonObject typed(const char *type)
{
    return QJsonObject{{kType, QString::fromLatin1(type)}};
}

// Felder werden streng geprüft: Ein Pflichtfeld mit falschem Typ macht die Nachricht
// ungültig. Unbekannte Felder werden übergangen (docs/08-kern-schnittstelle.md).
bool requireString(const QJsonObject &o, const QString &key, QString *value, QString *error)
{
    const QJsonValue v = o.value(key);
    if (!v.isString()) {
        *error = QStringLiteral("Feld %1 fehlt oder ist kein Text").arg(key);
        return false;
    }
    *value = v.toString();
    return true;
}

// Ganze Zahl im Bereich von qint64 (JSON-Zahlen sind double; ganze Zahlen bis 2^53 sind exakt).
bool requireInt(const QJsonObject &o, const QString &key, qint64 min, qint64 max, qint64 *value,
                QString *error)
{
    const QJsonValue v = o.value(key);
    const double d = v.toDouble();
    if (!v.isDouble() || std::trunc(d) != d || d < double(min) || d > double(max)) {
        *error = QStringLiteral("Feld %1 fehlt oder ist keine ganze Zahl von %2 bis %3")
                     .arg(key).arg(min).arg(max);
        return false;
    }
    *value = qint64(d);
    return true;
}

bool requireIntField(const QJsonObject &o, const QString &key, int min, int max, int *value,
                     QString *error)
{
    qint64 v = 0;
    if (!requireInt(o, key, min, max, &v, error))
        return false;
    *value = int(v);
    return true;
}

bool requireUuid(const QJsonObject &o, const QString &key, QUuid *id, QString *error)
{
    QString text;
    if (!requireString(o, key, &text, error))
        return false;
    const QUuid parsed = QUuid::fromString(text);
    if (parsed.isNull() || parsed.toString(QUuid::WithoutBraces) != text) {
        *error = QStringLiteral("Feld %1 ist keine Kennung (UUID in Kleinbuchstaben ohne Klammern)")
                     .arg(key);
        return false;
    }
    *id = parsed;
    return true;
}

QString idText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

// Zahl oder null; null → leer.
bool optionalNumber(const QJsonObject &o, const QString &key, std::optional<double> *value,
                    QString *error)
{
    const QJsonValue v = o.value(key);
    if (v.isNull()) {
        value->reset();
        return true;
    }
    if (!v.isDouble() || !std::isfinite(v.toDouble()) || v.toDouble() < 0) {
        *error = QStringLiteral("Feld %1 muss eine Zahl ab 0 oder null sein").arg(key);
        return false;
    }
    *value = v.toDouble();
    return true;
}

bool optionalKiB(const QJsonObject &o, const QString &key, std::optional<qint64> *value,
                 QString *error)
{
    if (o.value(key).isNull()) {
        value->reset();
        return true;
    }
    qint64 v = 0;
    if (!requireInt(o, key, 0, qint64(1) << 53, &v, error))
        return false;
    *value = v;
    return true;
}

QJsonValue numberOrNull(const std::optional<double> &v)
{
    return v ? QJsonValue(*v) : QJsonValue(QJsonValue::Null);
}

QJsonValue numberOrNull(const std::optional<qint64> &v)
{
    return v ? QJsonValue(double(*v)) : QJsonValue(QJsonValue::Null);
}

} // namespace

QByteArray encodeMessage(const QJsonObject &message)
{
    // Kompaktes JSON hat kein Zeilenende: «\n» in Texten wird als \n geschrieben.
    return QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
}

bool decodeMessage(const QByteArray &line, QJsonObject *message, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("kein gültiges JSON (%1)").arg(parseError.errorString());
        return false;
    }
    if (!doc.isObject()) {
        *error = QStringLiteral("Nachricht ist kein JSON-Objekt");
        return false;
    }
    if (!doc.object().value(kType).isString() || doc.object().value(kType).toString().isEmpty()) {
        *error = QStringLiteral("Feld type fehlt oder ist kein Text");
        return false;
    }
    *message = doc.object();
    return true;
}

QString messageType(const QJsonObject &message)
{
    return message.value(kType).toString();
}

LineSplitter::Result LineSplitter::next(QByteArray *line)
{
    const qsizetype end = m_buffer.indexOf('\n');
    if (end < 0)
        return m_buffer.size() > kMaxMessageBytes ? Result::TooLong : Result::None;
    if (end > kMaxMessageBytes)
        return Result::TooLong;
    *line = m_buffer.left(end);
    m_buffer.remove(0, end + 1);
    return Result::Line;
}

QJsonObject helloMessage(const Hello &hello)
{
    QJsonObject o = typed(msg::kHello);
    o.insert(QStringLiteral("program"), hello.program);
    o.insert(QStringLiteral("version"), hello.version);
    o.insert(QStringLiteral("protocol_version"), hello.protocolVersion);
    return o;
}

bool parseHello(const QJsonObject &message, Hello *hello, QString *error)
{
    Hello h;
    if (!requireString(message, QStringLiteral("program"), &h.program, error)
        || !requireString(message, QStringLiteral("version"), &h.version, error)
        || !requireIntField(message, QStringLiteral("protocol_version"), 1,
                            std::numeric_limits<int>::max(), &h.protocolVersion, error))
        return false;
    *hello = h;
    return true;
}

OverlaySummary summarizeOverlay(const Overlay &overlay)
{
    return {overlay.id, overlay.name, overlay.address, overlay.width, overlay.height,
            int(overlay.sources.size())};
}

QJsonObject overlayListMessage(const QList<OverlaySummary> &overlays)
{
    QJsonArray list;
    for (const OverlaySummary &s : overlays) {
        list.append(QJsonObject{{QStringLiteral("id"), idText(s.id)},
                                {QStringLiteral("name"), s.name},
                                {QStringLiteral("address"), s.address},
                                {QStringLiteral("width"), s.width},
                                {QStringLiteral("height"), s.height},
                                {QStringLiteral("source_count"), s.sourceCount}});
    }
    QJsonObject o = typed(msg::kOverlayList);
    o.insert(QStringLiteral("overlays"), list);
    return o;
}

bool parseOverlayList(const QJsonObject &message, QList<OverlaySummary> *overlays, QString *error)
{
    const QJsonValue v = message.value(QStringLiteral("overlays"));
    if (!v.isArray()) {
        *error = QStringLiteral("Feld overlays fehlt oder ist keine Liste");
        return false;
    }
    QList<OverlaySummary> result;
    const QJsonArray list = v.toArray();
    for (qsizetype i = 0; i < list.size(); ++i) {
        const QString prefix = QStringLiteral("overlays[%1]: ").arg(i);
        if (!list.at(i).isObject()) {
            *error = prefix + QStringLiteral("kein Objekt");
            return false;
        }
        const QJsonObject e = list.at(i).toObject();
        OverlaySummary s;
        QString fieldError;
        if (!requireUuid(e, QStringLiteral("id"), &s.id, &fieldError)
            || !requireString(e, QStringLiteral("name"), &s.name, &fieldError)
            || !requireString(e, QStringLiteral("address"), &s.address, &fieldError)
            || !requireIntField(e, QStringLiteral("width"), kOverlayMinSize, kOverlayMaxWidth,
                                &s.width, &fieldError)
            || !requireIntField(e, QStringLiteral("height"), kOverlayMinSize, kOverlayMaxHeight,
                                &s.height, &fieldError)
            || !requireIntField(e, QStringLiteral("source_count"), 0,
                                std::numeric_limits<int>::max(), &s.sourceCount, &fieldError)) {
            *error = prefix + fieldError;
            return false;
        }
        result.append(s);
    }
    *overlays = result;
    return true;
}

QJsonObject overlayMessage(const char *type, const Overlay &overlay)
{
    QJsonObject o = typed(type);
    o.insert(QStringLiteral("overlay"), overlayToJson(overlay));
    return o;
}

bool parseOverlayMessage(const QJsonObject &message, Overlay *overlay, QString *error)
{
    const QJsonValue v = message.value(QStringLiteral("overlay"));
    if (!v.isObject()) {
        *error = QStringLiteral("Feld overlay fehlt oder ist kein Objekt");
        return false;
    }
    // Gleich streng wie beim Lesen einer Overlay-Datei (docs/06-overlay-format.md).
    QString overlayError;
    if (!overlayFromJson(v.toObject(), overlay, &overlayError)) {
        *error = QStringLiteral("overlay: ") + overlayError;
        return false;
    }
    return true;
}

QJsonObject overlayIdMessage(const char *type, const QUuid &id)
{
    QJsonObject o = typed(type);
    o.insert(QStringLiteral("id"), idText(id));
    return o;
}

bool parseOverlayId(const QJsonObject &message, QUuid *id, QString *error)
{
    return requireUuid(message, QStringLiteral("id"), id, error);
}

QJsonObject statsMessage(const SystemStats &stats)
{
    QJsonObject o = typed(msg::kStats);
    o.insert(QStringLiteral("cpu_percent"), numberOrNull(stats.cpuPercent));
    o.insert(QStringLiteral("ram_used_kib"), numberOrNull(stats.ramUsedKiB));
    o.insert(QStringLiteral("ram_total_kib"), numberOrNull(stats.ramTotalKiB));
    if (stats.lan) {
        o.insert(QStringLiteral("lan"),
                 QJsonObject{{QStringLiteral("interface"), stats.lan->interface},
                             {QStringLiteral("rx_kbit_s"), stats.lan->receivedKbitPerSecond},
                             {QStringLiteral("tx_kbit_s"), stats.lan->sentKbitPerSecond}});
    } else {
        o.insert(QStringLiteral("lan"), QJsonValue::Null);
    }
    return o;
}

bool parseStats(const QJsonObject &message, SystemStats *stats, QString *error)
{
    SystemStats s;
    for (const char *key : {"cpu_percent", "ram_used_kib", "ram_total_kib", "lan"}) {
        if (!message.contains(QLatin1String(key))) {
            *error = QStringLiteral("Feld %1 fehlt").arg(QLatin1String(key));
            return false;
        }
    }
    if (!optionalNumber(message, QStringLiteral("cpu_percent"), &s.cpuPercent, error)
        || !optionalKiB(message, QStringLiteral("ram_used_kib"), &s.ramUsedKiB, error)
        || !optionalKiB(message, QStringLiteral("ram_total_kib"), &s.ramTotalKiB, error))
        return false;
    if (s.cpuPercent && *s.cpuPercent > 100.0) {
        *error = QStringLiteral("Feld cpu_percent muss zwischen 0 und 100 liegen");
        return false;
    }
    const QJsonValue lan = message.value(QStringLiteral("lan"));
    if (lan.isObject()) {
        const QJsonObject l = lan.toObject();
        LanStats ls;
        std::optional<double> rx;
        std::optional<double> tx;
        if (!requireString(l, QStringLiteral("interface"), &ls.interface, error)
            || !optionalNumber(l, QStringLiteral("rx_kbit_s"), &rx, error)
            || !optionalNumber(l, QStringLiteral("tx_kbit_s"), &tx, error))
            return false;
        if (!rx || !tx || ls.interface.isEmpty()) {
            *error = QStringLiteral("lan: interface, rx_kbit_s und tx_kbit_s sind Pflicht");
            return false;
        }
        ls.receivedKbitPerSecond = *rx;
        ls.sentKbitPerSecond = *tx;
        s.lan = ls;
    } else if (!lan.isNull()) {
        *error = QStringLiteral("Feld lan muss ein Objekt oder null sein");
        return false;
    }
    *stats = s;
    return true;
}

QJsonObject connectionsMessage(const ConnectionStates &states)
{
    QJsonObject o = typed(msg::kConnections);
    for (Connection c : kAllConnections)
        o.insert(connectionKey(c), connectionStateKey(states.value(c, ConnectionState::Off)));
    return o;
}

bool parseConnections(const QJsonObject &message, ConnectionStates *states, QString *error)
{
    ConnectionStates result;
    for (Connection c : kAllConnections) {
        QString key;
        ConnectionState state = ConnectionState::Off;
        if (!requireString(message, connectionKey(c), &key, error))
            return false;
        if (!connectionStateFromKey(key, &state)) {
            *error = QStringLiteral("Feld %1: unbekannter Zustand «%2»").arg(connectionKey(c), key);
            return false;
        }
        result.insert(c, state);
    }
    *states = result;
    return true;
}

QJsonObject quitReplyMessage(const QuitReply &reply)
{
    QJsonObject o = typed(msg::kQuitReply);
    o.insert(QStringLiteral("accepted"), reply.accepted);
    o.insert(QStringLiteral("other_windows"), reply.otherWindows);
    return o;
}

bool parseQuitReply(const QJsonObject &message, QuitReply *reply, QString *error)
{
    QuitReply r;
    const QJsonValue accepted = message.value(QStringLiteral("accepted"));
    if (!accepted.isBool()) {
        *error = QStringLiteral("Feld accepted fehlt oder ist nicht true/false");
        return false;
    }
    r.accepted = accepted.toBool();
    if (!requireIntField(message, QStringLiteral("other_windows"), 0,
                         std::numeric_limits<int>::max(), &r.otherWindows, error))
        return false;
    *reply = r;
    return true;
}

QJsonObject errorMessage(const ErrorInfo &error)
{
    QJsonObject o = typed(msg::kError);
    o.insert(QStringLiteral("code"), error.code);
    o.insert(QStringLiteral("message"), error.message);
    o.insert(QStringLiteral("request"),
             error.request.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(error.request));
    return o;
}

bool parseError(const QJsonObject &message, ErrorInfo *error, QString *parseErrorText)
{
    ErrorInfo e;
    if (!requireString(message, QStringLiteral("code"), &e.code, parseErrorText)
        || !requireString(message, QStringLiteral("message"), &e.message, parseErrorText))
        return false;
    const QJsonValue request = message.value(QStringLiteral("request"));
    if (request.isString()) {
        e.request = request.toString();
    } else if (!request.isNull()) {
        *parseErrorText = QStringLiteral("Feld request muss Text oder null sein");
        return false;
    }
    *error = e;
    return true;
}

QJsonObject simpleMessage(const char *type)
{
    return typed(type);
}

} // namespace dss
