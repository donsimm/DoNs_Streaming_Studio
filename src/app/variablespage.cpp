#include "variablespage.h"

#include "panels.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QSplitter>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace {

constexpr int kRowKeyRole = Qt::UserRole; // Kennung der Zeile, in Spalte 0

// Tabelle, in der nur «Wert» änderbar ist: Doppelklick oder F2 auf einer beliebigen Zelle der Zeile
// öffnen den Editor in «Wert» (die übrigen Tabellen setzen NoEditTriggers, hier gewollt anders,
// Plan e3-7). closed meldet das Ende einer Bearbeitung.
// QAbstractItemView::edit: https://doc.qt.io/qt-6/qabstractitemview.html#edit-1
// QAbstractItemView::closeEditor: https://doc.qt.io/qt-6/qabstractitemview.html#closeEditor
class ValueTable : public QTreeWidget
{
public:
    ValueTable(int valueColumn, QWidget *parent) : QTreeWidget(parent), m_valueColumn(valueColumn) {}

    std::function<void()> closed;
    bool editing() const { return state() == EditingState; }

protected:
    bool edit(const QModelIndex &index, EditTrigger trigger, QEvent *event) override
    {
        return QTreeWidget::edit(index.isValid() ? index.siblingAtColumn(m_valueColumn) : index, trigger, event);
    }
    void closeEditor(QWidget *editor, QAbstractItemDelegate::EndEditHint hint) override
    {
        QTreeWidget::closeEditor(editor, hint);
        if (closed)
            closed();
    }

private:
    const int m_valueColumn;
};

struct AreaSetup
{
    const char *name;
    QString title;
    QString tableName;
    QString searchName;
    QList<std::pair<QString, int>> columns; // Titel, Breite in Ziffern (letzte gedehnt)
};

AreaSetup areaSetup(VariablesPage::Area area)
{
    if (area == VariablesPage::Global)
        return {"globalVariables", QStringLiteral("Für alle"), QStringLiteral("Variablen für alle"),
                QStringLiteral("Variablen für alle suchen"),
                {{QStringLiteral("Name"), 22}, {QStringLiteral("Wert"), 22},
                 {QStringLiteral("Dauerhaft"), 11}, {QStringLiteral("Herkunft"), 0}}};
    return {"userVariables", QStringLiteral("Je Zuschauer"), QStringLiteral("Variablen je Zuschauer"),
            QStringLiteral("Variablen je Zuschauer suchen"),
            {{QStringLiteral("Zuschauer"), 22}, {QStringLiteral("Variable"), 16},
             {QStringLiteral("Wert"), 16}, {QStringLiteral("Herkunft"), 0}}};
}

} // namespace

VariablesPage::VariablesPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("variablesPage"));
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("variablesSplitter"));
    m_splitter->setChildrenCollapsible(false);
    row->addWidget(m_splitter);
    for (int i = 0; i < AreaCount; ++i) {
        const Area area = Area(i);
        m_splitter->addWidget(dockPanel(areaSetup(area).title, createArea(area), m_splitter));
        m_splitter->setStretchFactor(i, 1);
    }
}

QWidget *VariablesPage::createArea(Area area)
{
    const AreaSetup setup = areaSetup(area);
    const QString name = QString::fromLatin1(setup.name);
    auto *w = new QWidget;
    auto *column = new QVBoxLayout(w);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    // Suche und Filter auf einer Zeile wie in ListDetailPage
    auto *searchRow = new QHBoxLayout;
    searchRow->setContentsMargins(0, 0, 0, 0);
    searchRow->setSpacing(6);
    m_search[area] = new QLineEdit(w);
    m_search[area]->setObjectName(name + QStringLiteral("Search"));
    m_search[area]->setAccessibleName(setup.searchName);
    m_search[area]->setPlaceholderText(QStringLiteral("Suchen …"));
    m_search[area]->setClearButtonEnabled(true);
    connect(m_search[area], &QLineEdit::textChanged, this, [this, area] { emit searchChanged(area); });
    searchRow->addWidget(m_search[area], 1);
    if (area == User) {
        m_filter = new QComboBox(w);
        m_filter->setObjectName(name + QStringLiteral("Filter"));
        m_filter->setAccessibleName(QStringLiteral("Variable der Zuschauer"));
        connect(m_filter, &QComboBox::currentIndexChanged, this, [this] {
            if (!m_filling)
                emit filterChanged();
        });
        searchRow->addWidget(m_filter);
    }
    column->addLayout(searchRow);

    // Tabelle wie in ListDetailPage, ohne Kästchen; nur «Wert» änderbar
    auto *table = new ValueTable(valueColumn(area), w);
    m_tables[area] = table;
    table->setObjectName(name);
    table->setAccessibleName(setup.tableName);
    table->setColumnCount(int(setup.columns.size()));
    QStringList titles;
    for (const auto &c : setup.columns)
        titles << c.first;
    table->setHeaderLabels(titles);
    table->setRootIsDecorated(false);
    table->setUniformRowHeights(true);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    table->header()->setStretchLastSection(true);
    const int digit = table->fontMetrics().horizontalAdvance(QLatin1Char('0'));
    for (int i = 0; i + 1 < setup.columns.size(); ++i)
        table->setColumnWidth(i, digit * setup.columns.at(i).second);
    connect(table, &QTreeWidget::currentItemChanged, this, [this, area] {
        if (!m_filling)
            emit selectionChanged(area);
    });
    connect(table, &QTreeWidget::itemChanged, this, [this, area](QTreeWidgetItem *item, int changed) {
        if (m_filling || changed != valueColumn(area))
            return;
        // Später melden: Das MainWindow füllt die Tabelle neu, während der Editor noch schliesst
        const QString key = item->data(0, kRowKeyRole).toString();
        const QString text = item->text(changed);
        QMetaObject::invokeMethod(this, [this, area, key, text] { emit valueEdited(area, key, text); },
                                  Qt::QueuedConnection);
    });
    table->closed = [this, area] {
        if (m_pending[area])
            QMetaObject::invokeMethod(this, [this, area] {
                if (m_pending[area])
                    setRows(area, m_pendingRows[area]);
            }, Qt::QueuedConnection);
    };
    column->addWidget(table, 1);

    // Prüfmeldung unter der Tabelle, immer da (Entscheid des Nutzers, 2026-11-16)
    auto *problemRow = new QHBoxLayout;
    problemRow->setContentsMargins(8, 0, 8, 0);
    m_problems[area] = problemLine(w);
    problemRow->addWidget(m_problems[area]);
    column->addLayout(problemRow);

    m_remove[area] = symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"),
                                  QStringLiteral("Variable löschen"), nullptr);
    // Tooltip gleich, zugänglicher Name je Bereich unterscheidbar
    m_remove[area]->setAccessibleName(area == Global ? QStringLiteral("Variable für alle löschen")
                                                     : QStringLiteral("Variable je Zuschauer löschen"));
    m_remove[area]->setObjectName(name + QStringLiteral("Remove"));
    m_remove[area]->setEnabled(false);
    connect(m_remove[area], &QToolButton::clicked, this, [this, area] { emit removeClicked(area); });
    column->addWidget(buttonBar({m_remove[area]}, {}, w));
    return w;
}

void VariablesPage::setRows(Area area, const QList<Row> &rows)
{
    if (static_cast<const ValueTable *>(m_tables[area])->editing()) {
        m_pendingRows[area] = rows;
        m_pending[area] = true;
        return;
    }
    m_pending[area] = false;
    m_pendingRows[area].clear();
    fillRows(area, rows);
}

void VariablesPage::fillRows(Area area, const QList<Row> &rows)
{
    QTreeWidget *table = m_tables[area];
    const QString previous = selectedRow(area);
    m_filling = true;
    table->clear();
    QTreeWidgetItem *selectItem = nullptr;
    const int columns = table->columnCount();
    for (const Row &row : rows) {
        auto *item = new QTreeWidgetItem(table);
        for (int i = 0; i < row.cells.size() && i < columns; ++i)
            item->setText(i, row.cells.at(i));
        item->setData(0, kRowKeyRole, row.key);
        if (m_editable)
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        if (!previous.isEmpty() && row.key == previous)
            selectItem = item;
    }
    table->setCurrentItem(selectItem);
    m_filling = false;
    emit selectionChanged(area);
}

QString VariablesPage::selectedRow(Area area) const
{
    const QTreeWidgetItem *item = m_tables[area]->currentItem();
    return item ? item->data(0, kRowKeyRole).toString() : QString();
}

void VariablesPage::setEditable(bool editable)
{
    if (m_editable == editable)
        return;
    m_editable = editable;
    m_filling = true;
    for (QTreeWidget *table : m_tables)
        for (int i = 0; i < table->topLevelItemCount(); ++i) {
            QTreeWidgetItem *item = table->topLevelItem(i);
            item->setFlags(editable ? item->flags() | Qt::ItemIsEditable : item->flags() & ~Qt::ItemIsEditable);
        }
    m_filling = false;
}

void VariablesPage::setRemoveEnabled(Area area, bool enabled)
{
    m_remove[area]->setEnabled(enabled);
}

void VariablesPage::setProblem(Area area, const QString &text)
{
    m_problems[area]->setText(text);
    m_problems[area]->setToolTip(text);
}

QString VariablesPage::problem(Area area) const
{
    return m_problems[area]->text();
}

QString VariablesPage::searchText(Area area) const
{
    return m_search[area]->text();
}

void VariablesPage::setFilterChoices(const QStringList &values, const QStringList &labels)
{
    const QString previous = filterValue();
    m_filling = true;
    m_filter->clear();
    for (int i = 0; i < values.size(); ++i)
        m_filter->addItem(i < labels.size() ? labels.at(i) : values.at(i), values.at(i));
    const int index = m_filter->findData(previous);
    m_filter->setCurrentIndex(index >= 0 ? index : 0);
    m_filling = false;
    if (filterValue() != previous)
        emit filterChanged();
}

QString VariablesPage::filterValue() const
{
    return m_filter->currentData().toString();
}
