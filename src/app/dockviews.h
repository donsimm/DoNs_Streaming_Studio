#pragma once

#include "overlaylist.h"
#include "windowedit.h"

#include <QIcon>
#include <QListWidget>
#include <QTreeWidget>
#include <QUuid>

class QMimeData;

// Listen der Docks «Overlays» und «Quellen» (seit P10b auch «Medien») mit Ziehen per Maus (P08, Teile C und D). Sie
// verschieben beim Loslassen nichts selbst: Sie melden nur, was wohin gezogen wurde. Den neuen
// Stand zeigt das Fenster erst, wenn der Kern ihn an alle schickt (Teil A).

// Baum der Overlays: Overlays und Ordner auf der obersten Ebene, Overlays in Ordnern.
class OverlayTree : public QTreeWidget
{
    Q_OBJECT

public:
    // Daten eines Eintrags: Kennung und ob es ein Ordner ist.
    static constexpr int kIdRole = Qt::UserRole;
    static constexpr int kFolderRole = Qt::UserRole + 1;
    // P10 (E-24): Ausgabe ein/aus eines Overlays (bool); ohne Wert kein Schalter.
    static constexpr int kOutputRole = Qt::UserRole + 2;

    explicit OverlayTree(QWidget *parent = nullptr);

    // Symbole des Schalters «Ausgabe» (ein, aus); leer = kein Schalter.
    void setOutputIcons(const QIcon &on, const QIcon &off);
    // Fläche des Schalters eines Overlays: das letzte Stück des Einzugs links vom Eintrag
    // (Punkt 48); leer für Ordner und ohne Symbole.
    QRect outputSwitchRect(const QTreeWidgetItem *item) const;

signals:
    // Eintrag id (Overlay oder Ordner) auf target losgelassen; target ist bei EndOfList null.
    void itemDropped(const QUuid &id, bool isFolder, const QUuid &target, bool targetIsFolder,
                     dss::DropPosition position);
    // Klick auf den Schalter «Ausgabe» des Overlays id; die Auswahl bleibt.
    void outputClicked(const QUuid &id);

protected:
    void dropEvent(QDropEvent *event) override;
    void drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    bool onOutputSwitch(const QPoint &position, QUuid *id) const;

    QIcon m_outputOn;
    QIcon m_outputOff;
};

// Liste der Quellen, vorderste oben. Klick auf das Auge schaltet die Sichtbarkeit um.
class SourceList : public QListWidget
{
    Q_OBJECT

public:
    static constexpr int kIdRole = Qt::UserRole;

    explicit SourceList(QWidget *parent = nullptr);

    // P10b (Teil B 6): Dateien aus dem Dock «Medien» annehmen (Ziehen mit Einfügestrich).
    void setAcceptsMedia(bool accept);

signals:
    void visibilityClicked(const QUuid &id);
    // Quelle id an der Einfügestelle insertRow losgelassen (0 = über der obersten Zeile).
    void itemDropped(const QUuid &id, int insertRow);
    // P10b: Datei reference (Bild oder Video) aus dem Dock «Medien» an der Einfügestelle insertRow.
    void mediaDropped(const QString &reference, int insertRow);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    QStringList mimeTypes() const override;

private:
    int insertRowAt(const QPoint &position) const;

    bool m_acceptsMedia = false;
};

// P10b (E-28): Format beim Ziehen aus dem Dock «Medien». Inhalt: der Verweis "media/…" (UTF-8);
// nur Dateien, keine Ordner. Ordner ziehen nur innerhalb des Docks (kMediaFolderMimeType, leer).
inline constexpr char kMediaMimeType[] = "application/x-dss-media";
inline constexpr char kMediaFolderMimeType[] = "application/x-dss-media-folder";
// Verweis eines Bildes oder Videos aus dem Dock «Medien»; leer bei anderem Inhalt (auch Schriften).
QString droppedMediaSource(const QMimeData *mime);

// Baum des Docks «Medien» (P10b, Teil B): Ordner, Dateien und laufende Übernahmen in den Spalten
// Name (mit Vorschau), Typ, Grösse; Sortieren per Klick auf den Spaltenkopf (Ordner zuerst).
// Wie die Overlay-Liste verschiebt er beim Loslassen nichts selbst, sondern meldet es nur.
class MediaTree : public QTreeWidget
{
    Q_OBJECT

public:
    static constexpr int kFileRole = Qt::UserRole;        // Verweis "media/…" einer Datei
    static constexpr int kFolderRole = Qt::UserRole + 1;  // Kennung eines Ordners
    static constexpr int kImportRole = Qt::UserRole + 2;  // import_id einer laufenden Übernahme
    enum Column { NameColumn = 0, KindColumn = 1, SizeColumn = 2 };

    explicit MediaTree(QWidget *parent = nullptr);

    // Eintrag mit Schlüssel zum Sortieren (dss::mediaRowBefore)
    static QTreeWidgetItem *createItem(const dss::MediaSortKey &key);

signals:
    // Gezogen: Ordner draggedFolder oder Datei draggedFile; Ziel wie in dss::mediaDropFolder.
    void itemDropped(const QUuid &draggedFolder, const QString &draggedFile, bool targetIsFolder,
                     const QUuid &targetFolder, const QString &targetFile, dss::DropPosition position);
    // F2 auf dem gewählten Eintrag (Doppelklick: itemDoubleClicked)
    void renameRequested(QTreeWidgetItem *item);

protected:
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
};
