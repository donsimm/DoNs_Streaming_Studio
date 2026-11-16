#include "chatbotpage.h"

#include "actionspage.h"
#include "headerbar.h"
#include "panels.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QTreeWidgetItem *eventItem(const dss::EventRow &row)
{
    return new QTreeWidgetItem(QStringList{row.time, row.kind, row.name, row.details});
}

// Daten einer Zeile «Läufe der Aktionen» für Auswahl und Knöpfe
constexpr int kRunIdRole = Qt::UserRole;
constexpr int kActionIdRole = Qt::UserRole + 1;
constexpr int kCancellableRole = Qt::UserRole + 2;

// Ergebnisfarbe der Zelle: Das Stylesheet setzt QTreeView::item { color }, und QStyleSheetStyle
// überschreibt damit die Textfarbe aus Qt::ForegroundRole (im Bild geprüft, tst_fensterchatbot).
// Darum zeichnet der Delegat Hintergrund und Rahmen wie die Zeile und den Text selbst in der Farbe
// der Rolle. Gewählte Zeile wie alle (Farbe auf @primary@ wäre ein neues Paar im Theme).
class ResultColorDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const QVariant foreground = index.data(Qt::ForegroundRole);
        if (!foreground.canConvert<QBrush>() || option.state.testFlag(QStyle::State_Selected)) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        // Rand des Textes wie QCommonStyle (viewItemDrawText): PM_FocusFrameHMargin + 1
        const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, widget) + 1;
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget)
                                   .adjusted(margin, 0, -margin, 0);
        const QString text = opt.text;
        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
        painter->save();
        painter->setFont(opt.font);
        painter->setPen(foreground.value<QBrush>().color());
        painter->drawText(textRect, int(opt.displayAlignment),
                          opt.fontMetrics.elidedText(text, opt.textElideMode, textRect.width()));
        painter->restore();
    }
};

// Mitlaufen nur, wenn die Liste schon unten stand (sonst liest der Nutzer gerade weiter oben).
bool atBottom(QTreeWidget *list)
{
    const QScrollBar *bar = list->verticalScrollBar();
    return bar->value() == bar->maximum();
}

} // namespace

ChatBotPage::ChatBotPage(const Theme &theme, QWidget *parent) : QWidget(parent), m_theme(theme)
{
    setObjectName(QStringLiteral("chatBotPage"));
    setAttribute(Qt::WA_StyledBackground);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);

    // Kopfzeile mit fester Höhe (Regel 9): Ein neuer Zustand ändert nur den Text, nie die Anordnung.
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("chatBotHeader"));
    header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(6);
    auto *tabs = new QFrame(header);
    tabs->setObjectName(QStringLiteral("chatBotTabs"));
    m_tabRow = new QHBoxLayout(tabs);
    m_tabRow->setContentsMargins(2, 2, 2, 2);
    m_tabRow->setSpacing(2);
    m_tabs = new QButtonGroup(this);
    m_tabs->setExclusive(true);
    headerRow->addWidget(tabs, 0, Qt::AlignVCenter);
    headerRow->addStretch(1);
    // P14 e2-11: Stand und Freigabe nur aus setHeader; ein Klick meldet nur den Wunsch, den Stand
    // setzt erst die Antwort des Kerns (actions_status).
    m_enabled = new QCheckBox(QStringLiteral("Chat-Bot aktiv"), header);
    m_enabled->setObjectName(QStringLiteral("chatBotEnabled"));
    m_enabled->setEnabled(false);
    connect(m_enabled, &QCheckBox::clicked, this, &ChatBotPage::enabledClicked);
    headerRow->addWidget(m_enabled, 0, Qt::AlignVCenter);
    // Eine Zeile ohne Umbruch; ein langer Text wird abgeschnitten, Einzelheiten im Tooltip.
    m_state = new QLabel(header);
    m_state->setObjectName(QStringLiteral("chatBotState"));
    m_state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_state->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerRow->addWidget(m_state, 1);
    column->addWidget(header);

    m_pages = new QStackedWidget(this);
    column->addWidget(m_pages, 1);
    connect(m_tabs, &QButtonGroup::idClicked, m_pages, &QStackedWidget::setCurrentIndex);

    // P14 e2-12: Bereiche mit verschiebbaren Teilern (QSplitter::handle im Theme wie der Teiler
    // der Docks), keiner lässt sich ganz zuschieben (wie ActionsPage).
    m_liveSplitter = new QSplitter(Qt::Horizontal, m_pages);
    m_liveSplitter->setObjectName(QStringLiteral("chatBotLiveSplitter"));
    m_liveSplitter->setChildrenCollapsible(false);
    addSubTab(QStringLiteral("Live"), m_liveSplitter);
    m_tabs->button(Live)->setChecked(true);

    m_events = new QTreeWidget;
    m_events->setObjectName(QStringLiteral("chatBotEvents"));
    m_events->setColumnCount(4);
    m_events->setHeaderLabels({QStringLiteral("Zeit"), QStringLiteral("Art"), QStringLiteral("Name"),
                               QStringLiteral("Details")});
    m_events->setRootIsDecorated(false);
    m_events->setUniformRowHeights(true);
    m_events->setSelectionMode(QAbstractItemView::SingleSelection);
    m_events->setFocusPolicy(Qt::NoFocus);
    // Feste Spaltenbreiten aus der Schrift (keine Anpassung an den Inhalt bei jedem Ereignis).
    const int digit = m_events->fontMetrics().horizontalAdvance(QLatin1Char('0'));
    m_events->header()->setStretchLastSection(true);
    m_events->setColumnWidth(0, digit * 10);
    m_events->setColumnWidth(1, digit * 16);
    m_events->setColumnWidth(2, digit * 16);
    addLivePanel(QStringLiteral("Ereignisse"), m_events, 1);

    // Bereich «Läufe der Aktionen»: Tabelle wie «Ereignisse», darunter die Knopfleiste
    m_runs = new QTreeWidget;
    m_runs->setObjectName(QStringLiteral("chatBotRuns"));
    m_runs->setColumnCount(4);
    m_runs->setHeaderLabels({QStringLiteral("Zeit"), QStringLiteral("Aktion"), QStringLiteral("Auslöser"),
                             QStringLiteral("Ergebnis")});
    m_runs->setRootIsDecorated(false);
    m_runs->setUniformRowHeights(true);
    m_runs->setSelectionMode(QAbstractItemView::SingleSelection);
    m_runs->setFocusPolicy(Qt::NoFocus);
    // Breiten je Spalte: Zeit hh:mm:ss, ein Aktionsname, der längste Auslöser («Aktion ausführen»)
    m_runs->header()->setStretchLastSection(true);
    m_runs->setColumnWidth(0, digit * 10);
    m_runs->setColumnWidth(1, digit * 20);
    m_runs->setColumnWidth(2, digit * 18);
    m_runs->setItemDelegateForColumn(3, new ResultColorDelegate(m_runs));
    connect(m_runs, &QTreeWidget::itemSelectionChanged, this, &ChatBotPage::refreshRunButtons);

    m_openAction = new QPushButton(QStringLiteral("Im Editor öffnen"));
    m_openAction->setObjectName(QStringLiteral("chatBotRunOpenAction"));
    m_cancelRun = new QPushButton(QStringLiteral("Lauf abbrechen"));
    m_cancelRun->setObjectName(QStringLiteral("chatBotRunCancel"));
    m_onlyErrors = new QCheckBox(QStringLiteral("Nur Fehler"));
    m_onlyErrors->setObjectName(QStringLiteral("chatBotRunsOnlyErrors"));
    connect(m_openAction, &QPushButton::clicked, this, [this] {
        if (const QTreeWidgetItem *item = m_runs->currentItem(); item && item->isSelected())
            emit openActionClicked(item->data(0, kActionIdRole).toUuid());
    });
    connect(m_cancelRun, &QPushButton::clicked, this, [this] {
        if (const QTreeWidgetItem *item = m_runs->currentItem(); item && item->isSelected())
            emit cancelRunClicked(item->data(0, kRunIdRole).toUuid());
    });
    connect(m_onlyErrors, &QCheckBox::toggled, this, &ChatBotPage::onlyErrorsToggled);

    auto *runsBox = new QWidget;
    auto *runsColumn = new QVBoxLayout(runsBox);
    runsColumn->setContentsMargins(0, 0, 0, 0);
    runsColumn->setSpacing(0);
    runsColumn->addWidget(m_runs, 1);
    runsColumn->addWidget(buttonBar({m_openAction, m_cancelRun}, {m_onlyErrors}, runsBox));
    addLivePanel(QStringLiteral("Läufe der Aktionen"), runsBox, 1);
    refreshRunButtons();

    m_actions = new ActionsPage(m_pages);
    addSubTab(QStringLiteral("Aktionen"), m_actions);
}

int ChatBotPage::addSubTab(const QString &title, QWidget *page)
{
    const int id = m_pages->addWidget(page);
    QToolButton *button = moduleButton(title, m_tabRow->parentWidget());
    button->setObjectName(QStringLiteral("chatBotTab%1").arg(id));
    m_tabs->addButton(button, id);
    m_tabRow->addWidget(button);
    return id;
}

QWidget *ChatBotPage::addLivePanel(const QString &title, QWidget *content, int stretch, int position)
{
    QWidget *panel = dockPanel(title, content, nullptr);
    m_liveSplitter->insertWidget(position, panel);
    m_liveSplitter->setStretchFactor(m_liveSplitter->indexOf(panel), stretch);
    return panel;
}

void ChatBotPage::setCurrentSubTab(SubTab tab)
{
    m_tabs->button(tab)->setChecked(true);
    m_pages->setCurrentIndex(tab);
}

void ChatBotPage::setHeader(const dss::ChatBotHeaderView &view)
{
    m_state->setText(view.text);
    m_state->setToolTip(view.toolTip);
    m_state->setProperty("state", view.state);
    // Das Stylesheet wertet [state="…"] nur beim Polieren aus (wie HeaderBar::setConnectionState).
    m_state->style()->unpolish(m_state);
    m_state->style()->polish(m_state);
    m_enabled->setChecked(view.switchChecked);
    m_enabled->setEnabled(view.switchEnabled);
}

void ChatBotPage::resetEvents(const QList<dss::EventRow> &rows)
{
    m_events->clear();
    QList<QTreeWidgetItem *> items;
    items.reserve(rows.size());
    for (const dss::EventRow &row : rows)
        items.append(eventItem(row));
    m_events->addTopLevelItems(items);
    m_events->scrollToBottom();
}

void ChatBotPage::appendEvent(const dss::EventRow &row, int removedFront)
{
    const bool follow = atBottom(m_events);
    for (int i = 0; i < removedFront && m_events->topLevelItemCount() > 0; ++i)
        delete m_events->takeTopLevelItem(0);
    m_events->addTopLevelItem(eventItem(row));
    if (follow)
        m_events->scrollToBottom();
}

void ChatBotPage::fillRunItem(QTreeWidgetItem *item, const dss::RunRow &row) const
{
    item->setText(0, row.time);
    item->setText(1, row.action);
    item->setText(2, row.trigger);
    item->setText(3, row.result);
    // Ergebnisfarbe nur aus den Zustandsfarben des Themes (Entscheid 2026-11-16); leer = Text
    if (row.state == QLatin1String("ok"))
        item->setForeground(3, m_theme.stateConnected);
    else if (row.state == QLatin1String("warning"))
        item->setForeground(3, m_theme.stateConnecting);
    else if (row.state == QLatin1String("error"))
        item->setForeground(3, m_theme.stateError);
    else
        item->setData(3, Qt::ForegroundRole, QVariant());
    item->setData(0, kRunIdRole, row.runId);
    item->setData(0, kActionIdRole, row.actionId);
    item->setData(0, kCancellableRole, row.cancellable);
}

QTreeWidgetItem *ChatBotPage::runItem(const dss::RunRow &row) const
{
    auto *item = new QTreeWidgetItem;
    fillRunItem(item, row);
    return item;
}

void ChatBotPage::resetRuns(const QList<dss::RunRow> &rows)
{
    const QTreeWidgetItem *current = m_runs->currentItem();
    const QUuid selected = current && current->isSelected() ? current->data(0, kRunIdRole).toUuid() : QUuid();
    m_runs->clear();
    QList<QTreeWidgetItem *> items;
    items.reserve(rows.size());
    QTreeWidgetItem *keep = nullptr;
    for (const dss::RunRow &row : rows) {
        items.append(runItem(row));
        if (!selected.isNull() && row.runId == selected)
            keep = items.last();
    }
    m_runs->addTopLevelItems(items);
    if (keep)
        m_runs->setCurrentItem(keep);
    m_runs->scrollToBottom();
    refreshRunButtons();
}

void ChatBotPage::applyRun(const dss::RunRows::Change &change, const dss::RunRow *row)
{
    const bool follow = atBottom(m_runs);
    for (int i = 0; i < change.removedFront && m_runs->topLevelItemCount() > 0; ++i)
        delete m_runs->takeTopLevelItem(0);
    if (row && change.index >= 0) {
        if (change.appended)
            m_runs->insertTopLevelItem(change.index, runItem(*row));
        else if (QTreeWidgetItem *item = m_runs->topLevelItem(change.index))
            fillRunItem(item, *row);
        if (change.appended && follow)
            m_runs->scrollToBottom();
    }
    refreshRunButtons();
}

void ChatBotPage::refreshRunButtons()
{
    // Ohne Auswahl ausgegraut; «Lauf abbrechen» nur bei wartenden und laufenden Läufen
    // (RunRow::cancellable), «Im Editor öffnen» nur mit einer Aktion zum Lauf.
    const QTreeWidgetItem *item = m_runs->currentItem();
    const bool selected = item && item->isSelected();
    m_openAction->setEnabled(selected && !item->data(0, kActionIdRole).toUuid().isNull());
    m_cancelRun->setEnabled(selected && item->data(0, kCancellableRole).toBool());
}
