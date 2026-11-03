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
            int(overlay.sources.size())};
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
                       {QStringLiteral("source_count"), s.sourceCount}};
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
                              std::numeric_limits<int>::max(), &s->sourceCount, error);
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
        {msg::kAddImageSource, FOverlay | FFile},
        {msg::kAddVideoSource, FOverlay | FFile},
        {msg::kAddTextSource, FOverlay},
        {msg::kRemoveSource, FOverlay | FSource},
        {msg::kMoveSource, FOverlay | FSource | FIndex},
        {msg::kUpdateSource, FOverlay | FSource | FChanges},
        {msg::kSetTextFont, FOverlay | FSource | FFile | FFontStyle},
        {msg::kReplaceImageFile, FOverlay | FSource | FFile},
        {msg::kSetOverlaySize, FOverlay | FSize},
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
    if (f & FFontStyle) {
        o.insert(QStringLiteral("weight"), r.weight ? QJsonValue(*r.weight) : QJsonValue(QJsonValue::Null));
        o.insert(QStringLiteral("italic"), r.italic ? QJsonValue(*r.italic) : QJsonValue(QJsonValue::Null));
    }
    if (f & FChanges)
        o.insert(QStringLiteral("changes"), sourceChangesToJson(r.changes));
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
          std::tuple{FExpanded, "expanded", &r.expanded}}) {
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

} // namespace dss
