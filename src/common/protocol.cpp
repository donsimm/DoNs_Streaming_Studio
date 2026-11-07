#include "protocol.h"

#include "overlayjson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QSet>

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

bool requireBool(const QJsonObject &o, const QString &key, bool *value, QString *error)
{
    const QJsonValue v = o.value(key);
    if (!v.isBool()) {
        *error = QStringLiteral("Feld %1 fehlt oder ist nicht true/false").arg(key);
        return false;
    }
    *value = v.toBool();
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
    if (!hello.dataDir.isEmpty())
        o.insert(QStringLiteral("data_dir"), hello.dataDir);
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
    // data_dir ist freiwillig (nur der Kern schickt es); wenn vorhanden, muss es Text sein.
    if (message.contains(QStringLiteral("data_dir"))
        && !requireString(message, QStringLiteral("data_dir"), &h.dataDir, error))
        return false;
    *hello = h;
    return true;
}

OverlaySummary summarizeOverlay(const Overlay &overlay)
{
    return {overlay.id, overlay.name, overlay.address, overlay.width, overlay.height,
            int(overlay.sources.size()), overlay.output};
}

const OverlaySummary *OverlayListData::summary(const QUuid &id) const
{
    for (const OverlaySummary &s : overlays) {
        if (s.id == id)
            return &s;
    }
    return nullptr;
}

namespace {

QJsonObject summaryToJson(const OverlaySummary &s)
{
    return QJsonObject{{QStringLiteral("id"), idText(s.id)},
                       {QStringLiteral("name"), s.name},
                       {QStringLiteral("address"), s.address},
                       {QStringLiteral("width"), s.width},
                       {QStringLiteral("height"), s.height},
                       {QStringLiteral("source_count"), s.sourceCount},
                       {QStringLiteral("output"), s.output}};
}

bool summaryFromJson(const QJsonValue &value, OverlaySummary *s, QString *error)
{
    if (!value.isObject()) {
        *error = QStringLiteral("kein Objekt");
        return false;
    }
    const QJsonObject e = value.toObject();
    return requireUuid(e, QStringLiteral("id"), &s->id, error)
           && requireString(e, QStringLiteral("name"), &s->name, error)
           && requireString(e, QStringLiteral("address"), &s->address, error)
           && requireIntField(e, QStringLiteral("width"), kOverlayMinSize, kOverlayMaxWidth,
                              &s->width, error)
           && requireIntField(e, QStringLiteral("height"), kOverlayMinSize, kOverlayMaxHeight,
                              &s->height, error)
           && requireIntField(e, QStringLiteral("source_count"), 0,
                              std::numeric_limits<int>::max(), &s->sourceCount, error)
           && requireBool(e, QStringLiteral("output"), &s->output, error);
}

} // namespace

QJsonObject overlayListMessage(const OverlayLayout &layout, const QList<OverlaySummary> &summaries,
                               const QString &listError)
{
    QHash<QUuid, OverlaySummary> byId;
    for (const OverlaySummary &s : summaries)
        byId.insert(s.id, s);
    QJsonArray entries;
    for (const ListEntry &e : layout.entries) {
        if (!e.isFolder) {
            if (byId.contains(e.overlayId))
                entries.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("overlay")},
                                           {QStringLiteral("overlay"), summaryToJson(byId.value(e.overlayId))}});
            continue;
        }
        QJsonArray overlays;
        for (const QUuid &id : e.folder.overlays) {
            if (byId.contains(id))
                overlays.append(summaryToJson(byId.value(id)));
        }
        entries.append(QJsonObject{{QStringLiteral("kind"), QStringLiteral("folder")},
                                   {QStringLiteral("id"), idText(e.folder.id)},
                                   {QStringLiteral("name"), e.folder.name},
                                   {QStringLiteral("expanded"), e.folder.expanded},
                                   {QStringLiteral("overlays"), overlays}});
    }
    QJsonObject o = typed(msg::kOverlayList);
    o.insert(QStringLiteral("entries"), entries);
    o.insert(QStringLiteral("list_error"),
             listError.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(listError));
    return o;
}

bool parseOverlayList(const QJsonObject &message, OverlayListData *data, QString *error)
{
    const QJsonValue v = message.value(QStringLiteral("entries"));
    if (!v.isArray()) {
        *error = QStringLiteral("Feld entries fehlt oder ist keine Liste");
        return false;
    }
    OverlayListData result;
    const QJsonValue listError = message.value(QStringLiteral("list_error"));
    if (listError.isString()) {
        result.listError = listError.toString();
    } else if (!listError.isNull()) {
        *error = QStringLiteral("Feld list_error muss Text oder null sein");
        return false;
    }
    QSet<QUuid> seen;
    const auto addSummary = [&](const QJsonValue &value, const QString &prefix, QUuid *id) {
        OverlaySummary s;
        QString fieldError;
        if (!summaryFromJson(value, &s, &fieldError)) {
            *error = prefix + fieldError;
            return false;
        }
        if (seen.contains(s.id)) {
            *error = prefix + QStringLiteral("Overlay kommt mehrfach vor");
            return false;
        }
        seen.insert(s.id);
        result.overlays.append(s);
        *id = s.id;
        return true;
    };
    const QJsonArray list = v.toArray();
    for (qsizetype i = 0; i < list.size(); ++i) {
        const QString prefix = QStringLiteral("entries[%1]: ").arg(i);
        const QJsonObject e = list.at(i).toObject();
        const QString kind = e.value(QStringLiteral("kind")).toString();
        ListEntry entry;
        if (kind == QLatin1String("overlay")) {
            if (!addSummary(e.value(QStringLiteral("overlay")), prefix, &entry.overlayId))
                return false;
        } else if (kind == QLatin1String("folder")) {
            entry.isFolder = true;
            QString fieldError;
            const QJsonValue expanded = e.value(QStringLiteral("expanded"));
            const QJsonValue overlays = e.value(QStringLiteral("overlays"));
            if (!requireUuid(e, QStringLiteral("id"), &entry.folder.id, &fieldError)
                || !requireString(e, QStringLiteral("name"), &entry.folder.name, &fieldError)) {
                *error = prefix + fieldError;
                return false;
            }
            if (!expanded.isBool() || !overlays.isArray()) {
                *error = prefix + QStringLiteral("Feld expanded (true/false) oder overlays (Liste) fehlt");
                return false;
            }
            entry.folder.expanded = expanded.toBool();
            const QJsonArray children = overlays.toArray();
            for (qsizetype j = 0; j < children.size(); ++j) {
                QUuid id;
                if (!addSummary(children.at(j), prefix + QStringLiteral("overlays[%1]: ").arg(j), &id))
                    return false;
                entry.folder.overlays.append(id);
            }
        } else {
            *error = prefix + QStringLiteral("Feld kind muss overlay oder folder sein");
            return false;
        }
        result.layout.entries.append(entry);
    }
    *data = result;
    return true;
}

QJsonObject overlayMessage(const char *type, const Overlay &overlay, const MediaSizes &sizes)
{
    QJsonObject o = typed(type);
    o.insert(QStringLiteral("overlay"), overlayToJson(overlay));
    QJsonObject sizeObject;
    for (auto it = sizes.constBegin(); it != sizes.constEnd(); ++it) {
        sizeObject.insert(it.key(), QJsonObject{{QStringLiteral("width"), it->width()},
                                                {QStringLiteral("height"), it->height()}});
    }
    o.insert(QStringLiteral("media_sizes"), sizeObject);
    return o;
}

bool parseOverlayMessage(const QJsonObject &message, Overlay *overlay, MediaSizes *sizes,
                         QString *error)
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
    const QJsonValue sizeValue = message.value(QStringLiteral("media_sizes"));
    if (!sizeValue.isObject()) {
        *error = QStringLiteral("Feld media_sizes fehlt oder ist kein Objekt");
        return false;
    }
    MediaSizes result;
    const QJsonObject sizeObject = sizeValue.toObject();
    for (auto it = sizeObject.constBegin(); it != sizeObject.constEnd(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const QJsonValue width = entry.value(QStringLiteral("width"));
        const QJsonValue height = entry.value(QStringLiteral("height"));
        const auto isSize = [](const QJsonValue &value) {
            return value.isDouble() && value.toDouble() == std::floor(value.toDouble())
                   && value.toDouble() >= kSourceMinSize && value.toDouble() <= 1e9;
        };
        if (!it.value().isObject() || entry.size() != 2 || !isSize(width) || !isSize(height)) {
            *error = QStringLiteral("media_sizes: Eintrag «%1» ist keine Grösse {width, height} "
                                    "in ganzen Pixeln ab %2")
                         .arg(it.key())
                         .arg(kSourceMinSize);
            return false;
        }
        result.insert(it.key(), QSize(width.toInt(), height.toInt()));
    }
    *sizes = result;
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

QJsonObject fontListMessage(const QList<FontFace> &faces, const QString &fontError)
{
    QJsonArray list;
    for (const FontFace &f : faces) {
        list.append(QJsonObject{{QStringLiteral("family"), f.family},
                                {QStringLiteral("style"), f.style},
                                {QStringLiteral("weight"), f.weight},
                                {QStringLiteral("italic"), f.italic},
                                {QStringLiteral("file"), f.file},
                                {QStringLiteral("origin"), fontOriginName(f.origin)}});
    }
    QJsonObject o = typed(msg::kFontList);
    o.insert(QStringLiteral("faces"), list);
    o.insert(QStringLiteral("error"),
             fontError.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(fontError));
    return o;
}

bool parseFontList(const QJsonObject &message, QList<FontFace> *faces, QString *fontError,
                   QString *error)
{
    const QJsonValue v = message.value(QStringLiteral("faces"));
    if (!v.isArray()) {
        *error = QStringLiteral("Feld faces fehlt oder ist keine Liste");
        return false;
    }
    const QJsonValue e = message.value(QStringLiteral("error"));
    if (!e.isNull() && !e.isString()) {
        *error = QStringLiteral("Feld error muss Text oder null sein");
        return false;
    }
    QList<FontFace> result;
    const QJsonArray list = v.toArray();
    for (qsizetype i = 0; i < list.size(); ++i) {
        const QJsonObject o = list.at(i).toObject();
        FontFace f;
        QString origin;
        QString fieldError;
        if (!requireString(o, QStringLiteral("family"), &f.family, &fieldError)
            || !requireString(o, QStringLiteral("style"), &f.style, &fieldError)
            || !requireIntField(o, QStringLiteral("weight"), kFontWeightMin, kFontWeightMax,
                                &f.weight, &fieldError)
            || !requireString(o, QStringLiteral("file"), &f.file, &fieldError)
            || !requireString(o, QStringLiteral("origin"), &origin, &fieldError)) {
            *error = QStringLiteral("faces[%1]: %2").arg(i).arg(fieldError);
            return false;
        }
        if (!o.value(QStringLiteral("italic")).isBool() || !fontOriginFromName(origin, &f.origin)) {
            *error = QStringLiteral("faces[%1]: italic (true/false) oder origin ungültig").arg(i);
            return false;
        }
        f.italic = o.value(QStringLiteral("italic")).toBool();
        result.append(f);
    }
    *faces = result;
    *fontError = e.toString();
    return true;
}

QJsonObject sourceChangesToJson(const SourceChanges &c)
{
    QJsonObject o;
    if (c.name)
        o.insert(QStringLiteral("name"), *c.name);
    if (c.x)
        o.insert(QStringLiteral("x"), *c.x);
    if (c.y)
        o.insert(QStringLiteral("y"), *c.y);
    if (c.width)
        o.insert(QStringLiteral("width"), *c.width);
    if (c.height)
        o.insert(QStringLiteral("height"), *c.height);
    if (c.rotation)
        o.insert(QStringLiteral("rotation"), *c.rotation);
    if (c.flipHorizontal)
        o.insert(QStringLiteral("flip_horizontal"), *c.flipHorizontal);
    if (c.flipVertical)
        o.insert(QStringLiteral("flip_vertical"), *c.flipVertical);
    if (c.visible)
        o.insert(QStringLiteral("visible"), *c.visible);
    QJsonObject v;
    if (c.loop)
        v.insert(QStringLiteral("loop"), *c.loop);
    if (c.holdAtEnd)
        v.insert(QStringLiteral("hold_at_end"), *c.holdAtEnd);
    if (!v.isEmpty())
        o.insert(QStringLiteral("video"), v);
    QJsonObject t;
    if (c.content)
        t.insert(QStringLiteral("content"), *c.content);
    if (c.fontSize)
        t.insert(QStringLiteral("font_size"), *c.fontSize);
    if (c.color)
        t.insert(QStringLiteral("color"), *c.color);
    if (c.align)
        t.insert(QStringLiteral("align"), textAlignName(*c.align));
    if (!t.isEmpty())
        o.insert(QStringLiteral("text"), t);
    return o;
}

namespace {

// Feld einer Änderung: nur ganze Zahlen im Bereich von int.
bool optionalIntChange(const QJsonObject &o, const QString &key, std::optional<int> *value,
                       QString *error)
{
    if (!o.contains(key))
        return true;
    int v = 0;
    if (!requireIntField(o, key, std::numeric_limits<int>::min(), std::numeric_limits<int>::max(),
                         &v, error))
        return false;
    *value = v;
    return true;
}

bool optionalStringChange(const QJsonObject &o, const QString &key, std::optional<QString> *value,
                          QString *error)
{
    if (!o.contains(key))
        return true;
    QString v;
    if (!requireString(o, key, &v, error))
        return false;
    *value = v;
    return true;
}

bool optionalBoolChange(const QJsonObject &o, const QString &key, std::optional<bool> *value,
                        QString *error)
{
    if (!o.contains(key))
        return true;
    if (!o.value(key).isBool()) {
        *error = QStringLiteral("Feld %1 muss true oder false sein").arg(key);
        return false;
    }
    *value = o.value(key).toBool();
    return true;
}

bool onlyKnownKeys(const QJsonObject &o, const QStringList &keys, const QString &path,
                   QString *error)
{
    for (auto it = o.begin(); it != o.end(); ++it) {
        if (!keys.contains(it.key())) {
            *error = QStringLiteral("Feld %1%2 ist unbekannt").arg(path, it.key());
            return false;
        }
    }
    return true;
}

} // namespace

bool sourceChangesFromJson(const QJsonObject &o, SourceChanges *changes, QString *error)
{
    // Streng: Ein unbekanntes Feld ist ein Fehler, damit kein Tippfehler still verloren geht.
    SourceChanges c;
    if (!onlyKnownKeys(o, {QStringLiteral("name"), QStringLiteral("x"), QStringLiteral("y"),
                           QStringLiteral("width"), QStringLiteral("height"),
                           QStringLiteral("rotation"), QStringLiteral("flip_horizontal"),
                           QStringLiteral("flip_vertical"), QStringLiteral("visible"),
                           QStringLiteral("video"), QStringLiteral("text")},
                       QStringLiteral("changes."), error)
        || !optionalStringChange(o, QStringLiteral("name"), &c.name, error)
        || !optionalIntChange(o, QStringLiteral("x"), &c.x, error)
        || !optionalIntChange(o, QStringLiteral("y"), &c.y, error)
        || !optionalIntChange(o, QStringLiteral("width"), &c.width, error)
        || !optionalIntChange(o, QStringLiteral("height"), &c.height, error)
        || !optionalIntChange(o, QStringLiteral("rotation"), &c.rotation, error)
        || !optionalBoolChange(o, QStringLiteral("flip_horizontal"), &c.flipHorizontal, error)
        || !optionalBoolChange(o, QStringLiteral("flip_vertical"), &c.flipVertical, error)
        || !optionalBoolChange(o, QStringLiteral("visible"), &c.visible, error))
        return false;
    if (o.contains(QStringLiteral("video"))) {
        if (!o.value(QStringLiteral("video")).isObject()) {
            *error = QStringLiteral("Feld changes.video muss ein Objekt sein");
            return false;
        }
        const QJsonObject v = o.value(QStringLiteral("video")).toObject();
        if (!onlyKnownKeys(v, {QStringLiteral("loop"), QStringLiteral("hold_at_end")},
                           QStringLiteral("changes.video."), error)
            || !optionalBoolChange(v, QStringLiteral("loop"), &c.loop, error)
            || !optionalBoolChange(v, QStringLiteral("hold_at_end"), &c.holdAtEnd, error))
            return false;
    }
    if (o.contains(QStringLiteral("text"))) {
        const QJsonObject t = o.value(QStringLiteral("text")).toObject();
        std::optional<QString> align;
        if (!o.value(QStringLiteral("text")).isObject()) {
            *error = QStringLiteral("Feld changes.text muss ein Objekt sein");
            return false;
        }
        if (!onlyKnownKeys(t, {QStringLiteral("content"), QStringLiteral("font_size"),
                               QStringLiteral("color"), QStringLiteral("align")},
                           QStringLiteral("changes.text."), error)
            || !optionalStringChange(t, QStringLiteral("content"), &c.content, error)
            || !optionalIntChange(t, QStringLiteral("font_size"), &c.fontSize, error)
            || !optionalStringChange(t, QStringLiteral("color"), &c.color, error)
            || !optionalStringChange(t, QStringLiteral("align"), &align, error))
            return false;
        if (align) {
            TextAlign a = TextAlign::Left;
            if (!textAlignFromName(*align, &a)) {
                *error = QStringLiteral("Feld changes.text.align muss left, center oder right sein");
                return false;
            }
            c.align = a;
        }
    }
    *changes = c;
    return true;
}

namespace {

// Felder je Typ einer Änderung.
enum ChangeField : unsigned {
    FOverlay = 1u << 0,
    FSource = 1u << 1,
    FFolder = 1u << 2,       // folder_id, Pflicht
    FTargetFolder = 1u << 3, // folder_id, UUID oder null
    FName = 1u << 4,
    FFile = 1u << 5,
    FSize = 1u << 6,         // width, height
    FIndex = 1u << 7,
    FChangeAddress = 1u << 8,
    FExpanded = 1u << 9,
    FFontStyle = 1u << 10,   // weight, italic (je Zahl/true/false oder null)
    FChanges = 1u << 11,
    FOutput = 1u << 12,      // output (true/false), P10
    FParent = 1u << 13,      // parent_id, UUID oder null (P10b)
    FImport = 1u << 14,      // import_id (P10b)
    FPlacement = 1u << 15,   // center_x, center_y, insert_index, je Zahl oder null/fehlt (P10b)
};

const QList<std::pair<const char *, unsigned>> &changeFieldTable()
{
    static const QList<std::pair<const char *, unsigned>> table = {
        {msg::kCreateOverlay, FName | FSize},
        {msg::kRenameOverlay, FOverlay | FName | FChangeAddress},
        {msg::kCopyOverlay, FOverlay},
        {msg::kRemoveOverlay, FOverlay},
        {msg::kMoveOverlay, FOverlay | FTargetFolder | FIndex},
        {msg::kCreateFolder, FName},
        {msg::kRenameFolder, FFolder | FName},
        {msg::kRemoveFolder, FFolder},
        {msg::kMoveFolder, FFolder | FIndex},
        {msg::kSetFolderExpanded, FFolder | FExpanded},
        {msg::kAddImageSource, FOverlay | FFile | FPlacement},
        {msg::kAddVideoSource, FOverlay | FFile | FPlacement},
        {msg::kAddTextSource, FOverlay},
        {msg::kRemoveSource, FOverlay | FSource},
        {msg::kMoveSource, FOverlay | FSource | FIndex},
        {msg::kUpdateSource, FOverlay | FSource | FChanges},
        {msg::kSetTextFont, FOverlay | FSource | FFile | FFontStyle},
        {msg::kReplaceImageFile, FOverlay | FSource | FFile},
        {msg::kSetOverlaySize, FOverlay | FSize},
        {msg::kSetOverlayOutput, FOverlay | FOutput},
        {msg::kNewToken, FOverlay},
        {msg::kAddMedia, FFile | FTargetFolder},
        {msg::kRenameMedia, FFile | FName},
        {msg::kMoveMedia, FFile | FTargetFolder},
        {msg::kRemoveMedia, FFile},
        {msg::kCreateMediaFolder, FName | FParent},
        {msg::kRenameMediaFolder, FFolder | FName},
        {msg::kRemoveMediaFolder, FFolder},
        {msg::kMoveMediaFolder, FFolder | FParent},
        {msg::kSetMediaFolderExpanded, FFolder | FExpanded},
        {msg::kCancelImport, FImport},
    };
    return table;
}

unsigned changeFields(const QString &type)
{
    for (const auto &[name, fields] : changeFieldTable()) {
        if (type == QLatin1String(name))
            return fields;
    }
    return 0;
}

} // namespace

QStringList changeTypes()
{
    QStringList types;
    for (const auto &entry : changeFieldTable())
        types.append(QString::fromLatin1(entry.first));
    return types;
}

bool isChangeType(const QString &type)
{
    return changeFields(type) != 0;
}

QJsonObject changeRequestMessage(const ChangeRequest &r)
{
    const unsigned f = changeFields(r.type);
    QJsonObject o{{kType, r.type}, {QStringLiteral("request_id"), double(r.requestId)}};
    if (f & FOverlay)
        o.insert(QStringLiteral("overlay_id"), idText(r.overlayId));
    if (f & FSource)
        o.insert(QStringLiteral("source_id"), idText(r.sourceId));
    if (f & FFolder)
        o.insert(QStringLiteral("folder_id"), idText(r.folderId));
    if (f & FTargetFolder)
        o.insert(QStringLiteral("folder_id"), r.folderId.isNull() ? QJsonValue(QJsonValue::Null)
                                                                  : QJsonValue(idText(r.folderId)));
    if (f & FName)
        o.insert(QStringLiteral("name"), r.name);
    if (f & FFile)
        o.insert(QStringLiteral("file"), r.file);
    if (f & FSize) {
        o.insert(QStringLiteral("width"), r.width);
        o.insert(QStringLiteral("height"), r.height);
    }
    if (f & FIndex)
        o.insert(QStringLiteral("index"), r.index);
    if (f & FChangeAddress)
        o.insert(QStringLiteral("change_address"), r.changeAddress);
    if (f & FExpanded)
        o.insert(QStringLiteral("expanded"), r.expanded);
    if (f & FOutput)
        o.insert(QStringLiteral("output"), r.output);
    if (f & FFontStyle) {
        o.insert(QStringLiteral("weight"), r.weight ? QJsonValue(*r.weight) : QJsonValue(QJsonValue::Null));
        o.insert(QStringLiteral("italic"), r.italic ? QJsonValue(*r.italic) : QJsonValue(QJsonValue::Null));
    }
    if (f & FChanges)
        o.insert(QStringLiteral("changes"), sourceChangesToJson(r.changes));
    if (f & FParent)
        o.insert(QStringLiteral("parent_id"), r.parentId.isNull() ? QJsonValue(QJsonValue::Null)
                                                                  : QJsonValue(idText(r.parentId)));
    if (f & FImport)
        o.insert(QStringLiteral("import_id"), double(r.importId));
    if (f & FPlacement) {
        const auto value = [](const std::optional<int> &v) {
            return v ? QJsonValue(*v) : QJsonValue(QJsonValue::Null);
        };
        o.insert(QStringLiteral("center_x"), value(r.centerX));
        o.insert(QStringLiteral("center_y"), value(r.centerY));
        o.insert(QStringLiteral("insert_index"), value(r.insertIndex));
    }
    return o;
}

qint64 messageRequestId(const QJsonObject &message)
{
    qint64 id = -1;
    QString ignored;
    if (!requireInt(message, QStringLiteral("request_id"), 0, qint64(1) << 53, &id, &ignored))
        return -1;
    return id;
}

bool parseChangeRequest(const QJsonObject &message, ChangeRequest *request, QString *error)
{
    ChangeRequest r;
    r.type = messageType(message);
    const unsigned f = changeFields(r.type);
    if (f == 0) {
        *error = QStringLiteral("«%1» ist keine Änderung").arg(r.type);
        return false;
    }
    if (!requireInt(message, QStringLiteral("request_id"), 0, qint64(1) << 53, &r.requestId, error))
        return false;
    if (((f & FOverlay) && !requireUuid(message, QStringLiteral("overlay_id"), &r.overlayId, error))
        || ((f & FSource) && !requireUuid(message, QStringLiteral("source_id"), &r.sourceId, error))
        || ((f & FFolder) && !requireUuid(message, QStringLiteral("folder_id"), &r.folderId, error))
        || ((f & FName) && !requireString(message, QStringLiteral("name"), &r.name, error))
        || ((f & FFile) && !requireString(message, QStringLiteral("file"), &r.file, error))
        || ((f & FSize) && (!requireIntField(message, QStringLiteral("width"), std::numeric_limits<int>::min(),
                                             std::numeric_limits<int>::max(), &r.width, error)
                            || !requireIntField(message, QStringLiteral("height"), std::numeric_limits<int>::min(),
                                                std::numeric_limits<int>::max(), &r.height, error)))
        || ((f & FIndex) && !requireIntField(message, QStringLiteral("index"), 0,
                                             std::numeric_limits<int>::max(), &r.index, error)))
        return false;
    if (f & FTargetFolder) {
        const QJsonValue v = message.value(QStringLiteral("folder_id"));
        if (!v.isNull() && !requireUuid(message, QStringLiteral("folder_id"), &r.folderId, error))
            return false;
    }
    for (const auto &[flag, key, target] :
         {std::tuple{FChangeAddress, "change_address", &r.changeAddress},
          std::tuple{FExpanded, "expanded", &r.expanded},
          std::tuple{FOutput, "output", &r.output}}) {
        if (!(f & flag))
            continue;
        const QJsonValue v = message.value(QLatin1String(key));
        if (!v.isBool()) {
            *error = QStringLiteral("Feld %1 fehlt oder ist nicht true/false").arg(QLatin1String(key));
            return false;
        }
        *target = v.toBool();
    }
    if (f & FFontStyle) {
        const QJsonValue w = message.value(QStringLiteral("weight"));
        const QJsonValue i = message.value(QStringLiteral("italic"));
        if (!w.isNull()) {
            int weight = 0;
            if (!requireIntField(message, QStringLiteral("weight"), kFontWeightMin, kFontWeightMax,
                                 &weight, error))
                return false;
            r.weight = weight;
        }
        if (i.isBool()) {
            r.italic = i.toBool();
        } else if (!i.isNull()) {
            *error = QStringLiteral("Feld italic muss true, false oder null sein");
            return false;
        }
    }
    if (f & FParent) {
        const QJsonValue v = message.value(QStringLiteral("parent_id"));
        if (!v.isNull() && !requireUuid(message, QStringLiteral("parent_id"), &r.parentId, error))
            return false;
    }
    if ((f & FImport)
        && !requireInt(message, QStringLiteral("import_id"), 1, qint64(1) << 53, &r.importId, error))
        return false;
    if (f & FPlacement) {
        // Freiwillig (P10b): fehlt oder null = wie bisher (Lage 0/0, zuoberst).
        for (const auto &[key, target, min] :
             {std::tuple{"center_x", &r.centerX, std::numeric_limits<int>::min()},
              std::tuple{"center_y", &r.centerY, std::numeric_limits<int>::min()},
              std::tuple{"insert_index", &r.insertIndex, 0}}) {
            const QJsonValue v = message.value(QLatin1String(key));
            if (v.isNull() || v.isUndefined())
                continue;
            int value = 0;
            if (!requireIntField(message, QLatin1String(key), min, std::numeric_limits<int>::max(),
                                 &value, error))
                return false;
            *target = value;
        }
        if (r.centerX.has_value() != r.centerY.has_value()) {
            *error = QStringLiteral("Felder center_x und center_y gehören zusammen");
            return false;
        }
    }
    if (f & FChanges) {
        const QJsonValue v = message.value(QStringLiteral("changes"));
        if (!v.isObject()) {
            *error = QStringLiteral("Feld changes fehlt oder ist kein Objekt");
            return false;
        }
        if (!sourceChangesFromJson(v.toObject(), &r.changes, error))
            return false;
    }
    *request = r;
    return true;
}

QJsonObject changeResultMessage(const ChangeResult &result)
{
    QJsonObject o = typed(msg::kChangeResult);
    o.insert(QStringLiteral("request_id"), double(result.requestId));
    o.insert(QStringLiteral("ok"), result.ok);
    o.insert(QStringLiteral("id"),
             result.id.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(idText(result.id)));
    o.insert(QStringLiteral("code"),
             result.ok ? QJsonValue(QJsonValue::Null) : QJsonValue(result.code));
    o.insert(QStringLiteral("message"),
             result.ok ? QJsonValue(QJsonValue::Null) : QJsonValue(result.message));
    o.insert(QStringLiteral("warning"), result.ok && !result.warning.isEmpty()
                                            ? QJsonValue(result.warning)
                                            : QJsonValue(QJsonValue::Null));
    return o;
}

bool parseChangeResult(const QJsonObject &message, ChangeResult *result, QString *error)
{
    ChangeResult r;
    if (!requireInt(message, QStringLiteral("request_id"), 0, qint64(1) << 53, &r.requestId, error))
        return false;
    const QJsonValue ok = message.value(QStringLiteral("ok"));
    if (!ok.isBool()) {
        *error = QStringLiteral("Feld ok fehlt oder ist nicht true/false");
        return false;
    }
    r.ok = ok.toBool();
    if (!message.value(QStringLiteral("id")).isNull()
        && !requireUuid(message, QStringLiteral("id"), &r.id, error))
        return false;
    if (!r.ok && (!requireString(message, QStringLiteral("code"), &r.code, error)
                  || !requireString(message, QStringLiteral("message"), &r.message, error)))
        return false;
    // warning (P09) ist freiwillig: Text oder null.
    const QJsonValue warning = message.value(QStringLiteral("warning"));
    if (warning.isString()) {
        r.warning = warning.toString();
    } else if (!warning.isNull() && !warning.isUndefined()) {
        *error = QStringLiteral("Feld warning muss Text oder null sein");
        return false;
    }
    *result = r;
    return true;
}

QJsonObject settingsMessage(const SettingsInfo &info)
{
    QJsonObject o = typed(msg::kSettings);
    o.insert(QStringLiteral("port"), info.settings.port);
    o.insert(QStringLiteral("home_address"), info.settings.homeAddress);
    o.insert(QStringLiteral("port_min"), info.portMin);
    o.insert(QStringLiteral("port_max"), info.portMax);
    o.insert(QStringLiteral("file"), info.file);
    o.insert(QStringLiteral("settings_error"),
             info.error.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(info.error));
    // P10c
    o.insert(QStringLiteral("obs_enabled"), info.settings.obsEnabled);
    o.insert(QStringLiteral("obs_host"), info.settings.obsHost);
    o.insert(QStringLiteral("obs_port"), info.settings.obsPort);
    o.insert(QStringLiteral("obs_password_set"), info.obsPasswordSet);
    return o;
}

bool parseSettings(const QJsonObject &message, SettingsInfo *info, QString *error)
{
    SettingsInfo i;
    const int maxPort = 65535;
    if (!requireIntField(message, QStringLiteral("port"), 0, maxPort, &i.settings.port, error)
        || !requireString(message, QStringLiteral("home_address"), &i.settings.homeAddress, error)
        || !requireIntField(message, QStringLiteral("port_min"), 0, maxPort, &i.portMin, error)
        || !requireIntField(message, QStringLiteral("port_max"), 0, maxPort, &i.portMax, error)
        || !requireString(message, QStringLiteral("file"), &i.file, error)
        || !requireBool(message, QStringLiteral("obs_enabled"), &i.settings.obsEnabled, error)
        || !requireString(message, QStringLiteral("obs_host"), &i.settings.obsHost, error)
        || !requireIntField(message, QStringLiteral("obs_port"), 0, maxPort, &i.settings.obsPort, error)
        || !requireBool(message, QStringLiteral("obs_password_set"), &i.obsPasswordSet, error))
        return false;
    const QJsonValue e = message.value(QStringLiteral("settings_error"));
    if (e.isString()) {
        i.error = e.toString();
    } else if (!e.isNull()) {
        *error = QStringLiteral("Feld settings_error fehlt oder ist weder Text noch null");
        return false;
    }
    if (i.portMin > i.portMax) {
        *error = QStringLiteral("port_min grösser als port_max");
        return false;
    }
    *info = i;
    return true;
}

QJsonObject settingsUpdateMessage(const SettingsUpdate &update)
{
    QJsonObject o = typed(msg::kUpdateSettings);
    o.insert(QStringLiteral("request_id"), double(update.requestId));
    if (update.port)
        o.insert(QStringLiteral("port"), *update.port);
    if (update.homeAddress)
        o.insert(QStringLiteral("home_address"), *update.homeAddress);
    if (update.obsEnabled)
        o.insert(QStringLiteral("obs_enabled"), *update.obsEnabled);
    if (update.obsHost)
        o.insert(QStringLiteral("obs_host"), *update.obsHost);
    if (update.obsPort)
        o.insert(QStringLiteral("obs_port"), *update.obsPort);
    if (update.obsPassword)
        o.insert(QStringLiteral("obs_password"), *update.obsPassword);
    return o;
}

QJsonObject resetSettingsMessage(qint64 requestId)
{
    QJsonObject o = typed(msg::kResetSettings);
    o.insert(QStringLiteral("request_id"), double(requestId));
    return o;
}

bool parseResetSettings(const QJsonObject &message, qint64 *requestId, QString *error)
{
    return requireInt(message, QStringLiteral("request_id"), 0, qint64(1) << 53, requestId, error);
}

bool parseSettingsUpdate(const QJsonObject &message, SettingsUpdate *update, QString *error)
{
    SettingsUpdate u;
    if (!requireInt(message, QStringLiteral("request_id"), 0, qint64(1) << 53, &u.requestId, error))
        return false;
    if (message.contains(QStringLiteral("port"))) {
        int port = 0;
        // Bereich prüft der Kern mit eigener Meldung (validatePort); hier nur «ganze Zahl».
        if (!requireIntField(message, QStringLiteral("port"), std::numeric_limits<int>::min(),
                             std::numeric_limits<int>::max(), &port, error))
            return false;
        u.port = port;
    }
    if (message.contains(QStringLiteral("home_address"))) {
        QString address;
        if (!requireString(message, QStringLiteral("home_address"), &address, error))
            return false;
        u.homeAddress = address;
    }
    if (message.contains(QStringLiteral("obs_enabled"))) {
        bool enabled = false;
        if (!requireBool(message, QStringLiteral("obs_enabled"), &enabled, error))
            return false;
        u.obsEnabled = enabled;
    }
    if (message.contains(QStringLiteral("obs_host"))) {
        QString host;
        if (!requireString(message, QStringLiteral("obs_host"), &host, error))
            return false;
        u.obsHost = host;
    }
    if (message.contains(QStringLiteral("obs_port"))) {
        int port = 0;
        if (!requireIntField(message, QStringLiteral("obs_port"), std::numeric_limits<int>::min(),
                             std::numeric_limits<int>::max(), &port, error))
            return false;
        u.obsPort = port;
    }
    if (message.contains(QStringLiteral("obs_password"))) {
        QString password;
        if (!requireString(message, QStringLiteral("obs_password"), &password, error))
            return false;
        u.obsPassword = password;
    }
    if (!u.port && !u.homeAddress && !u.obsEnabled && !u.obsHost && !u.obsPort && !u.obsPassword) {
        *error = QStringLiteral("update_settings ohne Wert");
        return false;
    }
    *update = u;
    return true;
}

QJsonObject obsStatusMessage(const ObsStatusInfo &info)
{
    const auto sceneObject = [](const ObsSceneInfo &s) {
        return QJsonObject{{QStringLiteral("uuid"), s.uuid}, {QStringLiteral("name"), s.name}};
    };
    const auto optional = [](const QString &text) {
        return text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text);
    };
    QJsonObject o = typed(msg::kObsStatus);
    o.insert(QStringLiteral("message"), optional(info.message));
    o.insert(QStringLiteral("obs_version"), optional(info.obsVersion));
    o.insert(QStringLiteral("websocket_version"), optional(info.websocketVersion));
    o.insert(QStringLiteral("current_scene"), info.currentScene.name.isEmpty()
                                                  ? QJsonValue(QJsonValue::Null)
                                                  : QJsonValue(sceneObject(info.currentScene)));
    QJsonArray scenes;
    for (const ObsSceneInfo &s : info.scenes)
        scenes.append(sceneObject(s));
    o.insert(QStringLiteral("scenes"), scenes);
    return o;
}

bool parseObsStatus(const QJsonObject &message, ObsStatusInfo *info, QString *error)
{
    ObsStatusInfo i;
    for (const auto &[key, value] : {std::pair{QStringLiteral("message"), &i.message},
                                     std::pair{QStringLiteral("obs_version"), &i.obsVersion},
                                     std::pair{QStringLiteral("websocket_version"), &i.websocketVersion}}) {
        const QJsonValue v = message.value(key);
        if (v.isString()) {
            *value = v.toString();
        } else if (!v.isNull()) {
            *error = QStringLiteral("Feld %1 fehlt oder ist weder Text noch null").arg(key);
            return false;
        }
    }
    const auto parseScene = [error](const QJsonObject &o, ObsSceneInfo *scene) {
        return requireString(o, QStringLiteral("uuid"), &scene->uuid, error)
               && requireString(o, QStringLiteral("name"), &scene->name, error);
    };
    const QJsonValue current = message.value(QStringLiteral("current_scene"));
    if (current.isObject()) {
        if (!parseScene(current.toObject(), &i.currentScene))
            return false;
    } else if (!current.isNull()) {
        *error = QStringLiteral("Feld current_scene fehlt oder ist weder Objekt noch null");
        return false;
    }
    if (!message.value(QStringLiteral("scenes")).isArray()) {
        *error = QStringLiteral("Feld scenes fehlt oder ist keine Liste");
        return false;
    }
    const QJsonArray scenes = message.value(QStringLiteral("scenes")).toArray();
    for (const QJsonValue &v : scenes) {
        ObsSceneInfo scene;
        if (!parseScene(v.toObject(), &scene))
            return false;
        i.scenes.append(scene);
    }
    *info = i;
    return true;
}

QJsonObject obsSourcesMessage(const QList<ObsSourceInfo> &sources)
{
    QJsonArray list;
    for (const ObsSourceInfo &s : sources) {
        const char *state = s.state == ObsSourceState::Present   ? "present"
                            : s.state == ObsSourceState::Missing ? "missing"
                                                                 : "none";
        list.append(QJsonObject{{QStringLiteral("overlay_id"), idText(s.overlayId)},
                                {QStringLiteral("state"), QString::fromLatin1(state)},
                                {QStringLiteral("input_name"), s.inputName}});
    }
    QJsonObject o = typed(msg::kObsSources);
    o.insert(QStringLiteral("sources"), list);
    return o;
}

bool parseObsSources(const QJsonObject &message, QList<ObsSourceInfo> *sources, QString *error)
{
    if (!message.value(QStringLiteral("sources")).isArray()) {
        *error = QStringLiteral("Feld sources fehlt oder ist keine Liste");
        return false;
    }
    QList<ObsSourceInfo> result;
    const QJsonArray list = message.value(QStringLiteral("sources")).toArray();
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        ObsSourceInfo s;
        QString state;
        if (!requireUuid(o, QStringLiteral("overlay_id"), &s.overlayId, error)
            || !requireString(o, QStringLiteral("state"), &state, error)
            || !requireString(o, QStringLiteral("input_name"), &s.inputName, error))
            return false;
        if (state == QLatin1String("present")) {
            s.state = ObsSourceState::Present;
        } else if (state == QLatin1String("missing")) {
            s.state = ObsSourceState::Missing;
        } else if (state != QLatin1String("none")) {
            *error = QStringLiteral("Feld state unbekannt: %1").arg(state);
            return false;
        }
        result.append(s);
    }
    *sources = result;
    return true;
}

namespace {

QJsonObject obsSourceRequestMessage(const char *type, const ObsSourceRequest &request)
{
    QJsonObject o = typed(type);
    o.insert(QStringLiteral("request_id"), double(request.requestId));
    o.insert(QStringLiteral("overlay_id"), idText(request.overlayId));
    o.insert(QStringLiteral("scene_uuid"), request.sceneUuid);
    return o;
}

bool parseObsSourceRequest(const QJsonObject &message, ObsSourceRequest *request, QString *error)
{
    qint64 id = 0;
    if (!requireInt(message, QStringLiteral("request_id"), 0, qint64(1) << 53, &id, error)
        || !requireUuid(message, QStringLiteral("overlay_id"), &request->overlayId, error)
        || !requireString(message, QStringLiteral("scene_uuid"), &request->sceneUuid, error))
        return false;
    request->requestId = id;
    return true;
}

} // namespace

QJsonObject obsCreateSourceMessage(const ObsSourceRequest &request)
{
    QJsonObject o = obsSourceRequestMessage(msg::kObsCreateSource, request);
    o.insert(QStringLiteral("name"), request.name);
    return o;
}

bool parseObsCreateSource(const QJsonObject &message, ObsSourceRequest *request, QString *error)
{
    ObsSourceRequest r;
    if (!parseObsSourceRequest(message, &r, error) || !requireString(message, QStringLiteral("name"), &r.name, error))
        return false;
    *request = r;
    return true;
}

QJsonObject obsAddToSceneMessage(const ObsSourceRequest &request)
{
    return obsSourceRequestMessage(msg::kObsAddToScene, request);
}

bool parseObsAddToScene(const QJsonObject &message, ObsSourceRequest *request, QString *error)
{
    ObsSourceRequest r;
    if (!parseObsSourceRequest(message, &r, error))
        return false;
    *request = r;
    return true;
}

QJsonObject browserSourceMessage(const BrowserSourceInfo &info)
{
    QJsonObject o = typed(msg::kBrowserSource);
    o.insert(QStringLiteral("overlay_id"), idText(info.overlayId));
    o.insert(QStringLiteral("url"), info.url.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(info.url));
    o.insert(QStringLiteral("problem"),
             info.problem.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(info.problem));
    return o;
}

bool parseBrowserSource(const QJsonObject &message, BrowserSourceInfo *info, QString *error)
{
    BrowserSourceInfo i;
    if (!requireUuid(message, QStringLiteral("overlay_id"), &i.overlayId, error))
        return false;
    for (const auto &[key, value] : {std::pair{QStringLiteral("url"), &i.url},
                                     std::pair{QStringLiteral("problem"), &i.problem}}) {
        const QJsonValue v = message.value(key);
        if (v.isString()) {
            *value = v.toString();
        } else if (!v.isNull()) {
            *error = QStringLiteral("Feld %1 fehlt oder ist weder Text noch null").arg(key);
            return false;
        }
    }
    if (i.url.isEmpty() == i.problem.isEmpty()) {
        *error = QStringLiteral("browser_source braucht genau eines von url und problem");
        return false;
    }
    *info = i;
    return true;
}

const MediaFileData *MediaListData::file(const QString &reference) const
{
    for (const MediaFileData &f : files) {
        if (f.entry.file == reference)
            return &f;
    }
    return nullptr;
}

namespace {

QJsonValue idOrNull(const QUuid &id)
{
    return id.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(idText(id));
}

bool uuidOrNull(const QJsonObject &o, const QString &key, QUuid *id, QString *error)
{
    if (o.value(key).isNull()) {
        *id = QUuid();
        return true;
    }
    return requireUuid(o, key, id, error);
}

QJsonValue textOrNull(const QString &text)
{
    return text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text);
}

bool optionalText(const QJsonObject &o, const QString &key, QString *text, QString *error)
{
    const QJsonValue v = o.value(key);
    if (v.isNull()) {
        text->clear();
        return true;
    }
    return requireString(o, key, text, error);
}

} // namespace

QJsonObject mediaListMessage(const MediaListData &data)
{
    QJsonObject o = typed(msg::kMediaList);
    QJsonArray folders;
    for (const MediaFolder &f : data.folders)
        folders.append(QJsonObject{{QStringLiteral("id"), idText(f.id)},
                                   {QStringLiteral("name"), f.name},
                                   {QStringLiteral("parent"), idOrNull(f.parent)},
                                   {QStringLiteral("expanded"), f.expanded}});
    QJsonArray files;
    for (const MediaFileData &f : data.files) {
        QJsonArray used;
        for (const MediaUsage &u : f.usedIn)
            used.append(QJsonObject{{QStringLiteral("overlay_id"), idText(u.overlayId)},
                                    {QStringLiteral("overlay_name"), u.overlayName},
                                    {QStringLiteral("source_id"), idText(u.sourceId)},
                                    {QStringLiteral("source_name"), u.sourceName}});
        files.append(QJsonObject{{QStringLiteral("file"), f.entry.file},
                                 {QStringLiteral("name"), f.entry.name},
                                 {QStringLiteral("folder"), idOrNull(f.entry.folder)},
                                 {QStringLiteral("kind"), mediaKindKey(f.kind)},
                                 {QStringLiteral("bytes"), double(f.bytes)},
                                 {QStringLiteral("codec"), textOrNull(f.codec)},
                                 {QStringLiteral("browser_problem"), textOrNull(f.browserProblem)},
                                 {QStringLiteral("used_in"), used}});
    }
    o.insert(QStringLiteral("folders"), folders);
    o.insert(QStringLiteral("files"), files);
    o.insert(QStringLiteral("error"), textOrNull(data.error));
    return o;
}

bool parseMediaList(const QJsonObject &message, MediaListData *data, QString *error)
{
    MediaListData d;
    const QJsonValue folders = message.value(QStringLiteral("folders"));
    const QJsonValue files = message.value(QStringLiteral("files"));
    if (!folders.isArray() || !files.isArray()) {
        *error = QStringLiteral("Felder folders und files müssen Listen sein");
        return false;
    }
    for (const QJsonValue &v : folders.toArray()) {
        if (!v.isObject()) {
            *error = QStringLiteral("Eintrag in folders ist kein Objekt");
            return false;
        }
        const QJsonObject f = v.toObject();
        MediaFolder folder;
        if (!requireUuid(f, QStringLiteral("id"), &folder.id, error)
            || !requireString(f, QStringLiteral("name"), &folder.name, error)
            || !uuidOrNull(f, QStringLiteral("parent"), &folder.parent, error)
            || !requireBool(f, QStringLiteral("expanded"), &folder.expanded, error))
            return false;
        d.folders.append(folder);
    }
    for (const QJsonValue &v : files.toArray()) {
        if (!v.isObject()) {
            *error = QStringLiteral("Eintrag in files ist kein Objekt");
            return false;
        }
        const QJsonObject f = v.toObject();
        MediaFileData file;
        QString kind;
        qint64 bytes = 0;
        if (!requireString(f, QStringLiteral("file"), &file.entry.file, error)
            || !requireString(f, QStringLiteral("name"), &file.entry.name, error)
            || !uuidOrNull(f, QStringLiteral("folder"), &file.entry.folder, error)
            || !requireString(f, QStringLiteral("kind"), &kind, error)
            || !requireInt(f, QStringLiteral("bytes"), 0, qint64(1) << 53, &bytes, error)
            || !optionalText(f, QStringLiteral("codec"), &file.codec, error)
            || !optionalText(f, QStringLiteral("browser_problem"), &file.browserProblem, error))
            return false;
        const std::optional<MediaKind> k = mediaKindFromKey(kind);
        if (!k) {
            *error = QStringLiteral("Feld kind: unbekannt «%1»").arg(kind);
            return false;
        }
        file.kind = *k;
        file.bytes = bytes;
        const QJsonValue used = f.value(QStringLiteral("used_in"));
        if (!used.isArray()) {
            *error = QStringLiteral("Feld used_in muss eine Liste sein");
            return false;
        }
        for (const QJsonValue &u : used.toArray()) {
            const QJsonObject uo = u.toObject();
            MediaUsage usage;
            if (!requireUuid(uo, QStringLiteral("overlay_id"), &usage.overlayId, error)
                || !requireString(uo, QStringLiteral("overlay_name"), &usage.overlayName, error)
                || !requireUuid(uo, QStringLiteral("source_id"), &usage.sourceId, error)
                || !requireString(uo, QStringLiteral("source_name"), &usage.sourceName, error))
                return false;
            file.usedIn.append(usage);
        }
        d.files.append(file);
    }
    if (!optionalText(message, QStringLiteral("error"), &d.error, error))
        return false;
    *data = d;
    return true;
}

QJsonObject importProgressMessage(const QList<ImportProgressData> &imports)
{
    QJsonObject o = typed(msg::kImportProgress);
    QJsonArray list;
    for (const ImportProgressData &i : imports)
        list.append(QJsonObject{{QStringLiteral("import_id"), double(i.importId)},
                                {QStringLiteral("name"), i.name},
                                {QStringLiteral("folder"), idOrNull(i.folder)},
                                {QStringLiteral("percent"), i.percent}});
    o.insert(QStringLiteral("imports"), list);
    return o;
}

bool parseImportProgress(const QJsonObject &message, QList<ImportProgressData> *imports,
                         QString *error)
{
    const QJsonValue list = message.value(QStringLiteral("imports"));
    if (!list.isArray()) {
        *error = QStringLiteral("Feld imports muss eine Liste sein");
        return false;
    }
    QList<ImportProgressData> result;
    for (const QJsonValue &v : list.toArray()) {
        const QJsonObject o = v.toObject();
        ImportProgressData i;
        if (!requireInt(o, QStringLiteral("import_id"), 1, qint64(1) << 53, &i.importId, error)
            || !requireString(o, QStringLiteral("name"), &i.name, error)
            || !uuidOrNull(o, QStringLiteral("folder"), &i.folder, error)
            || !requireIntField(o, QStringLiteral("percent"), 0, 100, &i.percent, error))
            return false;
        result.append(i);
    }
    *imports = result;
    return true;
}

QJsonObject getBrowserSourceMessage(const QUuid &overlayId)
{
    QJsonObject o = typed(msg::kGetBrowserSource);
    o.insert(QStringLiteral("overlay_id"),
             overlayId.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(idText(overlayId)));
    return o;
}

bool parseGetBrowserSource(const QJsonObject &message, QUuid *overlayId, QString *error)
{
    QUuid id;
    if (!message.value(QStringLiteral("overlay_id")).isNull()
        && !requireUuid(message, QStringLiteral("overlay_id"), &id, error))
        return false;
    *overlayId = id;
    return true;
}

QJsonObject serverStatusMessage(const ServerStatusInfo &info)
{
    QJsonObject o = typed(msg::kServerStatus);
    o.insert(QStringLiteral("enabled"), info.enabled);
    o.insert(QStringLiteral("listening"), QJsonArray::fromStringList(info.listening));
    o.insert(QStringLiteral("problems"), QJsonArray::fromStringList(info.problems));
    o.insert(QStringLiteral("invalid_use"),
             info.invalidUse.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(info.invalidUse));
    QJsonObject pages;
    for (auto it = info.pages.constBegin(); it != info.pages.constEnd(); ++it)
        pages.insert(idText(it.key()), it.value());
    o.insert(QStringLiteral("pages"), pages);
    return o;
}

bool parseServerStatus(const QJsonObject &message, ServerStatusInfo *info, QString *error)
{
    ServerStatusInfo i;
    if (!requireBool(message, QStringLiteral("enabled"), &i.enabled, error))
        return false;
    for (const auto &[key, list] : {std::pair{QStringLiteral("listening"), &i.listening},
                                    std::pair{QStringLiteral("problems"), &i.problems}}) {
        const QJsonValue v = message.value(key);
        if (!v.isArray()) {
            *error = QStringLiteral("Feld %1 fehlt oder ist keine Liste").arg(key);
            return false;
        }
        for (const QJsonValue &e : v.toArray()) {
            if (!e.isString()) {
                *error = QStringLiteral("Feld %1 enthält etwas anderes als Text").arg(key);
                return false;
            }
            list->append(e.toString());
        }
    }
    const QJsonValue invalid = message.value(QStringLiteral("invalid_use"));
    if (invalid.isString()) {
        i.invalidUse = invalid.toString();
    } else if (!invalid.isNull()) {
        *error = QStringLiteral("Feld invalid_use fehlt oder ist weder Text noch null");
        return false;
    }
    const QJsonValue pages = message.value(QStringLiteral("pages"));
    if (!pages.isObject()) {
        *error = QStringLiteral("Feld pages fehlt oder ist kein Objekt");
        return false;
    }
    const QJsonObject p = pages.toObject();
    for (auto it = p.constBegin(); it != p.constEnd(); ++it) {
        QUuid id;
        int count = 0;
        if (!requireUuid(QJsonObject{{QStringLiteral("id"), it.key()}}, QStringLiteral("id"), &id, error)
            || !requireIntField(p, it.key(), 0, 1 << 20, &count, error))
            return false;
        i.pages.insert(id, count);
    }
    *info = i;
    return true;
}

} // namespace dss
