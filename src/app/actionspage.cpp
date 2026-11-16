#include "actionspage.h"

#include "panels.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Daten einer Zeile im Baum
constexpr int kActionIdRole = Qt::UserRole;     // Kennung der Aktion, null bei Gruppen
constexpr int kGroupRole = Qt::UserRole + 1;    // Gruppe der Zeile, leer = ohne
constexpr int kIsGroupRole = Qt::UserRole + 2;

} // namespace

ActionsPage::ActionsPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("actionsPage"));
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("actionsSplitter"));
    m_splitter->setChildrenCollapsible(false);
    row->addWidget(m_splitter);

    const QString titles[3] = {QStringLiteral("Aktionen"), QStringLiteral("Aktion"), QStringLiteral("Schritt")};
    for (int i = 0; i < 3; ++i) {
        m_areas[i] = new QWidget;
        auto *column = new QVBoxLayout(m_areas[i]);
        column->setContentsMargins(0, 0, 0, 0);
        QWidget *panel = dockPanel(titles[i], m_areas[i], m_splitter);
        m_titles[i] = panel->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"), Qt::FindDirectChildrenOnly);
        m_splitter->addWidget(panel);
    }
    // Die Mitte bekommt den zusätzlichen Platz, links und rechts wachsen weniger (wie Docks neben
    // der Leinwand).
    m_splitter->setStretchFactor(List, 1);
    m_splitter->setStretchFactor(Action, 2);
    m_splitter->setStretchFactor(Detail, 1);

    // P14 e2-14, links: Suchfeld, Baum wie das Dock «Overlays», Leiste wie unter den Docks
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("actionsSearch"));
    m_search->setAccessibleName(QStringLiteral("Aktionen suchen"));
    m_search->setPlaceholderText(QStringLiteral("Suchen …"));
    m_search->setClearButtonEnabled(true);
    connect(m_search, &QLineEdit::textChanged, this, &ActionsPage::searchChanged);

    m_tree = new QTreeWidget;
    m_tree->setObjectName(QStringLiteral("actionsTree"));
    m_tree->setAccessibleName(QStringLiteral("Aktionen"));
    m_tree->setHeaderHidden(true);
    m_tree->setColumnCount(2);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    m_tree->setIndentation(14);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // Doppelklick benennt die Gruppe um wie im Dock «Medien»; auf- und zuklappen mit dem Pfeil
    m_tree->setExpandsOnDoubleClick(false);
    // Name so breit wie nötig, der Kurzinhalt (Anzahl bzw. Auslöser) nimmt den Rest
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(true);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current) {
        if (!m_filling)
            emit actionSelected(current ? current->data(0, kActionIdRole).toUuid() : QUuid());
    });
    connect(m_tree, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (m_filling || column != 0 || item->data(0, kIsGroupRole).toBool())
            return;
        emit actionEnabledToggled(item->data(0, kActionIdRole).toUuid(), item->checkState(0) == Qt::Checked);
    });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (m_groupEditing && item->data(0, kIsGroupRole).toBool())
            emit renameGroupRequested(item->data(0, kGroupRole).toString());
    });
    connect(m_tree, &QWidget::customContextMenuRequested, this, &ActionsPage::showGroupMenu);

    m_add = symbolButton(QStringLiteral("hinzufuegen.svg"), QStringLiteral("+"),
                         QStringLiteral("Aktion hinzufügen"), nullptr);
    m_add->setObjectName(QStringLiteral("actionAdd"));
    m_newGroup = symbolButton(QStringLiteral("ordner-neu.svg"), QStringLiteral("O"),
                              QStringLiteral("Neue Gruppe"), nullptr);
    m_newGroup->setObjectName(QStringLiteral("actionNewGroup"));
    m_copy = symbolButton(QStringLiteral("kopieren.svg"), QStringLiteral("K"),
                          QStringLiteral("Aktion kopieren"), nullptr);
    m_copy->setObjectName(QStringLiteral("actionCopy"));
    m_remove = symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"),
                            QStringLiteral("Aktion löschen"), nullptr);
    m_remove->setObjectName(QStringLiteral("actionRemove"));
    m_reset = symbolButton(QStringLiteral("zuruecksetzen.svg"), QStringLiteral("Z"),
                           QStringLiteral("Mitgelieferte Aktion zurücksetzen"), nullptr);
    m_reset->setObjectName(QStringLiteral("actionReset"));
    connect(m_add, &QToolButton::clicked, this, &ActionsPage::addClicked);
    connect(m_newGroup, &QToolButton::clicked, this, &ActionsPage::newGroupClicked);
    connect(m_copy, &QToolButton::clicked, this, &ActionsPage::copyClicked);
    connect(m_remove, &QToolButton::clicked, this, &ActionsPage::removeClicked);
    connect(m_reset, &QToolButton::clicked, this, &ActionsPage::resetClicked);

    auto *column = static_cast<QVBoxLayout *>(m_areas[List]->layout());
    column->setSpacing(0);
    column->addWidget(m_search);
    column->addWidget(m_tree, 1);
    column->addWidget(buttonBar({m_add, m_newGroup, m_copy, m_remove}, {m_reset}, m_areas[List]));
}

void ActionsPage::setAreaTitle(Area which, const QString &title)
{
    m_titles[which]->setText(title);
}

QString ActionsPage::areaTitle(Area which) const
{
    return m_titles[which]->text();
}

void ActionsPage::setActionRows(const QList<dss::ActionListRow> &rows)
{
    // Bisherige Auswahl merken: gewünschte Aktion, sonst die gewählte Aktion bzw. Gruppe
    const QUuid previousAction = m_wantedAction.isNull() ? selectedAction() : m_wantedAction;
    const QTreeWidgetItem *current = m_tree->currentItem();
    const QString previousGroup = current && current->data(0, kIsGroupRole).toBool()
        ? current->data(0, kGroupRole).toString() : QString();
    const QList<QUuid> before = m_rowActions;

    m_filling = true;
    m_tree->clear();
    m_rowActions.clear();
    QTreeWidgetItem *group = nullptr;
    QTreeWidgetItem *selectItem = nullptr;
    QHash<QUuid, QTreeWidgetItem *> actionItems;
    for (const dss::ActionListRow &row : rows) {
        QTreeWidgetItem *item = nullptr;
        if (row.depth > 0 && group)
            item = new QTreeWidgetItem(group);
        else
            item = new QTreeWidgetItem(m_tree);
        item->setText(0, row.text);
        item->setText(1, row.detail);
        item->setData(0, kGroupRole, row.group);
        item->setData(0, kIsGroupRole, row.isGroup);
        if (!row.warning.isEmpty())
            item->setToolTip(0, row.warning);
        if (row.isGroup) {
            // Gruppen ohne Kästchen (QTreeWidgetItem hat ItemIsUserCheckable schon von Haus aus)
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
            group = item;
            item->setExpanded(true);
            if (!previousGroup.isEmpty() && row.group.compare(previousGroup, Qt::CaseInsensitive) == 0)
                selectItem = item;
        } else {
            if (row.depth == 0)
                group = nullptr;
            item->setData(0, kActionIdRole, row.actionId);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, row.enabled ? Qt::Checked : Qt::Unchecked);
            m_rowActions.append(row.actionId);
            actionItems.insert(row.actionId, item);
        }
    }
    if (!selectItem && !previousAction.isNull()) {
        if (actionItems.contains(previousAction)) {
            selectItem = actionItems.value(previousAction);
            m_wantedAction = QUuid();
        } else if (m_wantedAction.isNull()) {
            selectItem = actionItems.value(dss::keptSelection(before, m_rowActions, previousAction));
        }
    }
    m_tree->setCurrentItem(selectItem);
    m_filling = false;
    emit actionSelected(selectedAction());
}

QString ActionsPage::searchText() const
{
    return m_search->text();
}

void ActionsPage::clearSearch()
{
    m_search->clear();
}

bool ActionsPage::selectAction(const QUuid &id)
{
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if (!(*it)->data(0, kIsGroupRole).toBool() && (*it)->data(0, kActionIdRole).toUuid() == id) {
            m_wantedAction = QUuid();
            m_tree->setCurrentItem(*it);
            return true;
        }
    }
    m_wantedAction = id;
    return false;
}

QUuid ActionsPage::selectedAction() const
{
    const QTreeWidgetItem *item = m_tree->currentItem();
    return item ? item->data(0, kActionIdRole).toUuid() : QUuid();
}

QString ActionsPage::selectedGroup() const
{
    const QTreeWidgetItem *item = m_tree->currentItem();
    return item ? item->data(0, kGroupRole).toString() : QString();
}

void ActionsPage::setListButtons(bool add, bool copyRemove, bool reset)
{
    m_groupEditing = add;
    m_add->setEnabled(add);
    m_newGroup->setEnabled(add);
    m_copy->setEnabled(copyRemove);
    m_remove->setEnabled(copyRemove);
    m_reset->setEnabled(reset);
}

void ActionsPage::showGroupMenu(const QPoint &position)
{
    // Wie das Dock «Overlays»: ohne Kern kein Menü, sonst nur an einer Gruppenzeile
    if (!m_groupEditing)
        return;
    const QTreeWidgetItem *item = m_tree->itemAt(position);
    if (!item || !item->data(0, kIsGroupRole).toBool())
        return;
    const QString group = item->data(0, kGroupRole).toString();
    QMenu menu(this);
    menu.addAction(QStringLiteral("Umbenennen …"), this, [this, group] { emit renameGroupRequested(group); });
    menu.addAction(QStringLiteral("Löschen …"), this, [this, group] { emit removeGroupRequested(group); });
    menu.exec(m_tree->viewport()->mapToGlobal(position));
}
