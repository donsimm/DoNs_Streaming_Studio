#include "commandspage.h"

#include "panels.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {

QLabel *hintLine(const QString &text, QWidget *parent)
{
    auto *hint = new QLabel(text, parent);
    hint->setObjectName(QStringLiteral("hintLabel"));
    hint->setWordWrap(true);
    return hint;
}

QSpinBox *cooldownSpin(const QString &objectName, const QString &accessibleName, QWidget *parent)
{
    auto *spin = new QSpinBox(parent);
    spin->setObjectName(objectName);
    spin->setAccessibleName(accessibleName);
    // Grenze aus der Logik (putCommand); keepValueVisible misst den längsten Wert
    spin->setRange(0, dss::kMaxCooldownS);
    spin->setSuffix(QStringLiteral(" s"));
    spin->setKeyboardTracking(false);
    keepValueVisible(spin);
    return spin;
}

} // namespace

ListDetailPage::Setup commandsPageSetup()
{
    ListDetailPage::Setup s;
    s.name = QStringLiteral("commands");
    s.listTitle = QStringLiteral("Befehle");
    s.detailTitle = QStringLiteral("Befehl");
    s.emptyHint = QStringLiteral("Kein Befehl gewählt");
    s.tableName = QStringLiteral("Befehle");
    s.columns = {{QStringLiteral("Aktiv"), 8},         {QStringLiteral("Auslöser"), 18},
                 {QStringLiteral("Gruppe"), 12},       {QStringLiteral("Wer darf"), 18},
                 {QStringLiteral("Abkl. Kanal"), 13},  {QStringLiteral("Abkl. Person"), 14},
                 {QStringLiteral("Aktionen"), 20},     {QStringLiteral("Benutzt"), 0}};
    s.search = true;
    s.searchName = QStringLiteral("Befehle suchen");
    s.filter = true;
    s.filterName = QStringLiteral("Gruppe der Befehle");
    s.addTip = QStringLiteral("Befehl hinzufügen");
    s.copyTip = QStringLiteral("Befehl kopieren");
    s.removeTip = QStringLiteral("Befehl löschen");
    s.listHint = true;
    return s;
}

CommandForm::CommandForm(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("commandForm"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(createCommandSection());
    column->addWidget(createPermissionSection());
    column->addWidget(createCooldownSection());
    column->addWidget(createRunSection());
    column->addStretch(1);

    // «▶ Testen als …»: Menü «Testzuschauer» / «eigener Kanal», immer Probelauf (Entscheid des
    // Nutzers, 2026-11-16); «echt ausführen» nur in «Aktionen».
    m_test = new QPushButton(QStringLiteral("▶ Testen als …"), this);
    m_test->setObjectName(QStringLiteral("commandTest"));
    m_testMenu = new QMenu(m_test);
    m_testMenu->setObjectName(QStringLiteral("commandTestMenu"));
    m_test->setMenu(m_testMenu);
    m_test->setEnabled(false);
    // Die Leiste steht unter dem Bildlauf (ListDetailPage::setDetailBar), nicht im Formular
    m_testBar = buttonBar({}, {m_test}, this);
    m_testBar->hide();
    setDetail(Detail(), true);
}

QWidget *CommandForm::createCommandSection()
{
    QGridLayout *grid = nullptr;
    QWidget *w = section(QStringLiteral("Befehl"), this, &grid);
    w->setObjectName(QStringLiteral("commandSection"));
    m_name = new QLineEdit(w);
    m_name->setObjectName(QStringLiteral("commandName"));
    m_group = new QComboBox(w);
    m_group->setObjectName(QStringLiteral("commandGroup"));
    m_group->setEditable(true);
    m_group->setInsertPolicy(QComboBox::NoInsert);
    // Ein Auslöser je Zeile; Enter beginnt eine neue Zeile, gespeichert wird beim Verlassen
    m_commands = new QPlainTextEdit(w);
    m_commands->setObjectName(QStringLiteral("commandTriggers"));
    m_commands->setTabChangesFocus(true);
    m_commands->setFixedHeight(m_commands->fontMetrics().lineSpacing() * 3 + 16);
    m_commands->installEventFilter(this);
    m_location = new QComboBox(w);
    m_location->setObjectName(QStringLiteral("commandLocation"));
    m_enabled = new QCheckBox(QStringLiteral("Aktiv"), w);
    m_enabled->setObjectName(QStringLiteral("commandEnabled"));

    grid->addWidget(fieldLabel(QStringLiteral("Name"), m_name, w), 0, 0);
    grid->addWidget(m_name, 0, 1);
    grid->addWidget(fieldLabel(QStringLiteral("Gruppe"), m_group, w), 1, 0);
    grid->addWidget(m_group, 1, 1);
    grid->addWidget(fieldLabel(QStringLiteral("Auslöser"), m_commands, w), 2, 0, Qt::AlignTop);
    grid->addWidget(m_commands, 2, 1);
    grid->addWidget(hintLine(dss::commandFormHints().triggers, w), 3, 1);
    grid->addWidget(fieldLabel(QStringLiteral("Ort"), m_location, w), 4, 0);
    grid->addWidget(m_location, 4, 1);
    grid->addWidget(m_enabled, 5, 0, 1, 2);
    m_problems[CommandSection] = problemLine(w);
    grid->addWidget(m_problems[CommandSection], 6, 0, 1, 2);
    grid->setColumnStretch(1, 1);

    connect(m_name, &QLineEdit::editingFinished, this, &CommandForm::finishNameEdit);
    connect(m_group->lineEdit(), &QLineEdit::editingFinished, this, &CommandForm::finishGroupEdit);
    connect(m_group, &QComboBox::activated, this, &CommandForm::finishGroupEdit);
    connect(m_location, &QComboBox::activated, this, [this](int index) {
        if (!m_filling && !m_shown.isNull() && index >= 0)
            emit locationChosen(m_location->itemData(index).toString());
    });
    connect(m_enabled, &QCheckBox::clicked, this, [this](bool checked) {
        if (!m_filling && !m_shown.isNull())
            emit enabledToggled(checked);
    });
    return w;
}

QWidget *CommandForm::createPermissionSection()
{
    QGridLayout *grid = nullptr;
    QWidget *w = section(QStringLiteral("Wer darf"), this, &grid);
    w->setObjectName(QStringLiteral("commandPermissionSection"));
    // Rollen je ein Kästchen auf einer Zeile wie die Auswahl mehrerer Werte in «Aktionen» rechts
    auto *roles = new QWidget(w);
    auto *line = new QHBoxLayout(roles);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(8);
    m_all = new QCheckBox(QStringLiteral("Alle"), roles);
    m_all->setObjectName(QStringLiteral("commandPermissionAll"));
    m_subscriber = new QCheckBox(QStringLiteral("Abonnenten"), roles);
    m_subscriber->setObjectName(QStringLiteral("commandPermissionSubscriber"));
    m_vip = new QCheckBox(QStringLiteral("VIPs"), roles);
    m_vip->setObjectName(QStringLiteral("commandPermissionVip"));
    m_moderator = new QCheckBox(QStringLiteral("Moderatoren"), roles);
    m_moderator->setObjectName(QStringLiteral("commandPermissionModerator"));
    for (QCheckBox *box : {m_all, m_subscriber, m_vip, m_moderator}) {
        line->addWidget(box);
        connect(box, &QCheckBox::clicked, this, [this, box] { emitPermissions(box); });
    }
    line->addStretch(1);
    // Benutzergruppen untereinander (ihre Zahl ist offen)
    m_userGroupBox = new QWidget(w);
    m_userGroupBox->setObjectName(QStringLiteral("commandUserGroups"));
    m_userGroupColumn = new QVBoxLayout(m_userGroupBox);
    m_userGroupColumn->setContentsMargins(0, 0, 0, 0);
    m_userGroupColumn->setSpacing(0);
    m_ignoreBot = new QCheckBox(QStringLiteral("Nachrichten des Bots ignorieren"), w);
    m_ignoreBot->setObjectName(QStringLiteral("commandIgnoreBot"));

    grid->addWidget(roles, 0, 0, 1, 2);
    grid->addWidget(m_userGroupBox, 1, 0, 1, 2);
    grid->addWidget(hintLine(dss::commandFormHints().permissions, w), 2, 0, 1, 2);
    grid->addWidget(m_ignoreBot, 3, 0, 1, 2);
    m_problems[PermissionSection] = problemLine(w);
    grid->addWidget(m_problems[PermissionSection], 4, 0, 1, 2);
    grid->setColumnStretch(1, 1);

    connect(m_ignoreBot, &QCheckBox::clicked, this, [this](bool checked) {
        if (!m_filling && !m_shown.isNull())
            emit ignoreBotToggled(checked);
    });
    return w;
}

QWidget *CommandForm::createCooldownSection()
{
    QGridLayout *grid = nullptr;
    QWidget *w = section(QStringLiteral("Abklingzeit"), this, &grid);
    w->setObjectName(QStringLiteral("commandCooldownSection"));
    m_globalCooldown = cooldownSpin(QStringLiteral("commandGlobalCooldown"), QStringLiteral("Abklingzeit Kanal"), w);
    m_userCooldown = cooldownSpin(QStringLiteral("commandUserCooldown"), QStringLiteral("Abklingzeit je Person"), w);
    grid->addWidget(fieldLabel(QStringLiteral("Kanal"), m_globalCooldown, w), 0, 0);
    grid->addWidget(m_globalCooldown, 0, 1, Qt::AlignLeft);
    grid->addWidget(fieldLabel(QStringLiteral("Je Person"), m_userCooldown, w), 1, 0);
    grid->addWidget(m_userCooldown, 1, 1, Qt::AlignLeft);
    grid->addWidget(hintLine(dss::commandFormHints().cooldown, w), 2, 0, 1, 2);
    m_problems[CooldownSection] = problemLine(w);
    grid->addWidget(m_problems[CooldownSection], 3, 0, 1, 2);
    grid->setColumnStretch(1, 1);

    connect(m_globalCooldown, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_filling && !m_shown.isNull())
            emit globalCooldownEdited(value);
    });
    connect(m_userCooldown, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_filling && !m_shown.isNull())
            emit userCooldownEdited(value);
    });
    return w;
}

QWidget *CommandForm::createRunSection()
{
    QGridLayout *grid = nullptr;
    QWidget *w = section(QStringLiteral("Ausführen"), this, &grid);
    w->setObjectName(QStringLiteral("commandRunSection"));
    m_action = new QComboBox(w);
    m_action->setObjectName(QStringLiteral("commandAction"));
    m_open = new QPushButton(QStringLiteral("Öffnen"), w);
    m_open->setObjectName(QStringLiteral("commandOpenAction"));
    auto *row = new QWidget(w);
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(8);
    line->addWidget(m_action, 1);
    line->addWidget(m_open);
    grid->addWidget(fieldLabel(QStringLiteral("Aktion"), m_action, w), 0, 0);
    grid->addWidget(row, 0, 1);
    grid->addWidget(hintLine(dss::commandFormHints().run, w), 1, 0, 1, 2);
    m_problems[RunSection] = problemLine(w);
    grid->addWidget(m_problems[RunSection], 2, 0, 1, 2);
    grid->setColumnStretch(1, 1);

    connect(m_action, &QComboBox::activated, this, [this](int index) {
        if (!m_filling && !m_shown.isNull() && index >= 0)
            emit actionChosen(m_action->itemData(index).toUuid());
    });
    connect(m_open, &QPushButton::clicked, this, [this] {
        const QUuid id = m_action->currentData().toUuid();
        if (!id.isNull())
            emit openActionClicked(id);
    });
    return w;
}

void CommandForm::setDetail(const Detail &detail, bool resetFields)
{
    const bool other = detail.id != m_shown;
    const bool editable = !detail.id.isNull() && detail.editable;
    m_filling = true;
    m_shown = detail.id;
    if (other) {
        for (QLabel *problem : m_problems) {
            problem->clear();
            problem->setToolTip(QString());
        }
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
    if (resetFields || other || !m_commands->hasFocus() || detail.commandsText != m_coreCommands) {
        if (m_commands->toPlainText() != detail.commandsText)
            m_commands->setPlainText(detail.commandsText);
        m_shownCommands = detail.commandsText;
    }
    m_coreCommands = detail.commandsText;

    if (m_location->count() != detail.locations.size()) {
        m_location->clear();
        for (const dss::LocationChoice &choice : detail.locations)
            m_location->addItem(choice.label, choice.key);
    }
    m_location->setCurrentIndex(m_location->findData(detail.location));
    m_enabled->setChecked(detail.enabled);

    m_all->setChecked(detail.permissions.all);
    m_subscriber->setChecked(detail.permissions.subscriber);
    m_vip->setChecked(detail.permissions.vip);
    m_moderator->setChecked(detail.permissions.moderator);
    setUserGroups(detail.userGroups);
    for (int i = 0; i < m_userGroupChecks.size(); ++i) {
        const dss::UserGroupChoice &group = m_userGroups.at(i);
        // «Gruppe fehlt» bleibt angehakt, damit permissionsFromChoice sie behält (e3-2)
        m_userGroupChecks.at(i)->setChecked(group.missing || detail.permissions.groups.contains(group.id));
    }
    m_ignoreBot->setChecked(detail.ignoreBot);

    m_globalCooldown->setValue(detail.globalCooldown);
    m_userCooldown->setValue(detail.userCooldown);

    m_action->clear();
    for (const dss::ActionChoice &choice : detail.actions)
        m_action->addItem(choice.label, choice.id);
    int actionIndex = -1;
    for (int i = 0; i < detail.actions.size(); ++i)
        if (detail.actions.at(i).id == detail.action)
            actionIndex = i;
    m_action->setCurrentIndex(actionIndex);

    for (QWidget *field : std::initializer_list<QWidget *>{m_name, m_group, m_commands, m_location, m_enabled, m_all,
                                                           m_subscriber, m_vip, m_moderator, m_userGroupBox,
                                                           m_ignoreBot, m_globalCooldown, m_userCooldown, m_action})
        field->setEnabled(editable);
    m_open->setEnabled(editable && !detail.action.isNull());
    m_filling = false;
}

void CommandForm::setUserGroups(const QList<dss::UserGroupChoice> &groups)
{
    if (groups == m_userGroups)
        return;
    // Neue Liste der Gruppen (actions_config oder «Gruppe fehlt» beim gewählten Befehl)
    qDeleteAll(m_userGroupChecks);
    m_userGroupChecks.clear();
    m_userGroups = groups;
    for (const dss::UserGroupChoice &group : groups) {
        auto *box = new QCheckBox(group.label, m_userGroupBox);
        box->setObjectName(QStringLiteral("commandUserGroup"));
        m_userGroupColumn->addWidget(box);
        connect(box, &QCheckBox::clicked, this, [this, box] { emitPermissions(box); });
        m_userGroupChecks.append(box);
    }
}

void CommandForm::emitPermissions(QCheckBox *clicked)
{
    if (m_filling || m_shown.isNull())
        return;
    dss::PermissionChoice choice;
    // Eine angehakte Rolle oder Gruppe hebt «Alle» auf; «Alle» leert die übrigen (permissionsFromChoice)
    choice.all = m_all->isChecked() && !(clicked != m_all && clicked->isChecked());
    choice.subscriber = m_subscriber->isChecked();
    choice.vip = m_vip->isChecked();
    choice.moderator = m_moderator->isChecked();
    for (int i = 0; i < m_userGroupChecks.size(); ++i)
        if (m_userGroupChecks.at(i)->isChecked())
            choice.groups.append(m_userGroups.at(i).id);
    emit permissionsEdited(choice);
}

void CommandForm::finishNameEdit()
{
    if (m_filling || m_shown.isNull() || m_name->text() == m_shownName)
        return;
    m_shownName = m_name->text();
    emit nameEdited(m_shownName);
}

void CommandForm::finishGroupEdit()
{
    if (m_filling || m_shown.isNull() || m_group->currentText() == m_shownGroup)
        return;
    m_shownGroup = m_group->currentText();
    emit groupEdited(m_shownGroup);
}

void CommandForm::finishCommandsEdit()
{
    if (m_filling || m_shown.isNull() || m_commands->toPlainText() == m_shownCommands)
        return;
    m_shownCommands = m_commands->toPlainText();
    emit commandsEdited(m_shownCommands);
}

bool CommandForm::eventFilter(QObject *watched, QEvent *event)
{
    // «Auslöser» speichert beim Verlassen (QPlainTextEdit hat kein editingFinished)
    if (watched == m_commands && event->type() == QEvent::FocusOut)
        finishCommandsEdit();
    return QWidget::eventFilter(watched, event);
}

void CommandForm::setSectionProblem(Section section, const QString &text)
{
    if (section == NoSection)
        return;
    m_problems[section]->setText(text);
    m_problems[section]->setToolTip(text);
}

QString CommandForm::sectionProblem(Section section) const
{
    return section == NoSection ? QString() : m_problems[section]->text();
}

void CommandForm::setTestState(const QStringList &users, bool allowed)
{
    if (users != m_testUsers) {
        m_testUsers = users;
        m_testMenu->clear();
        for (int i = 0; i < users.size(); ++i)
            m_testMenu->addAction(users.at(i), this, [this, i] { emit testChosen(i); });
    }
    m_test->setEnabled(allowed && !users.isEmpty());
}
