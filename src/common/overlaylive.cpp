#include "overlaylive.h"

#include <QJsonArray>
#include <QSet>

#include <cmath>

namespace dss {

namespace {

// Felder wie in protocol.cpp: streng geprüft, unbekannte Felder übergangen.
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

bool requireUuidText(const QJsonValue &v, const QString &key, QUuid *id, QString *error)
{
    const QString text = v.toString();
    const QUuid parsed = QUuid::fromString(text);
    if (!v.isString() || parsed.isNull() || parsed.toString(QUuid::WithoutBraces) != text) {
        *error = QStringLiteral("Feld %1 ist keine Kennung (UUID in Kleinbuchstaben ohne Klammern)").arg(key);
        return false;
    }
    *id = parsed;
    return true;
}

// Text oder null (fehlt das Feld, ist die Nachricht ungültig)
bool textOrNull(const QJsonObject &o, const QString &key, std::optional<QString> *value, QString *error)
{
    const QJsonValue v = o.value(key);
    if (v.isNull()) {
        value->reset();
        return true;
    }
    if (!v.isString()) {
        *error = QStringLiteral("Feld %1 fehlt oder ist weder Text noch null").arg(key);
        return false;
    }
    *value = v.toString();
    return true;
}

QString idText(const QUuid &id)
{
    return id.toString(QUuid::WithoutBraces);
}

QJsonValue textValue(const std::optional<QString> &text)
{
    return text ? QJsonValue(*text) : QJsonValue(QJsonValue::Null);
}

QJsonValue nonEmptyOrNull(const QString &text)
{
    return text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text);
}

} // namespace

QString liveModeName(LiveMode mode)
{
    switch (mode) {
    case LiveMode::Show:
        return QStringLiteral("show");
    case LiveMode::Hide:
        return QStringLiteral("hide");
    case LiveMode::Toggle:
        return QStringLiteral("toggle");
    }
    return QString();
}

bool liveModeFromName(const QString &name, LiveMode *mode)
{
    for (LiveMode m : {LiveMode::Show, LiveMode::Hide, LiveMode::Toggle}) {
        if (liveModeName(m) == name) {
            *mode = m;
            return true;
        }
    }
    return false;
}

Overlay applyLiveState(const Overlay &overlay, const LiveElements &elements)
{
    Overlay result = overlay;
    if (elements.isEmpty())
        return result;
    for (Source &s : result.sources) {
        const auto it = elements.constFind(s.id);
        if (it == elements.constEnd())
            continue;
        if (it->visible)
            s.visible = *it->visible;
        if (it->text && s.kind == SourceKind::Text)
            s.text.content = *it->text;
        if (!it->image.isEmpty() && s.kind == SourceKind::Image)
            s.file = it->image;
    }
    return result;
}

QString substitutePlaceholders(const QString &text, const QHash<QString, QString> &arguments,
                               const dss::actions::GlobalLookup &globals)
{
    dss::actions::Arguments values;
    values.reserve(arguments.size());
    for (auto it = arguments.cbegin(); it != arguments.cend(); ++it)
        values.insert(it.key(), it.value());
    return dss::actions::replacePlaceholders(text, values, globals);
}

bool isLiveImageUrl(const QString &image)
{
    return image.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)
           || image.startsWith(QLatin1String("http://"), Qt::CaseInsensitive);
}

QJsonObject liveSetMessage(const LiveSetRequest &request)
{
    QJsonArray elements;
    for (const LiveElementRequest &e : request.elements)
        elements.append(QJsonObject{{QStringLiteral("source"), idText(e.source)},
                                    {QStringLiteral("text"), textValue(e.text)},
                                    {QStringLiteral("image"), nonEmptyOrNull(e.image)}});
    QJsonObject arguments;
    for (auto it = request.arguments.cbegin(); it != request.arguments.cend(); ++it)
        arguments.insert(it.key(), it.value());
    return {{QStringLiteral("type"), QString::fromLatin1(msg::kOverlayLiveSet)},
            {QStringLiteral("request_id"), double(request.requestId)},
            {QStringLiteral("overlay_id"), idText(request.overlayId)},
            {QStringLiteral("mode"), liveModeName(request.mode)},
            {QStringLiteral("elements"), elements},
            {QStringLiteral("duration_ms"), request.durationMs},
            {QStringLiteral("arguments"), arguments}};
}

bool parseLiveSet(const QJsonObject &message, LiveSetRequest *request, QString *error)
{
    LiveSetRequest r;
    qint64 requestId = 0;
    qint64 duration = 0;
    if (!requireInt(message, QStringLiteral("request_id"), 0, qint64(1) << 53, &requestId, error)
        || !requireUuidText(message.value(QStringLiteral("overlay_id")), QStringLiteral("overlay_id"),
                            &r.overlayId, error)
        || !requireInt(message, QStringLiteral("duration_ms"), 0, kLiveMaxDurationMs, &duration, error))
        return false;
    r.requestId = requestId;
    r.durationMs = int(duration);
    if (!liveModeFromName(message.value(QStringLiteral("mode")).toString(), &r.mode)) {
        *error = QStringLiteral("Feld mode fehlt oder ist nicht show, hide oder toggle");
        return false;
    }
    const QJsonValue elements = message.value(QStringLiteral("elements"));
    if (!elements.isArray()) {
        *error = QStringLiteral("Feld elements fehlt oder ist keine Liste");
        return false;
    }
    for (const QJsonValue &v : elements.toArray()) {
        const QJsonObject o = v.toObject();
        LiveElementRequest e;
        std::optional<QString> image;
        if (!v.isObject()
            || !requireUuidText(o.value(QStringLiteral("source")), QStringLiteral("elements[].source"),
                                &e.source, error)
            || !textOrNull(o, QStringLiteral("text"), &e.text, error)
            || !textOrNull(o, QStringLiteral("image"), &image, error)) {
            if (!v.isObject())
                *error = QStringLiteral("Feld elements enthält etwas anderes als Objekte");
            return false;
        }
        e.image = image.value_or(QString());
        r.elements.append(e);
    }
    const QJsonValue arguments = message.value(QStringLiteral("arguments"));
    if (!arguments.isObject()) {
        *error = QStringLiteral("Feld arguments fehlt oder ist kein Objekt");
        return false;
    }
    const QJsonObject args = arguments.toObject();
    for (auto it = args.constBegin(); it != args.constEnd(); ++it) {
        if (!it.value().isString()) {
            *error = QStringLiteral("Argument %1 ist kein Text").arg(it.key());
            return false;
        }
        r.arguments.insert(it.key(), it.value().toString());
    }
    *request = r;
    return true;
}

QString validateLiveSet(const LiveSetRequest &request, const Overlay &overlay)
{
    if (request.overlayId != overlay.id)
        return QStringLiteral("Falsches Overlay.");
    if (request.elements.isEmpty())
        return QStringLiteral("Kein Element gewählt.");
    if (request.elements.size() > kLiveMaxElements)
        return QStringLiteral("Höchstens %1 Elemente auf einmal.").arg(kLiveMaxElements);
    if (request.durationMs < 0 || request.durationMs > kLiveMaxDurationMs)
        return QStringLiteral("Dauer %1 ms ungültig, erlaubt sind 0 (ohne Ende) bis %2 ms.")
            .arg(request.durationMs).arg(kLiveMaxDurationMs);
    if (request.arguments.size() > kLiveMaxArguments)
        return QStringLiteral("Höchstens %1 Argumente.").arg(kLiveMaxArguments);
    QSet<QUuid> seen;
    for (const LiveElementRequest &e : request.elements) {
        const int index = sourceIndex(overlay, e.source);
        if (index < 0)
            return QStringLiteral("Element %1 gibt es im Overlay «%2» nicht.").arg(idText(e.source), overlay.name);
        const Source &s = overlay.sources.at(index);
        if (seen.contains(e.source))
            return QStringLiteral("Element «%1» ist doppelt gewählt.").arg(s.name);
        seen.insert(e.source);
        if (e.text && s.kind != SourceKind::Text)
            return QStringLiteral("«%1» ist kein Text, ein Text geht nur bei Textelementen.").arg(s.name);
        if (e.text && e.text->size() > kLiveMaxTextChars)
            return QStringLiteral("Text für «%1» zu lang (höchstens %2 Zeichen).").arg(s.name).arg(kLiveMaxTextChars);
        if (!e.image.isEmpty() && s.kind != SourceKind::Image)
            return QStringLiteral("«%1» ist kein Bild, ein Bild geht nur bei Bildelementen.").arg(s.name);
    }
    return QString();
}

QJsonObject liveResetMessage(const LiveResetRequest &request)
{
    QJsonArray sources;
    for (const QUuid &id : request.sources)
        sources.append(idText(id));
    return {{QStringLiteral("type"), QString::fromLatin1(msg::kOverlayLiveReset)},
            {QStringLiteral("request_id"), double(request.requestId)},
            {QStringLiteral("overlay_id"), idText(request.overlayId)},
            {QStringLiteral("sources"), sources}};
}

bool parseLiveReset(const QJsonObject &message, LiveResetRequest *request, QString *error)
{
    LiveResetRequest r;
    qint64 requestId = 0;
    if (!requireInt(message, QStringLiteral("request_id"), 0, qint64(1) << 53, &requestId, error)
        || !requireUuidText(message.value(QStringLiteral("overlay_id")), QStringLiteral("overlay_id"),
                            &r.overlayId, error))
        return false;
    r.requestId = requestId;
    const QJsonValue sources = message.value(QStringLiteral("sources"));
    if (!sources.isArray()) {
        *error = QStringLiteral("Feld sources fehlt oder ist keine Liste");
        return false;
    }
    for (const QJsonValue &v : sources.toArray()) {
        QUuid id;
        if (!requireUuidText(v, QStringLiteral("sources[]"), &id, error))
            return false;
        r.sources.append(id);
    }
    *request = r;
    return true;
}

QJsonObject overlayLiveMessage(const QList<OverlayLiveInfo> &overlays)
{
    QJsonArray list;
    for (const OverlayLiveInfo &info : overlays) {
        QJsonArray elements;
        for (auto it = info.elements.cbegin(); it != info.elements.cend(); ++it) {
            const LiveElement &e = it.value();
            elements.append(QJsonObject{
                {QStringLiteral("source"), idText(it.key())},
                {QStringLiteral("visible"), e.visible ? QJsonValue(*e.visible) : QJsonValue(QJsonValue::Null)},
                {QStringLiteral("text"), textValue(e.text)},
                {QStringLiteral("image"), nonEmptyOrNull(e.image)},
                {QStringLiteral("remaining_ms"),
                 e.remainingMs < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(double(e.remainingMs))}});
        }
        list.append(QJsonObject{{QStringLiteral("overlay_id"), idText(info.overlayId)},
                                {QStringLiteral("elements"), elements}});
    }
    return {{QStringLiteral("type"), QString::fromLatin1(msg::kOverlayLive)},
            {QStringLiteral("overlays"), list}};
}

bool parseOverlayLive(const QJsonObject &message, QList<OverlayLiveInfo> *overlays, QString *error)
{
    const QJsonValue list = message.value(QStringLiteral("overlays"));
    if (!list.isArray()) {
        *error = QStringLiteral("Feld overlays fehlt oder ist keine Liste");
        return false;
    }
    QList<OverlayLiveInfo> result;
    for (const QJsonValue &v : list.toArray()) {
        const QJsonObject o = v.toObject();
        OverlayLiveInfo info;
        if (!requireUuidText(o.value(QStringLiteral("overlay_id")), QStringLiteral("overlay_id"),
                             &info.overlayId, error))
            return false;
        const QJsonValue elements = o.value(QStringLiteral("elements"));
        if (!elements.isArray()) {
            *error = QStringLiteral("Feld elements fehlt oder ist keine Liste");
            return false;
        }
        for (const QJsonValue &ev : elements.toArray()) {
            const QJsonObject eo = ev.toObject();
            QUuid source;
            LiveElement e;
            std::optional<QString> image;
            if (!requireUuidText(eo.value(QStringLiteral("source")), QStringLiteral("elements[].source"),
                                 &source, error)
                || !textOrNull(eo, QStringLiteral("text"), &e.text, error)
                || !textOrNull(eo, QStringLiteral("image"), &image, error))
                return false;
            const QJsonValue visible = eo.value(QStringLiteral("visible"));
            if (visible.isBool()) {
                e.visible = visible.toBool();
            } else if (!visible.isNull()) {
                *error = QStringLiteral("Feld visible fehlt oder ist weder true/false noch null");
                return false;
            }
            const QJsonValue remaining = eo.value(QStringLiteral("remaining_ms"));
            if (remaining.isNull()) {
                e.remainingMs = -1;
            } else if (!requireInt(eo, QStringLiteral("remaining_ms"), 0, kLiveMaxDurationMs, &e.remainingMs, error)) {
                return false;
            }
            e.image = image.value_or(QString());
            info.elements.insert(source, e);
        }
        result.append(info);
    }
    *overlays = result;
    return true;
}

QString liveStateText(const std::optional<LiveElement> &element, qint64 remainingMs)
{
    if (!element)
        return QStringLiteral("nicht live");
    QString text = !element->visible ? QStringLiteral("live geändert")
                   : *element->visible ? QStringLiteral("live sichtbar")
                                       : QStringLiteral("live versteckt");
    if (remainingMs >= 0)
        text += QStringLiteral(", noch %1 s").arg((remainingMs + 999) / 1000);
    return text;
}

} // namespace dss
