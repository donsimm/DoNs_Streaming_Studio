#pragma once

#include "windowdata.h"

#include <QWidget>

class ActionsPage;
class QButtonGroup;
class QCheckBox;
class QHBoxLayout;
class QLabel;
class QStackedWidget;
class QTreeWidget;

// P13: Reiter «Chat-Bot» (Plan P13, 2.7; Entwurf 1-gesamtansicht). Oben die Kopfzeile mit den
// Unterreitern im Stil der Modul-Reiter und rechts dem Zustand von Kanal und Bot, darunter der
// Unterreiter «Live» mit dem Bereich «Ereignisse». Dünn (CLAUDE.md Regel 9): Texte und Liste
// kommen aus windowdata.h. P14 fügt mit addSubTab weitere Unterreiter und mit addLivePanel die
// Bereiche «Chat» und «Läufe der Aktionen» hinzu. P14 e2-11: Schalter «Chat-Bot aktiv» links vom
// Zustand und Unterreiter «Aktionen» (actionspage.h).
class ChatBotPage : public QWidget
{
    Q_OBJECT

public:
    enum SubTab { Live = 0, Actions };

    explicit ChatBotPage(QWidget *parent = nullptr);

    // Zustand und Schalter nur aus chatBotHeaderView (windowdata.h).
    void setHeader(const dss::ChatBotHeaderView &view);
    ActionsPage *actionsPage() const { return m_actions; }
    // Bereich «Ereignisse»: ganze Liste neu (Verlauf) bzw. eine Zeile anhängen, nachdem vorne
    // removedFront alte Zeilen weggefallen sind (dss::EventRows::append).
    void resetEvents(const QList<dss::EventRow> &rows);
    void appendEvent(const dss::EventRow &row, int removedFront);

    // Für P14: Unterreiter hinten anfügen (Rückgabe: Nummer); Bereich im Unterreiter «Live» an
    // position (-1 = hinten) mit Titel wie ein Dock.
    int addSubTab(const QString &title, QWidget *page);
    QWidget *addLivePanel(const QString &title, QWidget *content, int stretch, int position = -1);

signals:
    // Klick auf «Chat-Bot aktiv» (nicht beim Setzen aus setHeader): gewünschter Stand.
    void enabledClicked(bool enabled);

private:
    QButtonGroup *m_tabs = nullptr;
    QHBoxLayout *m_tabRow = nullptr;
    QStackedWidget *m_pages = nullptr;
    QHBoxLayout *m_liveRow = nullptr;
    QCheckBox *m_enabled = nullptr;
    QLabel *m_state = nullptr;
    QTreeWidget *m_events = nullptr;
    ActionsPage *m_actions = nullptr;
};
