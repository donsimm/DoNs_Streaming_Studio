#pragma once

#include "chatbotlive.h"
#include "theme.h"
#include "windowdata.h"

#include <QUuid>
#include <QWidget>

class ActionsPage;
class QButtonGroup;
class QCheckBox;
class QHBoxLayout;
class QLabel;
class QPushButton;
class QSplitter;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

// P13: Reiter «Chat-Bot» (Plan P13, 2.7; Entwurf 1-gesamtansicht). Oben die Kopfzeile mit den
// Unterreitern im Stil der Modul-Reiter und rechts dem Zustand von Kanal und Bot, darunter der
// Unterreiter «Live» mit dem Bereich «Ereignisse». Dünn (CLAUDE.md Regel 9): Texte und Liste
// kommen aus windowdata.h. P14 fügt mit addSubTab weitere Unterreiter und mit addLivePanel die
// Bereiche «Chat» und «Läufe der Aktionen» hinzu. P14 e2-11: Schalter «Chat-Bot aktiv» links vom
// Zustand und Unterreiter «Aktionen» (actionspage.h). P14 e2-12: Bereiche in «Live» mit Teilern
// (QSplitter) und Bereich «Läufe der Aktionen»; Zeilen aus dss::RunRows (chatbotlive.h), die
// Ergebnisfarben aus dem Theme.
class ChatBotPage : public QWidget
{
    Q_OBJECT

public:
    enum SubTab { Live = 0, Actions };

    explicit ChatBotPage(const Theme &theme, QWidget *parent = nullptr);

    // Zustand und Schalter nur aus chatBotHeaderView (windowdata.h).
    void setHeader(const dss::ChatBotHeaderView &view);
    ActionsPage *actionsPage() const { return m_actions; }
    // Bereich «Ereignisse»: ganze Liste neu (Verlauf) bzw. eine Zeile anhängen, nachdem vorne
    // removedFront alte Zeilen weggefallen sind (dss::EventRows::append).
    void resetEvents(const QList<dss::EventRow> &rows);
    void appendEvent(const dss::EventRow &row, int removedFront);
    // Bereich «Läufe der Aktionen»: sichtbare Liste neu (dss::RunRows::visibleRows, Auswahl bleibt
    // beim selben Lauf) bzw. eine Änderung nach dss::RunRows::apply; row ist die Zeile des Laufs
    // (nullptr, wenn change.index -1).
    void resetRuns(const QList<dss::RunRow> &rows);
    void applyRun(const dss::RunRows::Change &change, const dss::RunRow *row);
    void setCurrentSubTab(SubTab tab);

    // Für P14: Unterreiter hinten anfügen (Rückgabe: Nummer); Bereich im Unterreiter «Live» an
    // position (-1 = hinten) mit Titel wie ein Dock.
    int addSubTab(const QString &title, QWidget *page);
    QWidget *addLivePanel(const QString &title, QWidget *content, int stretch, int position = -1);

signals:
    // Klick auf «Chat-Bot aktiv» (nicht beim Setzen aus setHeader): gewünschter Stand.
    void enabledClicked(bool enabled);
    // «Im Editor öffnen» mit der Aktion des gewählten Laufs, «Lauf abbrechen», «Nur Fehler»
    void openActionClicked(const QUuid &actionId);
    void cancelRunClicked(const QUuid &runId);
    void onlyErrorsToggled(bool onlyErrors);

private:
    QTreeWidgetItem *runItem(const dss::RunRow &row) const;
    void fillRunItem(QTreeWidgetItem *item, const dss::RunRow &row) const;
    void refreshRunButtons();

    Theme m_theme;
    QButtonGroup *m_tabs = nullptr;
    QHBoxLayout *m_tabRow = nullptr;
    QStackedWidget *m_pages = nullptr;
    QSplitter *m_liveSplitter = nullptr;
    QTreeWidget *m_runs = nullptr;
    QPushButton *m_openAction = nullptr;
    QPushButton *m_cancelRun = nullptr;
    QCheckBox *m_onlyErrors = nullptr;
    QCheckBox *m_enabled = nullptr;
    QLabel *m_state = nullptr;
    QTreeWidget *m_events = nullptr;
    ActionsPage *m_actions = nullptr;
};
