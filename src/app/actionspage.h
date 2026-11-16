#pragma once

#include "actionview.h"

#include <QList>
#include <QUuid>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPoint;
class QSplitter;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// P14 e2-11: Unterreiter «Aktionen» im Reiter «Chat-Bot» (Plan P14 6, Checkliste P14 e2 «Aktionen»).
// Drei Bereiche wie ein Dock (dockPanel) nebeneinander in einem QSplitter, fest ohne Lösen und
// Schliessen: links die Liste der Aktionen, in der Mitte die gewählte Aktion, rechts der gewählte
// Schritt oder Auslöser. Die Mitte und rechts füllen e2-15 und e2-16 in area(). Dünn (Regel 9):
// Zeilen und Freigaben kommen aus dss::actionListRows bzw. vom MainWindow.
class ActionsPage : public QWidget
{
    Q_OBJECT

public:
    enum Area { List = 0, Action, Detail };

    explicit ActionsPage(QWidget *parent = nullptr);

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

private:
    void showGroupMenu(const QPoint &position);

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
};
