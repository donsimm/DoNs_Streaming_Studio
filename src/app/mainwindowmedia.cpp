// Dock «Medien» (P10b, E-28, Schalter DSS_APP_MEDIA_DOCK): zeigt die Medienliste des Kerns
// (media_list, import_progress) und schickt Änderungen. Gerechnet wird in dss_common
// (windowedit.h: Sortieren, Ziel beim Ziehen, Zeile zur Auswahl); hier wird nur gefüllt, gefragt
// und geschickt (CLAUDE.md Regel 9). Den neuen Stand zeigt das Dock erst, wenn der Kern ihn an
// alle schickt.

#include "mainwindow.h"

#include "canvaswidget.h"
#include "coreclient.h"
#include "dialogs.h"
#include "dockviews.h"
#include "medialibrary.h"
#include "windowedit.h"

#if DSS_APP_RENDERER
#include "mediapreview.h"
#endif

#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QPixmap>
#include <QScrollBar>
#include <QStyle>
#include <QToolButton>

#ifndef DSS_APP_EDITING
#define DSS_APP_EDITING 1
#endif

namespace {

// Grösse der Vorschau in der Spalte «Name» (Teil B 1: kleines Vorschaubild)
constexpr QSize kPreviewSize(40, 24);

dss::ChangeRequest request(const char *type)
{
    dss::ChangeRequest r;
    r.type = QString::fromLatin1(type);
    return r;
}

void setRowFont(QTreeWidgetItem *item, bool bold, bool italic)
{
    for (int c = 0; c < item->columnCount(); ++c) {
        QFont f = item->font(c);
        f.setBold(bold);
        f.setItalic(italic);
        item->setFont(c, f);
    }
}

void setRowToolTip(QTreeWidgetItem *item, const QString &text)
{
    for (int c = 0; c < item->columnCount(); ++c)
        item->setToolTip(c, text);
}

} // namespace

void MainWindow::setupMediaDock()
{
    m_mediaDock = ::createMediaDock(this);
    m_mediaDock.tree->setIconSize(kPreviewSize);
#if DSS_APP_RENDERER
    m_mediaPreviews = new dss::MediaPreviewLoader(kPreviewSize, m_theme.text, this);
    connect(m_mediaPreviews, &dss::MediaPreviewLoader::previewReady, this, &MainWindow::onMediaPreview);
#endif
}

void MainWindow::connectMedia()
{
    MediaTree *tree = m_mediaDock.tree;
    connect(m_client, &dss::CoreClient::mediaListChanged, this, &MainWindow::refreshMedia);
    connect(m_client, &dss::CoreClient::importProgressChanged, this, &MainWindow::refreshMediaImports);
    connect(tree, &QTreeWidget::currentItemChanged, this, &MainWindow::onMediaItemChanged);
    refreshMedia();
    if (!editingAllowed()) {
        tree->setDragDropMode(QAbstractItemView::NoDragDrop);
        return;
    }
    connect(m_mediaDock.add, &QToolButton::clicked, this, &MainWindow::addMediaFiles);
    connect(m_mediaDock.addFolder, &QToolButton::clicked, this, &MainWindow::createMediaFolder);
    connect(m_mediaDock.remove, &QToolButton::clicked, this, &MainWindow::removeMediaItem);
    connect(tree, &QTreeWidget::itemDoubleClicked, this, &MainWindow::renameMediaItem);
    connect(tree, &MediaTree::renameRequested, this, &MainWindow::renameMediaItem);
    connect(tree, &QWidget::customContextMenuRequested, this, &MainWindow::showMediaMenu);
    connect(tree, &MediaTree::itemDropped, this, &MainWindow::onMediaDropped);
    connect(tree, &QTreeWidget::itemExpanded, this,
            [this](QTreeWidgetItem *item) { onMediaFolderExpanded(item, true); });
    connect(tree, &QTreeWidget::itemCollapsed, this,
            [this](QTreeWidgetItem *item) { onMediaFolderExpanded(item, false); });
    // Ziehen ins Overlay (Teil B 6)
    m_canvas->setAcceptsMedia(true);
    m_sourcesDock.list->setAcceptsMedia(true);
    connect(m_canvas, &CanvasWidget::mediaDropped, this,
            [this](const QString &reference, const QPoint &center) {
                addSourceFromMedia(reference, center, std::nullopt);
            });
    connect(m_sourcesDock.list, &SourceList::mediaDropped, this,
            [this](const QString &reference, int insertRow) {
                const std::optional<dss::Overlay> overlay = selectedOverlay();
                if (!overlay)
                    return;
                addSourceFromMedia(reference, QPoint(overlay->width / 2, overlay->height / 2),
                                   dss::newSourceIndex(*overlay, insertRow));
            });
}

void MainWindow::refreshMedia()
{
    // Baum neu füllen, wie der Kern die Liste schickt, und die Auswahl über Verweis, Kennung des
    // Ordners bzw. der Übernahme behalten.
    MediaTree *tree = m_mediaDock.tree;
    const dss::MediaListData &data = m_client->mediaList();
    const QList<dss::ImportProgressData> imports = m_client->imports();
    m_filling = true;
    const int scroll = tree->verticalScrollBar()->value();
    tree->clear();
    QHash<QUuid, QTreeWidgetItem *> folders;
    QTreeWidgetItem *selected = nullptr;
    const QIcon folderIcon = style()->standardIcon(QStyle::SP_DirIcon);
    const QIcon warningIcon = style()->standardIcon(QStyle::SP_MessageBoxWarning);
    QPixmap empty(kPreviewSize);
    empty.fill(Qt::transparent);
    const QIcon placeholder(empty); // gleiche Zeilenhöhe, bevor die Vorschau da ist
    dss::MediaLibrary library;
    library.folders = data.folders;
    const Qt::ItemFlags movable = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled
                                  | Qt::ItemIsDropEnabled;

    for (const dss::MediaFolder &f : data.folders) {
        QTreeWidgetItem *item = MediaTree::createItem(
            {true, f.name, QStringLiteral("Ordner"), 0, f.id.toString(QUuid::WithoutBraces)});
        item->setText(MediaTree::NameColumn, f.name);
        item->setText(MediaTree::KindColumn, QStringLiteral("Ordner"));
        item->setIcon(MediaTree::NameColumn, folderIcon);
        item->setData(MediaTree::NameColumn, MediaTree::kFolderRole, f.id);
        item->setFlags(movable);
        // Ordner zeigen das Aufklappzeichen auch leer, damit klar ist, dass etwas hineinpasst.
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        setRowFont(item, true, false);
        setRowToolTip(item, QStringLiteral("Ordner «%1»").arg(dss::mediaFolderPath(library, f.id)));
        folders.insert(f.id, item);
        if (f.id == m_selectedMediaFolder)
            selected = item;
    }
    const auto place = [&](QTreeWidgetItem *item, const QUuid &folder) {
        if (QTreeWidgetItem *parent = folders.value(folder))
            parent->addChild(item);
        else
            tree->addTopLevelItem(item);
    };
    for (const dss::MediaFolder &f : data.folders)
        place(folders.value(f.id), f.parent);

    for (const dss::MediaFileData &f : data.files) {
        const QString kind = dss::mediaKindName(f.kind);
        QTreeWidgetItem *item = MediaTree::createItem({false, f.entry.name, kind, f.bytes, f.entry.file});
        const bool unused = f.usedIn.isEmpty();
        item->setText(MediaTree::NameColumn, f.entry.name);
        item->setText(MediaTree::KindColumn, kind);
        item->setText(MediaTree::SizeColumn, dss::mediaSizeText(f.bytes));
        item->setTextAlignment(MediaTree::SizeColumn, Qt::AlignRight | Qt::AlignVCenter);
        QIcon preview = placeholder;
#if DSS_APP_RENDERER
        if (const QImage image = m_mediaPreviews->preview(f.entry.file); !image.isNull())
            preview = QIcon(QPixmap::fromImage(image));
#endif
        item->setIcon(MediaTree::NameColumn, preview);
        // Teil A 4: Warnzeichen, wenn die Browserquelle das Video nicht abspielt
        if (!f.browserProblem.isEmpty())
            item->setIcon(MediaTree::KindColumn, warningIcon);
        item->setData(MediaTree::NameColumn, MediaTree::kFileRole, f.entry.file);
        item->setFlags(movable);
        // Ungenutzte Dateien (Teil B 1, auch Kopien aus Punkt 74): kursiv, «Ungenutzt» im Hinweis
        // und in der Zeile unter der Liste. Eine Farbe ginge nicht: Das Stylesheet setzt die Farbe
        // aller Einträge (QTreeView::item) und hat Vorrang vor der Farbe des Eintrags.
        setRowFont(item, false, unused);
        setRowToolTip(item, dss::mediaFileToolTip(f));
        place(item, f.entry.folder);
        if (f.entry.file == m_selectedMediaFile)
            selected = item;
    }

    m_shownImports.clear();
    for (const dss::ImportProgressData &i : imports) {
        const std::optional<dss::MediaKind> kind = dss::mediaKindOf(i.name);
        const QString kindText = kind ? dss::mediaKindName(*kind) : QString();
        QTreeWidgetItem *item = MediaTree::createItem(
            {false, i.name, kindText, 0, QStringLiteral("~%1").arg(i.importId)});
        item->setText(MediaTree::NameColumn, i.name);
        item->setText(MediaTree::KindColumn, kindText);
        item->setText(MediaTree::SizeColumn, dss::importProgressText(i));
        item->setTextAlignment(MediaTree::SizeColumn, Qt::AlignRight | Qt::AlignVCenter);
        item->setIcon(MediaTree::NameColumn, placeholder);
        item->setData(MediaTree::NameColumn, MediaTree::kImportRole, i.importId);
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
        setRowFont(item, false, true);
        setRowToolTip(item, QStringLiteral("«%1» wird übernommen. «Löschen» bricht das Übernehmen ab.").arg(i.name));
        place(item, i.folder);
        m_shownImports.append(i.importId);
        if (i.importId == m_selectedImport)
            selected = item;
    }

    for (const dss::MediaFolder &f : data.folders)
        folders.value(f.id)->setExpanded(f.expanded);
    tree->sortItems(tree->sortColumn(), tree->header()->sortIndicatorOrder());
    if (selected) {
        // Ohne autoScroll: QTreeView::scrollTo klappt sonst den Ordner der Auswahl auf, und der
        // Baum zeigt etwas anderes als medien.json (https://doc.qt.io/qt-6.8/qtreeview.html#scrollTo).
        const bool autoScroll = tree->hasAutoScroll();
        tree->setAutoScroll(false);
        tree->setCurrentItem(selected);
        tree->setAutoScroll(autoScroll);
    } else {
        m_selectedMediaFile.clear();
        m_selectedMediaFolder = QUuid();
        m_selectedImport = 0;
    }
    tree->verticalScrollBar()->setValue(scroll);
    m_filling = false;
    // Ungültige medien.json: einmal melden (Umbenennen und Ordner lassen sich dann nicht ändern).
    if (data.error != m_lastMediaError) {
        m_lastMediaError = data.error;
        if (!data.error.isEmpty())
            showStatus(data.error);
    }
    refreshMediaSelection();
}

void MainWindow::refreshMediaImports()
{
    // Nur der Stand in Prozent hat sich geändert: Zeilen nachführen, nicht neu füllen (der Kern
    // schickt ihn alle 200 ms; Ziehen und Auswahl im Dock bleiben ungestört).
    const QList<dss::ImportProgressData> imports = m_client->imports();
    QList<qint64> ids;
    for (const dss::ImportProgressData &i : imports)
        ids.append(i.importId);
    if (ids != m_shownImports) {
        refreshMedia();
        return;
    }
    for (QTreeWidgetItemIterator it(m_mediaDock.tree); *it; ++it) {
        const qint64 id = (*it)->data(MediaTree::NameColumn, MediaTree::kImportRole).toLongLong();
        for (const dss::ImportProgressData &i : imports) {
            if (id != 0 && i.importId == id)
                (*it)->setText(MediaTree::SizeColumn, dss::importProgressText(i));
        }
    }
    refreshMediaSelection();
}

void MainWindow::onMediaPreview(const QString &reference)
{
#if DSS_APP_RENDERER
    const QImage image = m_mediaPreviews->preview(reference);
    if (image.isNull())
        return;
    const QIcon icon(QPixmap::fromImage(image));
    for (QTreeWidgetItemIterator it(m_mediaDock.tree); *it; ++it) {
        if ((*it)->data(MediaTree::NameColumn, MediaTree::kFileRole).toString() == reference)
            (*it)->setIcon(MediaTree::NameColumn, icon);
    }
#else
    Q_UNUSED(reference)
#endif
}

void MainWindow::onMediaItemChanged(QTreeWidgetItem *current)
{
    if (m_filling)
        return;
    m_selectedMediaFile = current ? current->data(MediaTree::NameColumn, MediaTree::kFileRole).toString() : QString();
    m_selectedMediaFolder = current ? current->data(MediaTree::NameColumn, MediaTree::kFolderRole).toUuid() : QUuid();
    m_selectedImport = current ? current->data(MediaTree::NameColumn, MediaTree::kImportRole).toLongLong() : 0;
    refreshMediaSelection();
}

void MainWindow::refreshMediaSelection()
{
    const dss::MediaSelectionInfo info =
        dss::mediaSelectionInfo(m_client->mediaList(), m_client->imports(), m_selectedMediaFile,
                                m_selectedMediaFolder, m_selectedImport);
    m_mediaDock.info->setText(info.line);
    m_mediaDock.info->setToolTip(info.line);
    const bool edit = editingAllowed() && m_client->isConnected();
    m_mediaDock.add->setEnabled(edit);
    m_mediaDock.addFolder->setEnabled(edit && m_client->mediaList().error.isEmpty());
    m_mediaDock.remove->setEnabled(edit && info.removable);
    m_mediaDock.remove->setToolTip(info.removeToolTip);
}

QUuid MainWindow::mediaTargetFolder() const
{
    // Gewählter Ordner, sonst der Ordner der gewählten Datei bzw. Übernahme, sonst oben
    if (!m_selectedMediaFolder.isNull())
        return m_selectedMediaFolder;
    const dss::MediaListData &data = m_client->mediaList();
    if (const dss::MediaFileData *f = data.file(m_selectedMediaFile))
        return f->entry.folder;
    for (const dss::ImportProgressData &i : m_client->imports()) {
        if (i.importId == m_selectedImport)
            return i.folder;
    }
    return {};
}

void MainWindow::addMediaFiles()
{
    // Mehrere Dateien in den gewählten Ordner, ohne Quelle (Teil B 4). Der Kern übernimmt sie
    // nacheinander im eigenen Thread und zeigt den Fortschritt (Teil A 3).
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("Dateien hinzufügen"), m_lastMediaDir, dss::mediaLibraryFileFilter());
    if (files.isEmpty())
        return;
    m_lastMediaDir = QFileInfo(files.first()).absolutePath();
    const QUuid folder = mediaTargetFolder();
    for (const QString &file : files) {
        dss::ChangeRequest r = request(dss::msg::kAddMedia);
        r.file = file;
        r.folderId = folder;
        sendChange(r);
    }
}

void MainWindow::createMediaFolder()
{
    QString name = dss::newFolderName();
    if (!askText(this, QStringLiteral("Ordner anlegen"), QStringLiteral("Name des Ordners"), &name))
        return;
    dss::ChangeRequest r = request(dss::msg::kCreateMediaFolder);
    r.name = name;
    r.parentId = mediaTargetFolder();
    sendChange(r);
}

void MainWindow::renameMediaItem(QTreeWidgetItem *item)
{
    if (!item || !m_client->isConnected())
        return;
    // Kopien: Während der Dialog offen ist, kann eine neue Liste kommen.
    const QString file = item->data(MediaTree::NameColumn, MediaTree::kFileRole).toString();
    const QUuid folderId = item->data(MediaTree::NameColumn, MediaTree::kFolderRole).toUuid();
    dss::MediaLibrary library;
    library.folders = m_client->mediaList().folders;
    if (const dss::MediaFileData *found = m_client->mediaList().file(file)) {
        const QString old = found->entry.name;
        QString name = old;
        if (!askText(this, QStringLiteral("Datei umbenennen"), QStringLiteral("Neuer Name"), &name) || name == old)
            return;
        dss::ChangeRequest r = request(dss::msg::kRenameMedia);
        r.file = file;
        r.name = name;
        sendChange(r);
    } else if (const dss::MediaFolder *folder = library.folder(folderId)) {
        const QString old = folder->name;
        QString name = old;
        if (!askText(this, QStringLiteral("Ordner umbenennen"), QStringLiteral("Neuer Name"), &name) || name == old)
            return;
        dss::ChangeRequest r = request(dss::msg::kRenameMediaFolder);
        r.folderId = folderId;
        r.name = name;
        sendChange(r);
    }
}

void MainWindow::removeMediaItem()
{
    const dss::MediaSelectionInfo info =
        dss::mediaSelectionInfo(m_client->mediaList(), m_client->imports(), m_selectedMediaFile,
                                m_selectedMediaFolder, m_selectedImport);
    if (!info.removable)
        return;
    if (m_selectedImport > 0) {
        // Abbrechen ohne Rückfrage: Es geht nur die Kopie verloren, die Datei des Nutzers bleibt.
        dss::ChangeRequest r = request(dss::msg::kCancelImport);
        r.importId = m_selectedImport;
        sendChange(r);
        return;
    }
    dss::MediaLibrary library;
    library.folders = m_client->mediaList().folders;
    if (const dss::MediaFolder *found = library.folder(m_selectedMediaFolder)) {
        const dss::MediaFolder folder = *found;
        const QString parent = folder.parent.isNull()
                                   ? QStringLiteral("auf die oberste Ebene")
                                   : QStringLiteral("in «%1»").arg(dss::mediaFolderPath(library, folder.parent));
        if (!confirm(this, QStringLiteral("Ordner löschen?"),
                     QStringLiteral("Ordner «%1» löschen?\n\nDie Dateien und Ordner darin bleiben "
                                    "erhalten und kommen %2.")
                         .arg(folder.name, parent),
                     QStringLiteral("Löschen"), QStringLiteral("Abbrechen")))
            return;
        dss::ChangeRequest r = request(dss::msg::kRemoveMediaFolder);
        r.folderId = folder.id;
        sendChange(r);
        return;
    }
    const dss::MediaFileData *found = m_client->mediaList().file(m_selectedMediaFile);
    if (!found)
        return;
    const QString name = found->entry.name;
    const QString file = found->entry.file;
    if (!confirm(this, QStringLiteral("Datei löschen?"),
                 QStringLiteral("«%1» löschen?\n\nDie Datei wird aus dem Datenordner gelöscht. Das "
                                "lässt sich nicht rückgängig machen.")
                     .arg(name),
                 QStringLiteral("Löschen"), QStringLiteral("Abbrechen")))
        return;
    dss::ChangeRequest r = request(dss::msg::kRemoveMedia);
    r.file = file;
    sendChange(r);
}

void MainWindow::showMediaMenu(const QPoint &position)
{
    if (!m_client->isConnected())
        return;
    QTreeWidgetItem *item = m_mediaDock.tree->itemAt(position);
    if (item)
        m_mediaDock.tree->setCurrentItem(item);
    QMenu menu(this);
    if (item && item->data(MediaTree::NameColumn, MediaTree::kImportRole).toLongLong() == 0) {
        menu.addAction(QStringLiteral("Umbenennen …"), this, [this, item] { renameMediaItem(item); });
    }
    QAction *remove = menu.addAction(m_mediaDock.remove->toolTip(), this, &MainWindow::removeMediaItem);
    remove->setEnabled(m_mediaDock.remove->isEnabled());
    menu.addSeparator();
    menu.addAction(QStringLiteral("Dateien hinzufügen …"), this, &MainWindow::addMediaFiles);
    menu.addAction(QStringLiteral("Ordner anlegen …"), this, &MainWindow::createMediaFolder)
        ->setEnabled(m_mediaDock.addFolder->isEnabled());
    menu.exec(m_mediaDock.tree->viewport()->mapToGlobal(position));
}

void MainWindow::onMediaDropped(const QUuid &draggedFolder, const QString &draggedFile,
                                bool targetIsFolder, const QUuid &targetFolder,
                                const QString &targetFile, dss::DropPosition position)
{
    const std::optional<QUuid> destination =
        dss::mediaDropFolder(m_client->mediaList(), draggedFolder, draggedFile, targetIsFolder,
                             targetFolder, targetFile, position);
    if (!destination)
        return;
    if (!draggedFolder.isNull()) {
        dss::ChangeRequest r = request(dss::msg::kMoveMediaFolder);
        r.folderId = draggedFolder;
        r.parentId = *destination;
        sendChange(r);
        return;
    }
    dss::ChangeRequest r = request(dss::msg::kMoveMedia);
    r.file = draggedFile;
    r.folderId = *destination;
    sendChange(r);
}

void MainWindow::onMediaFolderExpanded(QTreeWidgetItem *item, bool expanded)
{
    // Auf- und Zuklappen wird gespeichert (medien.json), nicht beim Füllen des Baums.
    if (m_filling)
        return;
    dss::MediaLibrary library;
    library.folders = m_client->mediaList().folders;
    const dss::MediaFolder *folder =
        library.folder(item->data(MediaTree::NameColumn, MediaTree::kFolderRole).toUuid());
    if (!folder || folder->expanded == expanded)
        return;
    dss::ChangeRequest r = request(dss::msg::kSetMediaFolderExpanded);
    r.folderId = folder->id;
    r.expanded = expanded;
    sendChange(r);
}

void MainWindow::addSourceFromMedia(const QString &reference, const QPoint &center,
                                    std::optional<int> insertIndex)
{
    // Neue Quelle aus einer Datei im Datenordner (Teil B 6): Der Kern kopiert nichts, die Grösse
    // ist die der Datei (Originalgrösse), die Mitte an center, der Name der aus der Medienliste.
    if (m_selectedOverlay.isNull())
        return;
    const std::optional<dss::MediaKind> kind = dss::mediaKindOf(reference);
    if (!kind || *kind == dss::MediaKind::Font)
        return;
    dss::ChangeRequest r =
        request(*kind == dss::MediaKind::Video ? dss::msg::kAddVideoSource : dss::msg::kAddImageSource);
    r.overlayId = m_selectedOverlay;
    r.file = reference;
    r.centerX = center.x();
    r.centerY = center.y();
    r.insertIndex = insertIndex;
    if (const qint64 id = sendChange(r); id > 0)
        m_selectOnResult.insert(id, SelectNew::Source);
}
