#pragma once

#include "actionview.h"
#include "theme.h"

#include <QList>
#include <QUuid>
#include <QWidget>

class ActionFieldsPanel;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPoint;
class QPushButton;
class QSplitter;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// P14 e2-11: Unterreiter «Aktionen» im Reiter «Chat-Bot» (Plan P14 6, Checkliste P14 e2 «Aktionen»).
// Drei Bereiche wie ein Dock (dockPanel) nebeneinander in einem QSplitter, fest ohne Lösen und
// Schliessen: links die Liste der Aktionen, in der Mitte die gewählte Aktion (e2-15: Abschnitte
// «Aktion», «Auslöser», «Schritte», Leiste mit «▶ Testen»), rechts der gewählte Schritt oder
// Auslöser (e2-16, in area()). Dünn (Regel 9): Zeilen, Texte und Freigaben kommen aus
// dss::actionview über das MainWindow.
class ActionsPage : public QWidget
{
    Q_OBJECT

public:
    enum Area { List = 0, Action, Detail };
    // P14 e2-15: Abschnitte der Mitte, je mit einer Prüfmeldung am Feld (#fieldProblem).
    // DetailSection (e2-16): rechte Spalte, Meldung unter dem Abschnitt des Felds (ActionFieldsPanel).
    enum Section { NoSection = -1, ActionSection = 0, TriggerSection, StepSection, DetailSection };

    // Stand der Mitte aus dem Stand des Kerns, vom MainWindow über dss::actionview berechnet.
    struct ActionDetail
    {
        QUuid actionId; // null: keine Aktion gewählt, Felder leer und ausgegraut
        QString name;
        QString group;
        QStringList groups;            // actionGroupChoices
        QList<dss::QueueChoice> queues; // queueChoices
        QUuid queue;
        bool enabled = true;
        QString hint;                  // templateHint bzw. «Keine Aktion gewählt»
        QList<dss::TriggerRow> triggers;
        QList<dss::StepRow> steps;
        bool editable = false;         // Kern verbunden und Stand gelesen
    };

    explicit ActionsPage(const Theme &theme, QWidget *parent = nullptr);

    // Inhalt eines Bereichs (QVBoxLayout ohne Ränder), in den die Teile kommen.
    QWidget *area(Area which) const { return m_areas[which]; }
    // Titel eines Bereichs, z. B. actionTitle() in der Mitte. Ändert nur den Text.
    void setAreaTitle(Area which, const QString &title);
    QString areaTitle(Area which) const;

    // P14 e2-14, links: Baum Gruppen → Aktionen. Die Auswahl bleibt erhalten (Aktion über die
    // Kennung, Gruppe über den Namen ohne Rücksicht auf Gross- und Kleinschreibung); gibt es sie
    // nicht mehr, wählt keptSelection die nächste Aktion.
    void setActionRows(const QList<dss::ActionListRow> &rows);
    QString searchText() const;
    void clearSearch();
    // Wählt die Aktion; false, wenn sie im Baum nicht steht (z. B. wegen der Suche). Kommt sie mit
    // den nächsten Zeilen, wird sie dann gewählt.
    bool selectAction(const QUuid &id);
    QUuid selectedAction() const;  // null bei einer Gruppe oder ohne Auswahl
    QString selectedGroup() const; // Gruppe der Auswahl (Gruppenzeile oder Aktion), leer = ohne
    // Freigabe der Knöpfe, vom MainWindow aus der Auswahl und actionResettable() berechnet.
    // add gilt auch für Menü und Doppelklick der Gruppen (ohne Kern keines von beiden).
    void setListButtons(bool add, bool copyRemove, bool reset);

    // P14 e2-15, Mitte. Felder zeigen den Wert des Kerns; ein Textfeld, an dem der Nutzer gerade
    // schreibt, bleibt, solange sich der Wert im Kern nicht ändert. resetFields: immer den Wert des
    // Kerns zeigen (nach einer Prüfmeldung oder Ablehnung). Auswahl der Auslöser und Schritte
    // bleibt über die Kennung erhalten.
    void setActionDetail(const ActionDetail &detail, bool resetFields = false);
    // Prüfmeldung unter dem Abschnitt; leer = keine. Die Zeile ist immer da (Höhe reserviert).
    void setSectionProblem(Section section, const QString &text);
    QString sectionProblem(Section section) const;
    // Freigaben aus dss::stepButtons; add gilt für «Schritt hinzufügen» und «Auslöser hinzufügen»,
    // removeTrigger für «Auslöser löschen».
    void setStepButtons(bool add, const dss::StepButtons &buttons, bool removeTrigger);
    // «Testwerte: Benutzer» aus dss::testUserChoices, Freigabe aus testAllowed/realTestAllowed.
    // Die Wahl bleibt über die Bezeichnung erhalten.
    void setTestState(const QList<dss::TestUserChoice> &users, bool test, bool real);
    int testUserIndex() const;
    bool realTest() const;
    // Wählt den Schritt bzw. Auslöser (auch wenn er erst mit dem nächsten Stand kommt).
    void selectStep(const QUuid &id);
    void selectTrigger(const QUuid &id);
    QUuid selectedStep() const;    // null: kein Schritt gewählt
    QUuid selectedTrigger() const; // null: kein Auslöser gewählt
    // Aktion, die die Mitte zeigt (Ziel der Bearbeitungen der Mitte); null = keine.
    QUuid detailAction() const { return m_shownAction; }
    // P14 e2-16, rechts: Felder des gewählten Schritts oder Auslösers in area(Detail)
    ActionFieldsPanel *fieldsPanel() const { return m_fields; }

signals:
    void searchChanged(const QString &text);
    void actionSelected(const QUuid &id); // null: Gruppe oder keine Auswahl
    void actionEnabledToggled(const QUuid &id, bool enabled);
    void addClicked();
    void newGroupClicked();
    void copyClicked();
    void removeClicked();
    void resetClicked();
    void renameGroupRequested(const QString &group);
    void removeGroupRequested(const QString &group);
    // P14 e2-15, Mitte: Textfelder beim Verlassen oder mit Enter, Kästchen und Auswahl sofort
    // (Plan 5). «Aktiv» meldet actionEnabledToggled mit der gewählten Aktion.
    void nameEdited(const QString &name);
    void groupEdited(const QString &group);
    void queueChosen(const QUuid &queue);
    void manageQueuesClicked(); // P14 e3-8: «Warteschlangen verwalten …»
    void triggerAddChosen(const QString &type);
    void triggerRemoveClicked();
    void triggerEnabledToggled(const QUuid &id, bool enabled);
    void stepAddChosen(const QString &type);
    void stepRemoveClicked();
    void stepMoveClicked(int direction); // -1 hoch, +1 runter
    void stepEnabledToggled(const QUuid &id, bool enabled);
    void stepSelected(const QUuid &id);    // null: kein Schritt gewählt
    void triggerSelected(const QUuid &id); // null: kein Auslöser gewählt
    void testClicked();

private:
    void showGroupMenu(const QPoint &position);
    void createActionArea();
    QWidget *createActionSection();
    QWidget *createTriggerSection();
    QWidget *createStepSection();
    void setTriggerRows(const QList<dss::TriggerRow> &rows);
    void setStepRows(const QList<dss::StepRow> &rows);
    void finishNameEdit();
    void finishGroupEdit();

    QSplitter *m_splitter = nullptr;
    QWidget *m_areas[3] = {};
    QLabel *m_titles[3] = {};

    QLineEdit *m_search = nullptr;
    QTreeWidget *m_tree = nullptr;
    QToolButton *m_add = nullptr;
    QToolButton *m_newGroup = nullptr;
    QToolButton *m_copy = nullptr;
    QToolButton *m_remove = nullptr;
    QToolButton *m_reset = nullptr;
    QList<QUuid> m_rowActions; // Aktionen in der Reihenfolge des Baums (für keptSelection)
    QUuid m_wantedAction;      // selectAction() vor dem Eintreffen der Zeile
    bool m_filling = false;
    bool m_groupEditing = false; // aus setListButtons(add): Kern verbunden, Liste geladen

    // Mitte
    Theme m_theme;
    QLineEdit *m_name = nullptr;
    QComboBox *m_group = nullptr;
    QComboBox *m_queue = nullptr;
    QCheckBox *m_enabled = nullptr;
    QLabel *m_hint = nullptr;
    QLabel *m_problems[3] = {};
    QToolButton *m_triggerAdd = nullptr;
    QToolButton *m_triggerRemove = nullptr;
    QTreeWidget *m_triggers = nullptr;
    QTreeWidget *m_steps = nullptr;
    QToolButton *m_stepAdd = nullptr;
    QToolButton *m_stepRemove = nullptr;
    QToolButton *m_stepUp = nullptr;
    QToolButton *m_stepDown = nullptr;
    QComboBox *m_testUser = nullptr;
    QPushButton *m_test = nullptr;
    QCheckBox *m_realTest = nullptr;
    QUuid m_shownAction;  // Aktion in der Mitte
    QString m_coreName;   // Wert des Kerns beim letzten setActionDetail
    QString m_coreGroup;
    int m_coreQueueIndex = -1; // Warteschlange der Aktion in m_queue (nach «Warteschlangen verwalten …»)
    QString m_shownName;  // zuletzt gezeigter bzw. gemeldeter Name (keine doppelte Meldung)
    QString m_shownGroup;
    QList<QUuid> m_stepIds; // Schritte in der Reihenfolge des Baums (für keptSelection)
    QList<QUuid> m_triggerIds;
    QUuid m_wantedStep;   // selectStep() vor dem Eintreffen der Zeile
    QUuid m_wantedTrigger;
    bool m_detailFilling = false;

    ActionFieldsPanel *m_fields = nullptr; // rechts
};
