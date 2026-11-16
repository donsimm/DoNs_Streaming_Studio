#include "actionspage.h"

#include "actionfieldspanel.h"
#include "panels.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStandardItemModel>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <functional>

namespace {

// Daten einer Zeile im Baum
constexpr int kActionIdRole = Qt::UserRole;     // Kennung der Aktion, null bei Gruppen
constexpr int kGroupRole = Qt::UserRole + 1;    // Gruppe der Zeile, leer = ohne
constexpr int kIsGroupRole = Qt::UserRole + 2;
// Zeile eines Schritts oder Auslösers in der Mitte
constexpr int kItemIdRole = Qt::UserRole;

// Menü eines Knopfs «hinzufügen» aus den Einträgen nach Abschnitten (stepChoices, triggerChoices)
void fillTypeMenu(QToolButton *button, const QList<dss::TypeChoice> &choices,
                  const std::function<void(const QString &)> &chosen)
{
    auto *menu = new QMenu(button);
    QString section;
    for (const dss::TypeChoice &choice : choices) {
        if (choice.section != section) {
            section = choice.section;
            menu->addSection(section);
        }
        const QString key = choice.key;
        menu->addAction(choice.label, button, [chosen, key] { chosen(key); });
    }
    button->setMenu(menu);
    button->setPopupMode(QToolButton::InstantPopup);
}

// Liste der Mitte wie der Baum links: ohne Kopf, Name so breit wie nötig, Rest gedehnt
QTreeWidget *detailTree(const QString &objectName, const QString &accessibleName)
{
    auto *tree = new QTreeWidget;
    tree->setObjectName(objectName);
    tree->setAccessibleName(accessibleName);
    tree->setHeaderHidden(true);
    tree->setColumnCount(2);
    tree->setSelectionMode(QAbstractItemView::SingleSelection);
    tree->setIndentation(14);
    tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setStretchLastSection(true);
    return tree;
}

// Auswahl nach dem Neufüllen einer Liste der Mitte: gewünschte Kennung, sonst keptSelection
QTreeWidgetItem *keptItem(QTreeWidget *tree, const QList<QUuid> &before, const QList<QUuid> &after,
                          const QUuid &previous)
{
    if (previous.isNull())
        return nullptr;
    const QUuid id = dss::keptSelection(before, after, previous);
    for (QTreeWidgetItemIterator it(tree); *it; ++it) {
        if (!id.isNull() && (*it)->data(0, kItemIdRole).toUuid() == id)
            return *it;
    }
    return nullptr;
}

} // namespace

ActionsPage::ActionsPage(const Theme &theme, QWidget *parent) : QWidget(parent), m_theme(theme)
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

    createActionArea();

    // P14 e2-16, rechts: Felder des gewählten Schritts oder Auslösers
    m_fields = new ActionFieldsPanel(m_areas[Detail]);
    m_areas[Detail]->layout()->addWidget(m_fields);
}

void ActionsPage::createActionArea()
{
    // P14 e2-15, Mitte: Abschnitte wie im Dock «Eigenschaften» (section), darunter die Leiste
    // wie unter den Docks (buttonBar). Nur Werte und Freigaben ändern sich, nie die Anordnung.
    QWidget *area = m_areas[Action];
    QWidget *actionSection = createActionSection();
    QWidget *triggerSection = createTriggerSection();
    QWidget *stepSection = createStepSection();

    m_stepAdd = symbolButton(QStringLiteral("hinzufuegen.svg"), QStringLiteral("+"),
                             QStringLiteral("Schritt hinzufügen"), nullptr);
    m_stepAdd->setObjectName(QStringLiteral("actionStepAdd"));
    fillTypeMenu(m_stepAdd, dss::stepChoices(), [this](const QString &type) { emit stepAddChosen(type); });
    m_stepRemove = symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"),
                                QStringLiteral("Schritt löschen"), nullptr);
    m_stepRemove->setObjectName(QStringLiteral("actionStepRemove"));
    m_stepUp = symbolButton(QStringLiteral("nach-oben.svg"), QStringLiteral("↑"),
                            QStringLiteral("Schritt nach oben"), nullptr);
    m_stepUp->setObjectName(QStringLiteral("actionStepUp"));
    m_stepDown = symbolButton(QStringLiteral("nach-unten.svg"), QStringLiteral("↓"),
                              QStringLiteral("Schritt nach unten"), nullptr);
    m_stepDown->setObjectName(QStringLiteral("actionStepDown"));
    connect(m_stepRemove, &QToolButton::clicked, this, &ActionsPage::stepRemoveClicked);
    connect(m_stepUp, &QToolButton::clicked, this, [this] { emit stepMoveClicked(-1); });
    connect(m_stepDown, &QToolButton::clicked, this, [this] { emit stepMoveClicked(1); });

    auto *testLabel = new QLabel(QStringLiteral("Testwerte: Benutzer"));
    m_testUser = new QComboBox;
    m_testUser->setObjectName(QStringLiteral("actionTestUser"));
    m_testUser->setEnabled(false);
    testLabel->setBuddy(m_testUser);
    m_test = new QPushButton(QStringLiteral("▶ Testen"));
    m_test->setObjectName(QStringLiteral("actionTest"));
    m_test->setEnabled(false);
    m_realTest = new QCheckBox(QStringLiteral("echt ausführen"));
    m_realTest->setObjectName(QStringLiteral("actionRealTest"));
    m_realTest->setEnabled(false);
    connect(m_test, &QPushButton::clicked, this, &ActionsPage::testClicked);

    auto *column = static_cast<QVBoxLayout *>(area->layout());
    column->setSpacing(0);
    column->addWidget(actionSection);
    column->addWidget(triggerSection, 1);
    column->addWidget(stepSection, 2);
    column->addWidget(buttonBar({m_stepAdd, m_stepRemove, m_stepUp, m_stepDown},
                                {testLabel, m_testUser, m_test, m_realTest}, area));
}

QWidget *ActionsPage::createActionSection()
{
    QGridLayout *grid = nullptr;
    QWidget *w = section(QStringLiteral("Aktion"), nullptr, &grid);
    w->setObjectName(QStringLiteral("actionSection"));
    m_name = new QLineEdit(w);
    m_name->setObjectName(QStringLiteral("actionName"));
    m_group = new QComboBox(w);
    m_group->setObjectName(QStringLiteral("actionGroup"));
    m_group->setEditable(true);
    m_group->setInsertPolicy(QComboBox::NoInsert);
    m_queue = new QComboBox(w);
    m_queue->setObjectName(QStringLiteral("actionQueue"));
    m_enabled = new QCheckBox(QStringLiteral("Aktiv"), w);
    m_enabled->setObjectName(QStringLiteral("actionEnabled"));
    m_hint = new QLabel(QStringLiteral("Keine Aktion gewählt"), w);
    m_hint->setObjectName(QStringLiteral("hintLabel"));
    m_hint->ensurePolished();
    m_hint->setMinimumHeight(m_hint->sizeHint().height());
    for (QWidget *field : {static_cast<QWidget *>(m_name), static_cast<QWidget *>(m_group),
                           static_cast<QWidget *>(m_queue), static_cast<QWidget *>(m_enabled)})
        field->setEnabled(false);

    const auto label = [w](const QString &text, QWidget *buddy) {
        auto *l = new QLabel(text, w);
        l->setBuddy(buddy);
        return l;
    };
    grid->addWidget(label(QStringLiteral("Name"), m_name), 0, 0);
    grid->addWidget(m_name, 0, 1);
    grid->addWidget(label(QStringLiteral("Gruppe"), m_group), 0, 2);
    grid->addWidget(m_group, 0, 3);
    grid->addWidget(label(QStringLiteral("Warteschlange"), m_queue), 1, 0);
    grid->addWidget(m_queue, 1, 1);
    grid->addWidget(m_enabled, 1, 2, 1, 2);
    grid->addWidget(m_hint, 2, 1, 1, 3);
    m_problems[ActionSection] = problemLine(w);
    grid->addWidget(m_problems[ActionSection], 3, 0, 1, 4);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);

    connect(m_name, &QLineEdit::editingFinished, this, &ActionsPage::finishNameEdit);
    connect(m_group->lineEdit(), &QLineEdit::editingFinished, this, &ActionsPage::finishGroupEdit);
    connect(m_group, &QComboBox::activated, this, &ActionsPage::finishGroupEdit);
    connect(m_queue, &QComboBox::activated, this, [this](int index) {
        // «Warteschlangen verwalten …» ist ausgegraut (Wirkung mit e3) und nicht wählbar
        if (!m_detailFilling && index >= 0)
            emit queueChosen(m_queue->itemData(index).toUuid());
    });
    connect(m_enabled, &QCheckBox::clicked, this, [this](bool checked) {
        if (!m_detailFilling && !m_shownAction.isNull())
            emit actionEnabledToggled(m_shownAction, checked);
    });
    return w;
}

QWidget *ActionsPage::createTriggerSection()
{
    m_triggerAdd = symbolButton(QStringLiteral("hinzufuegen.svg"), QStringLiteral("+"),
                                QStringLiteral("Auslöser hinzufügen"), nullptr);
    m_triggerAdd->setObjectName(QStringLiteral("actionTriggerAdd"));
    fillTypeMenu(m_triggerAdd, dss::triggerChoices(), [this](const QString &type) { emit triggerAddChosen(type); });
    m_triggerRemove = symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"),
                                   QStringLiteral("Auslöser löschen"), nullptr);
    m_triggerRemove->setObjectName(QStringLiteral("actionTriggerRemove"));
    connect(m_triggerRemove, &QToolButton::clicked, this, &ActionsPage::triggerRemoveClicked);

    QGridLayout *grid = nullptr;
    QWidget *w = section(QStringLiteral("Auslöser"), nullptr, &grid, {m_triggerAdd, m_triggerRemove});
    w->setObjectName(QStringLiteral("triggerSection"));
    m_triggers = detailTree(QStringLiteral("actionTriggers"), QStringLiteral("Auslöser"));
    m_triggers->setRootIsDecorated(false);
    m_triggers->setEnabled(false);
    grid->addWidget(m_triggers, 0, 0);
    m_problems[TriggerSection] = problemLine(w);
    grid->addWidget(m_problems[TriggerSection], 1, 0);
    grid->setRowStretch(0, 1);

    connect(m_triggers, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current) {
        if (m_detailFilling)
            return;
        // Rechts steht entweder ein Auslöser oder ein Schritt
        if (current && m_steps->currentItem())
            m_steps->setCurrentItem(nullptr);
        emit triggerSelected(current ? current->data(0, kItemIdRole).toUuid() : QUuid());
    });
    connect(m_triggers, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (!m_detailFilling && column == 0)
            emit triggerEnabledToggled(item->data(0, kItemIdRole).toUuid(), item->checkState(0) == Qt::Checked);
    });
    return w;
}

QWidget *ActionsPage::createStepSection()
{
    QGridLayout *grid = nullptr;
    QWidget *w = section(QStringLiteral("Schritte"), nullptr, &grid);
    w->setObjectName(QStringLiteral("stepSection"));
    m_steps = detailTree(QStringLiteral("actionSteps"), QStringLiteral("Schritte"));
    // Abgeschaltete Schritte rot (stateError aus dem Theme): Farbe in Bäumen nur über den Delegaten
    m_steps->setItemDelegate(new ForegroundDelegate(m_steps));
    m_steps->setEnabled(false);
    grid->addWidget(m_steps, 0, 0);
    m_problems[StepSection] = problemLine(w);
    grid->addWidget(m_problems[StepSection], 1, 0);
    grid->setRowStretch(0, 1);

    connect(m_steps, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *current) {
        if (m_detailFilling)
            return;
        if (current && m_triggers->currentItem())
            m_triggers->setCurrentItem(nullptr);
        emit stepSelected(current ? current->data(0, kItemIdRole).toUuid() : QUuid());
    });
    connect(m_steps, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *item, int column) {
        if (!m_detailFilling && column == 0 && (item->flags() & Qt::ItemIsUserCheckable))
            emit stepEnabledToggled(item->data(0, kItemIdRole).toUuid(), item->checkState(0) == Qt::Checked);
    });
    return w;
}

void ActionsPage::setActionDetail(const ActionDetail &detail, bool resetFields)
{
    const bool other = detail.actionId != m_shownAction;
    const bool present = !detail.actionId.isNull();
    const bool editable = present && detail.editable;
    m_detailFilling = true;
    m_shownAction = detail.actionId;
    if (other) {
        for (QLabel *problem : m_problems)
            problem->clear();
        m_wantedStep = QUuid();
        m_wantedTrigger = QUuid();
    }
    // Textfelder: Wert des Kerns, ausser der Nutzer schreibt gerade und der Kern hat nichts Neues
    if (resetFields || other || !m_name->hasFocus() || detail.name != m_coreName) {
        if (m_name->text() != detail.name)
            m_name->setText(detail.name);
        m_shownName = detail.name;
    }
    m_coreName = detail.name;
    if (resetFields || other || !m_group->hasFocus() || detail.group != m_coreGroup) {
        m_group->clear();
        m_group->addItems(detail.groups);
        m_group->setEditText(detail.group);
        m_shownGroup = detail.group;
    }
    m_coreGroup = detail.group;

    m_queue->clear();
    for (const dss::QueueChoice &choice : detail.queues)
        m_queue->addItem(choice.label, choice.id);
    if (auto *model = qobject_cast<QStandardItemModel *>(m_queue->model())) {
        for (int i = 0; i < detail.queues.size(); ++i) {
            if (detail.queues.at(i).manage)
                model->item(i)->setEnabled(false);
        }
    }
    m_queue->setCurrentIndex(dss::queueChoiceIndex(detail.queues, detail.queue));
    m_enabled->setChecked(present && detail.enabled);
    m_hint->setText(detail.hint);
    for (QWidget *field : {static_cast<QWidget *>(m_name), static_cast<QWidget *>(m_group),
                           static_cast<QWidget *>(m_queue), static_cast<QWidget *>(m_enabled),
                           static_cast<QWidget *>(m_triggers), static_cast<QWidget *>(m_steps)})
        field->setEnabled(editable);
    if (other) {
        m_triggerIds.clear();
        m_stepIds.clear();
        m_triggers->setCurrentItem(nullptr);
        m_steps->setCurrentItem(nullptr);
    }
    setTriggerRows(detail.triggers);
    setStepRows(detail.steps);
    m_detailFilling = false;
    emit triggerSelected(selectedTrigger());
    emit stepSelected(selectedStep());
}

void ActionsPage::setTriggerRows(const QList<dss::TriggerRow> &rows)
{
    const QUuid previous = m_wantedTrigger.isNull() ? selectedTrigger() : m_wantedTrigger;
    const QList<QUuid> before = m_triggerIds;
    m_triggers->clear();
    m_triggerIds.clear();
    for (const dss::TriggerRow &row : rows) {
        auto *item = new QTreeWidgetItem(m_triggers, QStringList{row.text, row.detail});
        item->setData(0, kItemIdRole, row.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(0, row.enabled ? Qt::Checked : Qt::Unchecked);
        if (!row.warning.isEmpty())
            item->setToolTip(0, row.warning);
        m_triggerIds.append(row.id);
    }
    if (m_triggerIds.contains(m_wantedTrigger))
        m_wantedTrigger = QUuid();
    // Gewählt ist höchstens eines von beiden (selectStep und selectTrigger leeren die andere Liste)
    m_triggers->setCurrentItem(keptItem(m_triggers, before, m_triggerIds, previous));
}

void ActionsPage::setStepRows(const QList<dss::StepRow> &rows)
{
    const QUuid previous = m_wantedStep.isNull() ? selectedStep() : m_wantedStep;
    const QList<QUuid> before = m_stepIds;
    m_steps->clear();
    m_stepIds.clear();
    QList<QTreeWidgetItem *> parents; // parents[d]: letzte Zeile der Tiefe d
    QFont typeFont;
    typeFont.setBold(true);
    for (const dss::StepRow &row : rows) {
        QTreeWidgetItem *parent = row.depth > 0 && row.depth <= parents.size() ? parents.at(row.depth - 1) : nullptr;
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_steps);
        parents.resize(row.depth);
        parents.append(item);
        item->setText(0, row.type);
        item->setText(1, row.summary);
        item->setData(0, kItemIdRole, row.id);
        if (row.branch) {
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable);
        } else {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, row.enabled ? Qt::Checked : Qt::Unchecked);
        }
        // Art fett; abgeschaltet (selbst oder darüber) rot durchgestrichen (Plan 3.1)
        QFont font0 = typeFont;
        QFont font1;
        if (row.inactive) {
            font0.setStrikeOut(true);
            font1.setStrikeOut(true);
            item->setForeground(0, m_theme.stateError);
            item->setForeground(1, m_theme.stateError);
        }
        item->setFont(0, font0);
        item->setFont(1, font1);
        if (!row.warning.isEmpty()) {
            item->setToolTip(0, row.warning);
            item->setToolTip(1, row.warning);
        }
        item->setExpanded(true);
        m_stepIds.append(row.id);
    }
    if (m_stepIds.contains(m_wantedStep))
        m_wantedStep = QUuid();
    m_steps->setCurrentItem(keptItem(m_steps, before, m_stepIds, previous));
}

void ActionsPage::finishNameEdit()
{
    if (m_detailFilling || m_shownAction.isNull() || m_name->text() == m_shownName)
        return;
    m_shownName = m_name->text();
    emit nameEdited(m_shownName);
}

void ActionsPage::finishGroupEdit()
{
    if (m_detailFilling || m_shownAction.isNull() || m_group->currentText() == m_shownGroup)
        return;
    m_shownGroup = m_group->currentText();
    emit groupEdited(m_shownGroup);
}

void ActionsPage::setSectionProblem(Section section, const QString &text)
{
    if (section == NoSection)
        return;
    if (section == DetailSection) {
        m_fields->setProblem(text);
        return;
    }
    m_problems[section]->setText(text);
    m_problems[section]->setToolTip(text);
}

QString ActionsPage::sectionProblem(Section section) const
{
    if (section == DetailSection)
        return m_fields->problem();
    return section == NoSection ? QString() : m_problems[section]->text();
}

void ActionsPage::setStepButtons(bool add, const dss::StepButtons &buttons, bool removeTrigger)
{
    m_stepAdd->setEnabled(add);
    m_triggerAdd->setEnabled(add);
    m_stepRemove->setEnabled(buttons.canRemove);
    m_stepUp->setEnabled(buttons.canMoveUp);
    m_stepDown->setEnabled(buttons.canMoveDown);
    m_triggerRemove->setEnabled(removeTrigger);
}

void ActionsPage::setTestState(const QList<dss::TestUserChoice> &users, bool test, bool real)
{
    const QString previous = m_testUser->currentText();
    QStringList labels;
    for (const dss::TestUserChoice &user : users)
        labels.append(user.label);
    QStringList shown;
    for (int i = 0; i < m_testUser->count(); ++i)
        shown.append(m_testUser->itemText(i));
    if (labels != shown) {
        const QSignalBlocker block(m_testUser);
        m_testUser->clear();
        m_testUser->addItems(labels);
        m_testUser->setCurrentIndex(qMax(0, labels.indexOf(previous)));
    }
    m_testUser->setEnabled(test && !users.isEmpty());
    m_test->setEnabled(test && !users.isEmpty());
    m_realTest->setEnabled(real);
}

int ActionsPage::testUserIndex() const
{
    return m_testUser->currentIndex();
}

bool ActionsPage::realTest() const
{
    return m_realTest->isEnabled() && m_realTest->isChecked();
}

void ActionsPage::selectStep(const QUuid &id)
{
    m_wantedTrigger = QUuid();
    m_triggers->setCurrentItem(nullptr);
    for (QTreeWidgetItemIterator it(m_steps); *it; ++it) {
        if ((*it)->data(0, kItemIdRole).toUuid() == id) {
            m_wantedStep = QUuid();
            m_steps->setCurrentItem(*it);
            return;
        }
    }
    m_wantedStep = id;
}

void ActionsPage::selectTrigger(const QUuid &id)
{
    m_wantedStep = QUuid();
    m_steps->setCurrentItem(nullptr);
    for (QTreeWidgetItemIterator it(m_triggers); *it; ++it) {
        if ((*it)->data(0, kItemIdRole).toUuid() == id) {
            m_wantedTrigger = QUuid();
            m_triggers->setCurrentItem(*it);
            return;
        }
    }
    m_wantedTrigger = id;
}

QUuid ActionsPage::selectedStep() const
{
    const QTreeWidgetItem *item = m_steps->currentItem();
    return item ? item->data(0, kItemIdRole).toUuid() : QUuid();
}

QUuid ActionsPage::selectedTrigger() const
{
    const QTreeWidgetItem *item = m_triggers->currentItem();
    return item ? item->data(0, kItemIdRole).toUuid() : QUuid();
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
