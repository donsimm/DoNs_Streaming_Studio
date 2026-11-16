#include "chatbotpage.h"

#include "actionspage.h"
#include "headerbar.h"
#include "listdetailpage.h"
#include "panels.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
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

// Mitlaufen nur, wenn die Liste schon unten stand (sonst liest der Nutzer gerade weiter oben).
bool atBottom(QTreeWidget *list)
{
    const QScrollBar *bar = list->verticalScrollBar();
    return bar->value() == bar->maximum();
}

// Änderung nach dss::RowsAppend: vorne removedFront Zeilen weg, dann item (falls da) anhängen;
// nach unten rollen nur, wenn die Liste schon unten stand.
void appendItem(QTreeWidget *list, int removedFront, QTreeWidgetItem *item)
{
    const bool follow = atBottom(list);
    for (int i = 0; i < removedFront && list->topLevelItemCount() > 0; ++i)
        delete list->takeTopLevelItem(0);
    if (item) {
        list->addTopLevelItem(item);
        if (follow)
            list->scrollToBottom();
    }
}

// Daten einer Zeile «Chat» für den Delegaten
constexpr int kChatEventRole = Qt::UserRole;
constexpr int kChatBadgesRole = Qt::UserRole + 1;
constexpr int kChatNameRole = Qt::UserRole + 2;
constexpr int kChatColorRole = Qt::UserRole + 3;
constexpr int kChatTextRole = Qt::UserRole + 4;

QTreeWidgetItem *chatItem(const dss::ChatRow &row)
{
    // Angezeigter Text auch als Text der Zelle (Suche, Tooltip, Tests); gezeichnet vom Delegaten
    const QString message = row.name.isEmpty() ? row.text : row.name + QStringLiteral(": ") + row.text;
    auto *item = new QTreeWidgetItem(QStringList{row.time, message});
    for (int column = 0; column < 2; ++column)
        item->setData(column, kChatEventRole, row.isEvent);
    item->setData(1, kChatBadgesRole, row.badges);
    item->setData(1, kChatNameRole, row.name);
    item->setData(1, kChatColorRole, row.color);
    item->setData(1, kChatTextRole, row.text);
    return item;
}

// Zeilen «Chat»: graue Ereigniszeilen (chatEventRow), Abzeichen in badgeBot/badgeChannel/badgeMod,
// Name in der Chatfarbe der Zeile. Wie ForegroundDelegate (panels.h) selbst gezeichnet, weil das Stylesheet
// Text- und Hintergrundfarbe aus den Rollen überdeckt. Farben nur aus dem Theme und der Chatfarbe.
class ChatRowDelegate : public QStyledItemDelegate
{
public:
    ChatRowDelegate(const Theme &theme, QObject *parent) : QStyledItemDelegate(parent), m_theme(theme) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool event = index.data(kChatEventRole).toBool();
        if (event && !option.state.testFlag(QStyle::State_Selected))
            painter->fillRect(option.rect, m_theme.chatEventRow);
        if (event || index.column() == 0) {
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
        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        painter->save();
        painter->setClipRect(textRect);
        painter->setRenderHint(QPainter::Antialiasing);
        const QFontMetrics &fm = opt.fontMetrics;
        const int height = fm.height();
        const int top = textRect.top() + (textRect.height() - height) / 2;
        int x = textRect.left();
        // Abzeichen: Kästchen mit Rundung 4 px wie die Listen, Innenabstand 4 px, Abstand 4 px
        QFont badgeFont = opt.font;
        badgeFont.setBold(true);
        const QFontMetrics badgeMetrics(badgeFont);
        painter->setFont(badgeFont);
        for (const QString &badge : index.data(kChatBadgesRole).toStringList()) {
            const QRect box(x, top, badgeMetrics.horizontalAdvance(badge) + 8, height);
            painter->setPen(Qt::NoPen);
            painter->setBrush(badgeColor(badge));
            painter->drawRoundedRect(box, 4, 4);
            painter->setPen(m_theme.text);
            painter->drawText(box, Qt::AlignCenter, badge);
            x = box.right() + 1 + 4;
        }
        // Name fett in der Chatfarbe (leer = Text), danach die Nachricht
        const QString name = index.data(kChatNameRole).toString() + QStringLiteral(": ");
        const QColor nameColor(index.data(kChatColorRole).toString());
        painter->setPen(nameColor.isValid() ? nameColor : m_theme.text);
        painter->drawText(QRect(x, textRect.top(), textRect.right() - x + 1, textRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, name);
        x += badgeMetrics.horizontalAdvance(name);
        painter->setFont(opt.font);
        painter->setPen(m_theme.text);
        const QRect rest(x, textRect.top(), textRect.right() - x + 1, textRect.height());
        painter->drawText(rest, Qt::AlignLeft | Qt::AlignVCenter,
                          fm.elidedText(index.data(kChatTextRole).toString(), Qt::ElideRight, rest.width()));
        painter->restore();
    }

private:
    QColor badgeColor(const QString &badge) const
    {
        // Texte der Abzeichen aus dss::chatRow (chatbotlive.cpp)
        if (badge == QLatin1String("Bot"))
            return m_theme.badgeBot;
        if (badge == QLatin1String("Mod"))
            return m_theme.badgeMod;
        return m_theme.badgeChannel;
    }

    Theme m_theme;
};

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
    m_events->setAccessibleName(QStringLiteral("Ereignisse"));
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
    // P14 e2-13: Leiste mit «Chat-Nachrichten ausblenden», Standard wie dss::EventRows
    m_hideChat = new QCheckBox(QStringLiteral("Chat-Nachrichten ausblenden"));
    m_hideChat->setObjectName(QStringLiteral("chatBotHideChat"));
    m_hideChat->setAccessibleName(QStringLiteral("Chat-Nachrichten ausblenden"));
    m_hideChat->setChecked(dss::EventRows().hideChat());
    connect(m_hideChat, &QCheckBox::toggled, this, &ChatBotPage::hideChatToggled);
    auto *eventsBox = new QWidget;
    auto *eventsColumn = new QVBoxLayout(eventsBox);
    eventsColumn->setContentsMargins(0, 0, 0, 0);
    eventsColumn->setSpacing(0);
    eventsColumn->addWidget(m_events, 1);
    eventsColumn->addWidget(buttonBar({m_hideChat}, {}, eventsBox));
    addLivePanel(QStringLiteral("Ereignisse"), eventsBox, 1);

    // Bereich «Läufe der Aktionen»: Tabelle wie «Ereignisse», darunter die Knopfleiste
    m_runs = new QTreeWidget;
    m_runs->setObjectName(QStringLiteral("chatBotRuns"));
    m_runs->setAccessibleName(QStringLiteral("Läufe der Aktionen"));
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
    m_runs->setItemDelegateForColumn(3, new ForegroundDelegate(m_runs));
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

    // P14 e2-13: Bereich «Chat» links: Liste wie «Ereignisse» ohne Spaltenkopf (Zeit, Nachricht),
    // darunter die Eingabezeile als Leiste. Texte, Zähler und Freigabe nur aus setChatSend.
    m_chat = new QTreeWidget;
    m_chat->setObjectName(QStringLiteral("chatBotChat"));
    m_chat->setAccessibleName(QStringLiteral("Chat"));
    m_chat->setColumnCount(2);
    m_chat->setHeaderHidden(true);
    m_chat->setRootIsDecorated(false);
    m_chat->setUniformRowHeights(true);
    m_chat->setSelectionMode(QAbstractItemView::SingleSelection);
    m_chat->setFocusPolicy(Qt::NoFocus);
    m_chat->header()->setStretchLastSection(true);
    m_chat->setColumnWidth(0, digit * 10);
    m_chat->setItemDelegate(new ChatRowDelegate(m_theme, m_chat));

    m_chatAccount = new QComboBox;
    m_chatAccount->setObjectName(QStringLiteral("chatBotChatAccount"));
    m_chatAccount->setAccessibleName(QStringLiteral("Konto für den Chat"));
    m_chatText = new QLineEdit;
    m_chatText->setObjectName(QStringLiteral("chatBotChatText"));
    m_chatText->setAccessibleName(QStringLiteral("Nachricht an den Chat"));
    m_chatText->setPlaceholderText(QStringLiteral("Nachricht an den Chat …"));
    // Breite für «500 / 500»: Der Wechsel des Zählers ändert die Anordnung nicht (Regel 9).
    m_chatCounter = new QLabel;
    m_chatCounter->setObjectName(QStringLiteral("chatBotChatCounter"));
    m_chatCounter->setAccessibleName(QStringLiteral("Zeichen der Nachricht"));
    m_chatCounter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_chatCounter->setText(QStringLiteral("%1 / %1").arg(dss::kChatSendMaxLength));
    m_chatCounter->setMinimumWidth(m_chatCounter->sizeHint().width());
    m_chatSend = new QPushButton(QStringLiteral("Senden"));
    m_chatSend->setObjectName(QStringLiteral("chatBotChatSend"));
    m_chatSend->setEnabled(false);
    connect(m_chatText, &QLineEdit::textChanged, this, &ChatBotPage::chatInputChanged);
    connect(m_chatAccount, &QComboBox::currentIndexChanged, this, &ChatBotPage::chatInputChanged);
    connect(m_chatText, &QLineEdit::returnPressed, this, &ChatBotPage::sendChat);
    connect(m_chatSend, &QPushButton::clicked, this, &ChatBotPage::sendChat);

    auto *chatBox = new QWidget;
    auto *chatColumn = new QVBoxLayout(chatBox);
    chatColumn->setContentsMargins(0, 0, 0, 0);
    chatColumn->setSpacing(0);
    chatColumn->addWidget(m_chat, 1);
    // Das Eingabefeld nimmt den freien Platz statt der Dehnung von buttonBar.
    chatColumn->addWidget(buttonBar({m_chatAccount, m_chatText}, {m_chatCounter, m_chatSend}, chatBox, m_chatText));
    addLivePanel(QStringLiteral("Chat"), chatBox, 1, 0);

    m_actions = new ActionsPage(m_theme, m_pages);
    addSubTab(QStringLiteral("Aktionen"), m_actions);

    // P14 e3-4: «Befehle» und «Zeitnachrichten» als Tabelle links, Gewähltes rechts. Spalten aus
    // CommandRow bzw. TimerRow (actionview.h), Breiten in Ziffern der Schrift.
    ListDetailPage::Setup commands;
    commands.name = QStringLiteral("commands");
    commands.listTitle = QStringLiteral("Befehle");
    commands.detailTitle = QStringLiteral("Befehl");
    commands.emptyHint = QStringLiteral("Kein Befehl gewählt");
    commands.tableName = QStringLiteral("Befehle");
    commands.columns = {{QStringLiteral("Aktiv"), 8},         {QStringLiteral("Auslöser"), 18},
                        {QStringLiteral("Gruppe"), 12},       {QStringLiteral("Wer darf"), 18},
                        {QStringLiteral("Abkl. Kanal"), 13},  {QStringLiteral("Abkl. Person"), 14},
                        {QStringLiteral("Aktionen"), 20},     {QStringLiteral("Benutzt"), 0}};
    commands.search = true;
    commands.searchName = QStringLiteral("Befehle suchen");
    commands.filter = true;
    commands.filterName = QStringLiteral("Gruppe der Befehle");
    commands.addTip = QStringLiteral("Befehl hinzufügen");
    commands.copyTip = QStringLiteral("Befehl kopieren");
    commands.removeTip = QStringLiteral("Befehl löschen");
    commands.listHint = true;
    m_commands = new ListDetailPage(m_theme, commands, m_pages);
    addSubTab(QStringLiteral("Befehle"), m_commands);

    ListDetailPage::Setup timers;
    timers.name = QStringLiteral("timers");
    timers.listTitle = QStringLiteral("Zeitnachrichten");
    timers.detailTitle = QStringLiteral("Zeitnachricht");
    timers.emptyHint = QStringLiteral("Keine Zeitnachricht gewählt");
    timers.tableName = QStringLiteral("Zeitnachrichten");
    timers.columns = {{QStringLiteral("Aktiv"), 8},         {QStringLiteral("Name"), 18},
                      {QStringLiteral("Abstand"), 24},      {QStringLiteral("Chat-Zeilen"), 13},
                      {QStringLiteral("Nur im Stream"), 15}, {QStringLiteral("Aktionen"), 0}};
    timers.addTip = QStringLiteral("Zeitnachricht hinzufügen");
    timers.copyTip = QStringLiteral("Zeitnachricht kopieren");
    timers.removeTip = QStringLiteral("Zeitnachricht löschen");
    m_timers = new ListDetailPage(m_theme, timers, m_pages);
    addSubTab(QStringLiteral("Zeitnachrichten"), m_timers);

    // «Variablen»: «Für alle» und «Je Zuschauer» nebeneinander mit Teiler wie «Live»
    // (Entscheid des Nutzers 2026-11-16); Inhalt der Bereiche ab e3-7.
    auto *variables = new QSplitter(Qt::Horizontal, m_pages);
    variables->setObjectName(QStringLiteral("variablesSplitter"));
    variables->setChildrenCollapsible(false);
    const QString variableTitles[2] = {QStringLiteral("Für alle"), QStringLiteral("Je Zuschauer")};
    for (int i = 0; i < 2; ++i) {
        m_variableAreas[i] = new QWidget;
        auto *areaColumn = new QVBoxLayout(m_variableAreas[i]);
        areaColumn->setContentsMargins(0, 0, 0, 0);
        areaColumn->setSpacing(0);
        variables->addWidget(dockPanel(variableTitles[i], m_variableAreas[i], variables));
        variables->setStretchFactor(i, 1);
    }
    addSubTab(QStringLiteral("Variablen"), variables);
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

void ChatBotPage::appendEvent(const dss::RowsAppend &change, const dss::EventRow &row)
{
    appendItem(m_events, change.removedFront, change.appended ? eventItem(row) : nullptr);
}

void ChatBotPage::resetChat(const QList<dss::ChatRow> &rows)
{
    m_chat->clear();
    QList<QTreeWidgetItem *> items;
    items.reserve(rows.size());
    for (const dss::ChatRow &row : rows)
        items.append(chatItem(row));
    m_chat->addTopLevelItems(items);
    m_chat->scrollToBottom();
}

void ChatBotPage::appendChat(const dss::RowsAppend &change, const dss::ChatRow &row)
{
    appendItem(m_chat, change.removedFront, change.appended ? chatItem(row) : nullptr);
}

void ChatBotPage::setChatSend(const dss::ChatSendView &view)
{
    m_chatCounter->setText(view.counter);
    m_chatCounter->setProperty("state", view.tooLong ? QStringLiteral("error") : QString());
    // Das Stylesheet wertet [state="…"] nur beim Polieren aus (wie setHeader).
    m_chatCounter->style()->unpolish(m_chatCounter);
    m_chatCounter->style()->polish(m_chatCounter);
    m_chatSendEnabled = view.sendEnabled;
    m_chatSend->setEnabled(view.sendEnabled);
    m_chatSend->setToolTip(view.problem);
    m_chatText->setToolTip(view.problem);
    // Konten: beim ersten Mal anlegen; ein ausgegrautes gewähltes Konto bleibt gewählt (problem).
    const QSignalBlocker block(m_chatAccount);
    if (m_chatAccount->count() != view.accounts.size()) {
        m_chatAccount->clear();
        for (const dss::ChatSendAccount &account : view.accounts)
            m_chatAccount->addItem(account.label, account.account);
    }
    auto *model = qobject_cast<QStandardItemModel *>(m_chatAccount->model());
    for (int i = 0; i < view.accounts.size(); ++i) {
        const dss::ChatSendAccount &account = view.accounts.at(i);
        m_chatAccount->setItemText(i, account.label);
        m_chatAccount->setItemData(i, account.account);
        m_chatAccount->setItemData(i, account.toolTip, Qt::ToolTipRole);
        if (QStandardItem *item = model ? model->item(i) : nullptr)
            item->setEnabled(account.enabled);
    }
}

QString ChatBotPage::chatText() const
{
    return m_chatText->text();
}

QString ChatBotPage::chatAccount() const
{
    return m_chatAccount->currentData().toString();
}

void ChatBotPage::clearChatText()
{
    m_chatText->clear();
}

void ChatBotPage::sendChat()
{
    // Enter wie «Senden»: nur mit freigegebenem Senden (Prüfung in dss::chatSendView)
    if (m_chatSendEnabled)
        emit chatSendClicked(m_chatText->text(), chatAccount());
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
