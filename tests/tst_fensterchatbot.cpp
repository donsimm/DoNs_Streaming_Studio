// Reiter «Chat-Bot» im echten Fenster (P14 e2-11): MainWindow wie in dss-app, verbunden mit einem
// eigenen dss-core, in der X11-Sitzung (CLAUDE.md Regel 6). Prüft den Schalter «Chat-Bot aktiv»
// (an, aus, ausgegraut bei «stumm» mit einem zweiten Kern unter der Verbindung der Version im
// Einsatz), die Zustände der Kopfzeile und den Unterreiter «Aktionen». Muster: tst_fensterbearbeiten.
// P14 e2-12: «Live» mit Teilern und Bereich «Läufe der Aktionen» (Probeläufe über den Testkern).
// P14 e2-13: Bereich «Chat» (Zeilen über test_event, Senden gegen den Twitch-Testserver, nie Twitch)
// und «Chat-Nachrichten ausblenden».
// P14 e2-14: «Aktionen» links (Suche, Baum, Leiste, Gruppen umbenennen und löschen).
// P14 e3-5: «Befehle» (Zeilen, Filter, Kästchen, Formular, Prüfmeldung, Löschen, «Öffnen», Testen).
// P14 e3-7: «Variablen» (Zeilen aus dem Import, Suche, Filter, Wert ändern, Prüfmeldung, Löschen).

#include "actionfields.h"
#include "actionfieldspanel.h"
#include "actionjson.h"
#include "actionmodel.h"
#include "actionspage.h"
#include "chatbotpage.h"
#include "commandspage.h"
#include "coreclient.h"
#include "coresession.h"
#include "listdetailpage.h"
#include "mainwindow.h"
#include "prozesshilfe.h"
#include "steptypes.h"
#include "symbols.h"
#include "theme.h"
#include "timerspage.h"
#include "triggertypes.h"
#include "twitchtestserver.h"
#include "variablespage.h"
#include "version.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QHeaderView>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <functional>
#include <memory>

using namespace dss;

class TestFensterChatBot : public QObject
{
    Q_OBJECT

    QTemporaryDir m_data;
    std::unique_ptr<QTemporaryDir> m_run;
    QString m_inUseSocket; // Verbindung der «Version im Einsatz» (Kern nur in switchMuted)
    std::unique_ptr<QProcess> m_core;
    std::unique_ptr<CoreClient> m_client;
    std::unique_ptr<MainWindow> m_window;

    template <typename T>
    T *child(const char *name)
    {
        T *w = m_window->findChild<T *>(QString::fromLatin1(name));
        if (!w)
            qWarning("nicht gefunden: %s", name);
        return w;
    }

    static void stopCore(QProcess *p)
    {
        if (p->state() == QProcess::NotRunning)
            return;
        p->terminate();
        if (!p->waitForFinished(3000)) {
            p->kill();
            p->waitForFinished(3000);
        }
    }

    // Klick auf den Schalter und warten, bis der Kern den Stand meldet (actions_status).
    bool clickSwitch(bool wanted)
    {
        auto *box = child<QCheckBox>("chatBotEnabled");
        if (!box || box->isChecked() == wanted)
            return false;
        QTest::mouseClick(box, Qt::LeftButton, {}, QPoint(4, box->height() / 2));
        return QTest::qWaitFor([&] {
            const std::optional<ActionsStatus> s = m_client->actionsStatus();
            return s && s->enabled == wanted && box->isChecked() == wanted;
        }, 3000);
    }

    // Anfrage an das Aktionssystem und Antwort des Kerns (change_result)
    std::optional<ChangeResult> request(const ActionsEdit &edit)
    {
        std::optional<ChangeResult> answer;
        const qint64 id = m_client->sendActionsEdit(edit);
        if (id == 0)
            return answer;
        const QMetaObject::Connection c = connect(m_client.get(), &CoreClient::changeFinished, this,
                                                  [&](const ChangeResult &r) { if (r.requestId == id) answer = r; });
        if (!QTest::qWaitFor([&] { return answer.has_value(); }, 3000))
            qWarning("keine Antwort auf %s", qPrintable(edit.type));
        disconnect(c);
        return answer;
    }

    // Legt eine Aktion an und startet einen Probelauf; Rückgabe: Kennung des Laufs
    QUuid testRun(const QString &name, const actions::Step &step)
    {
        actions::Action a;
        a.id = QUuid::createUuid();
        a.name = name;
        a.steps = {step};
        const std::optional<ChangeResult> put = request(
            {QString::fromLatin1(msg::kActionPut), QJsonObject{{QStringLiteral("action"), actions::actionToJson(a)}}});
        if (!put || !put->ok) {
            qWarning("action_put: %s", put ? qPrintable(put->message) : "keine Antwort");
            return QUuid();
        }
        const std::optional<ChangeResult> run = request(
            {QString::fromLatin1(msg::kActionTest),
             QJsonObject{{QStringLiteral("action_id"), a.id.toString(QUuid::WithoutBraces)},
                         {QStringLiteral("arguments"), QJsonObject()}, {QStringLiteral("dry_run"), true}}});
        if (!run || !run->ok) {
            qWarning("action_test: %s", run ? qPrintable(run->message) : "keine Antwort");
            return QUuid();
        }
        return run->id;
    }

    QTreeWidgetItem *runItem(const QString &action)
    {
        QTreeWidget *runs = child<QTreeWidget>("chatBotRuns");
        for (int i = 0; i < runs->topLevelItemCount(); ++i)
            if (runs->topLevelItem(i)->text(1) == action)
                return runs->topLevelItem(i);
        return nullptr;
    }

    // Kommt die Farbe von setForeground im Bild an (Stylesheet setzt QTreeView::item color)?
    bool cellShowsColor(QTreeWidget *list, QTreeWidgetItem *item, const QColor &color, int column = 3)
    {
        list->scrollToItem(item);
        QApplication::processEvents();
        const QRect row = list->visualItemRect(item);
        const QRect cell(list->header()->sectionViewportPosition(column), row.top(),
                         list->header()->sectionSize(column), row.height());
        const QImage image = list->viewport()->grab(cell).toImage();
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const QColor p = image.pixelColor(x, y);
                if (qAbs(p.red() - color.red()) < 12 && qAbs(p.green() - color.green()) < 12
                    && qAbs(p.blue() - color.blue()) < 12)
                    return true;
            }
        return false;
    }

    // Zeile in einer Liste, deren Spalte column den Text text hat
    static QTreeWidgetItem *findRow(QTreeWidget *list, int column, const QString &text)
    {
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            if (list->topLevelItem(i)->text(column) == text)
                return list->topLevelItem(i);
        return nullptr;
    }

    // twitch.chat_message für test_event (Aufbau wie tst_kernaktionen, chatEvent)
    static QJsonObject chatEvent(const QString &text, bool moderator, const QString &color)
    {
        const QJsonObject data{{QStringLiteral("message_id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                               {QStringLiteral("user_id"), QStringLiteral("900000001")},
                               {QStringLiteral("user_login"), QStringLiteral("dsstestzuschauer")},
                               {QStringLiteral("user_name"), QStringLiteral("DSS-Test Zuschauer")},
                               {QStringLiteral("text"), text},
                               {QStringLiteral("fragments"),
                                QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("text")},
                                                       {QStringLiteral("text"), text},
                                                       {QStringLiteral("emote_id"), QJsonValue::Null}}}},
                               {QStringLiteral("color"), color},
                               {QStringLiteral("badges"), QJsonArray()},
                               {QStringLiteral("role"), moderator ? 3 : 1},
                               {QStringLiteral("is_subscriber"), false},
                               {QStringLiteral("is_vip"), false},
                               {QStringLiteral("is_moderator"), moderator},
                               {QStringLiteral("is_broadcaster"), false},
                               {QStringLiteral("is_bot_account"), false},
                               {QStringLiteral("bits"), 0},
                               {QStringLiteral("reply_to"), QJsonValue::Null},
                               {QStringLiteral("reward_id"), QJsonValue::Null},
                               {QStringLiteral("message_type"), QStringLiteral("text")},
                               {QStringLiteral("is_highlight"), false},
                               {QStringLiteral("source_broadcaster_id"), QJsonValue::Null}};
        return QJsonObject{{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                           {QStringLiteral("type"), QStringLiteral("twitch.chat_message")},
                           {QStringLiteral("origin"), QStringLiteral("twitch")},
                           {QStringLiteral("time"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                           {QStringLiteral("origin_id"), QString()},
                           {QStringLiteral("data"), data}};
    }

    // Beantwortet den nächsten modalen Dialog, sobald er offen ist (wie tst_fensterbearbeiten).
    static void answerDialog(std::function<void(QWidget *)> answer)
    {
        auto *timer = new QTimer(qApp);
        timer->setInterval(20);
        QObject::connect(timer, &QTimer::timeout, timer, [timer, answer] {
            if (QWidget *dialog = QApplication::activeModalWidget()) {
                timer->stop();
                timer->deleteLater();
                answer(dialog);
            }
        });
        timer->start();
    }

    // Rückfrage (confirm) beantworten: Knopf mit dem Text button; *text bekommt die Frage.
    static void answerQuestion(const QString &button, QString *text)
    {
        answerDialog([button, text](QWidget *dialog) {
            auto *box = qobject_cast<QMessageBox *>(dialog);
            if (!box)
                return;
            *text = box->text();
            for (QAbstractButton *b : box->buttons())
                if (b->text() == button)
                    b->click();
        });
    }

    // Zeile im Baum der Aktionen (alle Ebenen) mit dem Text text in Spalte 0
    static QTreeWidgetItem *treeItem(QTreeWidget *tree, const QString &text)
    {
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->text(0) == text)
                return *it;
        return nullptr;
    }

    bool putTestAction(const QUuid &id, const QString &name, const QString &group)
    {
        actions::Action a;
        a.id = id;
        a.name = name;
        a.group = group;
        const std::optional<ChangeResult> put = request(
            {QString::fromLatin1(msg::kActionPut), QJsonObject{{QStringLiteral("action"), actions::actionToJson(a)}}});
        return put && put->ok;
    }

    // Aktion im Stand des Kerns (actions_config), leer = gibt es nicht
    std::optional<actions::Action> coreAction(const QUuid &id) const
    {
        const std::optional<ActionsConfigInfo> info = m_client->actionsConfig();
        const actions::Action *a = info && info->config ? info->config->action(id) : nullptr;
        return a ? std::optional<actions::Action>(*a) : std::nullopt;
    }

    static void openChatBot(QWidget *window)
    {
        for (QToolButton *b : window->findChild<QFrame *>(QStringLiteral("moduleTabs"))->findChildren<QToolButton *>())
            if (b->text() == QStringLiteral("Chat-Bot"))
                QTest::mouseClick(b, Qt::LeftButton);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        m_run = testhelp::runtimeDir();
        const QString socket = m_run->filePath(QStringLiteral("kern.sock"));
        m_inUseSocket = m_run->filePath(QStringLiteral("einsatz.sock"));
        m_core = testhelp::startCore(m_data.path(), socket, testhelp::testConfigHome(),
                                     {QStringLiteral("--einsatz-verbindung"), m_inUseSocket,
                                      QStringLiteral("--testereignisse")});
        QString log;
        QVERIFY2(testhelp::waitForLog(m_core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        CoreClient::Options o;
        o.socketPath = socket;
        o.program = QStringLiteral("dss-app"); // wie das Fenster
        o.version = version();
        m_client = std::make_unique<CoreClient>(o);

        const Theme theme = darkTheme();
        qApp->setStyleSheet(styleSheet(theme, symbolPath));
        m_window = std::make_unique<MainWindow>(theme, false, m_client.get());
        m_window->resize(1280, 800);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        m_client->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isConnected() && m_client->actionsStatus().has_value(), 5000);
        openChatBot(m_window.get());
        QTRY_VERIFY_WITH_TIMEOUT(child<QWidget>("chatBotPage")->isVisible(), 3000);
    }

    void cleanupTestCase()
    {
        if (m_window)
            m_window->close(); // bittet den Kern, sich zu beenden (einziges Fenster)
        m_window.reset();
        m_client.reset();
        if (m_core && m_core->state() != QProcess::NotRunning && !m_core->waitForFinished(3000))
            stopCore(m_core.get());
    }

    // Unterreiter nur über addSubTab mit moduleButton: «Live» und «Aktionen»; «Aktionen» zeigt drei
    // Bereiche wie ein Dock in einem QSplitter.
    void subTabs()
    {
        const QList<QToolButton *> tabs = child<QFrame>("chatBotTabs")->findChildren<QToolButton *>();
        QCOMPARE(tabs.size(), 5);
        // P14 e3-4: Reihenfolge nach Plan 6
        QStringList tabTitles;
        for (int i = 0; i < 5; ++i)
            tabTitles << child<QToolButton>(qPrintable(QStringLiteral("chatBotTab%1").arg(i)))->text();
        QCOMPARE(tabTitles, QStringList({QStringLiteral("Live"), QStringLiteral("Aktionen"), QStringLiteral("Befehle"),
                                         QStringLiteral("Zeitnachrichten"), QStringLiteral("Variablen")}));
        auto *live = child<QToolButton>("chatBotTab0");
        auto *actions = child<QToolButton>("chatBotTab1");
        QVERIFY(live && actions);
        QCOMPARE(actions->text(), QStringLiteral("Aktionen"));
        QVERIFY(actions->isCheckable());
        auto *page = child<ActionsPage>("actionsPage");
        QVERIFY(page);
        QVERIFY(!page->isVisible());
        QTest::mouseClick(actions, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), 3000);
        QVERIFY(actions->isChecked() && !live->isChecked());

        auto *splitter = page->findChild<QSplitter *>(QStringLiteral("actionsSplitter"));
        QVERIFY(splitter);
        QCOMPARE(splitter->count(), 3);
        QVERIFY(!splitter->childrenCollapsible());
        QStringList titles;
        for (int i = 0; i < splitter->count(); ++i) {
            auto *title = splitter->widget(i)->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"));
            QVERIFY(title);
            QVERIFY(splitter->widget(i)->findChild<QFrame *>(QStringLiteral("chatBotPanelBody")));
            titles << title->text();
        }
        QCOMPARE(titles, QStringList({QStringLiteral("Aktionen"), QStringLiteral("Aktion"), QStringLiteral("Schritt")}));
        QCOMPARE(page->areaTitle(ActionsPage::Action), QStringLiteral("Aktion"));
        page->setAreaTitle(ActionsPage::Action, QStringLiteral("Aktion «Begrüssung»"));
        QCOMPARE(splitter->widget(ActionsPage::Action)->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"))->text(),
                 QStringLiteral("Aktion «Begrüssung»"));
        page->setAreaTitle(ActionsPage::Action, QStringLiteral("Aktion"));
        for (int i = 0; i < 3; ++i)
            QVERIFY(page->area(ActionsPage::Area(i)) && page->area(ActionsPage::Area(i))->isVisible());

        QTest::mouseClick(live, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!page->isVisible(), 3000);
        QVERIFY(live->isChecked());
    }

    // P14 e3-4: «Befehle» und «Zeitnachrichten» als ListDetailPage, «Variablen» mit zwei Bereichen.
    // Spalten und Kopf, Kästchen sendet enabledToggled (nicht beim Füllen), abgeschaltete Zeile in
    // textDisabled, Auswahl bleibt beim Neufüllen, Wechsel ohne neue Widgets, Theme der Tabellen.
    void listDetailPages()
    {
        auto *chatBot = child<ChatBotPage>("chatBotPage");
        QVERIFY(chatBot);
        QVERIFY(chatBot->commandsPage() && chatBot->commandsPage()->objectName() == QStringLiteral("commandsPage"));
        QTest::mouseClick(child<QToolButton>("chatBotTab2"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(chatBot->commandsPage()->isVisible(), 3000);
        // Seit e3-5 füllt das MainWindow die Seite im Fenster; die Bausteine prüft eine eigene Seite
        // mit demselben Aufbau (commandsPageSetup).
        ListDetailPage standalone(darkTheme(), commandsPageSetup());
        standalone.resize(1000, 600);
        standalone.show();
        QVERIFY(QTest::qWaitForWindowExposed(&standalone));
        ListDetailPage *commands = &standalone;

        auto *splitter = commands->findChild<QSplitter *>(QStringLiteral("commandsSplitter"));
        QVERIFY(splitter);
        QCOMPARE(splitter->count(), 2);
        QVERIFY(!splitter->childrenCollapsible());
        QStringList titles;
        for (int i = 0; i < 2; ++i)
            titles << splitter->widget(i)->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"))->text();
        QCOMPARE(titles, QStringList({QStringLiteral("Befehle"), QStringLiteral("Befehl")}));
        commands->setDetailTitle(QStringLiteral("Befehl «!test»"));
        QCOMPARE(splitter->widget(1)->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"))->text(),
                 QStringLiteral("Befehl «!test»"));
        commands->setDetailTitle(QStringLiteral("Befehl"));

        auto *search = commands->findChild<QLineEdit *>(QStringLiteral("commandsSearch"));
        QVERIFY(search && search->isClearButtonEnabled() && !search->accessibleName().isEmpty());
        QCOMPARE(search->placeholderText(), QStringLiteral("Suchen …"));
        auto *filter = commands->findChild<QComboBox *>(QStringLiteral("commandsFilter"));
        QVERIFY(filter && filter->isVisible());
        QVERIFY(qAbs(filter->geometry().center().y() - search->geometry().center().y()) <= 1); // dieselbe Zeile
        QSignalSpy filterSpy(commands, &ListDetailPage::filterChanged);
        commands->setFilterChoices({QString(), QStringLiteral("Spiele")},
                                   {QStringLiteral("Alle Gruppen"), QStringLiteral("Spiele")});
        QCOMPARE(filterSpy.count(), 0); // Wert bleibt leer
        filter->setCurrentIndex(1);
        QCOMPARE(filterSpy.count(), 1);
        QCOMPARE(commands->filterValue(), QStringLiteral("Spiele"));
        commands->setFilterChoices({QString()}, {QStringLiteral("Alle Gruppen")});
        QCOMPARE(filterSpy.count(), 2); // «Spiele» gibt es nicht mehr
        QCOMPARE(commands->filterValue(), QString());

        auto *table = commands->findChild<QTreeWidget *>(QStringLiteral("commandsTable"));
        QVERIFY(table);
        QCOMPARE(table->columnCount(), 8);
        QStringList headers;
        for (int i = 0; i < 8; ++i)
            headers << table->headerItem()->text(i);
        QCOMPARE(headers, QStringList({QStringLiteral("Aktiv"), QStringLiteral("Auslöser"), QStringLiteral("Gruppe"),
                                       QStringLiteral("Wer darf"), QStringLiteral("Abkl. Kanal"),
                                       QStringLiteral("Abkl. Person"), QStringLiteral("Aktionen"), QStringLiteral("Benutzt")}));
        QVERIFY(!table->isHeaderHidden() && !table->rootIsDecorated() && table->uniformRowHeights());
        QCOMPARE(table->selectionMode(), QAbstractItemView::SingleSelection);
        QCOMPARE(table->editTriggers(), QAbstractItemView::NoEditTriggers);
        QVERIFY(table->header()->stretchLastSection());
        const int digit = table->fontMetrics().horizontalAdvance(QLatin1Char('0'));
        QCOMPARE(table->columnWidth(1), digit * 18);

        // Leiste: ausgegraut bis verbunden und geladen bzw. bis zur Auswahl
        auto *add = commands->findChild<QToolButton *>(QStringLiteral("commandsAdd"));
        auto *copy = commands->findChild<QToolButton *>(QStringLiteral("commandsCopy"));
        auto *remove = commands->findChild<QToolButton *>(QStringLiteral("commandsRemove"));
        QVERIFY(add && copy && remove);
        QVERIFY(!add->isEnabled() && !copy->isEnabled() && !remove->isEnabled());
        QCOMPARE(add->toolTip(), QStringLiteral("Befehl hinzufügen"));
        QCOMPARE(remove->accessibleName(), QStringLiteral("Befehl löschen"));
        commands->setListButtons(true, false);
        QVERIFY(add->isEnabled() && !copy->isEnabled() && !remove->isEnabled());
        commands->setHint(QStringLiteral("2 Befehle"));
        QCOMPARE(commands->hint(), QStringLiteral("2 Befehle"));

        // Ohne Auswahl rechts der Hinweis
        auto *empty = commands->findChild<QWidget *>(QStringLiteral("commandsEmpty"));
        QVERIFY(empty && empty->isVisible() && !commands->detailShown());
        QCOMPARE(empty->findChild<QLabel *>(QStringLiteral("hintLabel"))->text(), QStringLiteral("Kein Befehl gewählt"));

        const QUuid a = QUuid::createUuid(), b = QUuid::createUuid(), c = QUuid::createUuid();
        const auto row = [](const QUuid &id, bool enabled, const QString &trigger) {
            ListDetailPage::Row r;
            r.id = id;
            r.enabled = enabled;
            r.cells = {trigger, QStringLiteral("Spiele"), QStringLiteral("Alle")};
            return r;
        };
        QSignalSpy toggled(commands, &ListDetailPage::enabledToggled);
        QSignalSpy selected(commands, &ListDetailPage::selectionChanged);
        commands->setRows({row(a, true, QStringLiteral("!a")), row(b, false, QStringLiteral("!b")),
                           row(c, true, QStringLiteral("!c"))});
        QCOMPARE(toggled.count(), 0); // nicht beim Füllen
        QCOMPARE(table->topLevelItemCount(), 3);
        QTreeWidgetItem *itemB = table->topLevelItem(1);
        QCOMPARE(itemB->text(1), QStringLiteral("!b"));
        QCOMPARE(itemB->checkState(0), Qt::Unchecked);
        QVERIFY(itemB->flags() & Qt::ItemIsUserCheckable);
        for (int i = 0; i < 8; ++i)
            QCOMPARE(itemB->foreground(i).color(), darkTheme().textDisabled);
        QVERIFY(table->topLevelItem(0)->foreground(1).style() == Qt::NoBrush);

        table->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        QCOMPARE(toggled.count(), 1);
        QCOMPARE(toggled.at(0).at(0).toUuid(), a);
        QCOMPARE(toggled.at(0).at(1).toBool(), false);

        // Auswahl bleibt beim Neufüllen; gibt es sie nicht mehr, die nächste Zeile
        QVERIFY(commands->selectRow(b));
        QCOMPARE(commands->selectedRow(), b);
        QCOMPARE(selected.last().at(0).toUuid(), b);
        const int widgetsBefore = commands->findChildren<QWidget *>().size();
        commands->setRows({row(c, true, QStringLiteral("!c")), row(a, true, QStringLiteral("!a")),
                           row(b, true, QStringLiteral("!b2"))});
        QCOMPARE(commands->selectedRow(), b);
        QCOMPARE(selected.last().at(0).toUuid(), b);
        QCOMPARE(toggled.count(), 1);
        commands->setRows({row(c, true, QStringLiteral("!c")), row(a, true, QStringLiteral("!a"))});
        QVERIFY(!commands->selectedRow().isNull());
        QCOMPARE(commands->selectedRow(), a); // keptSelection: der Nachbar
        // Gewünschte Zeile, die erst mit den nächsten Zeilen kommt
        QVERIFY(!commands->selectRow(b));
        commands->setRows({row(c, true, QStringLiteral("!c")), row(b, true, QStringLiteral("!b"))});
        QCOMPARE(commands->selectedRow(), b);

        // Formular rechts: einmal eingesetzt, der Wechsel schaltet nur die Seite um
        auto *form = new QLabel(QStringLiteral("Formular"));
        form->setObjectName(QStringLiteral("commandsTestForm"));
        commands->setDetailForm(form);
        const int widgetsWithForm = commands->findChildren<QWidget *>().size();
        commands->showDetail(true);
        QVERIFY(commands->detailShown() && form->isVisible() && !empty->isVisible());
        commands->setRows({row(c, false, QStringLiteral("!c")), row(b, true, QStringLiteral("!b"))});
        commands->showDetail(false);
        QVERIFY(!commands->detailShown() && empty->isVisible());
        QCOMPARE(commands->findChildren<QWidget *>().size(), widgetsWithForm);
        QCOMPARE(widgetsWithForm, widgetsBefore + 1);
        commands->setDetailForm(nullptr);
        commands->setRows({});
        QVERIFY(commands->selectedRow().isNull());
        commands->setListButtons(false, false);
        commands->setHint(QString());

        // «Zeitnachrichten»: ohne Suchfeld, Filter und Hinweis in der Leiste
        ListDetailPage *timers = chatBot->timersPage();
        QVERIFY(timers && timers->objectName() == QStringLiteral("timersPage"));
        QTest::mouseClick(child<QToolButton>("chatBotTab3"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(timers->isVisible(), 3000);
        QVERIFY(!timers->findChild<QLineEdit *>(QStringLiteral("timersSearch")));
        QVERIFY(!timers->findChild<QComboBox *>(QStringLiteral("timersFilter")));
        auto *timersTable = timers->findChild<QTreeWidget *>(QStringLiteral("timersTable"));
        QVERIFY(timersTable);
        QStringList timerHeaders;
        for (int i = 0; i < timersTable->columnCount(); ++i)
            timerHeaders << timersTable->headerItem()->text(i);
        QCOMPARE(timerHeaders, QStringList({QStringLiteral("Aktiv"), QStringLiteral("Name"), QStringLiteral("Abstand"),
                                            QStringLiteral("Chat-Zeilen"), QStringLiteral("Nur im Stream"),
                                            QStringLiteral("Aktionen")}));
        auto *timersSplitter = timers->findChild<QSplitter *>(QStringLiteral("timersSplitter"));
        QCOMPARE(timersSplitter->widget(0)->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"))->text(),
                 QStringLiteral("Zeitnachrichten"));
        QCOMPARE(timers->findChild<QWidget *>(QStringLiteral("timersEmpty"))->findChild<QLabel *>(
                     QStringLiteral("hintLabel"))->text(), QStringLiteral("Keine Zeitnachricht gewählt"));

        // «Variablen»: zwei Bereiche nebeneinander mit Teiler
        QTest::mouseClick(child<QToolButton>("chatBotTab4"), Qt::LeftButton);
        auto *variables = child<QSplitter>("variablesSplitter");
        QTRY_VERIFY_WITH_TIMEOUT(variables && variables->isVisible(), 3000);
        QCOMPARE(variables->orientation(), Qt::Horizontal);
        QVERIFY(!variables->childrenCollapsible());
        QCOMPARE(variables->count(), 2);
        QStringList variableTitles;
        for (int i = 0; i < 2; ++i) {
            variableTitles << variables->widget(i)->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"))->text();
            QVERIFY(variables->widget(i)->isVisible());
        }
        QCOMPARE(variableTitles, QStringList({QStringLiteral("Für alle"), QStringLiteral("Je Zuschauer")}));

        // Theme: Zeilen der neuen Tabellen wie «Läufe der Aktionen»
        for (const char *name : {"#commandsTable::item", "#timersTable::item", "#globalVariables::item",
                                 "#userVariables::item", "#queuesList::item"})
            QVERIFY2(qApp->styleSheet().contains(QLatin1String(name)), name);

        QTest::mouseClick(child<QToolButton>("chatBotTab0"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!variables->isVisible(), 3000);
    }

    // Schalter links vom Zustand; ein Klick schickt actions_set_enabled, den Stand zeigt der Kern.
    // «aus» grau (state off), an ohne Twitch: Text von Twitch. Die Kopfzeile behält ihre Höhe.
    void switchOnOff()
    {
        auto *box = child<QCheckBox>("chatBotEnabled");
        auto *state = child<QLabel>("chatBotState");
        auto *header = child<QWidget>("chatBotHeader");
        QVERIFY(box && state && header);
        QCOMPARE(box->text(), QStringLiteral("Chat-Bot aktiv"));
        QVERIFY(box->isEnabled());
        QVERIFY(box->geometry().right() < state->geometry().left());
        QCOMPARE(state->geometry().left() - box->geometry().right() - 1, 6);
        QCOMPARE(header->sizePolicy().verticalPolicy(), QSizePolicy::Fixed);
        const int height = header->height();

        const bool start = m_client->actionsStatus()->enabled;
        QCOMPARE(box->isChecked(), start);
        QVERIFY(clickSwitch(!start));
        QVERIFY(clickSwitch(start));

        QVERIFY(clickSwitch(false) || !box->isChecked());
        QCOMPARE(m_client->actionsStatus()->state, QStringLiteral("off"));
        QCOMPARE(state->text(), QStringLiteral("aus"));
        QCOMPARE(state->property("state").toString(), QStringLiteral("off"));
        QCOMPARE(header->height(), height);

        QVERIFY(clickSwitch(true));
        QCOMPARE(m_client->actionsStatus()->state, QStringLiteral("on"));
        QVERIFY(state->text() != QStringLiteral("aus"));
        QVERIFY(state->text().startsWith(QStringLiteral("Twitch: "))); // Kern ohne Twitch
        QCOMPARE(header->height(), height);
    }

    // Stumm: zweiter Kern unter der Verbindung der Version im Einsatz. Schalter mit echtem Stand,
    // ausgegraut; Text «stumm: …» in der Farbe «warning».
    void switchMuted()
    {
        auto *box = child<QCheckBox>("chatBotEnabled");
        auto *state = child<QLabel>("chatBotState");
        auto *header = child<QWidget>("chatBotHeader");
        const int height = header->height();
        QVERIFY(clickSwitch(false) || !box->isChecked());

        QTemporaryDir inUseData;
        std::unique_ptr<QProcess> inUse = testhelp::startCore(inUseData.path(), m_inUseSocket);
        QString log;
        QVERIFY2(testhelp::waitForLog(inUse.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        // Der Kern prüft alle 2 s
        QTRY_COMPARE_WITH_TIMEOUT(m_client->actionsStatus()->state, QStringLiteral("muted"), 6000);
        QCOMPARE(state->text(), QStringLiteral("stumm: Version im Einsatz läuft"));
        QCOMPARE(state->property("state").toString(), QStringLiteral("warning"));
        QVERIFY(!box->isEnabled());
        QVERIFY(!box->isChecked());
        QCOMPARE(header->height(), height);
        // Ausgegraut: Ein Klick schickt nichts
        QTest::mouseClick(box, Qt::LeftButton, {}, QPoint(4, box->height() / 2));
        QTest::qWait(300);
        QVERIFY(!m_client->actionsStatus()->enabled);

        stopCore(inUse.get());
        QTRY_COMPARE_WITH_TIMEOUT(m_client->actionsStatus()->state, QStringLiteral("off"), 6000);
        QVERIFY(box->isEnabled());
        QCOMPARE(state->text(), QStringLiteral("aus"));
    }

    // «Live»: Bereiche nur über addLivePanel in einem QSplitter, Teiler 4 px, keiner zuzuschieben.
    void liveSplitter()
    {
        auto *splitter = child<QSplitter>("chatBotLiveSplitter");
        QVERIFY(splitter && splitter->isVisible());
        QVERIFY(!splitter->childrenCollapsible());
        QCOMPARE(splitter->count(), 3);
        QStringList titles;
        for (int i = 0; i < splitter->count(); ++i) {
            auto *title = splitter->widget(i)->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"));
            QVERIFY(title);
            QVERIFY(splitter->widget(i)->findChild<QFrame *>(QStringLiteral("chatBotPanelBody")));
            titles << title->text();
        }
        QCOMPARE(titles, QStringList({QStringLiteral("Chat"), QStringLiteral("Ereignisse"),
                                      QStringLiteral("Läufe der Aktionen")}));
        QCOMPARE(splitter->handle(1)->width(), 4);
        // Teiler verschieben: alle Bereiche bleiben sichtbar
        const QList<int> before = splitter->sizes();
        splitter->setSizes({before.at(0) + 40, before.at(1) - 40, before.at(2)});
        QCOMPARE(splitter->sizes().at(0), before.at(0) + 40);
        splitter->setSizes(before);

        // «Chat»: Liste ohne Kopf, darunter die Eingabezeile als Leiste (Konto, Feld, Zähler, Senden)
        auto *chat = child<QTreeWidget>("chatBotChat");
        auto *account = child<QComboBox>("chatBotChatAccount");
        auto *text = child<QLineEdit>("chatBotChatText");
        auto *counter = child<QLabel>("chatBotChatCounter");
        auto *send = child<QPushButton>("chatBotChatSend");
        QVERIFY(chat && account && text && counter && send);
        QVERIFY(chat->isHeaderHidden());
        QCOMPARE(chat->focusPolicy(), Qt::NoFocus);
        QCOMPARE(account->count(), 2);
        QCOMPARE(account->itemText(0), QStringLiteral("als Bot"));
        QCOMPARE(account->itemText(1), QStringLiteral("als Kanal"));
        QCOMPARE(text->placeholderText(), QStringLiteral("Nachricht an den Chat …"));
        QCOMPARE(counter->text(), QStringLiteral("0 / 500"));
        QCOMPARE(send->text(), QStringLiteral("Senden"));
        QCOMPARE(text->parentWidget()->objectName(), QStringLiteral("dockButtons"));
        QVERIFY(account->geometry().right() < text->geometry().left());
        QVERIFY(text->geometry().right() < counter->geometry().left());
        QVERIFY(counter->geometry().right() < send->geometry().left());
        QVERIFY(counter->minimumWidth() >= counter->fontMetrics().horizontalAdvance(QStringLiteral("500 / 500")));
        QVERIFY(text->width() > send->width()); // das Feld nimmt den freien Platz
        // keine Lücke zwischen Feld und Zähler (nur der Abstand der Leiste)
        QCOMPARE(counter->geometry().left() - text->geometry().right() - 1,
                 text->parentWidget()->layout()->spacing());
        // «Ereignisse»: Leiste mit «Chat-Nachrichten ausblenden», Standard an
        auto *hide = child<QCheckBox>("chatBotHideChat");
        QVERIFY(hide);
        // zugängliche Namen wie in den Docks
        QCOMPARE(chat->accessibleName(), QStringLiteral("Chat"));
        QCOMPARE(account->accessibleName(), QStringLiteral("Konto für den Chat"));
        QCOMPARE(text->accessibleName(), QStringLiteral("Nachricht an den Chat"));
        QCOMPARE(counter->accessibleName(), QStringLiteral("Zeichen der Nachricht"));
        QCOMPARE(hide->accessibleName(), QStringLiteral("Chat-Nachrichten ausblenden"));
        QCOMPARE(child<QTreeWidget>("chatBotEvents")->accessibleName(), QStringLiteral("Ereignisse"));
        QCOMPARE(child<QTreeWidget>("chatBotRuns")->accessibleName(), QStringLiteral("Läufe der Aktionen"));
        QCOMPARE(hide->text(), QStringLiteral("Chat-Nachrichten ausblenden"));
        QVERIFY(hide->isChecked());
        QCOMPARE(hide->parentWidget()->objectName(), QStringLiteral("dockButtons"));

        auto *runs = child<QTreeWidget>("chatBotRuns");
        QVERIFY(runs);
        QCOMPARE(runs->columnCount(), 4);
        QCOMPARE(runs->headerItem()->text(0), QStringLiteral("Zeit"));
        QCOMPARE(runs->headerItem()->text(1), QStringLiteral("Aktion"));
        QCOMPARE(runs->headerItem()->text(2), QStringLiteral("Auslöser"));
        QCOMPARE(runs->headerItem()->text(3), QStringLiteral("Ergebnis"));
        QVERIFY(runs->header()->stretchLastSection());
        QCOMPARE(runs->focusPolicy(), Qt::NoFocus);
        auto *open = child<QPushButton>("chatBotRunOpenAction");
        auto *cancel = child<QPushButton>("chatBotRunCancel");
        auto *onlyErrors = child<QCheckBox>("chatBotRunsOnlyErrors");
        QVERIFY(open && cancel && onlyErrors);
        QCOMPARE(open->text(), QStringLiteral("Im Editor öffnen"));
        QCOMPARE(cancel->text(), QStringLiteral("Lauf abbrechen"));
        QCOMPARE(onlyErrors->text(), QStringLiteral("Nur Fehler"));
        QCOMPARE(open->parentWidget()->objectName(), QStringLiteral("dockButtons"));
        QVERIFY(!open->isEnabled() && !cancel->isEnabled());
        QVERIFY(open->geometry().right() < cancel->geometry().left());
        QVERIFY(cancel->geometry().right() < onlyErrors->geometry().left());
    }

    // Probeläufe erscheinen; «Lauf abbrechen» schickt action_run_cancel; Ergebnisfarben aus dem
    // Theme; «Nur Fehler»; «Im Editor öffnen» wechselt auf «Aktionen»; Verlauf nach neuer Verbindung.
    void actionRuns()
    {
        const Theme theme = darkTheme();
        QVERIFY(clickSwitch(true) || child<QCheckBox>("chatBotEnabled")->isChecked());
        auto *runs = child<QTreeWidget>("chatBotRuns");
        auto *open = child<QPushButton>("chatBotRunOpenAction");
        auto *cancel = child<QPushButton>("chatBotRunCancel");
        auto *onlyErrors = child<QCheckBox>("chatBotRunsOnlyErrors");
        QVERIFY(runs && open && cancel && onlyErrors);

        // Lauf, der wartet: erscheint, abbrechen
        actions::Step wait = actions::newStep(QString::fromLatin1(actions::kStepDelay));
        wait.fields.insert(QStringLiteral("value_ms"), 10000);
        const QUuid waitRun = testRun(QStringLiteral("DSS-Test Warten"), wait);
        QVERIFY(!waitRun.isNull());
        QTRY_VERIFY_WITH_TIMEOUT(runItem(QStringLiteral("DSS-Test Warten")), 3000);
        QTreeWidgetItem *item = runItem(QStringLiteral("DSS-Test Warten"));
        QTRY_VERIFY_WITH_TIMEOUT(item->text(3).contains(QStringLiteral("läuft")), 3000);
        QVERIFY(item->text(3).startsWith(QStringLiteral("Probelauf")));
        QVERIFY(!item->text(0).isEmpty() && !item->text(2).isEmpty());
        QVERIFY(!cancel->isEnabled());
        runs->setCurrentItem(item);
        QVERIFY(cancel->isEnabled() && open->isEnabled());
        QTest::mouseClick(cancel, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!item->text(3).contains(QStringLiteral("läuft")), 3000);
        QCOMPARE(item->foreground(3).color(), theme.stateConnecting); // abgebrochen: warning
        QVERIFY(!cancel->isEnabled());
        QVERIFY(open->isEnabled());

        // Lauf mit Fehler (leerer Text) in stateError, auch im Bild
        actions::Step empty = actions::newStep(QString::fromLatin1(actions::kStepChatSend));
        empty.fields.insert(QStringLiteral("text"), QString());
        QVERIFY(!testRun(QStringLiteral("DSS-Test Fehler"), empty).isNull());
        QTRY_VERIFY_WITH_TIMEOUT(runItem(QStringLiteral("DSS-Test Fehler"))
                                     && runItem(QStringLiteral("DSS-Test Fehler"))->foreground(3).color() == theme.stateError,
                                 3000);
        QTreeWidgetItem *failed = runItem(QStringLiteral("DSS-Test Fehler"));
        QVERIFY2(cellShowsColor(runs, failed, theme.stateError), qPrintable(failed->text(3)));

        // «Nur Fehler»: nur der Fehler bleibt; Auswahl weg, Knöpfe ausgegraut
        const int all = runs->topLevelItemCount();
        QVERIFY(all >= 2);
        QTest::mouseClick(onlyErrors, Qt::LeftButton, {}, QPoint(4, onlyErrors->height() / 2));
        QVERIFY(onlyErrors->isChecked());
        QCOMPARE(runs->topLevelItemCount(), 1);
        QCOMPARE(runs->topLevelItem(0)->text(1), QStringLiteral("DSS-Test Fehler"));
        QVERIFY(!cancel->isEnabled() && !open->isEnabled());
        QTest::mouseClick(onlyErrors, Qt::LeftButton, {}, QPoint(4, onlyErrors->height() / 2));
        QCOMPARE(runs->topLevelItemCount(), all);

        // «Im Editor öffnen»: Wechsel auf den Unterreiter «Aktionen», die Aktion im Baum gewählt
        // (auch wenn die Suche sie ausblendet: die Suche wird geleert)
        child<QLineEdit>("actionsSearch")->setText(QStringLiteral("DSS-Test Fehler"));
        runs->setCurrentItem(runItem(QStringLiteral("DSS-Test Warten")));
        QTest::mouseClick(open, Qt::LeftButton);
        auto *page = child<ActionsPage>("actionsPage");
        QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), 3000);
        QVERIFY(child<QToolButton>("chatBotTab1")->isChecked());
        QVERIFY(child<QLineEdit>("actionsSearch")->text().isEmpty());
        QTreeWidget *tree = child<QTreeWidget>("actionsTree");
        QVERIFY(tree->currentItem());
        QCOMPARE(tree->currentItem()->text(0), QStringLiteral("DSS-Test Warten"));
        QTest::mouseClick(child<QToolButton>("chatBotTab0"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(runs->isVisible(), 3000);

        // Neue Verbindung: get_action_runs liefert den Verlauf, die Liste bleibt gleich
        int received = 0;
        const QMetaObject::Connection c = connect(m_client.get(), &CoreClient::actionRunsReceived, this,
                                                  [&](const QList<actions::RunRecord> &) { ++received; });
        m_client->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!m_client->isConnected(), 3000);
        m_client->start();
        QTRY_COMPARE_WITH_TIMEOUT(received, 1, 5000);
        disconnect(c);
        QCOMPARE(runs->topLevelItemCount(), all);
        QVERIFY(runItem(QStringLiteral("DSS-Test Fehler")));
    }

    // P14 e2-13: «Chat» zeigt Chat-Nachrichten (Abzeichen, Chatfarbe) und gestartete Läufe grau;
    // «Chat-Nachrichten ausblenden» in «Ereignisse»; Zähler und «zu lang»; ohne Twitch beide Konten
    // ausgegraut, «Senden» aus mit Grund.
    void chatPanel()
    {
        const Theme theme = darkTheme();
        auto *chat = child<QTreeWidget>("chatBotChat");
        auto *events = child<QTreeWidget>("chatBotEvents");
        auto *hide = child<QCheckBox>("chatBotHideChat");
        QVERIFY(chat && events && hide);

        // Gestarteter Probelauf: graue Zeile, auch im Bild (Laufmeldungen stehen nicht im Verlauf,
        // darum ein eigener Lauf nach der neuen Verbindung in actionRuns)
        actions::Step empty = actions::newStep(QString::fromLatin1(actions::kStepChatSend));
        empty.fields.insert(QStringLiteral("text"), QString());
        QVERIFY(!testRun(QStringLiteral("DSS-Test Chatzeile"), empty).isNull());
        const QString started = QStringLiteral("Probelauf · Aktion «DSS-Test Chatzeile» ausgelöst");
        QTRY_VERIFY_WITH_TIMEOUT(findRow(chat, 1, started), 3000);
        QTreeWidgetItem *startedRow = findRow(chat, 1, started);
        QVERIFY(startedRow->data(1, Qt::UserRole).toBool());
        QVERIFY(cellShowsColor(chat, startedRow, theme.chatEventRow, 0));

        // Chat-Nachricht eines Mods mit Chatfarbe: Zeile im Chat, Abzeichen und Name im Bild
        const QString message = QStringLiteral("DSS-Test Hallo");
        const std::optional<ChangeResult> put = request(
            {QString::fromLatin1(msg::kTestEvent), QJsonObject{{QStringLiteral("event"), chatEvent(message, true, QStringLiteral("#FF4040"))}}});
        QVERIFY2(put && put->ok, put ? qPrintable(put->message) : "keine Antwort");
        const QString line = QStringLiteral("DSS-Test Zuschauer: ") + message;
        QTRY_VERIFY_WITH_TIMEOUT(findRow(chat, 1, line), 3000);
        QTreeWidgetItem *row = findRow(chat, 1, line);
        QCOMPARE(row->data(1, Qt::UserRole + 1).toStringList(), QStringList{QStringLiteral("Mod")});
        QVERIFY(!row->data(1, Qt::UserRole).toBool());
        QVERIFY(cellShowsColor(chat, row, theme.badgeMod, 1));
        QVERIFY(cellShowsColor(chat, row, QColor(0xff, 0x40, 0x40), 1));
        // Hintergrund am rechten Rand der Zeitspalte (ohne Text): grau nur bei der Ereigniszeile
        const auto background = [&](QTreeWidgetItem *item) {
            chat->scrollToItem(item);
            QApplication::processEvents();
            const QPoint at(chat->header()->sectionSize(0) - 3, chat->visualItemRect(item).center().y());
            return chat->viewport()->grab(QRect(at, QSize(1, 1))).toImage().pixelColor(0, 0);
        };
        QCOMPARE(background(startedRow), theme.chatEventRow);
        QVERIFY(background(row) != theme.chatEventRow);

        // «Ereignisse»: Chat-Nachricht ausgeblendet; Umschalten füllt die Liste neu
        // (Der Kern meldet dazu «Erste Nachricht» mit demselben Namen; die bleibt sichtbar.)
        const auto viewerRows = [&] {
            int n = 0;
            for (int i = 0; i < events->topLevelItemCount(); ++i)
                n += events->topLevelItem(i)->text(2) == QStringLiteral("DSS-Test Zuschauer");
            return n;
        };
        QVERIFY(hide->isChecked());
        const int withoutChat = events->topLevelItemCount();
        const int viewerWithoutChat = viewerRows();
        QTest::mouseClick(hide, Qt::LeftButton, {}, QPoint(4, hide->height() / 2));
        QVERIFY(!hide->isChecked());
        QCOMPARE(events->topLevelItemCount(), withoutChat + 1);
        QCOMPARE(viewerRows(), viewerWithoutChat + 1);
        QTest::mouseClick(hide, Qt::LeftButton, {}, QPoint(4, hide->height() / 2));
        QCOMPARE(events->topLevelItemCount(), withoutChat);
        QCOMPARE(viewerRows(), viewerWithoutChat);

        // Eingabezeile: Zähler, «zu lang» in stateError über [state]
        auto *account = child<QComboBox>("chatBotChatAccount");
        auto *text = child<QLineEdit>("chatBotChatText");
        auto *counter = child<QLabel>("chatBotChatCounter");
        auto *send = child<QPushButton>("chatBotChatSend");
        const int counterWidth = counter->width();
        QTest::keyClicks(text, QStringLiteral("Hallo"));
        QCOMPARE(counter->text(), QStringLiteral("5 / 500"));
        QVERIFY(counter->property("state").toString() != QStringLiteral("error"));
        text->setText(QString(501, QLatin1Char('x')));
        QCOMPARE(counter->text(), QStringLiteral("501 / 500"));
        QCOMPARE(counter->property("state").toString(), QStringLiteral("error"));
        QCOMPARE(counter->palette().color(QPalette::WindowText), theme.stateError);
        QVERIFY(!send->isEnabled());
        QApplication::processEvents();
        QCOMPARE(counter->width(), counterWidth); // Anordnung bleibt (Regel 9)
        // Testkern ohne Twitch: beide Konten ausgegraut mit Grund, «Senden» aus, Enter schickt nichts
        text->setText(QStringLiteral("Hallo"));
        auto *model = qobject_cast<QStandardItemModel *>(account->model());
        QVERIFY(model);
        QVERIFY(!model->item(0)->isEnabled() && !model->item(1)->isEnabled());
        QVERIFY(!account->itemData(0, Qt::ToolTipRole).toString().isEmpty());
        QVERIFY(!send->isEnabled());
        QVERIFY(!send->toolTip().isEmpty());
        QCOMPARE(account->currentIndex(), 0); // ausgegraut, Auswahl bleibt
        int sent = 0;
        const QMetaObject::Connection c = connect(m_window->findChild<ChatBotPage *>(), &ChatBotPage::chatSendClicked,
                                                  this, [&] { ++sent; });
        QTest::keyClick(text, Qt::Key_Return);
        disconnect(c);
        QCOMPARE(sent, 0);
        text->clear();
        QCOMPARE(counter->text(), QStringLiteral("0 / 500"));
    }

    // Senden gegen den Twitch-Testserver (nie Twitch): eigener Kern mit angemeldetem Kanal, «als Kanal»
    // wählen, Enter schickt chat_send; danach Feld leer und die eigene Nachricht im Chat.
    void chatSendTwitch()
    {
        testhelp::TwitchTestServer server;
        server.deviceInterval = 1;
        QVERIFY(server.start());
        QTemporaryDir data;
        const auto runtime = testhelp::runtimeDir();
        QVERIFY(data.isValid() && runtime->isValid());
        const QString socket = runtime->filePath(QStringLiteral("kern.sock"));
        const QString home = testhelp::testConfigHome();
        QFile ini(home + QStringLiteral("/dons-streaming-studio-entwicklung/kern.ini"));
        QVERIFY(ini.open(QIODevice::Append));
        ini.write("[twitch]\naktiv=ja\nclient-id=testclient\n");
        ini.close();
        std::unique_ptr<QProcess> core = testhelp::startCore(
            data.path(), socket, home,
            {QStringLiteral("--twitch-testserver"), server.baseUrl().toString(), QStringLiteral("--einsatz-verbindung"),
             runtime->filePath(QStringLiteral("einsatz.sock"))});
        QString log;
        QVERIFY2(testhelp::waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        {
            CoreClient::Options o;
            o.socketPath = socket;
            o.program = QStringLiteral("dss-app");
            o.version = version();
            CoreClient client(o);
            const Theme theme = darkTheme();
            MainWindow window(theme, false, &client);
            window.resize(1280, 800);
            window.show();
            QVERIFY(QTest::qWaitForWindowExposed(&window));
            client.start();
            QTRY_VERIFY_WITH_TIMEOUT(client.isConnected() && client.actionsStatus().has_value(), 5000);
            openChatBot(&window);
            QVERIFY(client.sendTwitchAccountRequest(msg::kTwitchLoginStart, QStringLiteral("channel")) != 0);
            const testhelp::TwitchTestUser channel{QStringLiteral("100000001"), QStringLiteral("testkanal")};
            QVERIFY(QTest::qWaitFor([&] { return server.approveLatest(channel); }, 5000));

            auto *account = window.findChild<QComboBox *>(QStringLiteral("chatBotChatAccount"));
            auto *text = window.findChild<QLineEdit *>(QStringLiteral("chatBotChatText"));
            auto *chat = window.findChild<QTreeWidget *>(QStringLiteral("chatBotChat"));
            QVERIFY(account && text && chat);
            auto *model = qobject_cast<QStandardItemModel *>(account->model());
            QTRY_VERIFY_WITH_TIMEOUT(model->item(1)->isEnabled(), 5000);
            QVERIFY(!model->item(0)->isEnabled()); // Bot nicht angemeldet
            account->setCurrentIndex(1);
            const QString message = QStringLiteral("DSS-Test Fenster senden");
            QTest::keyClicks(text, message);
            QVERIFY(window.findChild<QPushButton *>(QStringLiteral("chatBotChatSend"))->isEnabled());
            QTest::keyClick(text, Qt::Key_Return);
            QTRY_COMPARE_WITH_TIMEOUT(server.chatMessages.size(), 1, 5000);
            QCOMPARE(server.chatMessages[0].body.value(QStringLiteral("message")).toString(), message);
            QCOMPARE(server.chatMessages[0].body.value(QStringLiteral("sender_id")).toString(), channel.id);
            QTRY_VERIFY_WITH_TIMEOUT(text->text().isEmpty(), 5000);
            QTRY_VERIFY_WITH_TIMEOUT(findRow(chat, 1, QStringLiteral("testkanal: ") + message), 5000);
            QCOMPARE(findRow(chat, 1, QStringLiteral("testkanal: ") + message)->data(1, Qt::UserRole + 1).toStringList(),
                     QStringList{QStringLiteral("Kanal")});
            window.close(); // bittet den Kern, sich zu beenden (einziges Fenster)
        }
        if (core->state() != QProcess::NotRunning && !core->waitForFinished(3000))
            stopCore(core.get());
    }

    // P14 e2-14: «Aktionen» links. Suchfeld, Baum Gruppen → Aktionen, Leiste; Kästchen schickt
    // action_put mit enabled; Hinzufügen, Löschen, Gruppe umbenennen (Doppelklick) und löschen
    // (Kontextmenü, Rückfrage abbrechen und bestätigen); «Gespeichert: …» in der Statuszeile.
    void actionsList()
    {
        QTest::mouseClick(child<QToolButton>("chatBotTab1"), Qt::LeftButton);
        auto *page = child<ActionsPage>("actionsPage");
        QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), 3000);
        auto *search = child<QLineEdit>("actionsSearch");
        auto *tree = child<QTreeWidget>("actionsTree");
        auto *add = child<QToolButton>("actionAdd");
        auto *newGroup = child<QToolButton>("actionNewGroup");
        auto *copy = child<QToolButton>("actionCopy");
        auto *remove = child<QToolButton>("actionRemove");
        auto *reset = child<QToolButton>("actionReset");
        QVERIFY(search && tree && add && newGroup && copy && remove && reset);
        QCOMPARE(page->areaTitle(ActionsPage::List), QStringLiteral("Aktionen"));
        QCOMPARE(search->placeholderText(), QStringLiteral("Suchen …"));
        QCOMPARE(search->accessibleName(), QStringLiteral("Aktionen suchen"));
        QCOMPARE(tree->accessibleName(), QStringLiteral("Aktionen"));
        QVERIFY(search->geometry().bottom() < tree->geometry().top());
        const QList<std::pair<QToolButton *, QString>> tips{
            {add, QStringLiteral("Aktion hinzufügen")}, {newGroup, QStringLiteral("Neue Gruppe")},
            {copy, QStringLiteral("Aktion kopieren")}, {remove, QStringLiteral("Aktion löschen")},
            {reset, QStringLiteral("Mitgelieferte Aktion zurücksetzen")}};
        for (const auto &[button, tip] : tips) {
            QCOMPARE(button->toolTip(), tip);
            QCOMPARE(button->accessibleName(), tip);
            QCOMPARE(button->iconSize(), QSize(14, 14));
            QCOMPARE(button->parentWidget()->objectName(), QStringLiteral("dockButtons"));
        }
        QVERIFY(add->geometry().right() < newGroup->geometry().left());
        QVERIFY(newGroup->geometry().right() < copy->geometry().left());
        QVERIFY(copy->geometry().right() < remove->geometry().left());
        // «Zurücksetzen» rechtsbündig nach der Dehnung
        QVERIFY(reset->geometry().left() - remove->geometry().right() > 3 * reset->width());

        // Zwei Aktionen in einer Gruppe, eine ohne
        const QUuid greet = QUuid::createUuid();
        const QUuid raid = QUuid::createUuid();
        const QUuid single = QUuid::createUuid();
        QVERIFY(putTestAction(greet, QStringLiteral("DSS-Test Begrüssung"), QStringLiteral("DSS-Test Gruppe")));
        QVERIFY(putTestAction(raid, QStringLiteral("DSS-Test Raid"), QStringLiteral("DSS-Test Gruppe")));
        QVERIFY(putTestAction(single, QStringLiteral("DSS-Test Einzel"), QString()));
        QTRY_VERIFY_WITH_TIMEOUT(treeItem(tree, QStringLiteral("DSS-Test Einzel")), 3000);
        QTreeWidgetItem *group = treeItem(tree, QStringLiteral("DSS-Test Gruppe"));
        QVERIFY(group);
        QCOMPARE(group->childCount(), 2);
        QCOMPARE(group->text(1), QStringLiteral("(2)"));
        QVERIFY(!(group->flags() & Qt::ItemIsUserCheckable));
        QCOMPARE(group->child(0)->text(0), QStringLiteral("DSS-Test Begrüssung"));
        QTreeWidgetItem *singleItem = treeItem(tree, QStringLiteral("DSS-Test Einzel"));
        QVERIFY(!singleItem->parent());
        QVERIFY(singleItem->flags() & Qt::ItemIsUserCheckable);
        QCOMPARE(singleItem->checkState(0), Qt::Checked);

        // Suche ohne Gross- und Kleinschreibung füllt nur den Baum neu
        search->setText(QStringLiteral("dss-test raid"));
        QVERIFY(treeItem(tree, QStringLiteral("DSS-Test Raid")));
        QVERIFY(!treeItem(tree, QStringLiteral("DSS-Test Einzel")));
        QVERIFY(!treeItem(tree, QStringLiteral("DSS-Test Begrüssung")));
        search->clear();
        QVERIFY(treeItem(tree, QStringLiteral("DSS-Test Einzel")));

        // Auswahl: Gruppe → Kopieren und Löschen aus; Aktion → an; eigene Aktion nicht zurücksetzbar
        tree->setCurrentItem(treeItem(tree, QStringLiteral("DSS-Test Gruppe")));
        QVERIFY(add->isEnabled() && newGroup->isEnabled());
        QVERIFY(!copy->isEnabled() && !remove->isEnabled() && !reset->isEnabled());
        tree->setCurrentItem(treeItem(tree, QStringLiteral("DSS-Test Einzel")));
        QVERIFY(copy->isEnabled() && remove->isEnabled());
        QVERIFY(!reset->isEnabled());

        // Kästchen schaltet die Aktion ab (action_put mit enabled), Statuszeile «Gespeichert: …»
        treeItem(tree, QStringLiteral("DSS-Test Einzel"))->setCheckState(0, Qt::Unchecked);
        QTRY_VERIFY_WITH_TIMEOUT(coreAction(single) && !coreAction(single)->enabled, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(m_window->statusBar()->currentMessage(),
                                  QStringLiteral("Gespeichert: DSS-Test Einzel"), 3000);
        QCOMPARE(treeItem(tree, QStringLiteral("DSS-Test Einzel"))->checkState(0), Qt::Unchecked);
        QCOMPARE(tree->currentItem()->text(0), QStringLiteral("DSS-Test Einzel")); // Auswahl bleibt

        // Hinzufügen in der Gruppe der Auswahl: die neue Aktion ist gewählt
        tree->setCurrentItem(treeItem(tree, QStringLiteral("DSS-Test Raid")));
        const int before = m_client->actionsConfig()->config->actions.size();
        QTest::mouseClick(add, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(m_client->actionsConfig()->config->actions.size(), before + 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(treeItem(tree, QStringLiteral("DSS-Test Gruppe"))->childCount(), 3, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!page->selectedAction().isNull()
                                     && page->selectedAction() != greet && page->selectedAction() != raid, 3000);
        const QUuid created = page->selectedAction();
        QCOMPARE(coreAction(created)->group, QStringLiteral("DSS-Test Gruppe"));
        QTRY_COMPARE_WITH_TIMEOUT(m_window->statusBar()->currentMessage(),
                                  QStringLiteral("Gespeichert: ") + coreAction(created)->name, 3000);

        // Löschen ohne Verweise: ohne Rückfrage, danach «Gelöscht: <Name>»
        const QString createdName = coreAction(created)->name;
        QTest::mouseClick(remove, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!coreAction(created), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(treeItem(tree, QStringLiteral("DSS-Test Gruppe"))->childCount(), 2, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(m_window->statusBar()->currentMessage(),
                                  QStringLiteral("Gelöscht: ") + createdName, 3000);

        // Doppelklick öffnet nur «Umbenennen»: Dialog abbrechen, die Gruppe bleibt aufgeklappt
        group = treeItem(tree, QStringLiteral("DSS-Test Gruppe"));
        QVERIFY(group->isExpanded());
        bool renameAsked = false;
        answerDialog([&renameAsked](QWidget *dialog) {
            if (auto *input = qobject_cast<QInputDialog *>(dialog)) {
                renameAsked = true;
                input->reject();
            }
        });
        {
            const QPoint center = tree->visualItemRect(group).center();
            QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, center);
            QTest::mouseDClick(tree->viewport(), Qt::LeftButton, {}, center);
        }
        QTRY_VERIFY_WITH_TIMEOUT(renameAsked, 3000);
        group = treeItem(tree, QStringLiteral("DSS-Test Gruppe"));
        QVERIFY(group && group->isExpanded());

        // Gruppe umbenennen per Doppelklick
        answerDialog([](QWidget *dialog) {
            if (auto *input = qobject_cast<QInputDialog *>(dialog)) {
                input->setTextValue(QStringLiteral("DSS-Test Gruppe Neu"));
                input->accept();
            }
        });
        // Doppelklick wie mit der Maus: Qt meldet ihn nur nach einem Klick auf denselben Eintrag
        // (wie doubleClick in tst_fensterbearbeiten)
        const QPoint groupCenter = tree->visualItemRect(group).center();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, groupCenter);
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, {}, groupCenter);
        QTRY_VERIFY_WITH_TIMEOUT(coreAction(greet)->group == QStringLiteral("DSS-Test Gruppe Neu")
                                     && coreAction(raid)->group == QStringLiteral("DSS-Test Gruppe Neu"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(treeItem(tree, QStringLiteral("DSS-Test Gruppe Neu")), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(m_window->statusBar()->currentMessage(),
                                  QStringLiteral("Gespeichert: DSS-Test Gruppe Neu"), 3000);

        // Gruppe löschen über das Kontextmenü: Rückfrage abbrechen, dann bestätigen
        const auto removeGroupByMenu = [&](const QString &button, QString *question) {
            QTreeWidgetItem *item = treeItem(tree, QStringLiteral("DSS-Test Gruppe Neu"));
            QVERIFY(item);
            // Das Menü ist kein modaler Dialog: Warten auf das offene Menü, dann «Löschen …»
            auto *timer = new QTimer(this);
            timer->setInterval(20);
            connect(timer, &QTimer::timeout, timer, [timer, button, question] {
                auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu)
                    return;
                timer->stop();
                timer->deleteLater();
                QAction *found = nullptr;
                for (QAction *a : menu->actions())
                    if (a->text() == QStringLiteral("Löschen …"))
                        found = a;
                QVERIFY(found);
                answerQuestion(button, question);
                menu->hide();
                found->trigger();
            });
            timer->start();
            emit tree->customContextMenuRequested(tree->visualItemRect(item).center());
        };
        QString question;
        removeGroupByMenu(QStringLiteral("Abbrechen"), &question);
        QTRY_VERIFY_WITH_TIMEOUT(!question.isEmpty(), 3000);
        QVERIFY2(question.startsWith(QStringLiteral("Gruppe «DSS-Test Gruppe Neu» mit 2 Aktionen löschen?")),
                 qPrintable(question));
        QTest::qWait(300);
        QVERIFY(coreAction(greet) && coreAction(raid));
        question.clear();
        removeGroupByMenu(QStringLiteral("Löschen"), &question);
        QTRY_VERIFY_WITH_TIMEOUT(!coreAction(greet) && !coreAction(raid), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!treeItem(tree, QStringLiteral("DSS-Test Gruppe Neu")), 3000);
        QVERIFY(coreAction(single));
        QTRY_COMPARE_WITH_TIMEOUT(m_window->statusBar()->currentMessage(),
                                  QStringLiteral("Gelöscht: DSS-Test Gruppe Neu"), 3000);

        // Ohne Kern kein Menü und kein Umbenennen, auch wenn noch eine Gruppenzeile da ist
        // (Zeilen von Hand gesetzt, nach der Trennung leert MainWindow den Baum sonst selbst)
        m_client->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!m_client->isConnected(), 3000);
        ActionListRow staleGroup;
        staleGroup.text = QStringLiteral("DSS-Test Gruppe Alt");
        staleGroup.group = staleGroup.text;
        staleGroup.isGroup = true;
        page->setActionRows({staleGroup});
        QTreeWidgetItem *stale = treeItem(tree, staleGroup.text);
        QVERIFY(stale);
        bool popupShown = false;
        bool dialogShown = false;
        auto *watch = new QTimer(this);
        watch->setInterval(20);
        connect(watch, &QTimer::timeout, watch, [&popupShown, &dialogShown] {
            if (QWidget *popup = QApplication::activePopupWidget()) {
                popupShown = true;
                popup->hide();
            }
            if (QWidget *dialog = QApplication::activeModalWidget()) {
                dialogShown = true;
                dialog->close();
            }
        });
        watch->start();
        const QPoint staleCenter = tree->visualItemRect(stale).center();
        emit tree->customContextMenuRequested(staleCenter);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, staleCenter);
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, {}, staleCenter);
        QTest::qWait(300);
        watch->stop();
        delete watch;
        QVERIFY(!popupShown);
        QVERIFY(!dialogShown);
        m_client->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isConnected(), 5000);
    }

    // Schritte der Aktion im Kern, alle Ebenen; -1 = Aktion fehlt
    int stepCount(const QUuid &id) const
    {
        const std::optional<actions::Action> a = coreAction(id);
        if (!a)
            return -1;
        std::function<int(const QList<actions::Step> &)> count = [&count](const QList<actions::Step> &steps) {
            int n = 0;
            for (const actions::Step &s : steps)
                n += 1 + count(s.steps);
            return n;
        };
        return count(a->steps);
    }

    // Eintrag text im Menü eines Knopfs «hinzufügen»
    static QAction *menuAction(QToolButton *button, const QString &text)
    {
        if (!button->menu())
            return nullptr;
        for (QAction *a : button->menu()->actions())
            if (!a->isSeparator() && a->text() == text)
                return a;
        return nullptr;
    }

    // P14 e2-15, Mitte: Titel und Hinweis, Abschnitte, Name mit Enter, Prüfmeldung am Feld,
    // Auslöser und Schritte, durchgestrichen, «▶ Testen» im Testkern
    void actionDetail()
    {
        QTest::mouseClick(child<QToolButton>("chatBotTab1"), Qt::LeftButton);
        auto *page = child<ActionsPage>("actionsPage");
        QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(m_client->actionsConfig() && m_client->actionsConfig()->config, 3000);
        auto *tree = child<QTreeWidget>("actionsTree");
        auto *name = child<QLineEdit>("actionName");
        auto *group = child<QComboBox>("actionGroup");
        auto *queue = child<QComboBox>("actionQueue");
        auto *enabled = child<QCheckBox>("actionEnabled");
        auto *triggers = child<QTreeWidget>("actionTriggers");
        auto *steps = child<QTreeWidget>("actionSteps");
        auto *triggerAdd = child<QToolButton>("actionTriggerAdd");
        auto *triggerRemove = child<QToolButton>("actionTriggerRemove");
        auto *stepAdd = child<QToolButton>("actionStepAdd");
        auto *stepRemove = child<QToolButton>("actionStepRemove");
        auto *stepUp = child<QToolButton>("actionStepUp");
        auto *stepDown = child<QToolButton>("actionStepDown");
        auto *testUser = child<QComboBox>("actionTestUser");
        auto *test = child<QPushButton>("actionTest");
        auto *realTest = child<QCheckBox>("actionRealTest");
        auto *actionSection = child<QWidget>("actionSection");
        auto *hint = actionSection ? actionSection->findChild<QLabel *>(QStringLiteral("hintLabel")) : nullptr;
        QVERIFY(tree && name && group && queue && enabled && triggers && steps && triggerAdd && triggerRemove
                && stepAdd && stepRemove && stepUp && stepDown && testUser && test && realTest && hint);
        const QList<QLabel *> problems =
            page->area(ActionsPage::Action)->findChildren<QLabel *>(QStringLiteral("fieldProblem"));
        QCOMPARE(problems.size(), 3);
        for (QLabel *problem : problems)
            QVERIFY(problem->height() > 0); // Höhe reserviert, auch ohne Meldung

        // Abschnitte über section(), Tooltips und Leiste wie unter den Docks
        const QList<std::pair<QToolButton *, QString>> tips{
            {triggerAdd, QStringLiteral("Auslöser hinzufügen")}, {triggerRemove, QStringLiteral("Auslöser löschen")},
            {stepAdd, QStringLiteral("Schritt hinzufügen")}, {stepRemove, QStringLiteral("Schritt löschen")},
            {stepUp, QStringLiteral("Schritt nach oben")}, {stepDown, QStringLiteral("Schritt nach unten")}};
        for (const auto &[button, tip] : tips) {
            QCOMPARE(button->toolTip(), tip);
            QCOMPARE(button->accessibleName(), tip);
            QCOMPARE(button->iconSize(), QSize(14, 14));
        }
        QCOMPARE(stepAdd->popupMode(), QToolButton::InstantPopup);
        QCOMPARE(triggerAdd->popupMode(), QToolButton::InstantPopup);
        QCOMPARE(stepAdd->parentWidget()->objectName(), QStringLiteral("dockButtons"));
        QCOMPARE(test->parentWidget()->objectName(), QStringLiteral("dockButtons"));
        QCOMPARE(test->text(), QStringLiteral("▶ Testen"));
        QCOMPARE(realTest->text(), QStringLiteral("echt ausführen"));
        // «Testen» rechtsbündig nach der Dehnung
        QVERIFY(test->geometry().left() > stepDown->geometry().right() + 3 * stepDown->width());

        // Ohne Auswahl: Titel «Aktion», Hinweis, Felder ausgegraut
        tree->setCurrentItem(nullptr);
        QCOMPARE(page->areaTitle(ActionsPage::Action), QStringLiteral("Aktion"));
        QCOMPARE(hint->text(), QStringLiteral("Keine Aktion gewählt"));
        QVERIFY(!name->isEnabled() && !group->isEnabled() && !queue->isEnabled() && !enabled->isEnabled());
        QVERIFY(!test->isEnabled() && !stepAdd->isEnabled() && !triggerAdd->isEnabled());

        const QUuid id = QUuid::createUuid();
        QVERIFY(putTestAction(id, QStringLiteral("DSS-Test Mitte"), QString()));
        QTRY_VERIFY_WITH_TIMEOUT(page->selectAction(id), 3000);
        QCOMPARE(page->areaTitle(ActionsPage::Action), QStringLiteral("Aktion «DSS-Test Mitte»"));
        QCOMPARE(name->text(), QStringLiteral("DSS-Test Mitte"));
        QVERIFY(hint->text().isEmpty()); // eigene Aktion
        QVERIFY(name->isEnabled() && enabled->isChecked());
        QCOMPARE(queue->currentText(), QStringLiteral("Standard"));
        // «Warteschlangen verwalten …» ausgegraut (Wirkung mit e3)
        auto *queueModel = qobject_cast<QStandardItemModel *>(queue->model());
        QVERIFY(queueModel);
        const int manage = queue->findText(QStringLiteral("Warteschlangen verwalten …"));
        QVERIFY(manage >= 0);
        QVERIFY(!queueModel->item(manage)->isEnabled());

        // Name mit Enter speichern: action_put, Titel und Statuszeile mit dem neuen Namen
        name->selectAll();
        QTest::keyClicks(name, QStringLiteral("DSS-Test Mitte Neu"));
        QTest::keyClick(name, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(coreAction(id)->name, QStringLiteral("DSS-Test Mitte Neu"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(page->areaTitle(ActionsPage::Action), QStringLiteral("Aktion «DSS-Test Mitte Neu»"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(m_window->statusBar()->currentMessage(),
                                  QStringLiteral("Gespeichert: DSS-Test Mitte Neu"), 3000);

        // Leerer Name: Prüfmeldung unter dem Abschnitt «Aktion», das Feld zeigt wieder den Wert des Kerns
        QLabel *actionProblem = actionSection->findChild<QLabel *>(QStringLiteral("fieldProblem"));
        QVERIFY(actionProblem && actionProblem->text().isEmpty());
        const int problemHeight = actionProblem->height();
        name->clear();
        QTest::keyClick(name, Qt::Key_Return);
        QVERIFY(!actionProblem->text().isEmpty());
        QCOMPARE(page->sectionProblem(ActionsPage::ActionSection), actionProblem->text());
        QCOMPARE(name->text(), QStringLiteral("DSS-Test Mitte Neu"));
        QCOMPARE(coreAction(id)->name, QStringLiteral("DSS-Test Mitte Neu"));
        QCOMPARE(actionProblem->height(), problemHeight); // Anordnung bleibt

        // Auslöser hinzufügen (Menü aus triggerChoices) und entfernen
        QAction *addTest = menuAction(triggerAdd, QStringLiteral("Test"));
        QVERIFY(triggerAdd->isEnabled() && addTest);
        addTest->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(coreAction(id)->triggers.size(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(triggers->topLevelItemCount(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(page->selectedTrigger(), coreAction(id)->triggers.first().id, 3000);
        QVERIFY(triggers->topLevelItem(0)->flags() & Qt::ItemIsUserCheckable);
        QVERIFY(triggerRemove->isEnabled());
        QTest::mouseClick(triggerRemove, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(coreAction(id)->triggers.isEmpty(), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(triggers->topLevelItemCount(), 0, 3000);
        QVERIFY(!triggerRemove->isEnabled());

        // Schritte: Gruppe, darin zwei Kommentare (der neue Schritt ist jeweils gewählt)
        const auto addStep = [&](const QString &label, int count) {
            QAction *a = menuAction(stepAdd, label);
            QVERIFY2(a, qPrintable(label));
            a->trigger();
            QTRY_COMPARE_WITH_TIMEOUT(stepCount(id), count, 3000);
            QTRY_VERIFY_WITH_TIMEOUT(!page->selectedStep().isNull(), 3000);
        };
        const QString groupLabel = actions::stepType(QString::fromLatin1(actions::kStepGroup))->label;
        const QString commentLabel = actions::stepType(QString::fromLatin1(actions::kStepComment))->label;
        addStep(groupLabel, 1);
        addStep(commentLabel, 2);
        const QUuid first = page->selectedStep();
        addStep(commentLabel, 3);
        const QUuid second = page->selectedStep();
        QTRY_COMPARE_WITH_TIMEOUT(steps->topLevelItemCount(), 1, 3000);
        QTreeWidgetItem *groupItem = steps->topLevelItem(0);
        QCOMPARE(groupItem->childCount(), 2);
        QCOMPARE(groupItem->text(0), groupLabel);
        QVERIFY(groupItem->font(0).bold()); // Art fett
        QVERIFY(!groupItem->font(1).bold());
        QCOMPARE(coreAction(id)->steps.first().steps.at(1).id, second);

        // Schritt nach oben: Reihenfolge im Kern, Auswahl bleibt
        QVERIFY(stepUp->isEnabled() && !stepDown->isEnabled() && stepRemove->isEnabled());
        QTest::mouseClick(stepUp, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(coreAction(id)->steps.first().steps.first().id, second, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(page->selectedStep(), second, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!stepUp->isEnabled() && stepDown->isEnabled(), 3000);
        QCOMPARE(steps->topLevelItem(0)->child(1)->data(0, Qt::UserRole).toUuid(), first);

        // Kästchen der Gruppe aus: Gruppe und Schritte darin rot durchgestrichen (stateError)
        steps->topLevelItem(0)->setCheckState(0, Qt::Unchecked);
        QTRY_VERIFY_WITH_TIMEOUT(!coreAction(id)->steps.first().enabled, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(steps->topLevelItem(0)->font(0).strikeOut(), 3000);
        QTreeWidgetItem *child0 = steps->topLevelItem(0)->child(0);
        QVERIFY(child0->font(0).strikeOut() && child0->font(1).strikeOut());
        QCOMPARE(child0->foreground(1).color(), darkTheme().stateError);
        QCOMPARE(child0->checkState(0), Qt::Checked); // selbst an, nur darüber abgeschaltet

        // Schritt entfernen: Rückfrage aus removeStepQuestion, Abbrechen lässt alles stehen
        steps->setCurrentItem(steps->topLevelItem(0));
        QString question;
        answerQuestion(QStringLiteral("Abbrechen"), &question);
        QTest::mouseClick(stepRemove, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!question.isEmpty(), 3000);
        QCOMPARE(question, removeStepQuestion(*coreAction(id), coreAction(id)->steps.first().id));
        QCOMPARE(stepCount(id), 3);
        question.clear();
        answerQuestion(QStringLiteral("Löschen"), &question);
        QTest::mouseClick(stepRemove, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(stepCount(id), 0, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(steps->topLevelItemCount(), 0, 3000);

        // «▶ Testen» mit «Testzuschauer»: Probelauf im Testkern
        QVERIFY(testUser->findText(QStringLiteral("Testzuschauer")) >= 0);
        testUser->setCurrentIndex(testUser->findText(QStringLiteral("Testzuschauer")));
        menuAction(stepAdd, commentLabel)->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(stepCount(id), 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(test->isEnabled(), 3000);
        QVERIFY(!realTest->isChecked());
        QTest::mouseClick(test, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(m_window->statusBar()->currentMessage(),
                                  QStringLiteral("Probelauf gestartet: DSS-Test Mitte Neu"), 3000);

        // Aktion löschen: «Gelöscht: <Name>»
        QTest::mouseClick(child<QToolButton>("actionRemove"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!coreAction(id), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(m_window->statusBar()->currentMessage(),
                                  QStringLiteral("Gelöscht: DSS-Test Mitte Neu"), 3000);
    }

    // P14 e2-16, rechts: eine Seite je Art, Wechsel ohne Neuaufbau, Zeiten in Sekunden mit Komma,
    // «weich» ausgegraut mit Grund, Prüfmeldung unter dem Abschnitt, Hinweis bei Cheer.
    void actionFields()
    {
        QTest::mouseClick(child<QToolButton>("chatBotTab1"), Qt::LeftButton);
        auto *page = child<ActionsPage>("actionsPage");
        QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), 3000);
        ActionFieldsPanel *panel = page->fieldsPanel();
        QVERIFY(panel && panel->parentWidget() == page->area(ActionsPage::Detail));
        auto *scroll = child<QScrollArea>("actionFieldsScroll");
        QVERIFY(scroll);
        QCOMPARE(scroll->frameShape(), QFrame::NoFrame);
        QCOMPARE(scroll->horizontalScrollBarPolicy(), Qt::ScrollBarAlwaysOff);

        // Jede Art hat ihre Seite
        for (const actions::StepTypeInfo &t : actions::stepTypes())
            QVERIFY2(panel->page(ActionFieldsPanel::Target::Step, t.key), qPrintable(t.key));
        for (const actions::TriggerTypeInfo &t : actions::triggerTypes())
            QVERIFY2(panel->page(ActionFieldsPanel::Target::Trigger, t.key), qPrintable(t.key));
        const int widgetCount = int(panel->findChildren<QWidget *>().size());

        // Aktion mit Pause, Overlay-Schritt (ein Element mit einer Quelle, die es nicht gibt) und Cheer
        actions::Step delay = actions::newStep(QString::fromLatin1(actions::kStepDelay));
        actions::Step overlay = actions::newStep(QString::fromLatin1(actions::kStepOverlaySource));
        const QString missingSource = QUuid::createUuid().toString(QUuid::WithoutBraces);
        overlay.fields.insert(QStringLiteral("elements"),
                              QJsonArray{QJsonObject{{QStringLiteral("source"), missingSource},
                                                     {QStringLiteral("text"), QJsonValue::Null},
                                                     {QStringLiteral("image"), QJsonValue::Null}}});
        const actions::Trigger cheer = actions::newTrigger(QString::fromLatin1(actions::kTriggerCheer));
        actions::Action a;
        a.id = QUuid::createUuid();
        a.name = QStringLiteral("DSS-Test Felder");
        a.steps = {delay, overlay};
        a.triggers = {cheer};
        const std::optional<ChangeResult> put = request(
            {QString::fromLatin1(msg::kActionPut), QJsonObject{{QStringLiteral("action"), actions::actionToJson(a)}}});
        QVERIFY(put && put->ok);
        QTRY_VERIFY_WITH_TIMEOUT(page->selectAction(a.id), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(page->detailAction(), a.id, 3000);

        // Ohne Auswahl: Hinweis
        child<QTreeWidget>("actionSteps")->setCurrentItem(nullptr);
        child<QTreeWidget>("actionTriggers")->setCurrentItem(nullptr);
        QCOMPARE(panel->currentPage()->objectName(), QStringLiteral("fieldsPage:none"));
        QCOMPARE(panel->currentPage()->findChild<QLabel *>(QStringLiteral("hintLabel"))->text(),
                 QStringLiteral("Kein Schritt oder Auslöser gewählt"));
        QCOMPARE(page->areaTitle(ActionsPage::Detail), QStringLiteral("Schritt"));
        // Art ohne Einstellungen: grauer Hinweis aus der Logik
        QWidget *followPage = panel->page(ActionFieldsPanel::Target::Trigger,
                                          QString::fromLatin1(actions::kTriggerFollow));
        QVERIFY(followPage);
        QCOMPARE(followPage->findChild<QLabel *>(QStringLiteral("hintLabel"))->text(), emptyFormHint());

        // Pause: Dauer in Sekunden, 8,5 s gespeichert als 8500 ms
        QWidget *delayPage = panel->page(ActionFieldsPanel::Target::Step, delay.type);
        auto *duration = delayPage->findChild<QDoubleSpinBox *>(QStringLiteral("field:value_ms"));
        QVERIFY(duration && !duration->keyboardTracking());
        page->selectStep(delay.id);
        QCOMPARE(panel->currentPage(), delayPage);
        QCOMPARE(page->areaTitle(ActionsPage::Detail), stepTitle(delay));
        QCOMPARE(duration->text(), QStringLiteral("1 s"));
        duration->setFocus();
        duration->selectAll();
        QTest::keyClicks(duration, QStringLiteral("8,5"));
        QTest::keyClick(duration, Qt::Key_Return);
        const auto stepField = [&](const QUuid &step, const QString &field) {
            QJsonValue value;
            if (const std::optional<actions::Action> now = coreAction(a.id))
                actions::forEachStep(now->steps, [&](const actions::Step &s, const QList<int> &) {
                    if (s.id == step)
                        value = s.fields.value(field);
                });
            return value;
        };
        QTRY_COMPARE_WITH_TIMEOUT(stepField(delay.id, QStringLiteral("value_ms")).toInteger(), qint64(8500), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(m_window->statusBar()->currentMessage(),
                                  QStringLiteral("Gespeichert: DSS-Test Felder"), 3000);
        QCOMPARE(duration->text(), QStringLiteral("8,5 s"));

        // Overlay-Schritt: Seite umschalten, «weich» ausgegraut mit Grund
        QWidget *overlayPage = panel->page(ActionFieldsPanel::Target::Step, overlay.type);
        page->selectStep(overlay.id);
        QCOMPARE(panel->currentPage(), overlayPage);
        QCOMPARE(overlayPage->findChild<QDoubleSpinBox *>(QStringLiteral("field:duration_ms"))->text(),
                 QStringLiteral("5 s"));
        for (const char *name : {"field:transition_in", "field:transition_out", "field:transition_ms"}) {
            auto *field = overlayPage->findChild<QWidget *>(QString::fromLatin1(name));
            QVERIFY2(field && !field->isEnabled(), name);
            QVERIFY2(field->toolTip().startsWith(QStringLiteral("Wirkt noch nicht")), name);
        }
        auto *elements = overlayPage->findChild<QTreeWidget *>(QStringLiteral("field:elements"));
        QVERIFY(elements);
        QTRY_COMPARE_WITH_TIMEOUT(elements->topLevelItemCount(), 1, 3000);
        QCOMPARE(elements->topLevelItem(0)->text(0), missingSource + QStringLiteral(" (fehlt)"));
        QCOMPARE(overlayPage->findChild<QToolButton *>(QStringLiteral("field:elements:add"))->toolTip(),
                 QStringLiteral("Element hinzufügen"));
        QCOMPARE(overlayPage->findChild<QToolButton *>(QStringLiteral("field:elements:remove"))->toolTip(),
                 QStringLiteral("Element löschen"));

        // Inhalt des Elements: «Keine» ist keine Quelle → Prüfmeldung unter «Inhalt der Quellen»,
        // das Feld zeigt wieder den Wert des Kerns, die Anordnung bleibt
        elements->setCurrentItem(elements->topLevelItem(0));
        auto *source = overlayPage->findChild<QComboBox *>(QStringLiteral("element:source"));
        QVERIFY(source && source->isEnabled());
        QCOMPARE(source->currentData().toString(), missingSource);
        QLabel *problem = nullptr;
        for (QLabel *line : overlayPage->findChildren<QLabel *>(QStringLiteral("fieldProblem")))
            if (line->parentWidget()->findChild<QComboBox *>(QStringLiteral("element:source")))
                problem = line;
        QVERIFY(problem && problem->text().isEmpty() && problem->height() > 0);
        const int problemHeight = problem->height();
        source->setFocus();
        QTest::keyClick(source, Qt::Key_Home);
        QTRY_VERIFY_WITH_TIMEOUT(!problem->text().isEmpty(), 3000);
        QCOMPARE(page->sectionProblem(ActionsPage::DetailSection), problem->text());
        QCOMPARE(source->currentData().toString(), missingSource);
        QCOMPARE(problem->height(), problemHeight);

        // Cheer: Seite des Auslösers mit dem Hinweis unter «Bits»
        QWidget *cheerPage = panel->page(ActionFieldsPanel::Target::Trigger, cheer.type);
        page->selectTrigger(cheer.id);
        QCOMPARE(panel->currentPage(), cheerPage);
        QCOMPARE(page->areaTitle(ActionsPage::Detail), triggerTitle(cheer));
        QVERIFY(page->sectionProblem(ActionsPage::DetailSection).isEmpty()); // andere Auswahl
        bool hint = false;
        for (QLabel *label : cheerPage->findChildren<QLabel *>(QStringLiteral("hintLabel")))
            hint = hint || (label->text() == QStringLiteral("Nur «bis»: genau dieser Wert") && label->isVisible());
        QVERIFY(hint);

        // Zurück zur Pause: dieselben Widgets, nichts neu aufgebaut
        page->selectStep(delay.id);
        QCOMPARE(panel->currentPage(), delayPage);
        QCOMPARE(delayPage->findChild<QDoubleSpinBox *>(QStringLiteral("field:value_ms")), duration);
        QCOMPARE(int(panel->findChildren<QWidget *>().size()), widgetCount);

        QTest::mouseClick(child<QToolButton>("actionRemove"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!coreAction(a.id), 3000);
    }

    // P14 e3-5: «Befehle» gegen den Testkern. Zeilen aus commandRows, Filter und Suche, Kästchen
    // «Aktiv», Formular ohne Neuaufbau, Speichern mit «Gespeichert: <Name>», Prüfmeldung unter dem
    // Abschnitt und danach der Wert des Kerns, «Öffnen», «▶ Testen als …» im Probelauf, Löschen mit
    // Rückfrage («Gelöscht: <Name>»).
    void commandsTab()
    {
        auto *chatBot = child<ChatBotPage>("chatBotPage");
        ListDetailPage *page = chatBot->commandsPage();
        CommandForm *form = chatBot->commandForm();
        QVERIFY(page && form);
        const auto coreCommand = [this](const QUuid &id) -> std::optional<actions::Command> {
            const std::optional<ActionsConfigInfo> info = m_client->actionsConfig();
            const actions::Command *c = info && info->config ? info->config->command(id) : nullptr;
            return c ? std::optional<actions::Command>(*c) : std::nullopt;
        };
        const auto status = [this] { return m_window->statusBar()->currentMessage(); };

        actions::Command info;
        info.id = QUuid::createUuid();
        info.name = QStringLiteral("DSS-Test Info");
        info.group = QStringLiteral("Info");
        info.commands = {QStringLiteral("!info"), QStringLiteral("!i")};
        info.globalCooldownS = 30;
        actions::Command game;
        game.id = QUuid::createUuid();
        game.name = QStringLiteral("DSS-Test Spiel");
        game.group = QStringLiteral("Spiele");
        game.commands = {QStringLiteral("!spiel")};
        game.enabled = false;
        for (const actions::Command &c : {info, game}) {
            const std::optional<ChangeResult> put = request(
                {QStringLiteral("command_put"), QJsonObject{{QStringLiteral("command"), actions::commandToJson(c)}}});
            QVERIFY2(put && put->ok, put ? qPrintable(put->message) : "keine Antwort");
        }
        actions::Action action;
        action.id = QUuid::createUuid();
        action.name = QStringLiteral("DSS-Test Info-Aktion");
        actions::Trigger trigger = actions::newTrigger(QString::fromLatin1(actions::kTriggerCommand));
        trigger.fields.insert(QStringLiteral("command_id"), info.id.toString(QUuid::WithoutBraces));
        action.triggers = {trigger};
        actions::Step wait = actions::newStep(QString::fromLatin1(actions::kStepDelay));
        wait.fields.insert(QStringLiteral("value_ms"), 10);
        action.steps = {wait};
        const std::optional<ChangeResult> putAction = request(
            {QString::fromLatin1(msg::kActionPut), QJsonObject{{QStringLiteral("action"), actions::actionToJson(action)}}});
        QVERIFY2(putAction && putAction->ok, putAction ? qPrintable(putAction->message) : "keine Antwort");

        QTest::mouseClick(child<QToolButton>("chatBotTab2"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), 3000);
        // Beim Zeigen holt das Fenster die Variablen (Spalte «Benutzt»)
        QTRY_VERIFY_WITH_TIMEOUT(m_client->variables().has_value(), 3000);
        // «▶ Testen als …» unter dem Bildlauf, ohne Auswahl verborgen (Befund e3-5b)
        auto *test = page->findChild<QPushButton *>(QStringLiteral("commandTest"));
        auto *scroll = page->findChild<QScrollArea *>(QStringLiteral("commandsScroll"));
        QVERIFY(test && scroll);
        QVERIFY(!scroll->isAncestorOf(test) && !form->isAncestorOf(test));
        QVERIFY(page->selectedRow().isNull());
        QVERIFY(!test->isVisible());

        // Zeilen: Spalten aus CommandRow, abgeschaltet grau, Hinweis aus commandListHint
        auto *table = page->findChild<QTreeWidget *>(QStringLiteral("commandsTable"));
        QTRY_VERIFY_WITH_TIMEOUT(findRow(table, 1, QStringLiteral("!info, !i")) && findRow(table, 1, QStringLiteral("!spiel")),
                                 3000);
        QTreeWidgetItem *infoItem = findRow(table, 1, QStringLiteral("!info, !i"));
        QCOMPARE(infoItem->text(2), QStringLiteral("Info"));
        QCOMPARE(infoItem->text(4), QStringLiteral("30 s"));
        QCOMPARE(infoItem->text(6), QStringLiteral("DSS-Test Info-Aktion"));
        QTreeWidgetItem *gameItem = findRow(table, 1, QStringLiteral("!spiel"));
        QCOMPARE(gameItem->checkState(0), Qt::Unchecked);
        QCOMPARE(gameItem->foreground(1).color(), darkTheme().textDisabled);
        QCOMPARE(page->hint(), commandListHint(*m_client->actionsConfig()->config));
        QVERIFY(page->findChild<QToolButton *>(QStringLiteral("commandsAdd"))->isEnabled());

        // Filter «Gruppe» und Suche füllen nur die Tabelle neu
        auto *filter = page->findChild<QComboBox *>(QStringLiteral("commandsFilter"));
        QCOMPARE(filter->itemText(0), QStringLiteral("Alle Gruppen"));
        QVERIFY(filter->findText(QStringLiteral("Spiele")) > 0);
        filter->setCurrentIndex(filter->findText(QStringLiteral("Spiele")));
        QTRY_COMPARE_WITH_TIMEOUT(table->topLevelItemCount(), 1, 3000);
        QCOMPARE(table->topLevelItem(0)->text(1), QStringLiteral("!spiel"));
        filter->setCurrentIndex(0);
        auto *search = page->findChild<QLineEdit *>(QStringLiteral("commandsSearch"));
        search->setText(QStringLiteral("dss-test info"));
        QTRY_COMPARE_WITH_TIMEOUT(table->topLevelItemCount(), 1, 3000);
        QCOMPARE(table->topLevelItem(0)->text(1), QStringLiteral("!info, !i"));
        search->clear();
        QTRY_VERIFY_WITH_TIMEOUT(findRow(table, 1, QStringLiteral("!spiel")), 3000);

        // Kästchen «Aktiv» → command_put mit enabled
        findRow(table, 1, QStringLiteral("!spiel"))->setCheckState(0, Qt::Checked);
        QTRY_VERIFY_WITH_TIMEOUT(coreCommand(game.id) && coreCommand(game.id)->enabled, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gespeichert: DSS-Test Spiel"), 3000);

        // Auswahl: Titel und Werte; ein Wechsel schafft keine neuen Felder
        QVERIFY(page->selectRow(info.id));
        QVERIFY(page->detailShown());
        QVERIFY(test->isVisible());
        QVERIFY(test->mapTo(page, QPoint(0, 0)).y() >= scroll->mapTo(page, QPoint(0, scroll->height())).y());
        QCOMPARE(page->detailTitle(), QStringLiteral("Befehl «!info»"));
        auto *name = form->findChild<QLineEdit *>(QStringLiteral("commandName"));
        auto *triggers = form->findChild<QPlainTextEdit *>(QStringLiteral("commandTriggers"));
        auto *globalCooldown = form->findChild<QSpinBox *>(QStringLiteral("commandGlobalCooldown"));
        auto *actionChoice = form->findChild<QComboBox *>(QStringLiteral("commandAction"));
        QVERIFY(name && triggers && globalCooldown && actionChoice);
        QCOMPARE(name->text(), QStringLiteral("DSS-Test Info"));
        QCOMPARE(triggers->toPlainText(), QStringLiteral("!info\n!i"));
        QCOMPARE(globalCooldown->value(), 30);
        QCOMPARE(globalCooldown->suffix(), QStringLiteral(" s"));
        QVERIFY(!globalCooldown->keyboardTracking());
        QCOMPARE(globalCooldown->maximum(), kMaxCooldownS); // Grenze aus der Logik
        QCOMPARE(actionChoice->currentText(), QStringLiteral("DSS-Test Info-Aktion"));
        QCOMPARE(actionChoice->itemText(0), QStringLiteral("Keine"));
        const int widgetCount = int(form->findChildren<QWidget *>().size());
        QVERIFY(page->selectRow(game.id));
        QCOMPARE(name->text(), QStringLiteral("DSS-Test Spiel"));
        QCOMPARE(actionChoice->currentIndex(), 0);
        QVERIFY(!form->findChild<QPushButton *>(QStringLiteral("commandOpenAction"))->isEnabled());
        QCOMPARE(int(form->findChildren<QWidget *>().size()), widgetCount);
        QVERIFY(page->selectRow(info.id));

        // Name mit Enter speichern
        name->selectAll();
        QTest::keyClicks(name, QStringLiteral("DSS-Test Info 2"));
        QTest::keyClick(name, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(coreCommand(info.id) && coreCommand(info.id)->name == QStringLiteral("DSS-Test Info 2"),
                                 3000);
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gespeichert: DSS-Test Info 2"), 3000);

        // Prüfmeldung unter dem Abschnitt «Befehl», danach zeigt das Feld wieder den Wert des Kerns
        auto *commandSection = form->findChild<QWidget *>(QStringLiteral("commandSection"));
        auto *problem = commandSection->findChild<QLabel *>(QStringLiteral("fieldProblem"));
        QVERIFY(problem && problem->isVisible() && problem->text().isEmpty()); // Zeile immer da
        name->clear();
        QTest::keyClick(name, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(!problem->text().isEmpty(), 3000);
        QCOMPARE(name->text(), QStringLiteral("DSS-Test Info 2"));
        QVERIFY(problem->geometry().top() > name->geometry().bottom());

        // Abklingzeit: ganze Sekunden
        globalCooldown->setValue(45);
        QTRY_VERIFY_WITH_TIMEOUT(coreCommand(info.id) && coreCommand(info.id)->globalCooldownS == 45, 3000);
        QVERIFY(form->findChild<QWidget *>(QStringLiteral("commandCooldownSection"))
                    ->findChild<QLabel *>(QStringLiteral("fieldProblem"))->text().isEmpty());

        // «Wer darf»: eine Rolle hebt «Alle» auf, «Alle» leert die Rollen wieder
        auto *all = form->findChild<QCheckBox *>(QStringLiteral("commandPermissionAll"));
        auto *subscriber = form->findChild<QCheckBox *>(QStringLiteral("commandPermissionSubscriber"));
        QVERIFY(all->isChecked() && !subscriber->isChecked());
        QTest::mouseClick(subscriber, Qt::LeftButton, {}, QPoint(4, subscriber->height() / 2));
        QTRY_VERIFY_WITH_TIMEOUT(coreCommand(info.id)
                                     && coreCommand(info.id)->permissions.roles == QStringList{QStringLiteral("subscriber")},
                                 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!all->isChecked() && subscriber->isChecked(), 3000);
        QTest::mouseClick(all, Qt::LeftButton, {}, QPoint(4, all->height() / 2));
        QTRY_VERIFY_WITH_TIMEOUT(coreCommand(info.id) && coreCommand(info.id)->permissions.roles.isEmpty(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(all->isChecked() && !subscriber->isChecked(), 3000);

        // «Ausführen»: «Keine» gibt den Auslöser ab, die Aktion bekommt ihn wieder
        actionChoice->setCurrentIndex(0);
        emit actionChoice->activated(0);
        QTRY_VERIFY_WITH_TIMEOUT(commandAction(*m_client->actionsConfig()->config, info.id).isNull(), 3000);
        const int actionIndex = actionChoice->findText(QStringLiteral("DSS-Test Info-Aktion"));
        QVERIFY(actionIndex > 0);
        actionChoice->setCurrentIndex(actionIndex);
        emit actionChoice->activated(actionIndex);
        QTRY_COMPARE_WITH_TIMEOUT(commandAction(*m_client->actionsConfig()->config, info.id), action.id, 3000);

        // «▶ Testen als …» → «Testzuschauer»: action_test im Probelauf
        QCOMPARE(test->text(), QStringLiteral("▶ Testen als …"));
        QTRY_VERIFY_WITH_TIMEOUT(test->isEnabled(), 3000);
        QVERIFY(test->menu());
        QCOMPARE(test->menu()->actions().value(0) ? test->menu()->actions().at(0)->text() : QString(),
                 QStringLiteral("Testzuschauer"));
        test->menu()->actions().at(0)->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Probelauf gestartet: DSS-Test Info 2"), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(runItem(QStringLiteral("DSS-Test Info-Aktion")), 3000);
        // Ohne Aktion: Prüfmeldung unter «Ausführen»
        QVERIFY(page->selectRow(game.id));
        QTRY_VERIFY_WITH_TIMEOUT(test->isEnabled(), 3000);
        test->menu()->actions().at(0)->trigger();
        auto *runProblem = form->findChild<QWidget *>(QStringLiteral("commandRunSection"))
                               ->findChild<QLabel *>(QStringLiteral("fieldProblem"));
        QCOMPARE(runProblem->text(), QStringLiteral("Keine aktive Aktion führt diesen Befehl aus."));
        // Ein neuer Test leert die Prüfzeile zuerst, auch wenn er früh abbricht (Befund e3-5b)
        emit form->testChosen(-1);
        QVERIFY(runProblem->text().isEmpty());

        // «Öffnen» springt nach «Aktionen» und wählt die Aktion
        QVERIFY(page->selectRow(info.id));
        auto *open = form->findChild<QPushButton *>(QStringLiteral("commandOpenAction"));
        QTRY_VERIFY_WITH_TIMEOUT(open->isEnabled(), 3000);
        QTest::mouseClick(open, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(chatBot->actionsPage()->isVisible(), 3000);
        QCOMPARE(chatBot->actionsPage()->selectedAction(), action.id);
        QTest::mouseClick(child<QToolButton>("chatBotTab2"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), 3000);

        // Hinzufügen: neuer Befehl gewählt
        QTest::mouseClick(page->findChild<QToolButton *>(QStringLiteral("commandsAdd")), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(name->text(), QStringLiteral("Neuer Befehl"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gespeichert: Neuer Befehl"), 3000);
        const QUuid added = page->selectedRow();
        QVERIFY(coreCommand(added));

        // Löschen: unbenutzt ohne Rückfrage, benutzt mit Rückfrage («Abbrechen» lässt ihn stehen)
        auto *remove = page->findChild<QToolButton *>(QStringLiteral("commandsRemove"));
        QVERIFY(remove->isEnabled());
        QTest::mouseClick(remove, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!coreCommand(added), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gelöscht: Neuer Befehl"), 3000);
        QVERIFY(page->selectRow(info.id));
        QString question;
        answerQuestion(QStringLiteral("Abbrechen"), &question);
        QTest::mouseClick(remove, Qt::LeftButton);
        QCOMPARE(question, QStringLiteral("Befehl «!info» löschen? Er wird von 1 Aktion benutzt."));
        QVERIFY(coreCommand(info.id));
        question.clear();
        answerQuestion(QStringLiteral("Löschen"), &question);
        QTest::mouseClick(remove, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!coreCommand(info.id), 3000);
        QVERIFY(!question.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gelöscht: DSS-Test Info 2"), 3000);
        QVERIFY(page->selectRow(game.id));
        QTest::mouseClick(remove, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!coreCommand(game.id), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(page->selectedRow().isNull() && !page->detailShown(), 3000);
        QCOMPARE(page->detailTitle(), QStringLiteral("Befehl"));
        const std::optional<ChangeResult> removeAction = request(
            {QStringLiteral("action_remove"), QJsonObject{{QStringLiteral("action_id"), action.id.toString(QUuid::WithoutBraces)}}});
        QVERIFY(removeAction && removeAction->ok);
    }

    // P14 e3-6: Unterreiter «Zeitnachrichten» (Zeilen, Minuten mit Komma, «Zufällig» nur Freigabe,
    // Prüfmeldung, Löschen mit Rückfrage, «Öffnen»)
    void timersTab()
    {
        auto *chatBot = child<ChatBotPage>("chatBotPage");
        ListDetailPage *page = chatBot->timersPage();
        TimerForm *form = chatBot->timerForm();
        QVERIFY(page && form);
        const auto coreTimer = [this](const QUuid &id) -> std::optional<actions::Timer> {
            const std::optional<ActionsConfigInfo> info = m_client->actionsConfig();
            const actions::Timer *t = info && info->config ? info->config->timer(id) : nullptr;
            return t ? std::optional<actions::Timer>(*t) : std::nullopt;
        };
        const auto status = [this] { return m_window->statusBar()->currentMessage(); };

        actions::Timer info;
        info.id = QUuid::createUuid();
        info.name = QStringLiteral("DSS-Test Info");
        info.intervalS = 1200;
        info.lines = 5;
        actions::Timer once;
        once.id = QUuid::createUuid();
        once.name = QStringLiteral("DSS-Test Einmal");
        once.enabled = false;
        once.repeat = false;
        once.onlyLive = false;
        for (const actions::Timer &t : {info, once}) {
            const std::optional<ChangeResult> put = request(
                {QStringLiteral("timer_put"), QJsonObject{{QStringLiteral("timer"), actions::timerToJson(t)}}});
            QVERIFY2(put && put->ok, put ? qPrintable(put->message) : "keine Antwort");
        }
        actions::Action action;
        action.id = QUuid::createUuid();
        action.name = QStringLiteral("DSS-Test Info-Zeitaktion");
        actions::Trigger trigger = actions::newTrigger(QString::fromLatin1(actions::kTriggerTimer));
        trigger.fields.insert(QStringLiteral("timer_id"), info.id.toString(QUuid::WithoutBraces));
        action.triggers = {trigger};
        const std::optional<ChangeResult> putAction = request(
            {QString::fromLatin1(msg::kActionPut), QJsonObject{{QStringLiteral("action"), actions::actionToJson(action)}}});
        QVERIFY2(putAction && putAction->ok, putAction ? qPrintable(putAction->message) : "keine Antwort");

        QTest::mouseClick(child<QToolButton>("chatBotTab3"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), 3000);

        // Zeilen: Spalten aus TimerRow, abgeschaltet grau, kein Hinweis in der Leiste
        auto *table = page->findChild<QTreeWidget *>(QStringLiteral("timersTable"));
        QTRY_VERIFY_WITH_TIMEOUT(findRow(table, 1, info.name) && findRow(table, 1, once.name), 3000);
        QTreeWidgetItem *infoItem = findRow(table, 1, info.name);
        QCOMPARE(infoItem->text(2), QStringLiteral("20 min"));
        QCOMPARE(infoItem->text(3), QStringLiteral("5"));
        QCOMPARE(infoItem->text(4), QStringLiteral("ja"));
        QCOMPARE(infoItem->text(5), action.name);
        QTreeWidgetItem *onceItem = findRow(table, 1, once.name);
        QCOMPARE(onceItem->text(2), QStringLiteral("einmal nach 20 min"));
        QCOMPARE(onceItem->text(3), QStringLiteral("—"));
        QCOMPARE(onceItem->checkState(0), Qt::Unchecked);
        QCOMPARE(onceItem->foreground(1).color(), darkTheme().textDisabled);
        QVERIFY(!page->findChild<QToolButton *>(QStringLiteral("timersAdd"))
                     ->parentWidget()->findChild<QLabel *>(QStringLiteral("hintLabel")));
        QVERIFY(page->findChild<QToolButton *>(QStringLiteral("timersAdd"))->isEnabled());

        // Kästchen «Aktiv» → timer_put mit enabled
        findRow(table, 1, once.name)->setCheckState(0, Qt::Checked);
        QTRY_VERIFY_WITH_TIMEOUT(coreTimer(once.id) && coreTimer(once.id)->enabled, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gespeichert: DSS-Test Einmal"), 3000);

        // Auswahl: Titel und Werte; ein Wechsel schafft keine neuen Felder
        QVERIFY(page->selectRow(info.id));
        QVERIFY(page->detailShown());
        QCOMPARE(page->detailTitle(), QStringLiteral("Zeitnachricht «DSS-Test Info»"));
        auto *name = form->findChild<QLineEdit *>(QStringLiteral("timerName"));
        auto *interval = form->findChild<QDoubleSpinBox *>(QStringLiteral("timerInterval"));
        auto *random = form->findChild<QCheckBox *>(QStringLiteral("timerRandom"));
        auto *randomMin = form->findChild<QDoubleSpinBox *>(QStringLiteral("timerRandomMin"));
        auto *randomMax = form->findChild<QDoubleSpinBox *>(QStringLiteral("timerRandomMax"));
        auto *lines = form->findChild<QSpinBox *>(QStringLiteral("timerLines"));
        auto *actionChoice = form->findChild<QComboBox *>(QStringLiteral("timerAction"));
        QVERIFY(name && interval && random && randomMin && randomMax && lines && actionChoice);
        QCOMPARE(name->text(), info.name);
        QCOMPARE(interval->text(), QStringLiteral("20 min"));
        QCOMPARE(interval->suffix(), QStringLiteral(" min"));
        QCOMPARE(interval->minimum(), 1.0); // kleinster Wert aus minutesRow
        QVERIFY(!interval->keyboardTracking());
        QCOMPARE(lines->value(), 5);
        QCOMPARE(lines->maximum(), kMaxTimerLines); // Grenze aus der Logik
        QCOMPARE(actionChoice->currentText(), action.name);
        QCOMPARE(interval->maximum(), 1440.0); // 24 h aus minutesRow
        QLabel *intervalLabel = nullptr;
        for (QLabel *label : form->findChildren<QLabel *>())
            if (label->buddy() == interval)
                intervalLabel = label;
        QVERIFY(intervalLabel);
        // «von» und «bis» ohne «Zufällig» ausgegraut, aber sichtbar; «Abstand» frei
        QVERIFY(!random->isChecked());
        QVERIFY(randomMin->isVisible() && randomMax->isVisible());
        QVERIFY(!randomMin->isEnabled() && !randomMax->isEnabled());
        QVERIFY(interval->isEnabled() && intervalLabel->isEnabled());
        const int widgetCount = int(form->findChildren<QWidget *>().size());
        QVERIFY(page->selectRow(once.id));
        QCOMPARE(name->text(), once.name);
        QCOMPARE(actionChoice->currentIndex(), 0);
        QVERIFY(!form->findChild<QPushButton *>(QStringLiteral("timerOpenAction"))->isEnabled());
        QCOMPARE(int(form->findChildren<QWidget *>().size()), widgetCount);
        QVERIFY(page->selectRow(info.id));

        // Minuten mit Komma: «7,5» → 450 s im timer_put
        interval->setFocus();
        interval->selectAll();
        QTest::keyClicks(interval, QStringLiteral("7,5"));
        QTest::keyClick(interval, Qt::Key_Return);
        QTRY_VERIFY_WITH_TIMEOUT(coreTimer(info.id) && coreTimer(info.id)->intervalS == 450, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gespeichert: DSS-Test Info"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(findRow(table, 1, info.name)->text(2), QStringLiteral("7,5 min"), 3000);
        QCOMPARE(interval->text(), QStringLiteral("7,5 min"));

        // «Zufällig» schaltet nur die Freigabe von «Abstand» und «von»/«bis»; ohne Bereich gilt der Abstand
        QTest::mouseClick(random, Qt::LeftButton, {}, QPoint(4, random->height() / 2));
        QTRY_VERIFY_WITH_TIMEOUT(coreTimer(info.id) && coreTimer(info.id)->random, 3000);
        QCOMPARE(coreTimer(info.id)->randomMinS, 450);
        QCOMPARE(coreTimer(info.id)->randomMaxS, 450);
        QTRY_VERIFY_WITH_TIMEOUT(randomMin->isEnabled() && randomMax->isEnabled(), 3000);
        QVERIFY(!interval->isEnabled() && !intervalLabel->isEnabled());
        QVERIFY(interval->isVisible() && intervalLabel->isVisible());
        QCOMPARE(randomMin->text(), QStringLiteral("7,5 min"));
        QCOMPARE(int(form->findChildren<QWidget *>().size()), widgetCount);

        // Prüfmeldung unter «Abstand» (bis kürzer als von), danach wieder der Wert des Kerns
        auto *intervalSection = form->findChild<QWidget *>(QStringLiteral("timerIntervalSection"));
        auto *problem = intervalSection->findChild<QLabel *>(QStringLiteral("fieldProblem"));
        QVERIFY(problem && problem->isVisible() && problem->text().isEmpty()); // Zeile immer da
        randomMax->setValue(5);
        QTRY_COMPARE_WITH_TIMEOUT(problem->text(),
                                  QStringLiteral("Der längste Abstand darf nicht kürzer sein als der kürzeste."), 3000);
        QCOMPARE(randomMax->text(), QStringLiteral("7,5 min"));
        QCOMPARE(coreTimer(info.id)->randomMaxS, 450);
        QVERIFY(problem->geometry().top() > randomMax->geometry().bottom());
        randomMax->setValue(10);
        QTRY_VERIFY_WITH_TIMEOUT(coreTimer(info.id) && coreTimer(info.id)->randomMaxS == 600, 3000);
        QVERIFY(problem->text().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(findRow(table, 1, info.name)->text(2), QStringLiteral("7,5 bis 10 min, zufällig"), 3000);
        QTest::mouseClick(random, Qt::LeftButton, {}, QPoint(4, random->height() / 2));
        QTRY_VERIFY_WITH_TIMEOUT(coreTimer(info.id) && !coreTimer(info.id)->random, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!randomMin->isEnabled() && randomMin->isVisible(), 3000);
        QVERIFY(interval->isEnabled() && intervalLabel->isEnabled());

        // «Ausführen»: «Keine» gibt den Auslöser ab, die Aktion bekommt ihn wieder
        actionChoice->setCurrentIndex(0);
        emit actionChoice->activated(0);
        QTRY_VERIFY_WITH_TIMEOUT(timerAction(*m_client->actionsConfig()->config, info.id).isNull(), 3000);
        const int actionIndex = actionChoice->findText(action.name);
        QVERIFY(actionIndex > 0);
        actionChoice->setCurrentIndex(actionIndex);
        emit actionChoice->activated(actionIndex);
        QTRY_COMPARE_WITH_TIMEOUT(timerAction(*m_client->actionsConfig()->config, info.id), action.id, 3000);

        // «Öffnen» springt nach «Aktionen» und wählt die Aktion
        auto *open = form->findChild<QPushButton *>(QStringLiteral("timerOpenAction"));
        QTRY_VERIFY_WITH_TIMEOUT(open->isEnabled(), 3000);
        QTest::mouseClick(open, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(chatBot->actionsPage()->isVisible(), 3000);
        QCOMPARE(chatBot->actionsPage()->selectedAction(), action.id);
        QTest::mouseClick(child<QToolButton>("chatBotTab3"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), 3000);

        // Hinzufügen: neue Zeitnachricht gewählt
        QTest::mouseClick(page->findChild<QToolButton *>(QStringLiteral("timersAdd")), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(name->text(), QStringLiteral("Neue Zeitnachricht"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gespeichert: Neue Zeitnachricht"), 3000);
        const QUuid added = page->selectedRow();
        QVERIFY(coreTimer(added));

        // Löschen: unbenutzt ohne Rückfrage, benutzt mit Rückfrage («Abbrechen» lässt sie stehen)
        auto *remove = page->findChild<QToolButton *>(QStringLiteral("timersRemove"));
        QVERIFY(remove->isEnabled());
        QTest::mouseClick(remove, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!coreTimer(added), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gelöscht: Neue Zeitnachricht"), 3000);
        QVERIFY(page->selectRow(info.id));
        QString question;
        answerQuestion(QStringLiteral("Abbrechen"), &question);
        QTest::mouseClick(remove, Qt::LeftButton);
        QCOMPARE(question, QStringLiteral("Zeitnachricht «DSS-Test Info» löschen? Sie wird von 1 Aktion benutzt."));
        QVERIFY(coreTimer(info.id));
        question.clear();
        answerQuestion(QStringLiteral("Löschen"), &question);
        QTest::mouseClick(remove, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!coreTimer(info.id), 3000);
        QVERIFY(!question.isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gelöscht: DSS-Test Info"), 3000);
        QVERIFY(page->selectRow(once.id));
        QTest::mouseClick(remove, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!coreTimer(once.id), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(page->selectedRow().isNull() && !page->detailShown(), 3000);
        QCOMPARE(page->detailTitle(), QStringLiteral("Zeitnachricht"));
        const std::optional<ChangeResult> removeAction = request(
            {QStringLiteral("action_remove"), QJsonObject{{QStringLiteral("action_id"), action.id.toString(QUuid::WithoutBraces)}}});
        QVERIFY(removeAction && removeAction->ok);
    }

    // P14 e3-7: «Variablen» mit den Daten aus tests/daten/streamerbot-export (Import im Testkern):
    // Zeilen, Suche, Filter, Wert in der Zelle ändern, Prüfmeldung, Löschen mit Rückfrage, nachgeführt
    // nach einem Lauf.
    void variablesTab()
    {
        {
            CoreSession session(30000);
            QString error;
            QVERIFY2(session.open(m_run->filePath(QStringLiteral("kern.sock")), QStringLiteral("tst_fensterchatbot"),
                                  &error), qPrintable(error));
            const std::optional<QJsonObject> imported = session.actionsRequest(
                QString::fromLatin1(msg::kViewerImport),
                {{QStringLiteral("path"), QStringLiteral(DSS_TEST_EXPORT_DIR)}, {QStringLiteral("dry_run"), false},
                 {QStringLiteral("overwrite"), false}},
                QString::fromLatin1(msg::kViewerImportResult), &error);
            QVERIFY2(imported, qPrintable(error));
        }
        auto *chatBot = child<ChatBotPage>("chatBotPage");
        VariablesPage *page = chatBot->variablesPage();
        QVERIFY(page);
        const auto status = [this] { return m_window->statusBar()->currentMessage(); };
        const auto coreGlobal = [this](const QString &name) {
            const std::optional<VariablesInfo> info = m_client->variables();
            return info ? info->persisted.global.value(name).value : QJsonValue();
        };
        auto *global = page->findChild<QTreeWidget *>(QStringLiteral("globalVariables"));
        auto *user = page->findChild<QTreeWidget *>(QStringLiteral("userVariables"));
        auto *globalProblem = global->parentWidget()->findChildren<QLabel *>(QStringLiteral("fieldProblem")).value(0);
        auto *globalRemove = page->findChild<QToolButton *>(QStringLiteral("globalVariablesRemove"));
        auto *userRemove = page->findChild<QToolButton *>(QStringLiteral("userVariablesRemove"));
        QVERIFY(global && user && globalProblem && globalRemove && userRemove);
        QCOMPARE(globalProblem->text(), QString()); // Zeile immer da, leer ohne Meldung
        QCOMPARE(user->parentWidget()->findChildren<QLabel *>(QStringLiteral("fieldProblem")).size(), 1);
        // Gleicher Tooltip, zugänglicher Name je Bereich
        QCOMPARE(globalRemove->toolTip(), QStringLiteral("Variable löschen"));
        QCOMPARE(userRemove->toolTip(), QStringLiteral("Variable löschen"));
        QCOMPARE(globalRemove->accessibleName(), QStringLiteral("Variable für alle löschen"));
        QCOMPARE(userRemove->accessibleName(), QStringLiteral("Variable je Zuschauer löschen"));

        // Beim Zeigen holen; die Zuschauer kommen nach den Variablen
        QTest::mouseClick(child<QToolButton>("chatBotTab4"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(page->isVisible(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(findRow(global, 0, QStringLiteral("testZaehler")), 5000);
        QStringList headers;
        for (int i = 0; i < global->columnCount(); ++i)
            headers << global->headerItem()->text(i);
        QCOMPARE(headers, (QStringList{QStringLiteral("Name"), QStringLiteral("Wert"), QStringLiteral("Dauerhaft"),
                                       QStringLiteral("Herkunft")}));
        headers.clear();
        for (int i = 0; i < user->columnCount(); ++i)
            headers << user->headerItem()->text(i);
        QCOMPARE(headers, (QStringList{QStringLiteral("Zuschauer"), QStringLiteral("Variable"), QStringLiteral("Wert"),
                                       QStringLiteral("Herkunft")}));
        QTreeWidgetItem *counter = findRow(global, 0, QStringLiteral("testZaehler"));
        QCOMPARE(counter->text(1), QStringLiteral("42"));
        QCOMPARE(counter->text(2), QStringLiteral("dauerhaft"));
        QCOMPARE(counter->text(3), QStringLiteral("Streamer.bot"));
        QTRY_VERIFY_WITH_TIMEOUT(findRow(user, 0, QStringLiteral("TestZuschauer1")), 5000);
        QVERIFY(!globalRemove->isEnabled() && !userRemove->isEnabled()); // ohne Auswahl

        // Suche «Für alle» in Name und Wert; Filter «Je Zuschauer» nach Variable
        auto *search = page->findChild<QLineEdit *>(QStringLiteral("globalVariablesSearch"));
        QCOMPARE(search->placeholderText(), QStringLiteral("Suchen …"));
        search->setText(QStringLiteral("hallo"));
        QCOMPARE(global->topLevelItemCount(), 1);
        QCOMPARE(global->topLevelItem(0)->text(0), QStringLiteral("testText"));
        search->clear();
        auto *filter = page->findChild<QComboBox *>(QStringLiteral("userVariablesFilter"));
        QCOMPARE(filter->itemText(0), QStringLiteral("Alle Variablen"));
        QVERIFY(filter->findText(QStringLiteral("firsts")) > 0);
        filter->setCurrentIndex(filter->findText(QStringLiteral("firsts")));
        QVERIFY(user->topLevelItemCount() > 0);
        for (int i = 0; i < user->topLevelItemCount(); ++i)
            QCOMPARE(user->topLevelItem(i)->text(1), QStringLiteral("firsts"));
        filter->setCurrentIndex(0);
        QVERIFY(findRow(user, 1, QStringLiteral("watchtime")));

        // Wert ändern: F2 auf der Zeile öffnet «Wert» → variable_set, «Gespeichert: <Name>»
        const auto editValue = [&](QTreeWidget *table, QTreeWidgetItem *item, const QString &text) {
            table->setFocus();
            table->setCurrentItem(item, 0);
            QTest::keyClick(table, Qt::Key_F2);
            // Der Editor steht in «Wert», auch wenn Spalte 0 die aktuelle ist
            auto *editor = qobject_cast<QLineEdit *>(QApplication::focusWidget());
            const int valueColumn = table == global ? 1 : 2;
            if (!editor || editor->x() < table->header()->sectionViewportPosition(valueColumn) - 2) {
                qWarning("kein Editor in «Wert»");
                return false;
            }
            editor->selectAll();
            QTest::keyClicks(editor, text);
            QTest::keyClick(editor, Qt::Key_Return);
            return true;
        };
        counter = findRow(global, 0, QStringLiteral("testZaehler"));
        QVERIFY(editValue(global, counter, QStringLiteral("43")));
        QTRY_COMPARE_WITH_TIMEOUT(coreGlobal(QStringLiteral("testZaehler")), QJsonValue(43), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gespeichert: testZaehler"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(findRow(global, 0, QStringLiteral("testZaehler"))->text(1), QStringLiteral("43"), 3000);
        QCOMPARE(page->selectedRow(VariablesPage::Global).endsWith(QStringLiteral("/testZaehler")), true);

        // Zahl mit Text: Prüfmeldung unter der Tabelle, die Zelle zeigt wieder den Wert des Kerns
        QVERIFY(editValue(global, findRow(global, 0, QStringLiteral("testZaehler")), QStringLiteral("drei")));
        QTRY_COMPARE_WITH_TIMEOUT(globalProblem->text(), QStringLiteral("«drei» ist keine Zahl."), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(findRow(global, 0, QStringLiteral("testZaehler"))->text(1), QStringLiteral("43"), 3000);
        QCOMPARE(coreGlobal(QStringLiteral("testZaehler")), QJsonValue(43));
        // Neu gefüllt mit derselben Auswahl: Meldung bleibt; Suche ohne Wechsel der Auswahl auch
        QCOMPARE(globalProblem->text(), QStringLiteral("«drei» ist keine Zahl."));
        search->setText(QStringLiteral("testZ"));
        search->clear();
        QCOMPARE(globalProblem->text(), QStringLiteral("«drei» ist keine Zahl."));

        // Löschen nur mit Rückfrage: «Abbrechen» lässt die Variable stehen. Andere Zeile: Prüfzeile leer
        global->setCurrentItem(findRow(global, 0, QStringLiteral("testText")));
        QCOMPARE(globalProblem->text(), QString());
        QVERIFY(globalRemove->isEnabled());
        QString question;
        answerQuestion(QStringLiteral("Abbrechen"), &question);
        QTest::mouseClick(globalRemove, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(question, QStringLiteral("Variable «testText» löschen?"), 3000);
        QTest::qWait(300);
        QVERIFY(coreGlobal(QStringLiteral("testText")).isString());
        QVERIFY(findRow(global, 0, QStringLiteral("testText")));

        // Je Zuschauer: Rückfrage mit dem Namen aus VariableRow::viewer, dann variable_remove
        QTreeWidgetItem *watch = nullptr;
        for (int i = 0; i < user->topLevelItemCount(); ++i)
            if (user->topLevelItem(i)->text(0) == QStringLiteral("TestZuschauer1")
                && user->topLevelItem(i)->text(1) == QStringLiteral("watchtime"))
                watch = user->topLevelItem(i);
        QVERIFY(watch);
        user->setCurrentItem(watch);
        QVERIFY(userRemove->isEnabled());
        question.clear();
        answerQuestion(QStringLiteral("Löschen"), &question);
        QTest::mouseClick(userRemove, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(question, QStringLiteral("Variable «watchtime» von «TestZuschauer1» löschen?"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(status(), QStringLiteral("Gelöscht: watchtime"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_client->variables()->persisted.users.value(QStringLiteral("100000001"))
                                      .contains(QStringLiteral("watchtime")), 3000);

        // Nachgeführt nach einem Lauf (actions.run_finished), solange «Variablen» sichtbar ist
        actions::Action action;
        action.id = QUuid::createUuid();
        action.name = QStringLiteral("DSS-Test Variable setzen");
        actions::Step set = actions::newStep(QString::fromLatin1(actions::kStepGlobalSet));
        set.fields.insert(QStringLiteral("name"), QStringLiteral("dsstest_lauf"));
        set.fields.insert(QStringLiteral("operation"), QStringLiteral("increment"));
        set.fields.insert(QStringLiteral("value"), QStringLiteral("1"));
        action.steps = {set};
        const std::optional<ChangeResult> put = request(
            {QString::fromLatin1(msg::kActionPut), QJsonObject{{QStringLiteral("action"), actions::actionToJson(action)}}});
        QVERIFY2(put && put->ok, put ? qPrintable(put->message) : "keine Antwort");
        const std::optional<ChangeResult> run = request(
            {QString::fromLatin1(msg::kActionTest),
             QJsonObject{{QStringLiteral("action_id"), action.id.toString(QUuid::WithoutBraces)},
                         {QStringLiteral("arguments"), QJsonObject()}, {QStringLiteral("dry_run"), false}}});
        QVERIFY2(run && run->ok, run ? qPrintable(run->message) : "keine Antwort");
        QTRY_VERIFY_WITH_TIMEOUT(findRow(global, 0, QStringLiteral("dsstest_lauf")), 5000);
        QCOMPARE(findRow(global, 0, QStringLiteral("dsstest_lauf"))->text(1), QStringLiteral("1"));

        const std::optional<ChangeResult> removeAction = request(
            {QString::fromLatin1(msg::kActionRemove),
             QJsonObject{{QStringLiteral("action_id"), action.id.toString(QUuid::WithoutBraces)}}});
        QVERIFY(removeAction && removeAction->ok);
        QTest::mouseClick(child<QToolButton>("chatBotTab0"), Qt::LeftButton);
    }

    // Ohne Kern: Schalter aus und ausgegraut, Text von Twitch.
    void withoutCore()
    {
        auto *box = child<QCheckBox>("chatBotEnabled");
        m_client->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!m_client->isConnected(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!box->isEnabled(), 3000);
        QVERIFY(!box->isChecked());
        QVERIFY(child<QLabel>("chatBotState")->text().startsWith(QStringLiteral("Twitch: ")));
    }
};

int main(int argc, char *argv[])
{
    if (qgetenv("XDG_SESSION_TYPE") != "x11" || qEnvironmentVariableIsEmpty("DISPLAY")) {
        qInfo("übersprungen: keine X11-Sitzung (XDG_SESSION_TYPE/DISPLAY)");
        return 77;
    }
    QApplication app(argc, argv);
    QApplication::setStyle(QStringLiteral("Fusion"));
    TestFensterChatBot test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_fensterchatbot.moc"
