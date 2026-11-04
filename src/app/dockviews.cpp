#include "dockviews.h"

#include "medialibrary.h"

#include <QDrag>
#include <QDropEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMetaMethod>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>

namespace {

// Nichts selbst verschieben: Mit IgnoreAction entfernt QAbstractItemView::startDrag den
// gezogenen Eintrag nicht (sonst bei InternalMove und MoveAction).
// https://doc.qt.io/qt-6.8/qabstractitemview.html#DragDropMode-enum
void refuseLocalMove(QDropEvent *event)
{
    event->setDropAction(Qt::IgnoreAction);
    event->accept();
}

} // namespace

OverlayTree::OverlayTree(QWidget *parent) : QTreeWidget(parent)
{
    setHeaderHidden(true);
    setColumnCount(1);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setIndentation(14);
}

// P09, Punkt 48: Einzug links vom Eintrag ohne Auswahlfarbe (dort kommt mit P10 der Schalter
// «Ausgabe ein/aus», E-24). QTreeView::drawRow füllt den Einzug vor drawBranches mit
// PE_PanelItemViewRow, und das Stylesheet nimmt dafür den Hintergrund von ::item:selected
// (Qt 6.8.2: qtreeview.cpp Zeilen 1803-1841, qstylesheetstyle.cpp Zeilen 4840-4869,
// https://github.com/qt/qtbase/blob/v6.8.2/src/widgets/itemviews/qtreeview.cpp,
// https://github.com/qt/qtbase/blob/v6.8.2/src/widgets/styles/qstylesheetstyle.cpp).
// Deshalb hier zuerst mit dem Hintergrund der Liste übermalen, dann Pfeile wie bisher.
// P10 (E-24): Im letzten Stück des Einzugs eines Overlays steht der Schalter «Ausgabe». Bei einem
// Overlay ganz oben ist dort kein Pfeil (keine Kinder), bei einem Overlay im Ordner steht der Pfeil
// des Ordners eine Stufe weiter links.
void OverlayTree::drawBranches(QPainter *painter, const QRect &rect, const QModelIndex &index) const
{
    painter->fillRect(rect, viewport()->palette().brush(viewport()->backgroundRole()));
    QTreeWidget::drawBranches(painter, rect, index);
    const QVariant output = index.data(kOutputRole);
    if (m_outputOn.isNull() || !output.isValid() || index.data(kFolderRole).toBool())
        return;
    const QRect area(rect.right() + 1 - indentation(), rect.top(), indentation(), rect.height());
    const int side = qMin(12, qMin(area.width(), area.height()));
    const QRect box(area.center().x() - side / 2, area.center().y() - side / 2, side, side);
    (output.toBool() ? m_outputOn : m_outputOff).paint(painter, box);
}

void OverlayTree::setOutputIcons(const QIcon &on, const QIcon &off)
{
    m_outputOn = on;
    m_outputOff = off;
    viewport()->update();
}

QRect OverlayTree::outputSwitchRect(const QTreeWidgetItem *item) const
{
    if (!item || m_outputOn.isNull() || item->data(0, kFolderRole).toBool()
        || !item->data(0, kOutputRole).isValid())
        return {};
    const QRect row = visualItemRect(item);
    if (row.isEmpty())
        return {};
    // Der Eintrag beginnt nach dem Einzug; der Schalter steht im Stück direkt davor.
    return QRect(row.left() - indentation(), row.top(), indentation(), row.height());
}

bool OverlayTree::onOutputSwitch(const QPoint &position, QUuid *id) const
{
    const QTreeWidgetItem *item = itemAt(position);
    if (!outputSwitchRect(item).contains(position))
        return false;
    *id = item->data(0, kIdRole).toUuid();
    return true;
}

void OverlayTree::mousePressEvent(QMouseEvent *event)
{
    // Klick auf den Schalter: nur melden, Auswahl bleibt. Ohne Empfänger wie ein gewöhnlicher Klick.
    QUuid id;
    if (event->button() == Qt::LeftButton
        && isSignalConnected(QMetaMethod::fromSignal(&OverlayTree::outputClicked))
        && onOutputSwitch(event->position().toPoint(), &id)) {
        emit outputClicked(id);
        event->accept();
        return;
    }
    QTreeWidget::mousePressEvent(event);
}

void OverlayTree::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Doppelklick auf den Schalter: kein Umbenennen (zweiter Klick schaltet nicht noch einmal).
    QUuid id;
    if (isSignalConnected(QMetaMethod::fromSignal(&OverlayTree::outputClicked))
        && onOutputSwitch(event->position().toPoint(), &id)) {
        event->accept();
        return;
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}

void OverlayTree::dropEvent(QDropEvent *event)
{
    const QList<QTreeWidgetItem *> dragged = selectedItems();
    if (dragged.size() != 1 || event->source() != this) {
        event->ignore();
        return;
    }
    const QTreeWidgetItem *item = dragged.first();
    const QTreeWidgetItem *target = itemAt(event->position().toPoint());
    // Stelle des Einfügestrichs von Qt (aus dem letzten dragMoveEvent) in die Werte der Logik.
    // https://doc.qt.io/qt-6.8/qabstractitemview.html#DropIndicatorPosition-enum
    dss::DropPosition position = dss::DropPosition::EndOfList;
    if (target) {
        switch (dropIndicatorPosition()) {
        case AboveItem:
            position = dss::DropPosition::Above;
            break;
        case BelowItem:
            position = dss::DropPosition::Below;
            break;
        case OnItem:
            position = dss::DropPosition::OnItem;
            break;
        case OnViewport:
            break;
        }
    }
    emit itemDropped(item->data(0, kIdRole).toUuid(), item->data(0, kFolderRole).toBool(),
                     target ? target->data(0, kIdRole).toUuid() : QUuid(),
                     target && target->data(0, kFolderRole).toBool(), position);
    refuseLocalMove(event);
}

SourceList::SourceList(QWidget *parent) : QListWidget(parent)
{
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
    setDragDropOverwriteMode(false);
    setContextMenuPolicy(Qt::CustomContextMenu);
}

void SourceList::mousePressEvent(QMouseEvent *event)
{
    // Klick auf das Symbol (Auge) einer Zeile: nur melden, Auswahl bleibt. Ohne Empfänger
    // (nur Anzeige, Schalter DSS_APP_EDITING) wie ein gewöhnlicher Klick.
    if (event->button() == Qt::LeftButton
        && isSignalConnected(QMetaMethod::fromSignal(&SourceList::visibilityClicked))) {
        if (QListWidgetItem *item = itemAt(event->position().toPoint())) {
            QStyleOptionViewItem option;
            initViewItemOption(&option);
            option.rect = visualItemRect(item);
            option.features |= QStyleOptionViewItem::HasDecoration | QStyleOptionViewItem::HasDisplay;
            option.icon = item->icon();
            option.text = item->text();
            const QRect eye = style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, this);
            if (eye.contains(event->position().toPoint())) {
                emit visibilityClicked(item->data(kIdRole).toUuid());
                event->accept();
                return;
            }
        }
    }
    QListWidget::mousePressEvent(event);
}

void SourceList::setAcceptsMedia(bool accept)
{
    // Mit InternalMove nimmt QAbstractItemView nur Ziehen aus der eigenen Liste an; DragDrop nimmt
    // auch fremde an, aber nur in den Formaten aus mimeTypes() (eigene Zeilen und kMediaMimeType).
    // Eigene Zeilen verschiebt dropEvent weiter nicht selbst (refuseLocalMove).
    // https://doc.qt.io/qt-6.8/qabstractitemview.html#DragDropMode-enum
    m_acceptsMedia = accept;
    setDragDropMode(accept ? QAbstractItemView::DragDrop : QAbstractItemView::InternalMove);
}

QStringList SourceList::mimeTypes() const
{
    QStringList types = QListWidget::mimeTypes();
    if (m_acceptsMedia)
        types.append(QString::fromLatin1(kMediaMimeType));
    return types;
}

int SourceList::insertRowAt(const QPoint &position) const
{
    const QListWidgetItem *target = itemAt(position);
    if (!target)
        return count();
    const int targetRow = row(target);
    return dropIndicatorPosition() == QAbstractItemView::BelowItem ? targetRow + 1 : targetRow;
}

void SourceList::dropEvent(QDropEvent *event)
{
    if (m_acceptsMedia && event->source() != this) {
        const QString reference = droppedMediaSource(event->mimeData());
        if (reference.isEmpty()) {
            event->ignore();
            return;
        }
        emit mediaDropped(reference, insertRowAt(event->position().toPoint()));
        event->setDropAction(Qt::CopyAction);
        event->accept();
        return;
    }
    const QList<QListWidgetItem *> dragged = selectedItems();
    if (dragged.size() != 1 || event->source() != this) {
        event->ignore();
        return;
    }
    emit itemDropped(dragged.first()->data(kIdRole).toUuid(), insertRowAt(event->position().toPoint()));
    refuseLocalMove(event);
}

QString droppedMediaSource(const QMimeData *mime)
{
    if (!mime || !mime->hasFormat(QString::fromLatin1(kMediaMimeType)))
        return {};
    const QString reference = QString::fromUtf8(mime->data(QString::fromLatin1(kMediaMimeType)));
    const std::optional<dss::MediaKind> kind = dss::mediaKindOf(reference);
    // Schriften: keine Quelle aus einer Datei allein (Textquelle braucht Text), nicht Teil von P10b
    return kind && *kind != dss::MediaKind::Font ? reference : QString();
}

namespace {

// Eintrag des Docks «Medien» mit Schlüssel zum Sortieren. Qt stellt bei absteigender Folge a vor
// b, wenn b < a (QTreeModel::sortItems mit QTreeModelGreaterThan). operator< fragt deshalb in
// beiden Richtungen «steht this vor other bzw. other vor this»; so bleiben Ordner immer zuerst.
// https://doc.qt.io/qt-6.8/qtreewidgetitem.html#operator-lt
// https://github.com/qt/qtbase/blob/v6.8.2/src/widgets/itemviews/qtreewidget.cpp (sortItems)
class MediaItem : public QTreeWidgetItem
{
public:
    explicit MediaItem(const dss::MediaSortKey &key) : m_key(key) {}

    bool operator<(const QTreeWidgetItem &other) const override
    {
        const auto *o = dynamic_cast<const MediaItem *>(&other);
        const QTreeWidget *tree = treeWidget();
        if (!o || !tree)
            return QTreeWidgetItem::operator<(other);
        const int section = tree->sortColumn();
        const dss::MediaColumn column = section == MediaTree::KindColumn ? dss::MediaColumn::Kind
                                        : section == MediaTree::SizeColumn ? dss::MediaColumn::Size
                                                                           : dss::MediaColumn::Name;
        if (tree->header()->sortIndicatorOrder() == Qt::AscendingOrder)
            return dss::mediaRowBefore(m_key, o->m_key, column, true);
        return dss::mediaRowBefore(o->m_key, m_key, column, false);
    }

private:
    dss::MediaSortKey m_key;
};

} // namespace

MediaTree::MediaTree(QWidget *parent) : QTreeWidget(parent)
{
    setColumnCount(3);
    setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Typ"), QStringLiteral("Grösse")});
    header()->setStretchLastSection(false);
    header()->setSectionResizeMode(NameColumn, QHeaderView::Stretch);
    header()->setSectionResizeMode(KindColumn, QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(SizeColumn, QHeaderView::ResizeToContents);
    header()->setSortIndicatorShown(true);
    headerItem()->setToolTip(NameColumn, QStringLiteral("Kursiv: ungenutzt, keine Quelle nutzt die Datei"));
    headerItem()->setToolTip(KindColumn, QStringLiteral("Warnzeichen: Die Browserquelle zeigt das Video nicht an"));
    setSortingEnabled(true);
    sortByColumn(NameColumn, Qt::AscendingOrder);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setDragDropMode(QAbstractItemView::InternalMove);
    setDefaultDropAction(Qt::MoveAction);
    setDropIndicatorShown(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setIndentation(14);
    setUniformRowHeights(true);
    setRootIsDecorated(true);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Doppelklick benennt um (Teil B 3); auf- und zuklappen mit dem Pfeil oder den Pfeiltasten
    setExpandsOnDoubleClick(false);
}

QTreeWidgetItem *MediaTree::createItem(const dss::MediaSortKey &key)
{
    return new MediaItem(key);
}

QStringList MediaTree::mimeTypes() const
{
    return {QString::fromLatin1(kMediaMimeType), QString::fromLatin1(kMediaFolderMimeType)};
}

QMimeData *MediaTree::mimeData(const QList<QTreeWidgetItem *> &items) const
{
    if (items.size() != 1)
        return nullptr;
    const QTreeWidgetItem *item = items.first();
    auto *mime = new QMimeData;
    const QString file = item->data(NameColumn, kFileRole).toString();
    if (!file.isEmpty())
        mime->setData(QString::fromLatin1(kMediaMimeType), file.toUtf8());
    else
        mime->setData(QString::fromLatin1(kMediaFolderMimeType), QByteArray());
    return mime;
}

void MediaTree::startDrag(Qt::DropActions supportedActions)
{
    // Eigenes QDrag statt QAbstractItemView::startDrag: Es entfernt bei MoveAction nie einen
    // Eintrag, und ausserhalb (Leinwand, Liste «Quellen») gilt CopyAction (eine neue Quelle
    // entsteht, die Datei bleibt). Innerhalb des Docks gilt MoveAction (Verschieben).
    Q_UNUSED(supportedActions)
    QTreeWidgetItem *item = currentItem();
    if (!item || !(item->flags() & Qt::ItemIsDragEnabled))
        return;
    QMimeData *mime = mimeData({item});
    if (!mime)
        return;
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    const QIcon icon = item->icon(NameColumn);
    if (!icon.isNull())
        drag->setPixmap(icon.pixmap(iconSize()));
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);
}

void MediaTree::dropEvent(QDropEvent *event)
{
    const QList<QTreeWidgetItem *> dragged = selectedItems();
    if (dragged.size() != 1 || event->source() != this) {
        event->ignore();
        return;
    }
    const QTreeWidgetItem *item = dragged.first();
    const QTreeWidgetItem *target = itemAt(event->position().toPoint());
    // Laufende Übernahmen sind kein Ziel (weder Datei noch Ordner): wie am Ende der Liste
    if (target && target->data(NameColumn, kFileRole).toString().isEmpty()
        && target->data(NameColumn, kFolderRole).toUuid().isNull())
        target = nullptr;
    dss::DropPosition position = dss::DropPosition::EndOfList;
    if (target) {
        switch (dropIndicatorPosition()) {
        case AboveItem:
            position = dss::DropPosition::Above;
            break;
        case BelowItem:
            position = dss::DropPosition::Below;
            break;
        case OnItem:
            position = dss::DropPosition::OnItem;
            break;
        case OnViewport:
            break;
        }
    }
    const QUuid targetFolder = target ? target->data(NameColumn, kFolderRole).toUuid() : QUuid();
    emit itemDropped(item->data(NameColumn, kFolderRole).toUuid(), item->data(NameColumn, kFileRole).toString(),
                     !targetFolder.isNull(), targetFolder,
                     target ? target->data(NameColumn, kFileRole).toString() : QString(), position);
    refuseLocalMove(event);
}

void MediaTree::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_F2 && event->modifiers() == Qt::NoModifier && currentItem()) {
        emit renameRequested(currentItem());
        event->accept();
        return;
    }
    QTreeWidget::keyPressEvent(event);
}
