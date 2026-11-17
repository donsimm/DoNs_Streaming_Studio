#include "cameraprotocol.h"

#include "protocol.h"

#include <QJsonValue>

namespace dss {

namespace {

// Feste Liste (checkCameraCall): Methode und Muster des Pfads
struct AllowedCall
{
    const char *method;
    const char *pattern;
};

constexpr AllowedCall kAllowedCalls[] = {
    {"GET", "status"},
    {"GET", "cameras/*/controls"},
    {"PUT", "cameras/*/controls"},
    {"GET", "cameras/*/config"},
    {"PUT", "cameras/*/config"},
    {"POST", "cameras/*/move"},
    {"POST", "cameras/*/stop"},
    {"POST", "cameras/*/step"},
    {"POST", "cameras/*/home"},
    {"POST", "cameras/*/wake"},
    {"POST", "cameras/*/sleep"},
    {"POST", "camera-presets"},
    {"GET", "camera-presets/*"},
    {"PUT", "camera-presets/*"},
    {"DELETE", "camera-presets/*"},
    {"POST", "camera-presets/*/save-current"},
    {"POST", "camera-presets/*/recall"},
    {"PUT", "lights/*"}, // auch lights/all
    {"POST", "lights"},
    {"PUT", "lights/*/config"},
    {"DELETE", "lights/*"},
    {"POST", "light-presets"},
    {"GET", "light-presets/*"},
    {"PUT", "light-presets/*"},
    {"DELETE", "light-presets/*"},
    {"POST", "light-presets/*/save-current"},
    {"POST", "light-presets/*/recall"},
    {"GET", "export"},
};

constexpr qsizetype kMaxPathLength = 512;

bool isIdPart(const QString &part)
{
    if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
        return false;
    for (const QChar c : part) {
        const bool ok = (c >= QLatin1Char('a') && c <= QLatin1Char('z')) || (c >= QLatin1Char('A') && c <= QLatin1Char('Z'))
                        || (c >= QLatin1Char('0') && c <= QLatin1Char('9')) || c == QLatin1Char('.')
                        || c == QLatin1Char('_') || c == QLatin1Char('~') || c == QLatin1Char(':')
                        || c == QLatin1Char('%') || c == QLatin1Char('-');
        if (!ok)
            return false;
    }
    return true;
}

bool matchesPattern(const QStringList &parts, const char *pattern)
{
    const QStringList expected = QString::fromLatin1(pattern).split(QLatin1Char('/'));
    if (expected.size() != parts.size())
        return false;
    for (qsizetype i = 0; i < parts.size(); ++i) {
        if (expected.at(i) == QLatin1String("*") ? !isIdPart(parts.at(i)) : expected.at(i) != parts.at(i))
            return false;
    }
    return true;
}

QJsonObject typed(const char *type)
{
    return QJsonObject{{QStringLiteral("type"), QString::fromLatin1(type)}};
}

bool checkType(const QJsonObject &message, const char *type, QString *error)
{
    if (messageType(message) != QLatin1String(type)) {
        *error = QStringLiteral("Nachricht ist nicht vom Typ %1").arg(QLatin1String(type));
        return false;
    }
    return true;
}

bool requireText(const QJsonObject &o, const QString &key, QString *value, QString *error)
{
    const QJsonValue v = o.value(key);
    if (!v.isString()) {
        *error = QStringLiteral("Feld %1 fehlt oder ist kein Text").arg(key);
        return false;
    }
    *value = v.toString();
    return true;
}

bool requireObject(const QJsonObject &o, const QString &key, QJsonObject *value, QString *error)
{
    const QJsonValue v = o.value(key);
    if (!v.isObject()) {
        *error = QStringLiteral("Feld %1 fehlt oder ist kein Objekt").arg(key);
        return false;
    }
    *value = v.toObject();
    return true;
}

// Text oder null (null und leer gelten gleich)
bool optionalText(const QJsonObject &o, const QString &key, QString *value, QString *error)
{
    const QJsonValue v = o.value(key);
    if (v.isNull()) {
        value->clear();
        return true;
    }
    if (!v.isString()) {
        *error = QStringLiteral("Feld %1 fehlt oder ist weder Text noch null").arg(key);
        return false;
    }
    *value = v.toString();
    return true;
}

QJsonValue textOrNull(const QString &text)
{
    return text.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(text);
}

bool requireRequestId(const QJsonObject &message, qint64 *id, QString *error)
{
    *id = messageRequestId(message);
    if (*id < 0) {
        *error = QStringLiteral("Feld request_id fehlt oder ist keine ganze Zahl ab 0");
        return false;
    }
    return true;
}

} // namespace

QStringList cameraServiceStates()
{
    return {QString::fromLatin1(kCameraStateOff), QString::fromLatin1(kCameraStateConnecting),
            QString::fromLatin1(kCameraStateConnected), QString::fromLatin1(kCameraStateError),
            QString::fromLatin1(kCameraStateUnavailable)};
}

QJsonObject cameraServiceMessage(const CameraServiceInfo &info)
{
    QJsonObject o = typed(msg::kCameraService);
    o.insert(QStringLiteral("state"), info.state);
    o.insert(QStringLiteral("message"), textOrNull(info.message));
    o.insert(QStringLiteral("status"), info.status);
    return o;
}

bool parseCameraService(const QJsonObject &message, CameraServiceInfo *info, QString *error)
{
    CameraServiceInfo i;
    if (!checkType(message, msg::kCameraService, error) || !requireText(message, QStringLiteral("state"), &i.state, error)
        || !optionalText(message, QStringLiteral("message"), &i.message, error)
        || !requireObject(message, QStringLiteral("status"), &i.status, error))
        return false;
    if (!cameraServiceStates().contains(i.state)) {
        *error = QStringLiteral("Unbekannter Zustand «%1»").arg(i.state);
        return false;
    }
    *info = i;
    return true;
}

QJsonObject cameraEventMessage(const CameraEventInfo &event)
{
    QJsonObject o = typed(msg::kCameraEvent);
    o.insert(QStringLiteral("name"), event.name);
    o.insert(QStringLiteral("data"), event.data);
    return o;
}

bool parseCameraEvent(const QJsonObject &message, CameraEventInfo *event, QString *error)
{
    CameraEventInfo e;
    if (!checkType(message, msg::kCameraEvent, error) || !requireText(message, QStringLiteral("name"), &e.name, error)
        || !requireObject(message, QStringLiteral("data"), &e.data, error))
        return false;
    if (e.name.isEmpty()) {
        *error = QStringLiteral("Feld name ist leer");
        return false;
    }
    *event = e;
    return true;
}

QJsonObject cameraCallMessage(const CameraCall &call)
{
    QJsonObject o = typed(msg::kCameraCall);
    o.insert(QStringLiteral("request_id"), double(call.requestId));
    o.insert(QStringLiteral("method"), call.method);
    o.insert(QStringLiteral("path"), call.path);
    o.insert(QStringLiteral("body"), call.body ? QJsonValue(*call.body) : QJsonValue(QJsonValue::Null));
    return o;
}

bool parseCameraCall(const QJsonObject &message, CameraCall *call, QString *error)
{
    CameraCall c;
    if (!checkType(message, msg::kCameraCall, error) || !requireRequestId(message, &c.requestId, error)
        || !requireText(message, QStringLiteral("method"), &c.method, error)
        || !requireText(message, QStringLiteral("path"), &c.path, error))
        return false;
    const QJsonValue body = message.value(QStringLiteral("body"));
    if (body.isObject()) {
        c.body = body.toObject();
    } else if (!body.isUndefined() && !body.isNull()) {
        *error = QStringLiteral("Feld body ist weder Objekt noch null");
        return false;
    }
    *call = c;
    return true;
}

QJsonObject cameraResultMessage(const CameraResult &result)
{
    QJsonObject o = typed(msg::kCameraResult);
    o.insert(QStringLiteral("request_id"), double(result.requestId));
    o.insert(QStringLiteral("status"), result.status);
    o.insert(QStringLiteral("body"), result.body);
    o.insert(QStringLiteral("error"), textOrNull(result.error));
    return o;
}

bool parseCameraResult(const QJsonObject &message, CameraResult *result, QString *error)
{
    CameraResult r;
    if (!checkType(message, msg::kCameraResult, error) || !requireRequestId(message, &r.requestId, error)
        || !requireObject(message, QStringLiteral("body"), &r.body, error)
        || !optionalText(message, QStringLiteral("error"), &r.error, error))
        return false;
    const QJsonValue status = message.value(QStringLiteral("status"));
    const int s = status.toInt(-1);
    if (!status.isDouble() || s < 0 || s > 999 || double(s) != status.toDouble()) {
        *error = QStringLiteral("Feld status fehlt oder ist keine ganze Zahl von 0 bis 999");
        return false;
    }
    r.status = s;
    *result = r;
    return true;
}

QString checkCameraCall(const QString &method, const QString &path)
{
    const QString shown = method + QLatin1Char(' ') + path.left(80);
    if (path.size() > kMaxPathLength)
        return QStringLiteral("Pfad für den Kamera-Dienst zu lang (höchstens %1 Zeichen).").arg(kMaxPathLength);
    const QStringList parts = path.split(QLatin1Char('/'));
    for (const AllowedCall &allowed : kAllowedCalls) {
        if (method == QLatin1String(allowed.method) && matchesPattern(parts, allowed.pattern))
            return {};
    }
    return QStringLiteral("Anfrage «%1» an den Kamera-Dienst ist nicht erlaubt.").arg(shown);
}

QStringList allowedCameraCalls()
{
    QStringList list;
    for (const AllowedCall &allowed : kAllowedCalls)
        list.append(QLatin1String(allowed.method) + QLatin1Char(' ') + QLatin1String(allowed.pattern));
    return list;
}

QString cameraApiPath(const QString &input)
{
    QString path = input;
    if (path.startsWith(QLatin1String("/api/v1/")))
        path = path.mid(8);
    else if (path.startsWith(QLatin1String("api/v1/")))
        path = path.mid(7);
    return path;
}

} // namespace dss
