#pragma once

#include "chatbotlive.h"
#include "theme.h"
#include "windowdata.h"

#include <QUuid>
#include <QWidget>

class ActionsPage;
class CommandForm;
class ListDetailPage;
class QButtonGroup;
class QCheckBox;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
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
// Ergebnisfarben aus dem Theme. P14 e2-13: Bereich «Chat» links mit der Eingabezeile (Zeilen aus
// dss::ChatRows, Eingabe aus dss::chatSendView) und in «Ereignisse» «Chat-Nachrichten ausblenden».
class ChatBotPage : public QWidget
{
    Q_OBJECT

public:
    enum SubTab { Live = 0, Actions, Commands, Timers, Variables };

    explicit ChatBotPage(const Theme &theme, QWidget *parent = nullptr);

    // Zustand und Schalter nur aus chatBotHeaderView (windowdata.h).
    void setHeader(const dss::ChatBotHeaderView &view);
    ActionsPage *actionsPage() const { return m_actions; }
    // P14 e3-4: Unterreiter «Befehle» und «Zeitnachrichten» (listdetailpage.h); «Variablen» mit den
    // Bereichen «Für alle» und «Je Zuschauer» nebeneinander (Inhalt ab e3-7).
    ListDetailPage *commandsPage() const { return m_commands; }
    CommandForm *commandForm() const { return m_commandForm; } // P14 e3-5: rechts in «Befehle»
    ListDetailPage *timersPage() const { return m_timers; }
    QWidget *variablesArea(int which) const { return m_variableAreas[which]; } // 0 Für alle, 1 Je Zuschauer
    // Bereich «Ereignisse»: sichtbare Liste neu (dss::EventRows::visibleRows) bzw. eine Änderung
    // nach dss::EventRows::add; row gilt nur bei change.appended.
    void resetEvents(const QList<dss::EventRow> &rows);
    void appendEvent(const dss::RowsAppend &change, const dss::EventRow &row);
    // Bereich «Chat»: ganze Liste neu bzw. eine Änderung nach dss::ChatRows::add (wie oben).
    void resetChat(const QList<dss::ChatRow> &rows);
    void appendChat(const dss::RowsAppend &change, const dss::ChatRow &row);
    // Eingabezeile nur aus dss::chatSendView (Zähler, «zu lang», Freigabe, Konten, Grund).
    void setChatSend(const dss::ChatSendView &view);
    QString chatText() const;
    QString chatAccount() const; // bot, broadcaster (chat_send)
    void clearChatText();
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
    // Eingabezeile: Text oder Konto geändert (das Fenster setzt danach setChatSend); «Senden» oder
    // Enter mit freigegebenem Senden; «Chat-Nachrichten ausblenden» umgeschaltet.
    void chatInputChanged();
    void chatSendClicked(const QString &text, const QString &account);
    void hideChatToggled(bool hideChat);
    // P14 e3-5: anderer Unterreiter gezeigt (SubTab)
    void subTabChanged(int tab);

private:
    void sendChat();

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
    QCheckBox *m_hideChat = nullptr;
    QTreeWidget *m_chat = nullptr;
    QComboBox *m_chatAccount = nullptr;
    QLineEdit *m_chatText = nullptr;
    QLabel *m_chatCounter = nullptr;
    QPushButton *m_chatSend = nullptr;
    bool m_chatSendEnabled = false;
    ActionsPage *m_actions = nullptr;
    ListDetailPage *m_commands = nullptr;
    CommandForm *m_commandForm = nullptr;
    ListDetailPage *m_timers = nullptr;
    QWidget *m_variableAreas[2] = {};
};
