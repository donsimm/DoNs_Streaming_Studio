#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

// Protokoll von obs-websocket 5.x (P10c, T-05): Nachrichten bauen und lesen, ohne Verbindung und
// ohne Qt Gui (CLAUDE.md Regel 9). Kennt weder den Kern noch die Schnittstelle zum Fenster.
//
// Quellen:
//   Protokoll: https://raw.githubusercontent.com/obsproject/obs-websocket/master/docs/generated/protocol.md
//     (Stand master, gelesen 2026-11-07; die Doku nennt die Felder der Elemente von «scenes» und
//     «inputs» nicht)
//   Felder der Elemente: https://raw.githubusercontent.com/obsproject/obs-websocket/master/src/utils/Obs_ArrayHelper.cpp
//     (sceneName, sceneUuid, sceneIndex; inputName, inputUuid, inputKind)

namespace dss::obs {

inline constexpr int kRpcVersion = 1;
inline constexpr char kSubprotocol[] = "obswebsocket.json";
inline constexpr int kDefaultPort = 4455;
// Art der Quelle in OBS (obs-browser)
inline constexpr char kBrowserKind[] = "browser_source";

// Feld «op»
enum class OpCode {
    Hello = 0,
    Identify = 1,
    Identified = 2,
    Reidentify = 3,
    Event = 5,
    Request = 6,
    RequestResponse = 7,
};

// EventSubscription: General (1) | Config (2) | Scenes (4) | Inputs (8)
inline constexpr int kEventSubscriptions = 1 | 2 | 4 | 8;

// WebSocketCloseCode, nur die Codes, bei denen der Kern anders handelt
inline constexpr int kCloseAuthenticationFailed = 4009;
inline constexpr int kCloseUnsupportedRpcVersion = 4010;
inline constexpr int kCloseSessionInvalidated = 4011;

// RequestStatus, nur die Codes, bei denen der Kern anders handelt
inline constexpr int kStatusSuccess = 100;
inline constexpr int kStatusNotReady = 207;
inline constexpr int kStatusResourceNotFound = 600;
inline constexpr int kStatusResourceAlreadyExists = 601;

// Die Doku verbietet bei 4011 ein automatisches Wiederverbinden. 4009 und 4010 scheitern beim
// nächsten Versuch wieder gleich.
bool closeCodeAllowsReconnect(int closeCode);

// base64(sha256(base64(sha256(Passwort + Salt)) + Challenge)). Das Passwort selbst geht nie über
// das Netz.
QString authenticationString(const QString &password, const QString &salt, const QString &challenge);

// Eine Nachricht von OBS: Feld «op» und Objekt «d». false mit Meldung, wenn sie nicht lesbar ist.
bool parseFrame(const QString &text, OpCode *op, QJsonObject *data, QString *error);
QString encodeFrame(OpCode op, const QJsonObject &data);

struct Hello
{
    QString obsVersion;       // obsStudioVersion
    QString websocketVersion; // obsWebSocketVersion
    int rpcVersion = 0;
    bool authenticationRequired = false;
    QString salt;
    QString challenge;
};
bool parseHello(const QJsonObject &data, Hello *hello, QString *error);

// Inhalt von Identify; password wird nur mit Salt und Challenge von OBS verrechnet.
QJsonObject identifyData(const Hello &hello, const QString &password);

QJsonObject requestData(const QString &type, const QString &id, const QJsonObject &fields);

struct Response
{
    QString requestId;
    bool ok = false;
    int code = 0;
    QString comment;
    QJsonObject data;
};
bool parseResponse(const QJsonObject &data, Response *response, QString *error);

struct Event
{
    QString type;
    QJsonObject data;
};
bool parseEvent(const QJsonObject &data, Event *event, QString *error);

// Szene in OBS
struct Scene
{
    QString uuid;
    QString name;

    bool operator==(const Scene &) const = default;
    bool isNull() const { return uuid.isEmpty() && name.isEmpty(); }
};

// Browserquelle oder andere Quelle in OBS
struct Input
{
    QString uuid;
    QString name;

    bool operator==(const Input &) const = default;
};

// Feld «scenes» aus GetSceneList in der Reihenfolge der Liste in OBS (oberste zuerst). Elemente
// ohne Kennung oder Namen werden übergangen.
QList<Scene> parseScenes(const QJsonObject &data);
// Feld «inputs» aus GetInputList. Elemente ohne Kennung oder Namen werden übergangen.
QList<Input> parseInputs(const QJsonObject &data);
// Aktuelle Szene aus GetSceneList (currentProgramSceneUuid, currentProgramSceneName) bzw. aus dem
// Ereignis CurrentProgramSceneChanged (sceneUuid, sceneName). Die Doku erlaubt null.
Scene parseCurrentScene(const QJsonObject &data);
Scene parseChangedScene(const QJsonObject &data);

} // namespace dss::obs
