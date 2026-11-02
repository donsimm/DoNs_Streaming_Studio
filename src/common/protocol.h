#pragma once

#include "connectionstate.h"
#include "overlay.h"
#include "systemstats.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

// Schnittstelle zwischen Kern und Fenster (P07, Teil D): JSON, eine Nachricht pro Zeile.
// Vollständige Beschreibung: docs/08-kern-schnittstelle.md. Hier steht nur das Umwandeln von
// Nachrichten in Daten und zurück, ohne Verbindung und ohne Qt Gui (CLAUDE.md Regel 9).
// Der Token eines Overlays kommt in keiner Nachricht vor (CLAUDE.md Regel 12).

namespace dss {

// Version der Schnittstelle; steht in der Begrüssung beider Seiten.
inline constexpr int kProtocolVersion = 1;
// Längste erlaubte Zeile (ohne Zeilenende). Längere Zeilen beenden die Verbindung.
inline constexpr qsizetype kMaxMessageBytes = 16 * 1024 * 1024;

// Werte des Feldes «type»
namespace msg {
inline constexpr char kHello[] = "hello";
inline constexpr char kGetOverlayList[] = "get_overlay_list";
inline constexpr char kGetOverlay[] = "get_overlay";
inline constexpr char kQuitRequest[] = "quit_request";
inline constexpr char kOverlayList[] = "overlay_list";
inline constexpr char kOverlay[] = "overlay";
inline constexpr char kOverlayChanged[] = "overlay_changed";
inline constexpr char kOverlayRemoved[] = "overlay_removed";
inline constexpr char kStats[] = "stats";
inline constexpr char kConnections[] = "connections";
inline constexpr char kQuitReply[] = "quit_reply";
inline constexpr char kError[] = "error";
} // namespace msg

// Fehlercodes in der Nachricht «error»
namespace err {
inline constexpr char kInvalidJson[] = "invalid_json";
inline constexpr char kInvalidMessage[] = "invalid_message";
inline constexpr char kUnknownType[] = "unknown_type";
inline constexpr char kHelloMissing[] = "hello_missing";
inline constexpr char kProtocolVersion[] = "protocol_version";
inline constexpr char kUnknownOverlay[] = "unknown_overlay";
inline constexpr char kMessageTooLong[] = "message_too_long";
} // namespace err

// Eine Zeile: kompaktes JSON (enthält nie ein Zeilenende) und «\n».
QByteArray encodeMessage(const QJsonObject &message);
// Liest eine Zeile (ohne «\n»): muss ein JSON-Objekt mit Text im Feld «type» sein.
bool decodeMessage(const QByteArray &line, QJsonObject *message, QString *error);
QString messageType(const QJsonObject &message);

// Zerlegt den Datenstrom einer Verbindung in Zeilen.
class LineSplitter
{
public:
    enum class Result { None, Line, TooLong };

    void append(const QByteArray &data) { m_buffer.append(data); }
    // Line: *line enthält die nächste Zeile ohne «\n». TooLong: eine Zeile ist länger als
    // kMaxMessageBytes; die Verbindung ist dann zu beenden.
    Result next(QByteArray *line);

private:
    QByteArray m_buffer;
};

// Begrüssung, in beide Richtungen gleich aufgebaut.
struct Hello
{
    QString program;        // «dss-core», «dss-app», …
    QString version;        // Version des Programms
    int protocolVersion = 0;

    bool operator==(const Hello &) const = default;
};
QJsonObject helloMessage(const Hello &hello);
bool parseHello(const QJsonObject &message, Hello *hello, QString *error);

// Eintrag der Overlay-Liste.
struct OverlaySummary
{
    QUuid id;
    QString name;
    QString address;
    int width = 0;
    int height = 0;
    int sourceCount = 0;

    bool operator==(const OverlaySummary &) const = default;
};
OverlaySummary summarizeOverlay(const Overlay &overlay);

QJsonObject overlayListMessage(const QList<OverlaySummary> &overlays);
bool parseOverlayList(const QJsonObject &message, QList<OverlaySummary> *overlays, QString *error);

// type: msg::kOverlay (Antwort auf get_overlay) oder msg::kOverlayChanged. Das Overlay steht im
// Feld «overlay» im Format der Overlay-Datei (docs/06-overlay-format.md), ohne Token.
QJsonObject overlayMessage(const char *type, const Overlay &overlay);
bool parseOverlayMessage(const QJsonObject &message, Overlay *overlay, QString *error);

// overlay_removed und get_overlay: nur die Kennung im Feld «id».
QJsonObject overlayIdMessage(const char *type, const QUuid &id);
bool parseOverlayId(const QJsonObject &message, QUuid *id, QString *error);

QJsonObject statsMessage(const SystemStats &stats);
bool parseStats(const QJsonObject &message, SystemStats *stats, QString *error);

using ConnectionStates = QHash<Connection, ConnectionState>;
QJsonObject connectionsMessage(const ConnectionStates &states);
bool parseConnections(const QJsonObject &message, ConnectionStates *states, QString *error);

struct QuitReply
{
    bool accepted = false;
    int otherWindows = 0;

    bool operator==(const QuitReply &) const = default;
};
QJsonObject quitReplyMessage(const QuitReply &reply);
bool parseQuitReply(const QJsonObject &message, QuitReply *reply, QString *error);

struct ErrorInfo
{
    QString code;
    QString message;
    QString request; // Typ der Anfrage, auf die sich der Fehler bezieht; leer, wenn keine

    bool operator==(const ErrorInfo &) const = default;
};
QJsonObject errorMessage(const ErrorInfo &error);
bool parseError(const QJsonObject &message, ErrorInfo *error, QString *parseErrorText);

// Nachricht ohne weitere Felder (get_overlay_list, quit_request).
QJsonObject simpleMessage(const char *type);

} // namespace dss
