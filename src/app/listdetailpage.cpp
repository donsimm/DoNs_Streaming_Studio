#include "listdetailpage.h"

#include "actionview.h"
#include "panels.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int kRowIdRole = Qt::UserRole; // Kennung der Zeile, in Spalte 0

} // namespace

ListDetailPage::ListDetailPage(const Theme &theme, const Setup &setup, QWidget *parent)
    : QWidget(parent), m_theme(theme)
{
    setObjectName(setup.name + QStringLiteral("Page"));
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(setup.name + QStringLiteral("Splitter"));
    m_splitter->setChildrenCollapsible(false);
    row->addWidget(m_splitter);

    // Links: Suche und Filter auf einer Zeile, Tabelle, Leiste wie unter den Docks
    auto *list = new QWidget;
    auto *listColumn = new QVBoxLayout(list);
    listColumn->setContentsMargins(0, 0, 0, 0);
    listColumn->setSpacing(0);
    if (setup.search || setup.filter) {
        auto *searchRow = new QHBoxLayout;
        searchRow->setContentsMargins(0, 0, 0, 0);
        searchRow->setSpacing(6);
        if (setup.search) {
            m_search = new QLineEdit(list);
            m_search->setObjectName(setup.name + QStringLiteral("Search"));
            m_search->setAccessibleName(setup.searchName);
            m_search->setPlaceholderText(QStringLiteral("Suchen …"));
            m_search->setClearButtonEnabled(true);
            connect(m_search, &QLineEdit::textChanged, this, &ListDetailPage::searchChanged);
            searchRow->addWidget(m_search, 1);
        }
        if (setup.filter) {
            m_filter = new QComboBox(list);
            m_filter->setObjectName(setup.name + QStringLiteral("Filter"));
            m_filter->setAccessibleName(setup.filterName);
            connect(m_filter, &QComboBox::currentIndexChanged, this, [this] {
                if (!m_filling)
                    emit filterChanged(filterValue());
            });
            searchRow->addWidget(m_filter);
        }
        listColumn->addLayout(searchRow);
    }

    // Tabelle wie «Läufe der Aktionen»: Spaltenkopf, feste Breiten aus der Schrift, letzte gedehnt
    m_table = new QTreeWidget(list);
    m_table->setObjectName(setup.name + QStringLiteral("Table"));
    m_table->setAccessibleName(setup.tableName);
    m_table->setColumnCount(int(setup.columns.size()));
    QStringList titles;
    for (const Column &c : setup.columns)
        titles << c.title;
    m_table->setHeaderLabels(titles);
    m_table->setRootIsDecorated(false);
    m_table->setUniformRowHeights(true);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->header()->setStretchLastSection(true);
    const int digit = m_table->fontMetrics().horizontalAdvance(QLatin1Char('0'));
    for (int i = 0; i + 1 < setup.columns.size(); ++i)
        m_table->setColumnWidth(i, digit * setup.columns.at(i).digits);
    // Abgeschaltete Zeilen in textDisabled: Das Stylesheet überschreibt Qt::ForegroundRole
    m_table->setItemDelegate(new ForegroundDelegate(m_table));
    connect(m_table, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current) {
        if (!m_filling)
            emit selectionChanged(current ? current->data(0, kRowIdRole).toUuid() : QUuid());
    });
    connect(m_table, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (!m_filling && column == 0)
            emit enabledToggled(item->data(0, kRowIdRole).toUuid(), item->checkState(0) == Qt::Checked);
    });
    listColumn->addWidget(m_table, 1);

    m_add = symbolButton(QStringLiteral("hinzufuegen.svg"), QStringLiteral("+"), setup.addTip, nullptr);
    m_add->setObjectName(setup.name + QStringLiteral("Add"));
    m_copy = symbolButton(QStringLiteral("kopieren.svg"), QStringLiteral("K"), setup.copyTip, nullptr);
    m_copy->setObjectName(setup.name + QStringLiteral("Copy"));
    m_remove = symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"), setup.removeTip, nullptr);
    m_remove->setObjectName(setup.name + QStringLiteral("Remove"));
    connect(m_add, &QToolButton::clicked, this, &ListDetailPage::addClicked);
    connect(m_copy, &QToolButton::clicked, this, &ListDetailPage::copyClicked);
    connect(m_remove, &QToolButton::clicked, this, &ListDetailPage::removeClicked);
    QList<QWidget *> right;
    if (setup.listHint) {
        m_listHint = new QLabel;
        m_listHint->setObjectName(QStringLiteral("hintLabel"));
        right << m_listHint;
    }
    listColumn->addWidget(buttonBar({m_add, m_copy, m_remove}, right, list));
    setListButtons(false, false);

    QWidget *listPanel = dockPanel(setup.listTitle, list, m_splitter);
    m_splitter->addWidget(listPanel);

    // Rechts: wie «Aktionen» rechts, Bildlauf nur senkrecht ohne Rahmen; Seite 0 der Hinweis ohne
    // Auswahl, Seite 1 das Formular der Seite
    auto *detail = new QWidget;
    auto *detailColumn = new QVBoxLayout(detail);
    detailColumn->setContentsMargins(0, 0, 0, 0);
    detailColumn->setSpacing(0);
    auto *scroll = new QScrollArea(detail);
    scroll->setObjectName(setup.name + QStringLiteral("Scroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_detailPages = new QStackedWidget(scroll);
    m_detailPages->setObjectName(setup.name + QStringLiteral("DetailPages"));
    scroll->setWidget(m_detailPages);
    detailColumn->addWidget(scroll);

    auto *empty = new QWidget(m_detailPages);
    empty->setObjectName(setup.name + QStringLiteral("Empty"));
    auto *emptyColumn = new QVBoxLayout(empty);
    emptyColumn->setContentsMargins(8, 8, 8, 0);
    auto *hint = new QLabel(setup.emptyHint, empty);
    hint->setObjectName(QStringLiteral("hintLabel"));
    emptyColumn->addWidget(hint);
    emptyColumn->addStretch(1);
    m_detailPages->addWidget(empty);

    QWidget *detailPanel = dockPanel(setup.detailTitle, detail, m_splitter);
    m_detailTitle = detailPanel->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"), Qt::FindDirectChildrenOnly);
    m_splitter->addWidget(detailPanel);
    // Die Liste bekommt den zusätzlichen Platz (Entwurf: Liste breit)
    m_splitter->setStretchFactor(0, 2);
    m_splitter->setStretchFactor(1, 1);
}

void ListDetailPage::setRows(const QList<Row> &rows)
{
    const QUuid previous = m_wantedRow.isNull() ? selectedRow() : m_wantedRow;
    const QList<QUuid> before = m_rowIds;

    m_filling = true;
    m_table->clear();
    m_rowIds.clear();
    QHash<QUuid, QTreeWidgetItem *> items;
    const int columns = m_table->columnCount();
    for (const Row &row : rows) {
        auto *item = new QTreeWidgetItem(m_table);
        for (int i = 0; i < row.cells.size() && i + 1 < columns; ++i)
            item->setText(i + 1, row.cells.at(i));
        item->setData(0, kRowIdRole, row.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, row.enabled ? Qt::Checked : Qt::Unchecked);
        for (int i = 0; i < columns; ++i) {
            if (!row.enabled)
                item->setForeground(i, m_theme.textDisabled);
            if (!row.toolTip.isEmpty())
                item->setToolTip(i, row.toolTip);
        }
        m_rowIds.append(row.id);
        items.insert(row.id, item);
    }
    QTreeWidgetItem *selectItem = nullptr;
    if (!previous.isNull()) {
        if (items.contains(previous)) {
            selectItem = items.value(previous);
            m_wantedRow = QUuid();
        } else if (m_wantedRow.isNull()) {
            selectItem = items.value(dss::keptSelection(before, m_rowIds, previous));
        }
    }
    m_table->setCurrentItem(selectItem);
    m_filling = false;
    emit selectionChanged(selectedRow());
}

bool ListDetailPage::selectRow(const QUuid &id)
{
    for (int i = 0; i < m_table->topLevelItemCount(); ++i) {
        QTreeWidgetItem *item = m_table->topLevelItem(i);
        if (item->data(0, kRowIdRole).toUuid() == id) {
            m_wantedRow = QUuid();
            m_table->setCurrentItem(item);
            return true;
        }
    }
    m_wantedRow = id;
    return false;
}

QUuid ListDetailPage::selectedRow() const
{
    const QTreeWidgetItem *item = m_table->currentItem();
    return item ? item->data(0, kRowIdRole).toUuid() : QUuid();
}

void ListDetailPage::setListButtons(bool add, bool copyRemove)
{
    m_add->setEnabled(add);
    m_copy->setEnabled(copyRemove);
    m_remove->setEnabled(copyRemove);
}

void ListDetailPage::setHint(const QString &text)
{
    if (m_listHint)
        m_listHint->setText(text);
}

QString ListDetailPage::hint() const
{
    return m_listHint ? m_listHint->text() : QString();
}

void ListDetailPage::setDetailTitle(const QString &title)
{
    m_detailTitle->setText(title);
}

QString ListDetailPage::detailTitle() const
{
    return m_detailTitle->text();
}

void ListDetailPage::setDetailForm(QWidget *form)
{
    if (m_form) {
        m_detailPages->removeWidget(m_form);
        m_form->deleteLater();
    }
    m_form = form;
    if (m_form)
        m_detailPages->addWidget(m_form);
}

void ListDetailPage::showDetail(bool show)
{
    m_detailPages->setCurrentIndex(show && m_form ? 1 : 0);
}

bool ListDetailPage::detailShown() const
{
    return m_detailPages->currentIndex() == 1;
}

QString ListDetailPage::searchText() const
{
    return m_search ? m_search->text() : QString();
}

void ListDetailPage::setFilterChoices(const QStringList &values, const QStringList &labels)
{
    if (!m_filter)
        return;
    const QString previous = filterValue();
    m_filling = true;
    m_filter->clear();
    for (int i = 0; i < values.size(); ++i)
        m_filter->addItem(i < labels.size() ? labels.at(i) : values.at(i), values.at(i));
    const int index = m_filter->findData(previous);
    m_filter->setCurrentIndex(index >= 0 ? index : 0);
    m_filling = false;
    if (filterValue() != previous)
        emit filterChanged(filterValue());
}

QString ListDetailPage::filterValue() const
{
    return m_filter ? m_filter->currentData().toString() : QString();
}
