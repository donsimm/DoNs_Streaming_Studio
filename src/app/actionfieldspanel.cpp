#include "actionfieldspanel.h"

#include "panels.h"
#include "steptypes.h"
#include "triggertypes.h"

#include <QApplication>
#include <algorithm>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

using dss::actions::FieldKind;

namespace {

// Grösster Betrag in einem Zahlenfeld. Manche Felder haben keine obere Grenze (grösste double);
// keepValueVisible misst den längsten Wert, so breit darf kein Feld werden. 2^31 − 1 wie QSpinBox.
constexpr double kSpinLimit = 2147483647.0;

QString pageKey(ActionFieldsPanel::Target target, const QString &type)
{
    return (target == ActionFieldsPanel::Target::Step ? QStringLiteral("step:") : QStringLiteral("trigger:")) + type;
}

bool nullable(const dss::FieldRow &row)
{
    return row.kind == FieldKind::NullableString || row.kind == FieldKind::NullableUuid;
}

// Ein Feld, an dem der Nutzer gerade schreibt (auch im Editor einer Zelle)
bool hasFocusWithin(const QWidget *widget)
{
    const QWidget *focus = QApplication::focusWidget();
    return focus && (focus == widget || widget->isAncestorOf(focus));
}

// Liste der Elemente bzw. Testwerte wie die Listen der Mitte, mit fester Höhe für einige Zeilen
QTreeWidget *fieldList(const QString &objectName, const QString &accessibleName, bool columns)
{
    auto *list = new QTreeWidget;
    list->setObjectName(objectName);
    list->setAccessibleName(accessibleName);
    list->setRootIsDecorated(false);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setUniformRowHeights(true);
    if (columns) {
        list->setColumnCount(2);
        list->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Wert")});
        list->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                              | QAbstractItemView::SelectedClicked);
        list->header()->setStretchLastSection(true);
    } else {
        list->setHeaderHidden(true);
        list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    }
    // Höhe für fünf Zeilen; Wechsel der Einträge ändert die Anordnung nicht (Regel 9)
    list->setFixedHeight(list->fontMetrics().lineSpacing() * 5 + 16);
    return list;
}

} // namespace

ActionFieldsPanel::ActionFieldsPanel(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("actionFields"));
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    // Wie das Dock «Eigenschaften»: Bildlauf nur senkrecht, ohne Rahmen
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("actionFieldsScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_pages = new QStackedWidget(scroll);
    m_pages->setObjectName(QStringLiteral("actionFieldsPages"));
    scroll->setWidget(m_pages);
    column->addWidget(scroll);

    // Seite 0: keine Auswahl, Hinweis wie «Keine Quelle gewählt»
    auto *empty = new QWidget(m_pages);
    empty->setObjectName(QStringLiteral("fieldsPage:none"));
    auto *emptyColumn = new QVBoxLayout(empty);
    emptyColumn->setContentsMargins(8, 8, 8, 0);
    auto *hint = new QLabel(QStringLiteral("Kein Schritt oder Auslöser gewählt"), empty);
    hint->setObjectName(QStringLiteral("hintLabel"));
    emptyColumn->addWidget(hint);
    emptyColumn->addStretch(1);
    m_pages->addWidget(empty);
    m_pageList.append(Page{});
    m_pageList.first().widget = empty;

    for (const dss::actions::StepTypeInfo &t : dss::actions::stepTypes())
        buildPage(Target::Step, t.key, dss::stepForm(t.key));
    for (const dss::actions::TriggerTypeInfo &t : dss::actions::triggerTypes())
        buildPage(Target::Trigger, t.key, dss::triggerForm(t.key));
    m_pages->setCurrentIndex(0);
}

void ActionFieldsPanel::buildPage(Target target, const QString &type, const QList<dss::FieldSection> &form)
{
    Page page;
    page.target = target;
    page.type = type;
    page.form = form;
    page.widget = new QWidget(m_pages);
    page.widget->setObjectName(QStringLiteral("fieldsPage:") + pageKey(target, type));
    auto *column = new QVBoxLayout(page.widget);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    if (form.isEmpty()) {
        // Art ohne Einstellungen: grauer Hinweis wie ohne Auswahl
        auto *hint = new QLabel(dss::emptyFormHint(), page.widget);
        hint->setObjectName(QStringLiteral("hintLabel"));
        hint->setContentsMargins(8, 8, 8, 0);
        column->addWidget(hint);
    }
    for (const dss::FieldSection &section : form) {
        column->addWidget(buildSection(page, section, page.widget, false));
        // Overlay-Schritt: unter der Liste der Inhalt des gewählten Elements
        const bool elements = std::any_of(section.rows.cbegin(), section.rows.cend(), [](const dss::FieldRow &r) {
            return r.kind == FieldKind::ElementList;
        });
        if (elements)
            column->addWidget(buildSection(page, dss::elementForm(), page.widget, true));
    }
    column->addStretch(1);
    m_pages->addWidget(page.widget);
    const int index = int(m_pageList.size());
    m_pageIndex.insert(pageKey(target, type), index);
    m_pageList.append(page);
    for (int i = 0; i < page.fields.size(); ++i)
        connectField(index, i, false);
    for (int i = 0; i < page.elementFields.size(); ++i)
        connectField(index, i, true);
}

QWidget *ActionFieldsPanel::buildSection(Page &page, const dss::FieldSection &section, QWidget *parent,
                                         bool elements)
{
    // Felder zuerst: Listen bringen ihre Knöpfe «+» und «−» für die Titelzeile mit
    QList<Field> fields;
    QList<QToolButton *> titleButtons;
    for (const dss::FieldRow &row : section.rows) {
        Field field = buildField(row, nullptr, elements ? QStringLiteral("element:") : QStringLiteral("field:"));
        if (field.add)
            titleButtons << field.add << field.remove;
        fields.append(field);
    }
    QGridLayout *grid = nullptr;
    QWidget *w = ::section(section.title, parent, &grid, titleButtons);
    int r = 0;
    for (const Field &field : std::as_const(fields)) {
        if (field.check) {
            // Kästchen über beide Spalten wie im Dock «Eigenschaften»
            grid->addWidget(field.check, r, 0, 1, 2);
        } else if (field.list) {
            grid->addWidget(field.list, r, 0, 1, 2);
        } else {
            grid->addWidget(field.label, r, 0);
            grid->addWidget(field.editor, r, 1);
        }
        ++r;
    }
    for (const dss::FieldButton &b : section.buttons) {
        auto *button = new QPushButton(b.label, w);
        button->setObjectName(QStringLiteral("fieldButton:") + b.name);
        button->setEnabled(false);
        button->setToolTip(b.inactiveReason);
        grid->addWidget(button, r++, 1, Qt::AlignLeft);
        page.buttons.append(button);
    }
    if (!section.hint.isEmpty()) {
        auto *hint = new QLabel(section.hint, w);
        hint->setObjectName(QStringLiteral("hintLabel"));
        hint->setWordWrap(true);
        grid->addWidget(hint, r++, 0, 1, 2);
    }
    QLabel *problem = problemLine(w);
    grid->addWidget(problem, r, 0, 1, 2);
    grid->setColumnStretch(1, 1);
    if (elements) {
        page.elementProblem = problem;
        page.elementFields.append(fields);
    } else {
        page.problems.insert(section.title, problem);
        page.fields.append(fields);
    }
    return w;
}

ActionFieldsPanel::Field ActionFieldsPanel::buildField(const dss::FieldRow &row, QWidget *parent,
                                                       const QString &prefix)
{
    Field f;
    f.row = row;
    const QString name = prefix + row.name;
    switch (row.kind) {
    case FieldKind::Bool:
        f.check = new QCheckBox(row.label, parent);
        f.check->setObjectName(name);
        break;
    case FieldKind::Int:
    case FieldKind::Number: {
        auto *spin = new FieldSpinBox(parent);
        spin->setDecimals(row.decimals);
        spin->setRange(qMax(row.min, -kSpinLimit), qMin(row.max, kSpinLimit));
        if (!row.suffix.isEmpty())
            spin->setSuffix(QLatin1Char(' ') + row.suffix);
        spin->setKeyboardTracking(false);
        keepValueVisible(spin);
        f.spin = spin;
        f.editor = spin;
        break;
    }
    case FieldKind::Enum:
        f.combo = new QComboBox(parent);
        for (const auto &[value, label] : row.values)
            f.combo->addItem(label, value);
        f.editor = f.combo;
        break;
    case FieldKind::StringList:
        if (!row.values.isEmpty()) {
            // Je Wert ein Kästchen; keines gewählt = alle (triggertypes.cpp)
            auto *box = new QWidget(parent);
            auto *line = new QHBoxLayout(box);
            line->setContentsMargins(0, 0, 0, 0);
            line->setSpacing(8);
            for (const auto &[value, label] : row.values) {
                auto *option = new QCheckBox(label, box);
                option->setObjectName(name + QLatin1Char(':') + value);
                line->addWidget(option);
                f.options.append(option);
            }
            line->addStretch(1);
            f.editor = box;
        } else {
            f.line = new QLineEdit(parent);
            f.readOnly = true;
        }
        break;
    case FieldKind::StringMap:
    case FieldKind::ElementList: {
        const bool map = row.kind == FieldKind::StringMap;
        f.list = fieldList(name, row.label, map);
        f.add = symbolButton(QStringLiteral("hinzufuegen.svg"), QStringLiteral("+"),
                             map ? QStringLiteral("Testwert hinzufügen") : QStringLiteral("Element hinzufügen"), nullptr);
        f.add->setObjectName(name + QStringLiteral(":add"));
        f.remove = symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"),
                                map ? QStringLiteral("Testwert löschen") : QStringLiteral("Element löschen"), nullptr);
        f.remove->setObjectName(name + QStringLiteral(":remove"));
        if (!map) {
            // Wie «Schritt hinzufügen»: Menü mit den Quellen, gefüllt in showElements
            f.add->setMenu(new QMenu(f.add));
            f.add->setPopupMode(QToolButton::InstantPopup);
        }
        break;
    }
    case FieldKind::Object:
        f.line = new QLineEdit(parent);
        f.readOnly = true;
        break;
    case FieldKind::String:
    case FieldKind::NullableString:
    case FieldKind::NullableUuid:
        if (!row.choices.isEmpty() && row.choices != QLatin1String("media")) {
            f.combo = new QComboBox(parent);
        } else if (!row.values.isEmpty()) {
            // Text mit Vorschlägen (z. B. Farbe der Ansage): Liste oder eigener Text
            f.combo = new QComboBox(parent);
            f.combo->setEditable(true);
            f.combo->setInsertPolicy(QComboBox::NoInsert);
            for (const auto &[value, label] : row.values)
                f.combo->addItem(label, value);
        } else {
            f.line = new QLineEdit(parent);
        }
        break;
    }
    if (f.line) {
        f.line->setReadOnly(f.readOnly);
        f.editor = f.line;
    }
    if (f.combo)
        f.editor = f.combo;
    if (f.editor) {
        f.editor->setObjectName(name);
        f.label = new QLabel(row.label, parent);
        f.label->setBuddy(f.options.isEmpty() ? f.editor : f.options.first());
    }
    if (!row.inactiveReason.isEmpty()) {
        // Sichtbar, aber ausgegraut, bis die Funktion wirkt (Entscheid des Nutzers, 2026-11-16)
        for (QWidget *w : {static_cast<QWidget *>(f.label), f.editor, static_cast<QWidget *>(f.check)}) {
            if (w) {
                w->setEnabled(false);
                w->setToolTip(row.inactiveReason);
            }
        }
    }
    return f;
}

void ActionFieldsPanel::connectField(int pageIndex, int fieldIndex, bool element)
{
    const Field &f = element ? m_pageList[pageIndex].elementFields[fieldIndex] : m_pageList[pageIndex].fields[fieldIndex];
    // Gilt nur für die gezeigte Seite und nicht während setState
    const auto active = [this, pageIndex] { return !m_filling && m_pages->currentIndex() == pageIndex; };
    const auto send = [this, pageIndex, element](const QString &key, const QJsonValue &value) {
        if (element)
            commitElement(m_pageList[pageIndex], key, value);
        else
            commit(key, value);
    };
    const dss::FieldRow row = f.row;
    if (f.check) {
        connect(f.check, &QCheckBox::clicked, this, [active, send, row](bool checked) {
            if (active())
                send(row.name, checked);
        });
    }
    if (f.spin) {
        connect(f.spin, &QDoubleSpinBox::valueChanged, this, [active, send, row](double shown) {
            if (!active())
                return;
            const double stored = dss::fromDisplay(row, shown);
            send(row.name, row.kind == FieldKind::Int ? QJsonValue(qint64(stored)) : QJsonValue(stored));
        });
    }
    if (f.line && !f.readOnly) {
        QLineEdit *line = f.line;
        connect(line, &QLineEdit::editingFinished, this, [active, send, row, line] {
            if (!active())
                return;
            const QString text = line->text();
            send(row.name, text.isEmpty() && nullable(row) ? QJsonValue(QJsonValue::Null) : QJsonValue(text));
        });
    }
    if (f.combo) {
        QComboBox *combo = f.combo;
        connect(combo, &QComboBox::activated, this, [active, send, row, combo](int index) {
            if (!active() || index < 0)
                return;
            const QString value = combo->itemData(index).toString();
            send(row.name, value.isEmpty() && nullable(row) ? QJsonValue(QJsonValue::Null) : QJsonValue(value));
        });
        if (combo->isEditable()) {
            connect(combo->lineEdit(), &QLineEdit::editingFinished, this, [active, send, row, combo] {
                if (!active())
                    return;
                const int index = combo->findText(combo->currentText());
                send(row.name, index >= 0 ? combo->itemData(index).toString() : combo->currentText());
            });
        }
    }
    for (QCheckBox *option : f.options) {
        const QList<QCheckBox *> options = f.options;
        connect(option, &QCheckBox::clicked, this, [active, send, row, options] {
            if (!active())
                return;
            QJsonArray values;
            for (int i = 0; i < options.size(); ++i) {
                if (options.at(i)->isChecked())
                    values.append(row.values.at(i).first);
            }
            send(row.name, values);
        });
    }
    if (f.list && row.kind == FieldKind::ElementList) {
        connect(f.list, &QTreeWidget::currentItemChanged, this, [this, pageIndex] {
            if (m_filling || m_pages->currentIndex() != pageIndex)
                return;
            m_filling = true;
            Page &page = m_pageList[pageIndex];
            showElements(page, QJsonValue(), false);
            enableFields(page);
            m_filling = false;
        });
        connect(f.remove, &QToolButton::clicked, this, [this, active, row, pageIndex] {
            const int index = selectedElement(m_pageList[pageIndex]);
            if (!active() || index < 0)
                return;
            QJsonArray elements = m_state.fields.value(row.name).toArray();
            if (index < elements.size())
                elements.removeAt(index);
            commit(row.name, elements);
        });
    }
    if (f.list && row.kind == FieldKind::StringMap) {
        QTreeWidget *list = f.list;
        connect(list, &QTreeWidget::itemChanged, this, [this, active, pageIndex, fieldIndex] {
            if (active())
                commitMap(m_pageList[pageIndex].fields[fieldIndex]);
        });
        connect(list, &QTreeWidget::currentItemChanged, this, [this, pageIndex] {
            if (!m_filling && m_pages->currentIndex() == pageIndex)
                enableFields(m_pageList[pageIndex]);
        });
        connect(f.add, &QToolButton::clicked, this, [active, list] {
            if (!active())
                return;
            // Neue Zeile erst nur hier; gespeichert wird sie mit einem Namen (commitMap)
            auto *item = new QTreeWidgetItem(list);
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            list->setCurrentItem(item);
            list->editItem(item, 0);
        });
        connect(f.remove, &QToolButton::clicked, this, [this, active, list, pageIndex, fieldIndex] {
            if (!active() || !list->currentItem())
                return;
            delete list->currentItem();
            commitMap(m_pageList[pageIndex].fields[fieldIndex]);
        });
    }
}

void ActionFieldsPanel::setState(const State &state, bool resetFields)
{
    const bool other = state.target != m_state.target || state.id != m_state.id;
    const QJsonObject previous = other ? QJsonObject() : m_state.fields;
    m_filling = true;
    if (other) {
        m_lastField.clear();
        m_lastInElement = false;
        m_problem.clear();
        for (Page &page : m_pageList) {
            for (QLabel *problem : std::as_const(page.problems))
                problem->clear();
            if (page.elementProblem)
                page.elementProblem->clear();
        }
    }
    m_state = state;
    // Nur die Seite wechseln; die Felder aller Arten bestehen seit dem Aufbau
    const int index = state.target == Target::None ? 0 : m_pageIndex.value(pageKey(state.target, state.type), 0);
    m_pages->setCurrentIndex(index);
    if (Page *page = shownPage()) {
        const bool keep = !resetFields && !other;
        for (Field &field : page->fields) {
            if (field.row.kind != FieldKind::ElementList)
                showField(field, state.fields.value(field.row.name), previous.value(field.row.name), keep);
        }
        showElements(*page, previous.value(QStringLiteral("elements")), keep);
        enableFields(*page);
    }
    m_filling = false;
}

void ActionFieldsPanel::showField(Field &f, const QJsonValue &value, const QJsonValue &previous, bool keepTyping)
{
    // Ein Feld, an dem der Nutzer schreibt, bleibt, solange der Kern nichts Neues meldet
    const bool keep = keepTyping && value == previous
        && ((f.editor && hasFocusWithin(f.editor)) || (f.list && hasFocusWithin(f.list)));
    if (keep)
        return;
    if (f.check)
        f.check->setChecked(value.toBool());
    if (f.spin)
        f.spin->setValue(dss::toDisplay(f.row, value.toDouble()));
    if (f.line) {
        if (f.readOnly)
            f.line->setText(value.isObject() ? QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact))
                            : value.isArray() ? QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact))
                                              : value.toVariant().toString());
        else
            f.line->setText(value.toString());
    }
    if (f.combo && f.combo->isEditable()) {
        const int index = f.combo->findData(value.toString());
        f.combo->setEditText(index >= 0 ? f.combo->itemText(index) : value.toString());
    } else if (f.combo) {
        if (!f.row.choices.isEmpty()) {
            f.combo->clear();
            for (const dss::FieldChoice &choice : choicesFor(f.row, value.toString()))
                f.combo->addItem(choice.label, choice.value);
        }
        f.combo->setCurrentIndex(f.combo->findData(f.row.choices.isEmpty() ? value.toString() : value.toString().toLower()));
    }
    const QJsonArray values = value.toArray();
    for (int i = 0; i < f.options.size(); ++i)
        f.options.at(i)->setChecked(values.contains(f.row.values.at(i).first));
    if (f.list && f.row.kind == FieldKind::StringMap) {
        const int row = f.list->currentIndex().row();
        f.list->clear();
        const QJsonObject map = value.toObject();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
            auto *item = new QTreeWidgetItem(f.list, QStringList{it.key(), it.value().toString()});
            item->setFlags(item->flags() | Qt::ItemIsEditable);
        }
        if (row >= 0 && f.list->topLevelItemCount() > 0)
            f.list->setCurrentItem(f.list->topLevelItem(qMin(row, f.list->topLevelItemCount() - 1)));
    }
}

void ActionFieldsPanel::showElements(Page &page, const QJsonValue &previous, bool keepTyping)
{
    const auto it = std::find_if(page.fields.begin(), page.fields.end(), [](const Field &f) {
        return f.row.kind == FieldKind::ElementList;
    });
    if (it == page.fields.end())
        return;
    Field &f = *it;
    const QJsonArray elements = m_state.fields.value(f.row.name).toArray();
    const auto sourceRow = std::find_if(page.elementFields.cbegin(), page.elementFields.cend(), [](const Field &e) {
        return e.row.name == QLatin1String("source");
    });
    const dss::FieldRow source = sourceRow != page.elementFields.cend() ? sourceRow->row : dss::FieldRow();
    const auto label = [this, &source](const QString &id) {
        for (const dss::FieldChoice &choice : choicesFor(source, id)) {
            if (choice.value == id.toLower())
                return choice.label;
        }
        return id;
    };

    // Liste neu, Auswahl über die Stelle; ein neues Element am Ende wird gewählt
    const int before = f.list->topLevelItemCount();
    int row = f.list->currentIndex().row();
    f.list->clear();
    for (const QJsonValue &e : elements)
        new QTreeWidgetItem(f.list, QStringList{label(e.toObject().value(QStringLiteral("source")).toString())});
    if (elements.size() > before && before > 0)
        row = int(elements.size()) - 1;
    if (row >= 0 && !elements.isEmpty())
        f.list->setCurrentItem(f.list->topLevelItem(qMin(row, int(elements.size()) - 1)));

    // Menü «+»: Quellen aus den Daten, jede legt ein Element ohne eigenen Inhalt an
    QMenu *menu = f.add->menu();
    menu->clear();
    const QString fieldName = f.row.name;
    for (const dss::FieldChoice &choice : choicesFor(source, QString())) {
        if (choice.value.isEmpty())
            continue;
        const QString id = choice.value;
        menu->addAction(choice.label, this, [this, fieldName, id] {
            QJsonArray list = m_state.fields.value(fieldName).toArray();
            list.append(QJsonObject{{QStringLiteral("source"), id},
                                    {QStringLiteral("text"), QJsonValue::Null},
                                    {QStringLiteral("image"), QJsonValue::Null}});
            commit(fieldName, list);
        });
    }

    const int index = selectedElement(page);
    const QJsonObject element = index >= 0 ? elements.at(index).toObject() : QJsonObject();
    const QJsonArray previousElements = previous.toArray();
    const QJsonObject previousElement = keepTyping && index >= 0 && index < previousElements.size()
        ? previousElements.at(index).toObject() : QJsonObject();
    for (Field &e : page.elementFields)
        showField(e, element.value(e.row.name), previousElement.value(e.row.name), keepTyping);
}

void ActionFieldsPanel::enableFields(Page &page)
{
    const bool editable = m_state.editable;
    const auto apply = [editable](Field &f, bool allowed) {
        const bool on = editable && allowed && f.row.inactiveReason.isEmpty();
        for (QWidget *w : {f.editor, static_cast<QWidget *>(f.check), static_cast<QWidget *>(f.list)}) {
            if (w)
                w->setEnabled(on);
        }
        for (QCheckBox *option : std::as_const(f.options))
            option->setEnabled(on);
        if (f.add) {
            f.add->setEnabled(on);
            f.remove->setEnabled(on && f.list->currentItem());
        }
    };
    for (Field &f : page.fields)
        apply(f, true);
    const bool element = selectedElement(page) >= 0;
    for (Field &f : page.elementFields)
        apply(f, element);
}

void ActionFieldsPanel::commit(const QString &field, const QJsonValue &value, bool element)
{
    if (value == m_state.fields.value(field))
        return;
    // Vor dem Senden: Eine Prüfmeldung der Logik kommt noch während emit zurück (setProblem)
    m_lastField = field;
    m_lastInElement = element;
    emit fieldEdited(m_state.target, m_state.id, field, value);
}

void ActionFieldsPanel::commitElement(Page &page, const QString &key, const QJsonValue &value)
{
    const int index = selectedElement(page);
    QJsonArray elements = m_state.fields.value(QStringLiteral("elements")).toArray();
    if (index < 0 || index >= elements.size())
        return;
    QJsonObject element = elements.at(index).toObject();
    if (element.value(key) == value)
        return;
    element.insert(key, value);
    elements.replace(index, element);
    commit(QStringLiteral("elements"), elements, true);
}

void ActionFieldsPanel::commitMap(const Field &field)
{
    // Zeilen ohne Namen gehören nicht zu den Testwerten
    QJsonObject map;
    for (int i = 0; i < field.list->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *item = field.list->topLevelItem(i);
        if (!item->text(0).isEmpty())
            map.insert(item->text(0), item->text(1));
    }
    commit(field.row.name, map);
}

QList<dss::FieldChoice> ActionFieldsPanel::choicesFor(const dss::FieldRow &row, const QString &current) const
{
    return dss::fieldChoices(row.choices, m_state.config, m_state.names, current);
}

int ActionFieldsPanel::selectedElement(const Page &page) const
{
    for (const Field &f : page.fields) {
        if (f.row.kind == FieldKind::ElementList)
            return f.list->currentItem() ? f.list->currentIndex().row() : -1;
    }
    return -1;
}

ActionFieldsPanel::Page *ActionFieldsPanel::shownPage()
{
    const int index = m_pages->currentIndex();
    return index > 0 ? &m_pageList[index] : nullptr;
}

void ActionFieldsPanel::setProblem(const QString &text)
{
    m_problem = text;
    Page *page = shownPage();
    if (!page)
        return;
    for (QLabel *problem : std::as_const(page->problems))
        problem->clear();
    if (page->elementProblem)
        page->elementProblem->clear();
    if (text.isEmpty())
        return;
    // Unter dem Abschnitt des zuletzt bearbeiteten Felds (Inhalt eines Elements: unter «Inhalt der
    // Quellen»), sonst unter dem ersten
    QLabel *problem = m_lastInElement ? page->elementProblem
                                      : page->problems.value(dss::fieldSection(page->form, m_lastField));
    if (!problem && !page->form.isEmpty())
        problem = page->problems.value(page->form.first().title);
    if (problem) {
        problem->setText(text);
        problem->setToolTip(text);
    }
}

QString ActionFieldsPanel::problem() const
{
    return m_problem;
}

QWidget *ActionFieldsPanel::page(Target target, const QString &type) const
{
    const int index = m_pageIndex.value(pageKey(target, type), -1);
    return index > 0 ? m_pageList.at(index).widget : nullptr;
}

QWidget *ActionFieldsPanel::currentPage() const
{
    return m_pages->currentWidget();
}
