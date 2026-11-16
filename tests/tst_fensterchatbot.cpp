// Reiter «Chat-Bot» im echten Fenster (P14 e2-11): MainWindow wie in dss-app, verbunden mit einem
// eigenen dss-core, in der X11-Sitzung (CLAUDE.md Regel 6). Prüft den Schalter «Chat-Bot aktiv»
// (an, aus, ausgegraut bei «stumm» mit einem zweiten Kern unter der Verbindung der Version im
// Einsatz), die Zustände der Kopfzeile und den Unterreiter «Aktionen». Muster: tst_fensterbearbeiten.

#include "actionspage.h"
#include "chatbotpage.h"
#include "coreclient.h"
#include "mainwindow.h"
#include "prozesshilfe.h"
#include "symbols.h"
#include "theme.h"
#include "version.h"

#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QLabel>
#include <QSplitter>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QToolButton>

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

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        m_run = testhelp::runtimeDir();
        const QString socket = m_run->filePath(QStringLiteral("kern.sock"));
        m_inUseSocket = m_run->filePath(QStringLiteral("einsatz.sock"));
        m_core = testhelp::startCore(m_data.path(), socket, testhelp::testConfigHome(),
                                     {QStringLiteral("--einsatz-verbindung"), m_inUseSocket});
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
        for (QToolButton *b : child<QFrame>("moduleTabs")->findChildren<QToolButton *>())
            if (b->text() == QStringLiteral("Chat-Bot"))
                QTest::mouseClick(b, Qt::LeftButton);
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
        QCOMPARE(tabs.size(), 2);
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
