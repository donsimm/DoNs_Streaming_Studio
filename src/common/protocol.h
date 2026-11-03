#pragma once

#include "connectionstate.h"
#include "fontcatalog.h"
#include "overlay.h"
#include "overlaylist.h"
#include "systemstats.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

// Schnittstelle zwischen Kern und Fenster (P07, Teil D; Änderungen seit P08, Teil A): JSON, eine
// Nachricht pro Zeile.
// Vollständige Beschreibung: docs/08-kern-schnittstelle.md. Hier steht nur das Umwandeln von
// Nachrichten in Daten und zurück, ohne Verbindung und ohne Qt Gui (CLAUDE.md Regel 9).
// Der Token eines Overlays kommt in keiner Nachricht vor (CLAUDE.md Regel 12).

namespace dss {

// Version der Schnittstelle; steht in der Begrüssung beider Seiten. 2 seit P08: Änderungen vom
// Fenster an den Kern, overlay_list mit Ordnern in der Reihenfolge der Liste. 3 seit P09: Overlays
// im Format 2 der Overlay-Datei (video.hold_at_end), set_overlay_size
// 4 seit P09b: Overlays im Format 3 (flip_horizontal, flip_vertical); overlay und
//   overlay_changed mit media_sizes (Originalgrösse der Dateien)
// (Begründung: docs/08-kern-schnittstelle.md, «Version»).
inline constexpr int kProtocolVersion = 4;
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
// P08: Schriften und Änderungen
inline constexpr char kGetFonts[] = "get_fonts";
inline constexpr char kFontList[] = "font_list";
inline constexpr char kChangeResult[] = "change_result";
inline constexpr char kCreateOverlay[] = "create_overlay";
inline constexpr char kRenameOverlay[] = "rename_overlay";
inline constexpr char kCopyOverlay[] = "copy_overlay";
inline constexpr char kRemoveOverlay[] = "remove_overlay";
inline constexpr char kMoveOverlay[] = "move_overlay";
inline constexpr char kCreateFolder[] = "create_folder";
inline constexpr char kRenameFolder[] = "rename_folder";
inline constexpr char kRemoveFolder[] = "remove_folder";
inline constexpr char kMoveFolder[] = "move_folder";
inline constexpr char kSetFolderExpanded[] = "set_folder_expanded";
inline constexpr char kAddImageSource[] = "add_image_source";
inline constexpr char kAddVideoSource[] = "add_video_source";
inline constexpr char kAddTextSource[] = "add_text_source";
inline constexpr char kRemoveSource[] = "remove_source";
inline constexpr char kMoveSource[] = "move_source";
inline constexpr char kUpdateSource[] = "update_source";
inline constexpr char kSetTextFont[] = "set_text_font";
inline constexpr char kReplaceImageFile[] = "replace_image_file";
// P09: Auflösung eines Overlays (Entscheid 5)
inline constexpr char kSetOverlaySize[] = "set_overlay_size";
} // namespace msg

// Alle Typen von Änderungen (Fenster → Kern), in der Reihenfolge von docs/08.
QStringList changeTypes();
bool isChangeType(const QString &type);

// Fehlercodes in der Nachricht «error»
namespace err {
inline constexpr char kInvalidJson[] = "invalid_json";
inline constexpr char kInvalidMessage[] = "invalid_message";
inline constexpr char kUnknownType[] = "unknown_type";
inline constexpr char kHelloMissing[] = "hello_missing";
inline constexpr char kProtocolVersion[] = "protocol_version";
inline constexpr char kUnknownOverlay[] = "unknown_overlay";
inline constexpr char kMessageTooLong[] = "message_too_long";
// P08: Antworten auf Änderungen (change_result)
inline constexpr char kUnknownSource[] = "unknown_source";
inline constexpr char kUnknownFolder[] = "unknown_folder";
inline constexpr char kRejected[] = "rejected";       // Modell oder Datei lehnt ab
inline constexpr char kSaveFailed[] = "save_failed";  // verzögertes Speichern gescheitert (error)
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
    // Nur vom Kern (P09): Datenordner als absoluter Pfad. Das Fenster liest Bilder, Videos und
    // Schriften für den Zeichner von dort (K-03). Leer = nicht angegeben.
    QString dataDir = {};

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

// overlay_list (seit Version 2): Einträge in der Reihenfolge der Liste mit Ordnern (liste.json).
struct OverlayListData
{
    OverlayLayout layout;             // nur Kennungen, wie liste.json
    QList<OverlaySummary> overlays;   // in der Reihenfolge der Liste
    QString listError;                // leer, wenn liste.json gültig ist

    const OverlaySummary *summary(const QUuid &id) const;
    bool operator==(const OverlayListData &) const = default;
};
// Einträge zu Kennungen ohne Zusammenfassung werden übergangen.
QJsonObject overlayListMessage(const OverlayLayout &layout, const QList<OverlaySummary> &summaries,
                               const QString &listError = QString());
bool parseOverlayList(const QJsonObject &message, OverlayListData *data, QString *error);

// type: msg::kOverlay (Antwort auf get_overlay) oder msg::kOverlayChanged. Das Overlay steht im
// Feld «overlay» im Format der Overlay-Datei (docs/06-overlay-format.md), ohne Token.
// Seit Version 4 (P09b) steht im Feld «media_sizes» die Originalgrösse der Bild- und
// Videodateien des Overlays: {"media/…": {"width": …, "height": …}}; Pflicht, darf leer sein.
QJsonObject overlayMessage(const char *type, const Overlay &overlay, const MediaSizes &sizes);
bool parseOverlayMessage(const QJsonObject &message, Overlay *overlay, MediaSizes *sizes,
                         QString *error);

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

// Nachricht ohne weitere Felder (get_overlay_list, quit_request, get_fonts).
QJsonObject simpleMessage(const char *type);

// font_list: alle Schnitte zur Auswahl (Teil F) und eine Meldung, wenn ein Teil fehlt.
QJsonObject fontListMessage(const QList<FontFace> &faces, const QString &fontError);
bool parseFontList(const QJsonObject &message, QList<FontFace> *faces, QString *fontError,
                   QString *error);

// Änderung einzelner Eigenschaften einer Quelle im Feld «changes» von update_source.
QJsonObject sourceChangesToJson(const SourceChanges &changes);
bool sourceChangesFromJson(const QJsonObject &object, SourceChanges *changes, QString *error);

// Änderung vom Fenster an den Kern (P08). Welche Felder zu welchem Typ gehören, steht in
// docs/08-kern-schnittstelle.md; die übrigen bleiben leer.
struct ChangeRequest
{
    QString type;                 // einer aus changeTypes()
    qint64 requestId = 0;         // vom Fenster gewählt, kommt in change_result zurück
    QUuid overlayId;
    QUuid sourceId;
    QUuid folderId;               // bei move_overlay das Ziel, null = oberste Ebene
    QString name;
    QString file;                 // Pfad der Datei des Nutzers oder Verweis "media/…" (Schrift)
    int width = 0;
    int height = 0;
    int index = 0;
    bool changeAddress = false;   // rename_overlay
    bool expanded = false;        // set_folder_expanded
    std::optional<int> weight;    // set_text_font; leer = aus der Datei
    std::optional<bool> italic;   // set_text_font; leer = aus der Datei
    SourceChanges changes;        // update_source

    bool operator==(const ChangeRequest &) const = default;
};
QJsonObject changeRequestMessage(const ChangeRequest &request);
bool parseChangeRequest(const QJsonObject &message, ChangeRequest *request, QString *error);
// request_id einer Nachricht, -1 wenn keine gültige da ist (für die Antwort auf eine sonst
// ungültige Änderung).
qint64 messageRequestId(const QJsonObject &message);

// Antwort des Kerns auf eine Änderung, nur an das Fenster, das sie geschickt hat.
struct ChangeResult
{
    qint64 requestId = 0;
    bool ok = false;
    QUuid id;         // neu angelegtes Overlay, Ordner oder Quelle (sonst null)
    QString code;     // bei !ok, siehe err
    QString message;  // bei !ok, auf Deutsch
    QString warning = {}; // P09: bei ok ein Hinweis (zum Beispiel Grösse der Datei nicht lesbar)

    bool operator==(const ChangeResult &) const = default;
};
QJsonObject changeResultMessage(const ChangeResult &result);
bool parseChangeResult(const QJsonObject &message, ChangeResult *result, QString *error);

} // namespace dss
