#pragma once

#include <QWidget>

class QLabel;
class QSplitter;

// P14 e2-11: Unterreiter «Aktionen» im Reiter «Chat-Bot» (Plan P14 6, Checkliste P14 e2 «Aktionen»).
// Drei Bereiche wie ein Dock (dockPanel) nebeneinander in einem QSplitter, fest ohne Lösen und
// Schliessen: links die Liste der Aktionen, in der Mitte die gewählte Aktion, rechts der gewählte
// Schritt oder Auslöser. Gerüst: Die Inhalte füllen e2-14 bis e2-16 in area(). Dünn (Regel 9).
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

private:
    QSplitter *m_splitter = nullptr;
    QWidget *m_areas[3] = {};
    QLabel *m_titles[3] = {};
};
