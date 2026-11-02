#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUuid>

// Reihenfolge und Ordner der Overlay-Liste (P08, Teil B; docs/01-festlegungen.md,
// «Overlay-Liste»). Nur Qt Core (CLAUDE.md Regel 9).
//
// Die Liste steht in einer eigenen Datei des Kerns, <Datenordner>/liste.json, nicht in den
// Overlay-Dateien. Ordner haben eine Ebene: Ein Ordner enthält nur Overlays, keine Ordner.
// Die Adresse eines Overlays hängt nicht vom Ordner ab. Format: docs/06-overlay-format.md

namespace dss {

// Version des Formats von liste.json, die dieses Programm schreibt und lesen kann.
inline constexpr int kListFormatVersion = 1;
inline constexpr char kListFileName[] = "liste.json";

struct ListFolder
{
    QUuid id;
    QString name;
    bool expanded = true;      // in der Liste aufgeklappt
    QList<QUuid> overlays;     // Reihenfolge der Overlays im Ordner

    bool operator==(const ListFolder &) const = default;
};

// Eintrag der obersten Ebene: ein Overlay oder ein Ordner.
struct ListEntry
{
    bool isFolder = false;
    QUuid overlayId;   // nur bei !isFolder
    ListFolder folder; // nur bei isFolder

    bool operator==(const ListEntry &) const = default;
};

struct OverlayLayout
{
    QList<ListEntry> entries; // Reihenfolge der obersten Ebene, oben zuerst

    bool operator==(const OverlayLayout &) const = default;

    // Alle Overlays in der Reihenfolge der Liste (Inhalt eines Ordners an seiner Stelle).
    QList<QUuid> overlayIds() const;
    // Index des Ordners in entries, -1 wenn es ihn nicht gibt.
    int folderIndex(const QUuid &folderId) const;
    const ListFolder *folder(const QUuid &folderId) const;
    // Lage eines Overlays: Ordner (null = oberste Ebene) und Index darin. false, wenn es fehlt.
    bool findOverlay(const QUuid &overlayId, QUuid *folderId, int *index) const;
};

// Liste nach Name: alle Overlays auf der obersten Ebene, in der übergebenen Reihenfolge.
OverlayLayout layoutFromIds(const QList<QUuid> &overlayIds);

// Bringt die Liste in Einklang mit den vorhandenen Overlays: Einträge zu Overlays, die es
// nicht gibt, werden entfernt; Overlays, die fehlen, kommen in der Reihenfolge von existing
// ans Ende der obersten Ebene. Gibt true zurück, wenn sich etwas geändert hat.
bool normalizeLayout(OverlayLayout *layout, const QList<QUuid> &existing);

// Datei liste.json. Liest streng wie die Overlay-Dateien: fehlende, unbekannte oder ungültige
// Felder, doppelte Kennungen und eine unbekannte format_version machen die Datei ungültig.
QJsonObject layoutToJson(const OverlayLayout &layout);
bool layoutFromJson(const QJsonObject &object, OverlayLayout *layout, QString *error);
QByteArray serializeLayout(const OverlayLayout &layout);
bool parseLayout(const QByteArray &data, OverlayLayout *layout, QString *error);

// Änderungen. Leerer Text = erfolgreich, sonst eine Meldung auf Deutsch; bei einem Fehler
// bleibt die Liste unverändert.
QString validateFolderName(const QString &name);
// Neues Overlay direkt nach after (im selben Ordner), ohne after oder wenn after fehlt ans Ende
// der obersten Ebene.
QString addOverlayToLayout(OverlayLayout *layout, const QUuid &overlayId, const QUuid &after = {});
QString removeOverlayFromLayout(OverlayLayout *layout, const QUuid &overlayId);
// Verschiebt ein Overlay in den Ordner folderId (null = oberste Ebene) an die Stelle index.
// index zählt in der Liste des Ziels ohne das verschobene Overlay (0 = zuoberst, Anzahl = ans
// Ende).
QString moveOverlayInLayout(OverlayLayout *layout, const QUuid &overlayId, const QUuid &folderId,
                            int index);
// Verschiebt einen Ordner auf der obersten Ebene; index wie oben.
QString moveFolderInLayout(OverlayLayout *layout, const QUuid &folderId, int index);
// Neuer, leerer, aufgeklappter Ordner am Ende der obersten Ebene.
QString createFolderInLayout(OverlayLayout *layout, const QUuid &folderId, const QString &name);
QString renameFolderInLayout(OverlayLayout *layout, const QUuid &folderId, const QString &name);
// Löscht den Ordner; seine Overlays kommen in ihrer Reihenfolge an seine Stelle auf der
// obersten Ebene (sie werden nicht gelöscht).
QString removeFolderFromLayout(OverlayLayout *layout, const QUuid &folderId);
QString setFolderExpandedInLayout(OverlayLayout *layout, const QUuid &folderId, bool expanded);

// Ziehen mit der Maus (Teil C): Wo ein Eintrag landet, wenn er über, unter oder auf einem
// anderen Eintrag losgelassen wird. Das Fenster übersetzt nur die Anzeige von Qt in diese Werte.
enum class DropPosition { Above, Below, OnItem, EndOfList };

// Ziel für ein gezogenes Overlay. target ist ein Overlay oder ein Ordner (targetIsFolder),
// bei EndOfList ohne Bedeutung. Auf einem Ordner losgelassen: ans Ende des Ordners. false, wenn
// sich nichts ändert oder das Ziel nicht gilt. *index wie bei moveOverlayInLayout.
bool overlayDropTarget(const OverlayLayout &layout, const QUuid &overlayId, const QUuid &target,
                       bool targetIsFolder, DropPosition position, QUuid *folderId, int *index);
// Ziel für einen gezogenen Ordner. Ordner bleiben auf der obersten Ebene: Auf einem Overlay in
// einem Ordner losgelassen gilt nicht (Ordner in Ordner ist nicht möglich).
bool folderDropTarget(const OverlayLayout &layout, const QUuid &folderId, const QUuid &target,
                      bool targetIsFolder, DropPosition position, int *index);

} // namespace dss
