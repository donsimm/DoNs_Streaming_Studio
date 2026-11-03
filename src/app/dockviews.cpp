#include "dockviews.h"

#include <QDropEvent>
#include <QMetaMethod>
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

void SourceList::dropEvent(QDropEvent *event)
{
    const QList<QListWidgetItem *> dragged = selectedItems();
    if (dragged.size() != 1 || event->source() != this) {
        event->ignore();
        return;
    }
    const QListWidgetItem *target = itemAt(event->position().toPoint());
    int insertRow = count();
    if (target) {
        const int targetRow = row(target);
        insertRow = dropIndicatorPosition() == QAbstractItemView::BelowItem ? targetRow + 1 : targetRow;
    }
    emit itemDropped(dragged.first()->data(kIdRole).toUuid(), insertRow);
    refuseLocalMove(event);
}
