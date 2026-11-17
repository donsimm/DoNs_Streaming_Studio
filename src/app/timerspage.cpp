#include "timerspage.h"

#include "panels.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

ListDetailPage::Setup timersPageSetup()
{
    ListDetailPage::Setup s;
    s.name = QStringLiteral("timers");
    s.listTitle = QStringLiteral("Zeitnachrichten");
    s.detailTitle = QStringLiteral("Zeitnachricht");
    s.emptyHint = QStringLiteral("Keine Zeitnachricht gewählt");
    s.tableName = QStringLiteral("Zeitnachrichten");
    s.columns = {{QStringLiteral("Aktiv"), 8},          {QStringLiteral("Name"), 18},
                 {QStringLiteral("Abstand"), 24},       {QStringLiteral("Chat-Zeilen"), 13},
                 {QStringLiteral("Nur im Stream"), 15}, {QStringLiteral("Aktionen"), 0}};
    s.addTip = QStringLiteral("Zeitnachricht hinzufügen");
    s.copyTip = QStringLiteral("Zeitnachricht kopieren");
    s.removeTip = QStringLiteral("Zeitnachricht löschen");
    return s;
}

TimerForm::TimerForm(QWidget *parent)
    : QWidget(parent),
      m_intervalRow(dss::minutesRow(QStringLiteral("interval_s"), QStringLiteral("Abstand"))),
      m_randomMinRow(dss::minutesRow(QStringLiteral("random_min_s"), QStringLiteral("von"))),
      m_randomMaxRow(dss::minutesRow(QStringLiteral("random_max_s"), QStringLiteral("bis")))
{
    setObjectName(QStringLiteral("timerForm"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(createTimerSection());
    column->addWidget(createIntervalSection());
    column->addWidget(createRunSection());
    column->addStretch(1);
    setDetail(Detail(), true);
}

QWidget *TimerForm::createTimerSection()
{
    QGridLayout *grid = nullptr;
    QWidget *w = section(QStringLiteral("Zeitnachricht"), this, &grid);
    w->setObjectName(QStringLiteral("timerSection"));
    m_name = new QLineEdit(w);
    m_name->setObjectName(QStringLiteral("timerName"));
    m_enabled = new QCheckBox(QStringLiteral("Aktiv"), w);
    m_enabled->setObjectName(QStringLiteral("timerEnabled"));
    m_repeat = new QCheckBox(QStringLiteral("Wiederholen"), w);
    m_repeat->setObjectName(QStringLiteral("timerRepeat"));

    grid->addWidget(fieldLabel(QStringLiteral("Name"), m_name, w), 0, 0);
    grid->addWidget(m_name, 0, 1);
    grid->addWidget(m_enabled, 1, 0, 1, 2);
    grid->addWidget(m_repeat, 2, 0, 1, 2);
    m_problems[TimerSection] = problemLine(w);
    grid->addWidget(m_problems[TimerSection], 3, 0, 1, 2);
    grid->setColumnStretch(1, 1);

    connect(m_name, &QLineEdit::editingFinished, this, &TimerForm::finishNameEdit);
    connect(m_enabled, &QCheckBox::clicked, this, [this](bool checked) {
        if (!m_filling && !m_shown.isNull())
            emit enabledToggled(checked);
    });
    connect(m_repeat, &QCheckBox::clicked, this, [this](bool checked) {
        if (!m_filling && !m_shown.isNull())
            emit repeatToggled(checked);
    });
    return w;
}

FieldSpinBox *TimerForm::minutesSpin(const dss::FieldRow &row, const QString &objectName, QWidget *parent)
{
    // Wie die Zahlenfelder in «Aktionen» rechts (actionfieldspanel.cpp): Komma, Einheit und Grenzen
    // aus der Logik, keepValueVisible misst den längsten Wert
    auto *spin = new FieldSpinBox(parent);
    spin->setObjectName(objectName);
    spin->setDecimals(row.decimals);
    spin->setRange(row.min, row.max);
    spin->setSuffix(QLatin1Char(' ') + row.suffix);
    spin->setKeyboardTracking(false);
    keepValueVisible(spin);
    return spin;
}

void TimerForm::connectMinutes(FieldSpinBox *spin, const dss::FieldRow &row, void (TimerForm::*edited)(int))
{
    connect(spin, &QDoubleSpinBox::valueChanged, this, [this, row, edited](double shown) {
        if (!m_filling && !m_shown.isNull())
            emit(this->*edited)(int(dss::fromDisplay(row, shown)));
    });
}

QWidget *TimerForm::createIntervalSection()
{
    QGridLayout *grid = nullptr;
    QWidget *w = section(QStringLiteral("Abstand"), this, &grid);
    w->setObjectName(QStringLiteral("timerIntervalSection"));
    m_interval = minutesSpin(m_intervalRow, QStringLiteral("timerInterval"), w);
    m_random = new QCheckBox(QStringLiteral("Zufällig"), w);
    m_random->setObjectName(QStringLiteral("timerRandom"));
    m_randomMin = minutesSpin(m_randomMinRow, QStringLiteral("timerRandomMin"), w);
    m_randomMin->setAccessibleName(QStringLiteral("Abstand von"));
    m_randomMax = minutesSpin(m_randomMaxRow, QStringLiteral("timerRandomMax"), w);
    m_randomMax->setAccessibleName(QStringLiteral("Abstand bis"));
    m_randomMinLabel = fieldLabel(m_randomMinRow.label, m_randomMin, w);
    m_randomMaxLabel = fieldLabel(m_randomMaxRow.label, m_randomMax, w);
    // Chat-Zeilen: Grenze aus der Logik (putTimer)
    m_lines = new QSpinBox(w);
    m_lines->setObjectName(QStringLiteral("timerLines"));
    m_lines->setRange(0, dss::kMaxTimerLines);
    m_lines->setKeyboardTracking(false);
    keepValueVisible(m_lines);
    m_onlyLive = new QCheckBox(QStringLiteral("Nur im Stream"), w);
    m_onlyLive->setObjectName(QStringLiteral("timerOnlyLive"));

    m_intervalLabel = fieldLabel(m_intervalRow.label, m_interval, w);
    grid->addWidget(m_intervalLabel, 0, 0);
    grid->addWidget(m_interval, 0, 1, Qt::AlignLeft);
    grid->addWidget(m_random, 1, 0, 1, 2);
    grid->addWidget(m_randomMinLabel, 2, 0);
    grid->addWidget(m_randomMin, 2, 1, Qt::AlignLeft);
    grid->addWidget(m_randomMaxLabel, 3, 0);
    grid->addWidget(m_randomMax, 3, 1, Qt::AlignLeft);
    grid->addWidget(fieldLabel(QStringLiteral("Chat-Zeilen"), m_lines, w), 4, 0);
    grid->addWidget(m_lines, 4, 1, Qt::AlignLeft);
    grid->addWidget(m_onlyLive, 5, 0, 1, 2);
    m_problems[IntervalSection] = problemLine(w);
    grid->addWidget(m_problems[IntervalSection], 6, 0, 1, 2);
    grid->setColumnStretch(1, 1);

    connectMinutes(m_interval, m_intervalRow, &TimerForm::intervalEdited);
    connectMinutes(m_randomMin, m_randomMinRow, &TimerForm::randomMinEdited);
    connectMinutes(m_randomMax, m_randomMaxRow, &TimerForm::randomMaxEdited);
    connect(m_random, &QCheckBox::clicked, this, [this](bool checked) {
        if (!m_filling && !m_shown.isNull())
            emit randomToggled(checked);
    });
    connect(m_lines, &QSpinBox::valueChanged, this, [this](int value) {
        if (!m_filling && !m_shown.isNull())
            emit linesEdited(value);
    });
    connect(m_onlyLive, &QCheckBox::clicked, this, [this](bool checked) {
        if (!m_filling && !m_shown.isNull())
            emit onlyLiveToggled(checked);
    });
    return w;
}

QWidget *TimerForm::createRunSection()
{
    QGridLayout *grid = nullptr;
    QWidget *w = section(QStringLiteral("Ausführen"), this, &grid);
    w->setObjectName(QStringLiteral("timerRunSection"));
    m_action = new QComboBox(w);
    m_action->setObjectName(QStringLiteral("timerAction"));
    m_open = new QPushButton(QStringLiteral("Öffnen"), w);
    m_open->setObjectName(QStringLiteral("timerOpenAction"));
    auto *row = new QWidget(w);
    auto *line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(8);
    line->addWidget(m_action, 1);
    line->addWidget(m_open);
    grid->addWidget(fieldLabel(QStringLiteral("Aktion"), m_action, w), 0, 0);
    grid->addWidget(row, 0, 1);
    m_problems[RunSection] = problemLine(w);
    grid->addWidget(m_problems[RunSection], 1, 0, 1, 2);
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

void TimerForm::setDetail(const Detail &detail, bool resetFields)
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
    // Name: Wert des Kerns, ausser der Nutzer schreibt gerade und der Kern hat nichts Neues
    if (resetFields || other || !m_name->hasFocus() || detail.name != m_coreName) {
        if (m_name->text() != detail.name)
            m_name->setText(detail.name);
        m_shownName = detail.name;
    }
    m_coreName = detail.name;
    m_enabled->setChecked(detail.enabled);
    m_repeat->setChecked(detail.repeat);

    m_interval->setValue(dss::toDisplay(m_intervalRow, detail.intervalS));
    m_random->setChecked(detail.random);
    m_randomMin->setValue(dss::toDisplay(m_randomMinRow, detail.randomMinS));
    m_randomMax->setValue(dss::toDisplay(m_randomMaxRow, detail.randomMaxS));
    m_lines->setValue(detail.lines);
    m_onlyLive->setChecked(detail.onlyLive);

    m_action->clear();
    for (const dss::ActionChoice &choice : detail.actions)
        m_action->addItem(choice.label, choice.id);
    int actionIndex = -1;
    for (int i = 0; i < detail.actions.size(); ++i)
        if (detail.actions.at(i).id == detail.action)
            actionIndex = i;
    m_action->setCurrentIndex(actionIndex);

    for (QWidget *field :
         std::initializer_list<QWidget *>{m_name, m_enabled, m_repeat, m_random, m_lines, m_onlyLive, m_action})
        field->setEnabled(editable);
    // «von» und «bis» ohne «Zufällig» nur ausgegraut, «Abstand» mit «Zufällig»; nie ausgeblendet
    // (Entscheide des Nutzers, 2026-11-16, Regel 9)
    for (QWidget *field : std::initializer_list<QWidget *>{m_intervalLabel, m_interval})
        field->setEnabled(editable && !detail.random);
    for (QWidget *field : std::initializer_list<QWidget *>{m_randomMinLabel, m_randomMin, m_randomMaxLabel, m_randomMax})
        field->setEnabled(editable && detail.random);
    m_open->setEnabled(editable && !detail.action.isNull());
    m_filling = false;
}

void TimerForm::finishNameEdit()
{
    if (m_filling || m_shown.isNull() || m_name->text() == m_shownName)
        return;
    m_shownName = m_name->text();
    emit nameEdited(m_shownName);
}

void TimerForm::setSectionProblem(Section section, const QString &text)
{
    if (section == NoSection)
        return;
    m_problems[section]->setText(text);
    m_problems[section]->setToolTip(text);
}

QString TimerForm::sectionProblem(Section section) const
{
    return section == NoSection ? QString() : m_problems[section]->text();
}
