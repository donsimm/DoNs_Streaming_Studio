#pragma once

#include "connectionstate.h"
#include "coresettings.h"
#include "fontcatalog.h"
#include "medialibrary.h"
#include "overlay.h"
#include "overlaylist.h"
#include "systemstats.h"

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

// Schnittstelle zwischen Kern und Fenster (P07, Teil D; Änderungen seit P08, Teil A): JSON, eine
// Nachricht pro Zeile.
// Vollständige Beschreibung: docs/08-kern-schnittstelle.md. Hier steht nur das Umwandeln von
// Nachrichten in Daten und zurück, ohne Verbindung und ohne Qt Gui (CLAUDE.md Regel 9).
// Der Token eines Overlays steht nur in browser_source (seit Version 7, P10), und nur an die
// Verbindung, die mit get_browser_source danach fragt; nie im Protokoll (CLAUDE.md Regel 12).

namespace dss {

// Version der Schnittstelle; steht in der Begrüssung beider Seiten. 2 seit P08: Änderungen vom
// Fenster an den Kern, overlay_list mit Ordnern in der Reihenfolge der Liste. 3 seit P09: Overlays
// im Format 2 der Overlay-Datei (video.hold_at_end), set_overlay_size
// 4 seit P09b: Overlays im Format 3 (flip_horizontal, flip_vertical); overlay und
//   overlay_changed mit media_sizes (Originalgrösse der Dateien)
// 5 seit P10-vor: Einstellungen des Kerns (get_settings, settings, update_settings)
// 6 seit P10: Overlays im Format 4 (output, E-24), overlay_list mit output, Änderung
//   set_overlay_output
// 7 seit P10: Neuer Token (new_token), Adresse der Browserquelle (get_browser_source,
//   browser_source)
// 8 seit P10: Zustand des Overlay-Servers (server_status)
// 9 seit P10: ungültige kern.ini zurücksetzen (reset_settings, Punkt 77)
// 10 seit P10b: Medienliste (get_media_list, media_list, import_progress), Änderungen der
//   Medien (add_media, rename_media, …, cancel_import), add_image_source und add_video_source
//   mit Lage (center_x, center_y, insert_index), Codes canceled und in_use
// 11 seit P10c: Verbindung zu OBS (obs_status, obs_sources, obs_create_source, obs_add_to_scene,
//   settings und update_settings mit obs_*, Codes obs_*)
// (Begründung: docs/08-kern-schnittstelle.md, «Version»).
inline constexpr int kProtocolVersion = 11;
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
// P10-vor: Einstellungen des Kerns (kern.ini)
inline constexpr char kGetSettings[] = "get_settings";
inline constexpr char kSettings[] = "settings";
inline constexpr char kUpdateSettings[] = "update_settings";
inline constexpr char kResetSettings[] = "reset_settings"; // P10, Punkt 77
// P10: Ausgabe ein/aus (E-24)
inline constexpr char kSetOverlayOutput[] = "set_overlay_output";
// P10: Neuer Token und Adresse der Browserquelle (E-12)
inline constexpr char kNewToken[] = "new_token";
inline constexpr char kGetBrowserSource[] = "get_browser_source";
inline constexpr char kBrowserSource[] = "browser_source";
inline constexpr char kServerStatus[] = "server_status";
// P10b: Dock «Medien» (E-28)
inline constexpr char kGetMediaList[] = "get_media_list";
inline constexpr char kMediaList[] = "media_list";
inline constexpr char kImportProgress[] = "import_progress";
inline constexpr char kAddMedia[] = "add_media";
inline constexpr char kRenameMedia[] = "rename_media";
inline constexpr char kMoveMedia[] = "move_media";
inline constexpr char kRemoveMedia[] = "remove_media";
inline constexpr char kCreateMediaFolder[] = "create_media_folder";
inline constexpr char kRenameMediaFolder[] = "rename_media_folder";
inline constexpr char kRemoveMediaFolder[] = "remove_media_folder";
inline constexpr char kMoveMediaFolder[] = "move_media_folder";
inline constexpr char kSetMediaFolderExpanded[] = "set_media_folder_expanded";
inline constexpr char kCancelImport[] = "cancel_import";
// P10c: Verbindung zu OBS
inline constexpr char kObsStatus[] = "obs_status";
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
inline constexpr char kCanceled[] = "canceled";       // P10b: Übernehmen abgebrochen
inline constexpr char kInUse[] = "in_use";            // P10b: Datei wird von Quellen verwendet
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
    bool output = true; // P10 (E-24): Ausgabe ein/aus

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
    QUuid parentId;               // P10b: create_media_folder, move_media_folder; null = oberste Ebene
    QString name;
    // Pfad der Datei des Nutzers oder Verweis "media/…" (Schrift; seit P10b auch Bild und Video
    // aus der Medienliste und die Änderungen der Medien)
    QString file;
    int width = 0;
    int height = 0;
    int index = 0;
    bool changeAddress = false;   // rename_overlay
    bool expanded = false;        // set_folder_expanded
    bool output = true;           // set_overlay_output (P10, E-24)
    std::optional<int> weight;    // set_text_font; leer = aus der Datei
    std::optional<bool> italic;   // set_text_font; leer = aus der Datei
    SourceChanges changes;        // update_source
    qint64 importId = 0;          // P10b: cancel_import
    // P10b: add_image_source, add_video_source. Mitte der neuen Quelle in Pixeln des Overlays
    // (beide oder keiner; leer = Lage 0/0 wie bisher) und Stelle in der Zeichenreihenfolge wie
    // bei move_source (leer = zuoberst).
    std::optional<int> centerX;
    std::optional<int> centerY;
    std::optional<int> insertIndex;

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

// settings (P10-vor): Einstellungen des Kerns, Antwort auf get_settings und nach jeder Änderung an
// alle. portMin/portMax: gültiger Bereich (das Fenster stellt sein Zahlenfeld danach ein).
struct SettingsInfo
{
    CoreSettings settings;
    int portMin = kPortMin;
    int portMax = kPortMax;
    QString file;  // Pfad von kern.ini
    QString error; // Datei ungültig (Änderungen werden abgewiesen); leer, wenn gültig
    // P10c: Das Fenster erfährt nur, ob ein Passwort gespeichert ist, nie das Passwort (Regel 12).
    bool obsPasswordSet = false;

    bool operator==(const SettingsInfo &) const = default;
};
QJsonObject settingsMessage(const SettingsInfo &info);
bool parseSettings(const QJsonObject &message, SettingsInfo *info, QString *error);

// update_settings (P10-vor): nur angegebene Werte ändern sich, mindestens einer. Antwort wie bei
// Änderungen: change_result (ungültiger Wert: ok false, code rejected, Meldung des Kerns).
struct SettingsUpdate
{
    qint64 requestId = 0;
    std::optional<int> port;
    std::optional<QString> homeAddress;
    // P10c: Verbindung zu OBS. obsPassword: fehlt = behalten, leer = löschen, sonst neu. Der Kern
    // schickt es nie zurück und schreibt es nie ins Protokoll (Regel 12).
    std::optional<bool> obsEnabled;
    std::optional<QString> obsHost;
    std::optional<int> obsPort;
    std::optional<QString> obsPassword;

    bool operator==(const SettingsUpdate &) const = default;
};
QJsonObject settingsUpdateMessage(const SettingsUpdate &update);
bool parseSettingsUpdate(const QJsonObject &message, SettingsUpdate *update, QString *error);
// reset_settings (P10, Punkt 77): nur request_id. Antwort settings an alle, dann change_result
// (bei ok mit warning = Hinweis auf die umbenannte alte Datei).
QJsonObject resetSettingsMessage(qint64 requestId);
bool parseResetSettings(const QJsonObject &message, qint64 *requestId, QString *error);

// browser_source (P10): Adresse der Browserquelle eines Overlays mit Token, nur an die Verbindung,
// die mit get_browser_source danach gefragt hat; danach nach jeder Änderung der Adresse (neuer
// Token, neue lesbare Adresse, Port, Adresse im Heimnetz) von selbst. url leer: keine Adresse
// möglich, Grund in problem.
struct BrowserSourceInfo
{
    QUuid overlayId;
    QString url;
    QString problem;

    bool operator==(const BrowserSourceInfo &) const = default;
};
QJsonObject browserSourceMessage(const BrowserSourceInfo &info);
bool parseBrowserSource(const QJsonObject &message, BrowserSourceInfo *info, QString *error);
// server_status (P10): Zustand des Overlay-Servers, nach hello und nach jeder Änderung an alle.
// Ohne Token. enabled false: Kern ohne Overlay-Server gebaut (Schalter DSS_CORE_OVERLAY_SERVER).
struct ServerStatusInfo
{
    bool enabled = false;
    QStringList listening;     // «127.0.0.1:8081», …
    QStringList problems;      // Port belegt, Adresse fehlt, … (auf Deutsch)
    QString invalidUse;        // Hinweis: eine Browserquelle benutzt eine ungültige Adresse; leer = keiner
    QHash<QUuid, int> pages;   // verbundene Seiten je Overlay

    bool operator==(const ServerStatusInfo &) const = default;
};
QJsonObject serverStatusMessage(const ServerStatusInfo &info);
bool parseServerStatus(const QJsonObject &message, ServerStatusInfo *info, QString *error);

// obs_status (P10c, T-05): Einzelheiten der Verbindung zu OBS, nach hello und nach jeder Änderung an
// alle. Der Zustand selbst (Farbe des Knopfes) steht in connections. Ohne Adresse und ohne Passwort.
struct ObsSceneInfo
{
    QString uuid;
    QString name;

    bool operator==(const ObsSceneInfo &) const = default;
};
struct ObsStatusInfo
{
    QString message;            // Beschreibung des Zustands auf Deutsch; leer, wenn verbunden
    QString obsVersion;         // leer, solange unbekannt
    QString websocketVersion;
    ObsSceneInfo currentScene;  // leer, solange nicht verbunden
    QList<ObsSceneInfo> scenes; // Reihenfolge wie in OBS

    bool operator==(const ObsStatusInfo &) const = default;
};
QJsonObject obsStatusMessage(const ObsStatusInfo &info);
bool parseObsStatus(const QJsonObject &message, ObsStatusInfo *info, QString *error);

// media_list (P10b, E-28): Namen und Ordner aller Mediendateien, nach hello, auf get_media_list
// und nach jeder Änderung an alle.
struct MediaFileData
{
    MediaEntry entry;
    MediaKind kind = MediaKind::Image;
    qint64 bytes = 0;
    QString codec;              // Video: Name in FFmpeg ("vp9", "hevc", …); leer = unbekannt
    QString browserProblem;     // Hinweis, wenn die Browserquelle die Datei nicht abspielt
    QList<MediaUsage> usedIn;   // leer = ungenutzt, darf gelöscht werden

    bool operator==(const MediaFileData &) const = default;
};
struct MediaListData
{
    QList<MediaFolder> folders;
    QList<MediaFileData> files;
    QString error;  // medien.json ungültig (Umbenennen und Ordner abgewiesen); leer = gültig

    const MediaFileData *file(const QString &reference) const;
    bool operator==(const MediaListData &) const = default;
};
QJsonObject mediaListMessage(const MediaListData &data);
bool parseMediaList(const QJsonObject &message, MediaListData *data, QString *error);

// import_progress (P10b, Punkt 73): alle laufenden Übernahmen, an alle, höchstens alle 200 ms,
// solange sich etwas ändert, und einmal leer, wenn die letzte fertig ist.
struct ImportProgressData
{
    qint64 importId = 0;  // für cancel_import
    QString name;         // ursprünglicher Dateiname
    QUuid folder;         // Ordner der Medienliste (add_media), null = oberste Ebene
    int percent = 0;      // 0 bis 100

    bool operator==(const ImportProgressData &) const = default;
};
QJsonObject importProgressMessage(const QList<ImportProgressData> &imports);
bool parseImportProgress(const QJsonObject &message, QList<ImportProgressData> *imports,
                         QString *error);

// get_browser_source: Kennung des Overlays; null = keine Adresse mehr schicken.
QJsonObject getBrowserSourceMessage(const QUuid &overlayId);
bool parseGetBrowserSource(const QJsonObject &message, QUuid *overlayId, QString *error);

} // namespace dss
