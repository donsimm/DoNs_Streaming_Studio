#pragma once

#include "actionfields.h"
#include "actionview.h"

#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QUuid>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QDoubleSpinBox;
class QStackedWidget;
class QToolButton;
class QTreeWidget;

// P14 e2-16: rechte Spalte im Unterreiter «Aktionen» (Plan P14 6, Checkliste P14 e2 «Aktionen»
// rechts). QScrollArea ohne Rahmen, darin ein QStackedWidget mit einer Seite je Art des Schritts
// oder Auslösers. Alle Seiten entstehen einmal beim Aufbau; der Wechsel der Auswahl schaltet nur die
// Seite um und setzt Werte und Freigaben (Regel 9). Abschnitte, Beschriftungen, Grenzen und Einheiten
// kommen aus dss::stepForm bzw. dss::triggerForm; Zahlen nur über dss::toDisplay/dss::fromDisplay.
class ActionFieldsPanel : public QWidget
{
    Q_OBJECT

public:
    enum class Target { None, Step, Trigger };

    // Stand aus dem Kern, vom MainWindow zur Auswahl der Mitte berechnet
    struct State
    {
        Target target = Target::None;
        QUuid id;
        QString type;
        QJsonObject fields;
        bool editable = false; // Kern verbunden und Stand gelesen
        // Für die Auswahl aus den Daten (dss::fieldChoices)
        dss::actions::ActionsConfig config;
        dss::ActionViewNames names;
    };

    explicit ActionFieldsPanel(QWidget *parent = nullptr);

    // Werte des Kerns zeigen. Ein Feld, an dem der Nutzer gerade schreibt, bleibt, solange sich sein
    // Wert im Kern nicht ändert; resetFields: immer den Wert des Kerns (nach Prüfmeldung oder Ablehnung).
    void setState(const State &state, bool resetFields = false);
    // Prüfmeldung unter dem Abschnitt des zuletzt bearbeiteten Felds (dss::fieldSection); leer = keine.
    void setProblem(const QString &text);
    QString problem() const;
    // Seite einer Art; nullptr bei unbekannter Art. Für Tests und die Prüfung «ohne Neuaufbau».
    QWidget *page(Target target, const QString &type) const;
    QWidget *currentPage() const;

signals:
    // Wert wie in aktionen.json (Zeiten in ms); das MainWindow speichert mit setStepField bzw.
    // setTriggerField.
    void fieldEdited(ActionFieldsPanel::Target target, const QUuid &id, const QString &field,
                     const QJsonValue &value);

private:
    // Feld einer Zeile aus FieldRow; je nach Art ist genau eines der Bedienelemente gesetzt.
    struct Field
    {
        dss::FieldRow row;
        QLabel *label = nullptr;    // nur bei Feldern rechts der Beschriftung
        QWidget *editor = nullptr;  // was in der Spalte rechts der Beschriftung steht
        QLineEdit *line = nullptr;
        QComboBox *combo = nullptr; // Enum, Auswahl aus den Daten, Text mit Vorschlägen (editierbar)
        QDoubleSpinBox *spin = nullptr;
        QCheckBox *check = nullptr;
        QList<QCheckBox *> options; // StringList mit values
        QTreeWidget *list = nullptr; // ElementList, StringMap
        QToolButton *add = nullptr;
        QToolButton *remove = nullptr;
        bool readOnly = false; // Rohdaten (Object): nur zeigen
    };
    struct Page
    {
        Target target = Target::None;
        QString type;
        QWidget *widget = nullptr;
        QList<dss::FieldSection> form;
        QList<Field> fields;
        QHash<QString, QLabel *> problems; // Titel des Abschnitts → Prüfmeldung
        QList<QPushButton *> buttons;      // FieldButton (ausgegraut mit Grund)
        // Overlay-Schritt: Inhalt des gewählten Elements (elementForm)
        QList<Field> elementFields;
        QLabel *elementProblem = nullptr;
    };

    void buildPage(Target target, const QString &type, const QList<dss::FieldSection> &form);
    QWidget *buildSection(Page &page, const dss::FieldSection &section, QWidget *parent, bool elements);
    Field buildField(const dss::FieldRow &row, QWidget *parent, const QString &prefix);
    void connectField(int pageIndex, int fieldIndex, bool element);
    void showField(Field &field, const QJsonValue &value, const QJsonValue &previous, bool keepTyping);
    void showElements(Page &page, const QJsonValue &previous, bool keepTyping);
    void enableFields(Page &page);
    // element: Inhalt eines Elements (Prüfmeldung unter «Inhalt der Quellen»)
    void commit(const QString &field, const QJsonValue &value, bool element = false);
    void commitElement(Page &page, const QString &key, const QJsonValue &value);
    void commitMap(const Field &field);
    QList<dss::FieldChoice> choicesFor(const dss::FieldRow &row, const QString &current) const;
    int selectedElement(const Page &page) const;
    Page *shownPage();

    QStackedWidget *m_pages = nullptr;
    QList<Page> m_pageList; // Index 0: ohne Auswahl
    QHash<QString, int> m_pageIndex; // «step:<Art>» bzw. «trigger:<Art>» → Seite
    State m_state;
    QString m_lastField; // zuletzt bearbeitet, für die Prüfmeldung
    bool m_lastInElement = false;
    QString m_problem;
    bool m_filling = false;
};
