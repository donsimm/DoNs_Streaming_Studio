#pragma once

#include "theme.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <QUuid>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QSplitter;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QVBoxLayout;

// P14 e3-4: Aufbau «Tabelle links, Gewähltes rechts» für die Unterreiter «Befehle» und
// «Zeitnachrichten» (Entscheid des Nutzers 2026-11-16, Checkliste P14 e3). Zwei Bereiche wie ein
// Dock (dockPanel) in einem waagrechten QSplitter, fest ohne Lösen und Schliessen: links optional
// Suchfeld und Filter, die Tabelle (Spalte 0 als Kästchen «Aktiv») und die Leiste mit hinzufügen,
// kopieren, löschen; rechts ein Bildlauf mit dem Hinweis ohne Auswahl oder dem Formular, das die
// Seite einsetzt. Ohne Logik (Regel 9): Zeilen, Texte und Freigaben kommen vom MainWindow.
class ListDetailPage : public QWidget
{
    Q_OBJECT

public:
    struct Column
    {
        QString title;
        int digits = 0; // Breite in Ziffern der Schrift; die letzte Spalte ist gedehnt
    };

    // Fester Aufbau der Seite. name ist der Vorsatz der Objektnamen («commands» → commandsPage,
    // commandsSplitter, commandsTable, commandsSearch, commandsFilter, commandsAdd …).
    struct Setup
    {
        QString name;
        QString listTitle;   // «Befehle»
        QString detailTitle; // Titel rechts ohne Auswahl, z. B. «Befehl»
        QString emptyHint;   // Hinweis rechts ohne Auswahl, z. B. «Kein Befehl gewählt»
        QString tableName;   // zugänglicher Name der Tabelle
        QList<Column> columns;
        bool search = false;
        QString searchName;  // zugänglicher Name des Suchfelds
        bool filter = false;
        QString filterName;  // zugänglicher Name des Filters
        QString addTip;      // «Befehl hinzufügen»
        QString copyTip;
        QString removeTip;
        bool listHint = false; // Hinweis rechts in der Leiste (#hintLabel)
    };

    // Zeile der Tabelle. cells[i] steht in Spalte i + 1; Spalte 0 trägt nur das Kästchen.
    struct Row
    {
        QUuid id;
        bool enabled = true; // Kästchen; abgeschaltete Zeilen ganz in textDisabled
        QStringList cells;
        QString toolTip;     // z. B. Warnung, in allen Spalten
    };

    ListDetailPage(const Theme &theme, const Setup &setup, QWidget *parent = nullptr);

    // Zeilen neu. Die Auswahl bleibt über die Kennung erhalten; gibt es sie nicht mehr, wählt
    // keptSelection die nächste Zeile. Danach selectionChanged mit der Auswahl.
    void setRows(const QList<Row> &rows);
    // Wählt die Zeile; false, wenn sie nicht in der Tabelle steht (z. B. wegen der Suche). Kommt sie
    // mit den nächsten Zeilen, wird sie dann gewählt.
    bool selectRow(const QUuid &id);
    QUuid selectedRow() const; // null ohne Auswahl
    // add: Kern verbunden und Liste geladen; copyRemove: dazu eine Auswahl
    void setListButtons(bool add, bool copyRemove);
    // Hinweis rechts in der Leiste (nur mit Setup::listHint)
    void setHint(const QString &text);
    QString hint() const;
    // Titel rechts; ändert nur den Text
    void setDetailTitle(const QString &title);
    QString detailTitle() const;
    // Formular rechts; die Seite übernimmt es. Einmal beim Aufbau, die Felder bleiben (Regel 9).
    void setDetailForm(QWidget *form);
    // Optionale Leiste unter dem Bildlauf rechts, immer sichtbar wie die Leiste der Mitte in
    // «Aktionen», aber nur mit dem Formular (Befund e3-5b). Die Seite übernimmt sie.
    void setDetailBar(QWidget *bar);
    // Formular statt des Hinweises zeigen; schaltet nur die Seite um und zeigt die Leiste dazu
    void showDetail(bool show);
    bool detailShown() const;

    QString searchText() const;
    // Einträge des Filters (Wert, Bezeichnung); die Wahl bleibt über den Wert erhalten, sonst der
    // erste Eintrag.
    void setFilterChoices(const QStringList &values, const QStringList &labels);
    QString filterValue() const;

signals:
    void searchChanged(const QString &text);
    void filterChanged(const QString &value);
    void selectionChanged(const QUuid &id); // null: keine Auswahl
    void enabledToggled(const QUuid &id, bool enabled);
    void addClicked();
    void copyClicked();
    void removeClicked();

private:
    Theme m_theme;
    QSplitter *m_splitter = nullptr;
    QLabel *m_detailTitle = nullptr;
    QLineEdit *m_search = nullptr;
    QComboBox *m_filter = nullptr;
    QTreeWidget *m_table = nullptr;
    QToolButton *m_add = nullptr;
    QToolButton *m_copy = nullptr;
    QToolButton *m_remove = nullptr;
    QLabel *m_listHint = nullptr;
    QStackedWidget *m_detailPages = nullptr;
    QWidget *m_form = nullptr;
    QVBoxLayout *m_detailColumn = nullptr;
    QWidget *m_detailBar = nullptr;
    QList<QUuid> m_rowIds; // Zeilen in der Reihenfolge der Tabelle (für keptSelection)
    QUuid m_wantedRow;     // selectRow() vor dem Eintreffen der Zeile
    bool m_filling = false;
};
