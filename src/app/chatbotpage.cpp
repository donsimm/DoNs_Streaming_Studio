#include "chatbotpage.h"

#include "actionspage.h"
#include "headerbar.h"
#include "panels.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QTreeWidgetItem *eventItem(const dss::EventRow &row)
{
    return new QTreeWidgetItem(QStringList{row.time, row.kind, row.name, row.details});
}

} // namespace

ChatBotPage::ChatBotPage(QWidget *parent) : QWidget(parent)
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

    auto *live = new QWidget(m_pages);
    m_liveRow = new QHBoxLayout(live);
    m_liveRow->setContentsMargins(0, 0, 0, 0);
    m_liveRow->setSpacing(4);
    addSubTab(QStringLiteral("Live"), live);
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
    m_liveRow->insertWidget(position, panel, stretch);
    return panel;
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
    // Mitlaufen nur, wenn die Liste schon unten stand (sonst liest der Nutzer gerade weiter oben).
    QScrollBar *bar = m_events->verticalScrollBar();
    const bool atBottom = bar->value() == bar->maximum();
    for (int i = 0; i < removedFront && m_events->topLevelItemCount() > 0; ++i)
        delete m_events->takeTopLevelItem(0);
    m_events->addTopLevelItem(eventItem(row));
    if (atBottom)
        m_events->scrollToBottom();
}
