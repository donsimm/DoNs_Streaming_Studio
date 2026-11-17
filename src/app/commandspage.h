#pragma once

#include "actionview.h"
#include "listdetailpage.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

// P14 e3-5: Unterreiter «Befehle» (Checkliste P14 e3, Abschnitt 3 «Befehle»). Aufbau der Tabelle
// links für ListDetailPage; Spalten aus dss::CommandRow, Breiten in Ziffern der Schrift.
ListDetailPage::Setup commandsPageSetup();

// Formular rechts in «Befehle»: Abschnitte «Befehl», «Wer darf», «Abklingzeit», «Ausführen» über
// section() mit je einer Prüfzeile (#fieldProblem); die Leiste mit «▶ Testen als …» (testBar) setzt
// die Seite unter den Bildlauf. Ohne
// Logik (Regel 9): Werte und Auswahlen kommen vom MainWindow aus dss::actionview, eine Änderung
// meldet nur den gewünschten Wert. Ein Wechsel der Auswahl füllt nur die Werte; die Kästchen der
// Benutzergruppen entstehen neu, wenn sich die Liste der Gruppen ändert.
class CommandForm : public QWidget
{
    Q_OBJECT

public:
    enum Section { NoSection = -1, CommandSection = 0, PermissionSection, CooldownSection, RunSection, SectionCount };

    struct Detail
    {
        QUuid id; // null: kein Befehl gewählt
        bool editable = false;
        QString name;
        QString group;
        QStringList groups; // Einträge der Gruppe (vorhandene Gruppen)
        QString commandsText; // dss::commandsText
        QList<dss::LocationChoice> locations;
        QString location;
        bool enabled = true;
        dss::PermissionChoice permissions;
        QList<dss::UserGroupChoice> userGroups;
        bool ignoreBot = true;
        int globalCooldown = 0;
        int userCooldown = 0;
        QList<dss::ActionChoice> actions;
        QUuid action; // null = «Keine»
    };

    explicit CommandForm(QWidget *parent = nullptr);

    // resetFields: Felder zeigen wieder den Wert des Kerns (nach Prüfmeldung oder Ablehnung), auch
    // das Feld, in dem der Nutzer gerade schreibt.
    void setDetail(const Detail &detail, bool resetFields);
    QUuid shownCommand() const { return m_shown; }
    void setSectionProblem(Section section, const QString &text);
    QString sectionProblem(Section section) const;
    // Menü «▶ Testen als …» (Bezeichnungen aus dss::testUserChoices) und Freigabe
    void setTestState(const QStringList &users, bool allowed);
    // Leiste mit «▶ Testen als …» für ListDetailPage::setDetailBar
    QWidget *testBar() const { return m_testBar; }

signals:
    void nameEdited(const QString &name);
    void groupEdited(const QString &group);
    void commandsEdited(const QString &text);
    void locationChosen(const QString &location);
    void enabledToggled(bool enabled);
    void permissionsEdited(const dss::PermissionChoice &choice);
    void ignoreBotToggled(bool ignore);
    void globalCooldownEdited(int seconds);
    void userCooldownEdited(int seconds);
    void actionChosen(const QUuid &actionId);
    void openActionClicked(const QUuid &actionId);
    void testChosen(int userIndex); // Eintrag aus dss::testUserChoices, immer Probelauf

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *createCommandSection();
    QWidget *createPermissionSection();
    QWidget *createCooldownSection();
    QWidget *createRunSection();
    void setUserGroups(const QList<dss::UserGroupChoice> &groups);
    void finishNameEdit();
    void finishGroupEdit();
    void finishCommandsEdit();
    void emitPermissions(QCheckBox *clicked);

    QLabel *m_problems[SectionCount] = {};
    QWidget *m_testBar = nullptr;
    QLineEdit *m_name = nullptr;
    QComboBox *m_group = nullptr;
    QPlainTextEdit *m_commands = nullptr;
    QComboBox *m_location = nullptr;
    QCheckBox *m_enabled = nullptr;
    QCheckBox *m_all = nullptr;
    QCheckBox *m_subscriber = nullptr;
    QCheckBox *m_vip = nullptr;
    QCheckBox *m_moderator = nullptr;
    QWidget *m_userGroupBox = nullptr;
    QVBoxLayout *m_userGroupColumn = nullptr;
    QList<QCheckBox *> m_userGroupChecks;
    QList<dss::UserGroupChoice> m_userGroups;
    QCheckBox *m_ignoreBot = nullptr;
    QSpinBox *m_globalCooldown = nullptr;
    QSpinBox *m_userCooldown = nullptr;
    QComboBox *m_action = nullptr;
    QPushButton *m_open = nullptr;
    QPushButton *m_test = nullptr;
    QMenu *m_testMenu = nullptr;
    QStringList m_testUsers;

    QUuid m_shown;
    // Stand im Feld und zuletzt vom Kern; Textfelder werden nicht überschrieben, solange der Nutzer
    // schreibt und der Kern nichts Neues meldet (wie ActionsPage)
    QString m_shownName, m_coreName;
    QString m_shownGroup, m_coreGroup;
    QString m_shownCommands, m_coreCommands;
    bool m_filling = false;
};
