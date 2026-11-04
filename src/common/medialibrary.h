#pragma once

#include "overlay.h"

#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <optional>

// Namen und Ordner der Mediendateien (P10b, E-28, Teil A 1). Nur Qt Core (CLAUDE.md Regel 9).
//
// Die Dateien in <Datenordner>/media heissen nach dem SHA-256 ihres Inhalts (K-03); der Name, den
// der Nutzer sieht, und die Einteilung in Ordner stehen in einer eigenen Datei des Kerns,
// <Datenordner>/medien.json. Ordner sind nur eine Einteilung: Die Dateien bleiben in media/, kein
// Verweis eines Overlays ändert sich. Format: docs/06-overlay-format.md, «medien.json».

namespace dss {

// Version des Formats von medien.json, die dieses Programm schreibt und lesen kann.
inline constexpr int kMediaLibraryFormatVersion = 1;
inline constexpr char kMediaLibraryFileName[] = "medien.json";

// Art einer Mediendatei, aus der Endung des Verweises (docs/06-overlay-format.md, «media/»).
enum class MediaKind { Image, Video, Font };
std::optional<MediaKind> mediaKindOf(const QString &reference);
// «Bild», «Video», «Schrift»
QString mediaKindName(MediaKind kind);
// Art aus dem Namen im Protokoll und zurück: "image", "video", "font"
QString mediaKindKey(MediaKind kind);
std::optional<MediaKind> mediaKindFromKey(const QString &key);

struct MediaFolder
{
    QUuid id;
    QString name;
    QUuid parent;          // null = oberste Ebene
    bool expanded = true;  // im Dock aufgeklappt

    bool operator==(const MediaFolder &) const = default;
};

struct MediaEntry
{
    QString file;   // Verweis "media/<SHA-256>.<Endung>"
    QString name;   // Name im Dock, zum Beispiel der ursprüngliche Dateiname
    QUuid folder;   // null = oberste Ebene

    bool operator==(const MediaEntry &) const = default;
};

struct MediaLibrary
{
    QList<MediaFolder> folders; // Reihenfolge des Anlegens; das Dock sortiert selbst
    QList<MediaEntry> files;    // Reihenfolge des Übernehmens

    bool operator==(const MediaLibrary &) const = default;

    const MediaFolder *folder(const QUuid &id) const;
    const MediaEntry *entry(const QString &file) const;
    // Ordner und alle Unterordner (ohne id selbst)
    QList<QUuid> descendants(const QUuid &id) const;
};

// medien.json. Liest streng wie liste.json: fehlende, unbekannte oder ungültige Felder, doppelte
// Kennungen oder Dateien, Eltern, die es nicht gibt, Schleifen und eine unbekannte format_version
// machen die Datei ungültig. Gleiche Namen sind erlaubt (wie im Dateimanager in verschiedenen
// Ordnern, hier auch im selben Ordner: Der Name ist nur eine Beschriftung).
QByteArray serializeMediaLibrary(const MediaLibrary &library);
bool parseMediaLibrary(const QByteArray &data, MediaLibrary *library, QString *error);

// Eine Datei in media/ und der Zeitpunkt der letzten Änderung (= Übernahme: Die Datei wird beim
// Übernehmen geschrieben und danach nie geändert; Sicherungen mit «cp -a» behalten ihn).
struct MediaFileInfo
{
    QString reference;
    QDateTime modified;
    qint64 bytes = 0;
};
// Alle regulären Dateien in mediaDir, deren Name die Form <SHA-256>.<erlaubte Endung> hat.
// Andere Dateien (vorläufige Dateien von QSaveFile, fremde Dateien) gehören nicht dazu.
QList<MediaFileInfo> scanMediaDir(const QString &mediaDir);

// Ersatzname für eine Datei ohne Namen: «Bild vom 2026-11-02» (Datum der Übernahme, lokale Zeit).
QString substituteMediaName(MediaKind kind, const QDate &date);

// Bringt die Datei in Einklang mit media/ (Teil A 1): Einträge zu Dateien, die fehlen, fallen
// weg; Dateien ohne Eintrag kommen mit Ersatzname auf die oberste Ebene, in der Reihenfolge ihres
// Datums. Gleiche Ersatznamen bekommen « (2)», « (3)», … in dieser Reihenfolge. *added (falls
// nicht nullptr) bekommt die Verweise der neuen Einträge. true, wenn sich etwas geändert hat.
bool reconcileMediaLibrary(MediaLibrary *library, const QList<MediaFileInfo> &present,
                           QStringList *added = nullptr, QStringList *dropped = nullptr);

// Änderungen. Leerer Text = erfolgreich, sonst eine Meldung auf Deutsch; bei einem Fehler bleibt
// die Datei unverändert.
QString validateMediaName(const QString &name);
// Neue Datei mit Name in folder. Gibt es die Datei schon (gleicher Inhalt nochmals hinzugefügt),
// bleiben ihr Name und ihr Ordner (Teil A 1).
QString addMediaToLibrary(MediaLibrary *library, const QString &file, const QString &name,
                          const QUuid &folder);
QString renameMediaInLibrary(MediaLibrary *library, const QString &file, const QString &name);
QString moveMediaFileInLibrary(MediaLibrary *library, const QString &file, const QUuid &folder);
QString removeMediaFromLibrary(MediaLibrary *library, const QString &file);
QString createMediaFolderInLibrary(MediaLibrary *library, const QUuid &id, const QString &name,
                                   const QUuid &parent);
QString renameMediaFolderInLibrary(MediaLibrary *library, const QUuid &id, const QString &name);
// Ordner löschen: Dateien und Unterordner kommen eine Ebene höher (in den Elternordner), wie
// bei der Overlay-Liste; es wird nie eine Datei gelöscht.
QString removeMediaFolderFromLibrary(MediaLibrary *library, const QUuid &id);
// Ordner in parent (null = oberste Ebene) verschieben; nicht in sich selbst oder einen Unterordner.
QString moveMediaFolderInLibrary(MediaLibrary *library, const QUuid &id, const QUuid &parent);
QString setMediaFolderExpandedInLibrary(MediaLibrary *library, const QUuid &id, bool expanded);

// Grösse für die Anzeige: «512 Byte», «12.3 KiB», «4.5 MiB», «1.2 GiB» (Faktor 1024).
QString mediaSizeText(qint64 bytes);

// Für dss-cli (Teil A 6): Pfad eines Ordners «Ordner/Unterordner», leer für die oberste Ebene.
QString mediaFolderPath(const MediaLibrary &library, const QUuid &folder);
// Ordner nach Pfad wie mediaFolderPath; "" und "/" = oberste Ebene (null). nullopt mit Meldung,
// wenn es ihn nicht gibt oder der Pfad mehrdeutig ist (gleiche Namen).
std::optional<QUuid> findMediaFolder(const MediaLibrary &library, const QString &path,
                                     QString *error);
// Dateien in der Reihenfolge der Anzeige: nach Pfad des Ordners, dann Name (ohne Gross- und
// Kleinschreibung), dann Verweis.
QList<MediaEntry> mediaFilesInOrder(const MediaLibrary &library);
// Datei nach Nummer (1 = erste in mediaFilesInOrder), Verweis "media/…" oder Name (muss eindeutig
// sein). Leer mit Meldung, wenn es keine oder mehrere gibt.
QString findMediaFile(const MediaLibrary &library, const QString &text, QString *error);

// Wo eine Datei verwendet wird (Teil A 2): Bild- und Videoquellen über file, Textquellen über
// ihre Schriftdatei.
struct MediaUsage
{
    QUuid overlayId;
    QString overlayName;
    QUuid sourceId;
    QString sourceName;

    bool operator==(const MediaUsage &) const = default;
};
using MediaUsages = QHash<QString, QList<MediaUsage>>; // Verweis → Verwendungen
MediaUsages mediaUsages(const QList<Overlay> &overlays);
// «verwendet in Overlay «A» (Quelle «x», Quelle «y»), Overlay «B» (Quelle «z»)»; leer, wenn keine
QString usageText(const QList<MediaUsage> &usages);

} // namespace dss
