#include "obsprotocol.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

#include <algorithm>

namespace dss::obs {

namespace {

QByteArray base64Sha256(const QByteArray &text)
{
    return QCryptographicHash::hash(text, QCryptographicHash::Sha256).toBase64();
}

} // namespace

bool closeCodeAllowsReconnect(int closeCode)
{
    return closeCode != kCloseAuthenticationFailed && closeCode != kCloseUnsupportedRpcVersion
           && closeCode != kCloseSessionInvalidated;
}

QString authenticationString(const QString &password, const QString &salt, const QString &challenge)
{
    const QByteArray secret = base64Sha256(password.toUtf8() + salt.toUtf8());
    return QString::fromLatin1(base64Sha256(secret + challenge.toUtf8()));
}

bool parseFrame(const QString &text, OpCode *op, QJsonObject *data, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(text.toUtf8(), &parseError);
    if (!document.isObject()) {
        *error = QStringLiteral("kein JSON-Objekt (%1)").arg(parseError.errorString());
        return false;
    }
    const QJsonObject object = document.object();
    if (!object.value(QStringLiteral("op")).isDouble() || !object.value(QStringLiteral("d")).isObject()) {
        *error = QStringLiteral("Felder op oder d fehlen");
        return false;
    }
    *op = OpCode(object.value(QStringLiteral("op")).toInt(-1));
    *data = object.value(QStringLiteral("d")).toObject();
    return true;
}

QString encodeFrame(OpCode op, const QJsonObject &data)
{
    const QJsonObject object{{QStringLiteral("op"), int(op)}, {QStringLiteral("d"), data}};
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool parseHello(const QJsonObject &data, Hello *hello, QString *error)
{
    if (!data.value(QStringLiteral("rpcVersion")).isDouble()) {
        *error = QStringLiteral("Hello ohne rpcVersion");
        return false;
    }
    Hello h;
    h.obsVersion = data.value(QStringLiteral("obsStudioVersion")).toString();
    h.websocketVersion = data.value(QStringLiteral("obsWebSocketVersion")).toString();
    h.rpcVersion = data.value(QStringLiteral("rpcVersion")).toInt();
    const QJsonValue authentication = data.value(QStringLiteral("authentication"));
    if (authentication.isObject()) {
        const QJsonObject a = authentication.toObject();
        if (!a.value(QStringLiteral("challenge")).isString() || !a.value(QStringLiteral("salt")).isString()) {
            *error = QStringLiteral("Hello ohne challenge oder salt");
            return false;
        }
        h.authenticationRequired = true;
        h.salt = a.value(QStringLiteral("salt")).toString();
        h.challenge = a.value(QStringLiteral("challenge")).toString();
    }
    *hello = h;
    return true;
}

QJsonObject identifyData(const Hello &hello, const QString &password)
{
    QJsonObject data{{QStringLiteral("rpcVersion"), kRpcVersion},
                     {QStringLiteral("eventSubscriptions"), kEventSubscriptions}};
    if (hello.authenticationRequired)
        data.insert(QStringLiteral("authentication"),
                    authenticationString(password, hello.salt, hello.challenge));
    return data;
}

QJsonObject requestData(const QString &type, const QString &id, const QJsonObject &fields)
{
    QJsonObject data{{QStringLiteral("requestType"), type}, {QStringLiteral("requestId"), id}};
    if (!fields.isEmpty())
        data.insert(QStringLiteral("requestData"), fields);
    return data;
}

bool parseResponse(const QJsonObject &data, Response *response, QString *error)
{
    const QJsonValue status = data.value(QStringLiteral("requestStatus"));
    if (!data.value(QStringLiteral("requestId")).isString() || !status.isObject()) {
        *error = QStringLiteral("RequestResponse ohne requestId oder requestStatus");
        return false;
    }
    const QJsonObject s = status.toObject();
    if (!s.value(QStringLiteral("result")).isBool() || !s.value(QStringLiteral("code")).isDouble()) {
        *error = QStringLiteral("requestStatus ohne result oder code");
        return false;
    }
    Response r;
    r.requestId = data.value(QStringLiteral("requestId")).toString();
    r.ok = s.value(QStringLiteral("result")).toBool();
    r.code = s.value(QStringLiteral("code")).toInt();
    r.comment = s.value(QStringLiteral("comment")).toString();
    r.data = data.value(QStringLiteral("responseData")).toObject();
    *response = r;
    return true;
}

bool parseEvent(const QJsonObject &data, Event *event, QString *error)
{
    if (!data.value(QStringLiteral("eventType")).isString()) {
        *error = QStringLiteral("Event ohne eventType");
        return false;
    }
    event->type = data.value(QStringLiteral("eventType")).toString();
    event->data = data.value(QStringLiteral("eventData")).toObject();
    return true;
}

QList<Scene> parseScenes(const QJsonObject &data)
{
    struct Indexed
    {
        int index;
        Scene scene;
    };
    QList<Indexed> list;
    const QJsonArray scenes = data.value(QStringLiteral("scenes")).toArray();
    for (const QJsonValue &v : scenes) {
        const QJsonObject o = v.toObject();
        const Scene scene{o.value(QStringLiteral("sceneUuid")).toString(),
                          o.value(QStringLiteral("sceneName")).toString()};
        if (!scene.uuid.isEmpty() && !scene.name.isEmpty())
            list.append({o.value(QStringLiteral("sceneIndex")).toInt(), scene});
    }
    // sceneIndex zählt von unten (Obs_ArrayHelper.cpp): Die oberste Szene in OBS hat den grössten.
    std::stable_sort(list.begin(), list.end(),
                     [](const Indexed &a, const Indexed &b) { return a.index > b.index; });
    QList<Scene> result;
    for (const Indexed &i : std::as_const(list))
        result.append(i.scene);
    return result;
}

QList<Input> parseInputs(const QJsonObject &data)
{
    QList<Input> result;
    const QJsonArray inputs = data.value(QStringLiteral("inputs")).toArray();
    for (const QJsonValue &v : inputs) {
        const QJsonObject o = v.toObject();
        const Input input{o.value(QStringLiteral("inputUuid")).toString(),
                          o.value(QStringLiteral("inputName")).toString()};
        if (!input.uuid.isEmpty() && !input.name.isEmpty())
            result.append(input);
    }
    return result;
}

Scene parseCurrentScene(const QJsonObject &data)
{
    return {data.value(QStringLiteral("currentProgramSceneUuid")).toString(),
            data.value(QStringLiteral("currentProgramSceneName")).toString()};
}

Scene parseChangedScene(const QJsonObject &data)
{
    return {data.value(QStringLiteral("sceneUuid")).toString(),
            data.value(QStringLiteral("sceneName")).toString()};
}

} // namespace dss::obs
