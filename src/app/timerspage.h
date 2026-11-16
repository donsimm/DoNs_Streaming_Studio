#pragma once

#include "actionfields.h"
#include "actionview.h"
#include "listdetailpage.h"

#include <QList>
#include <QString>
#include <QUuid>
#include <QWidget>

class FieldSpinBox;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

// P14 e3-6: Unterreiter «Zeitnachrichten» (Checkliste P14 e3, Abschnitt 3 «Zeitnachrichten»). Aufbau
// der Tabelle links für ListDetailPage; Spalten aus dss::TimerRow, Breiten in Ziffern der Schrift.
ListDetailPage::Setup timersPageSetup();

// Formular rechts in «Zeitnachrichten»: Abschnitte «Zeitnachricht», «Abstand», «Ausführen» über
// section() mit je einer Prüfzeile (#fieldProblem). Ohne Logik (Regel 9): Werte und Auswahlen kommen
// vom MainWindow aus dss::actionview, eine Änderung meldet nur den gewünschten Wert. Abstände in
// Minuten mit Komma (Entscheid des Nutzers, 2026-11-16), umgerechnet nur mit dss::toDisplay und
// dss::fromDisplay (minutesRow); die Signale tragen Sekunden. Ein Wechsel der Auswahl füllt nur die
// Werte; «von» und «bis» sind ohne «Zufällig» nur ausgegraut, «Abstand» mit «Zufällig».
class TimerForm : public QWidget
{
    Q_OBJECT

public:
    enum Section { NoSection = -1, TimerSection = 0, IntervalSection, RunSection, SectionCount };

    struct Detail
    {
        QUuid id; // null: keine Zeitnachricht gewählt
        bool editable = false;
        QString name;
        bool enabled = true;
        bool repeat = true;
        int intervalS = 0;
        bool random = false;
        int randomMinS = 0;
        int randomMaxS = 0;
        int lines = 0;
        bool onlyLive = true;
        QList<dss::ActionChoice> actions;
        QUuid action; // null = «Keine»
    };

    explicit TimerForm(QWidget *parent = nullptr);

    // resetFields: Felder zeigen wieder den Wert des Kerns (nach Prüfmeldung oder Ablehnung), auch
    // das Feld, in dem der Nutzer gerade schreibt.
    void setDetail(const Detail &detail, bool resetFields);
    QUuid shownTimer() const { return m_shown; }
    void setSectionProblem(Section section, const QString &text);
    QString sectionProblem(Section section) const;

signals:
    void nameEdited(const QString &name);
    void enabledToggled(bool enabled);
    void repeatToggled(bool repeat);
    void intervalEdited(int seconds);
    void randomToggled(bool random);
    void randomMinEdited(int seconds);
    void randomMaxEdited(int seconds);
    void linesEdited(int lines);
    void onlyLiveToggled(bool onlyLive);
    void actionChosen(const QUuid &actionId);
    void openActionClicked(const QUuid &actionId);

private:
    QWidget *createTimerSection();
    QWidget *createIntervalSection();
    QWidget *createRunSection();
    FieldSpinBox *minutesSpin(const dss::FieldRow &row, const QString &objectName, QWidget *parent);
    void connectMinutes(FieldSpinBox *spin, const dss::FieldRow &row, void (TimerForm::*edited)(int));
    void finishNameEdit();

    // Felder in Minuten (minutesRow): Grenzen, Einheit, Umrechnung
    const dss::FieldRow m_intervalRow;
    const dss::FieldRow m_randomMinRow;
    const dss::FieldRow m_randomMaxRow;

    QLabel *m_problems[SectionCount] = {};
    QLineEdit *m_name = nullptr;
    QCheckBox *m_enabled = nullptr;
    QCheckBox *m_repeat = nullptr;
    QLabel *m_intervalLabel = nullptr;
    FieldSpinBox *m_interval = nullptr;
    QCheckBox *m_random = nullptr;
    QLabel *m_randomMinLabel = nullptr;
    FieldSpinBox *m_randomMin = nullptr;
    QLabel *m_randomMaxLabel = nullptr;
    FieldSpinBox *m_randomMax = nullptr;
    QSpinBox *m_lines = nullptr;
    QCheckBox *m_onlyLive = nullptr;
    QComboBox *m_action = nullptr;
    QPushButton *m_open = nullptr;

    QUuid m_shown;
    // Stand im Feld und zuletzt vom Kern; der Name wird nicht überschrieben, solange der Nutzer
    // schreibt und der Kern nichts Neues meldet (wie CommandForm)
    QString m_shownName, m_coreName;
    bool m_filling = false;
};
