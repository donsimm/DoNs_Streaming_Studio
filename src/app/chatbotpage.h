#pragma once

#include "windowdata.h"

#include <QWidget>

class QButtonGroup;
class QHBoxLayout;
class QLabel;
class QStackedWidget;
class QTreeWidget;

// P13: Reiter «Chat-Bot» (Plan P13, 2.7; Entwurf 1-gesamtansicht). Oben die Kopfzeile mit den
// Unterreitern im Stil der Modul-Reiter und rechts dem Zustand von Kanal und Bot, darunter der
// Unterreiter «Live» mit dem Bereich «Ereignisse». Dünn (CLAUDE.md Regel 9): Texte und Liste
// kommen aus windowdata.h. P14 fügt mit addSubTab weitere Unterreiter und mit addLivePanel die
// Bereiche «Chat» und «Läufe der Aktionen» hinzu.
class ChatBotPage : public QWidget
{
    Q_OBJECT

public:
    enum SubTab { Live = 0 };

    explicit ChatBotPage(QWidget *parent = nullptr);

    void setHeader(const dss::ChatBotHeaderView &view);
    // Bereich «Ereignisse»: ganze Liste neu (Verlauf) bzw. eine Zeile anhängen, nachdem vorne
    // removedFront alte Zeilen weggefallen sind (dss::EventRows::append).
    void resetEvents(const QList<dss::EventRow> &rows);
    void appendEvent(const dss::EventRow &row, int removedFront);

    // Für P14: Unterreiter hinten anfügen (Rückgabe: Nummer); Bereich im Unterreiter «Live» an
    // position (-1 = hinten) mit Titel wie ein Dock.
    int addSubTab(const QString &title, QWidget *page);
    QWidget *addLivePanel(const QString &title, QWidget *content, int stretch, int position = -1);

private:
    QButtonGroup *m_tabs = nullptr;
    QHBoxLayout *m_tabRow = nullptr;
    QStackedWidget *m_pages = nullptr;
    QHBoxLayout *m_liveRow = nullptr;
    QLabel *m_state = nullptr;
    QTreeWidget *m_events = nullptr;
};
