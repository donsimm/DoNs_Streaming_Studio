// Reiter «Kameras» im echten Fenster (P15 e2): MainWindow wie in dss-app, verbunden mit einem eigenen
// dss-core, der mit dem echten Kamera-Dienst in der Attrappe verbunden ist (eigener Prozess auf
// 127.0.0.1, freier Port, kameradiensthilfe.h), in der X11-Sitzung (CLAUDE.md Regel 6). Prüft
// Kopfzeile, Steuerkreuz (Klick = Schritt, Halten = Fahrt mit stop, Pfeiltasten), KI-Verfolgung,
// «Positionen» (Speichern, Abrufen, Überschreiben, Umbenennen, Löschen), ganze Presets nach dem
// Start, schlafende Kamera, C922, Dialog «Kamera einrichten» und den Verlust des Dienstes.
// Mit DSS_TEST_BILDER legt der Test kameras-e2.png in diesem Ordner ab.
// Muster: tst_fensterchatbot (Fenster), tst_kernkamera (Kern mit Dienst).

#include "camerapage.h"
#include "camerasetupdialog.h"
#include "coreclient.h"
#include "kameradiensthilfe.h"
#include "mainwindow.h"
#include "presetnamedialog.h"
#include "prozesshilfe.h"
#include "theme.h"
#include "version.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QSplitter>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <memory>

using namespace dss;

namespace {

const QString kTiny2 = QStringLiteral("aaaa:0001"); // TINY2_ID in fake.py

struct Answer
{
    CameraCall call;
    CameraResult result;
};

} // namespace

class TestFensterKameras : public QObject
{
    Q_OBJECT

    QTemporaryDir m_serviceDir{QDir::tempPath() + QStringLiteral("/tst_fensterkameras-XXXXXX")};
    QTemporaryDir m_data;
    std::unique_ptr<QTemporaryDir> m_run;
    QString m_socket;
    std::unique_ptr<kameradienst::Service> m_service;
    std::unique_ptr<QProcess> m_core;
    std::unique_ptr<CoreClient> m_client;
    std::unique_ptr<MainWindow> m_window;
    QList<Answer> m_answers; // alle Antworten an das Fenster

    template <typename T>
    T *child(const char *name)
    {
        T *w = m_window->findChild<T *>(QString::fromLatin1(name));
        if (!w)
            qWarning("nicht gefunden: %s", name);
        return w;
    }

    // Wartet auf eine Antwort mit Methode und Pfadende; gibt sie zurück (status 0 = keine)
    Answer waitForAnswer(const QString &method, const QString &pathEnd, qsizetype from, int timeoutMs = 5000)
    {
        Answer found;
        (void)QTest::qWaitFor([&] {
            for (qsizetype i = from; i < m_answers.size(); ++i) {
                if (m_answers.at(i).call.method == method && m_answers.at(i).call.path.endsWith(pathEnd)) {
                    found = m_answers.at(i);
                    return true;
                }
            }
            return false;
        }, timeoutMs);
        return found;
    }

    qsizetype countAnswers(const QString &pathEnd, qsizetype from) const
    {
        qsizetype n = 0;
        for (qsizetype i = from; i < m_answers.size(); ++i)
            n += m_answers.at(i).call.path.endsWith(pathEnd) ? 1 : 0;
        return n;
    }

    CameraPage *page() { return child<CameraPage>("cameraPage"); }

    bool chooseCamera(const QString &id)
    {
        auto *choice = child<QComboBox>("cameraChoice");
        const int index = choice->findData(id);
        if (index < 0)
            return false;
        choice->setCurrentIndex(index);
        // Bildwerte und Einstellungen der Kamera geladen (Zoom, Tempo)
        return QTest::qWaitFor([&] {
            return page()->currentCameraId() == id && child<QSpinBox>("cameraSpeed")->isEnabled();
        }, 5000);
    }

    QToolButton *crossButton(int index)
    {
        return child<QToolButton>(qPrintable(QStringLiteral("cameraMove%1").arg(index)));
    }

    QTreeWidgetItem *presetItem(const QString &name)
    {
        auto *tree = child<QTreeWidget>("cameraPresets");
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (tree->topLevelItem(i)->text(0) == name)
                return tree->topLevelItem(i);
        }
        return nullptr;
    }

    bool waitForState(const QString &state)
    {
        return QTest::qWaitFor([&] {
            const auto info = m_client->cameraService();
            return info && info->state == state;
        }, 10000);
    }

    // Wach bzw. Schlaf der Tiny 2 laut Auswahl («· wach»)
    bool waitForTiny2(const QString &stateText)
    {
        auto *choice = child<QComboBox>("cameraChoice");
        return QTest::qWaitFor([&] {
            const int index = choice->findData(kTiny2);
            return index >= 0 && choice->itemText(index).endsWith(QStringLiteral("· ") + stateText);
        }, 10000);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_serviceDir.isValid());
        QVERIFY(m_data.isValid());
        const QString devices = kameradienst::createFakeDevices(m_serviceDir.filePath(QStringLiteral("geraete")));
        QVERIFY2(devices.isEmpty(), qPrintable(devices));
        const int port = kameradienst::freePort();
        QVERIFY(port > 0);
        m_service = std::make_unique<kameradienst::Service>(m_serviceDir.path(), port);
        QVERIFY2(m_service->start(), qPrintable(m_service->log()));

        // Kern mit [kamera-dienst] in kern.ini (wie tst_kernkamera)
        QVERIFY(testhelp::writeEmptyActions(m_data.path()));
        const QString configHome = testhelp::testConfigHome();
        {
            QFile ini(configHome + QStringLiteral("/dons-streaming-studio-entwicklung/kern.ini"));
            QVERIFY(ini.open(QIODevice::Append));
            ini.write(QStringLiteral("[kamera-dienst]\naktiv=ja\nadresse=127.0.0.1\nport=%1\n").arg(port).toUtf8());
        }
        m_run = testhelp::runtimeDir();
        m_socket = m_run->filePath(QStringLiteral("kern.sock"));
        m_core = testhelp::startCore(m_data.path(), m_socket, configHome);
        QString log;
        QVERIFY2(testhelp::waitForLog(m_core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));

        CoreClient::Options o;
        o.socketPath = m_socket;
        o.program = QStringLiteral("dss-app");
        o.version = version();
        m_client = std::make_unique<CoreClient>(o);
        connect(m_client.get(), &CoreClient::cameraResultReceived, this,
                [this](const CameraCall &call, const CameraResult &result) { m_answers.append({call, result}); });
        m_window = std::make_unique<MainWindow>(darkTheme(), false, m_client.get());
        m_window->resize(1600, 900);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        m_client->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isConnected(), 5000);
        for (QToolButton *b : child<QFrame>("moduleTabs")->findChildren<QToolButton *>()) {
            if (b->text() == QStringLiteral("Kameras"))
                QTest::mouseClick(b, Qt::LeftButton);
        }
        QTRY_VERIFY_WITH_TIMEOUT(page()->isVisible(), 3000);
        QVERIFY2(waitForState(QStringLiteral("connected")), qPrintable(m_core->readAllStandardError()));
        QVERIFY(chooseCamera(kTiny2));
    }

    void cleanupTestCase()
    {
        if (m_window)
            m_window->close(); // bittet den Kern, sich zu beenden (einziges Fenster)
        m_window.reset();
        m_client.reset();
        if (m_core && m_core->state() != QProcess::NotRunning && !m_core->waitForFinished(3000)) {
            m_core->kill();
            m_core->waitForFinished(3000);
        }
        if (m_service)
            m_service->stop();
    }

    // Dritter Modul-Reiter, Kopfzeile mit Auswahl, Wecken/Schlafen, «Einrichten …» und Zustand
    void header()
    {
        QStringList tabs;
        for (QToolButton *b : child<QFrame>("moduleTabs")->findChildren<QToolButton *>())
            tabs << b->text();
        QCOMPARE(tabs, (QStringList{QStringLiteral("Overlays"), QStringLiteral("Chat-Bot"), QStringLiteral("Kameras")}));

        auto *state = child<QLabel>("cameraState");
        QCOMPARE(state->text(), QStringLiteral("Kamera-Dienst: verbunden"));
        QCOMPARE(state->property("state").toString(), QStringLiteral("ok"));
        QVERIFY(child<QLabel>("cameraBlockedHint")->text().isEmpty());
        auto *choice = child<QComboBox>("cameraChoice");
        QCOMPARE(choice->count(), 2);
        QVERIFY(choice->findData(kameradienst::kC922) >= 0);
        QVERIFY2(waitForTiny2(QStringLiteral("wach")), qPrintable(choice->currentText()));
        QVERIFY(choice->currentText().startsWith(QStringLiteral("OBSBOT Tiny 2 · ")));

        auto *wake = child<QPushButton>("cameraWake");
        QCOMPARE(wake->text(), QStringLiteral("Schlafen"));
        QVERIFY(wake->isEnabled());
        const int width = wake->width();
        QVERIFY(child<QPushButton>("cameraSetup")->isEnabled());
        QVERIFY(child<QSplitter>("cameraColumns")->isEnabled());
        QCOMPARE(child<QWidget>("cameraImagePanel")->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"))->text(),
                 QStringLiteral("Bildwerte · OBSBOT Tiny 2"));
        QVERIFY(child<QWidget>("cameraLightPanel"));
        // Feste Breite: «Wecken» ändert die Anordnung nicht
        wake->setText(QStringLiteral("Wecken"));
        QVERIFY(wake->sizeHint().width() <= wake->minimumWidth());
        QCoreApplication::processEvents();
        QCOMPARE(wake->width(), width);
        wake->setText(QStringLiteral("Schlafen"));

        // Bewegen: Kreuz frei, Zoom in %, Tempo in °/s (Tiny 2)
        for (int i = 0; i < 9; ++i)
            QVERIFY(child<QToolButton>(i == 4 ? "cameraHome" : qPrintable(QStringLiteral("cameraMove%1").arg(i)))->isEnabled());
        QVERIFY(child<QLabel>("cameraZoomText")->text().endsWith(QStringLiteral(" %")));
        QCOMPARE(child<QSpinBox>("cameraSpeed")->suffix(), QStringLiteral(" °/s"));
        QVERIFY(child<QLabel>("cameraMoveHint")->text().isEmpty());
        QVERIFY(!child<QPushButton>("cameraHintWake")->isVisible());
    }

    // Klick = ein Schritt, Halten = Fahrt mit Wiederholung und «stop», Pfeiltasten wie die Maus
    void crossClickHoldKeys()
    {
        qsizetype from = m_answers.size();
        QTest::mouseClick(crossButton(5), Qt::LeftButton); // nach rechts
        Answer step = waitForAnswer(QStringLiteral("POST"), QStringLiteral("/step"), from);
        QCOMPARE(step.result.status, 200);
        QCOMPARE(step.call.path, QStringLiteral("cameras/%1/step").arg(kTiny2));
        QCOMPARE(step.call.body->value(QStringLiteral("pan")).toInt(), 1);
        QCOMPARE(countAnswers(QStringLiteral("/move"), from), 0);

        from = m_answers.size();
        QTest::mousePress(crossButton(1), Qt::LeftButton); // nach oben
        QTest::qWait(kCameraHoldDelayMs + 3 * kCameraHoldRepeatMs + 100);
        QTest::mouseRelease(crossButton(1), Qt::LeftButton);
        const Answer stop = waitForAnswer(QStringLiteral("POST"), QStringLiteral("/stop"), from);
        QCOMPARE(stop.result.status, 200);
        QVERIFY2(countAnswers(QStringLiteral("/move"), from) >= 3, qPrintable(QString::number(countAnswers(QStringLiteral("/move"), from))));
        QCOMPARE(countAnswers(QStringLiteral("/step"), from), 0);

        // Pfeiltaste bei Fokus auf dem Kreuz: ein Schritt nach links, der Fokus bleibt
        from = m_answers.size();
        crossButton(1)->setFocus();
        QTest::keyClick(crossButton(1), Qt::Key_Left);
        step = waitForAnswer(QStringLiteral("POST"), QStringLiteral("/step"), from);
        QCOMPARE(step.result.status, 200);
        QCOMPARE(step.call.body->value(QStringLiteral("pan")).toInt(), -1);
        QVERIFY(crossButton(1)->hasFocus());

        // Mitte und Zoom
        from = m_answers.size();
        QTest::mouseClick(child<QToolButton>("cameraHome"), Qt::LeftButton);
        QCOMPARE(waitForAnswer(QStringLiteral("POST"), QStringLiteral("/home"), from).result.status, 200);
        from = m_answers.size();
        QTest::mouseClick(child<QToolButton>("cameraZoomIn"), Qt::LeftButton);
        step = waitForAnswer(QStringLiteral("POST"), QStringLiteral("/step"), from);
        QCOMPARE(step.call.body->value(QStringLiteral("zoom")).toInt(), 1);
    }

    // KI-Verfolgung: Wahl schickt obsbot_ai; nach einer Bewegung zeigt sie «Aus» (ai_off_before_move)
    void aiTracking()
    {
        auto *ai = child<QComboBox>("cameraAi");
        QVERIFY(ai->isEnabled());
        QCOMPARE(ai->count(), 2);
        const int upper = ai->findText(QStringLiteral("Oberkörper"));
        QVERIFY(upper >= 0);
        const qsizetype from = m_answers.size();
        emit ai->activated(upper);
        QCOMPARE(waitForAnswer(QStringLiteral("PUT"), QStringLiteral("/controls"), from).result.status, 200);
        QTRY_COMPARE_WITH_TIMEOUT(ai->currentText(), QStringLiteral("Oberkörper"), 5000);

        // Schritt: die Anzeige geht sofort auf «Aus», ohne Ereignis des Dienstes
        QTest::mouseClick(crossButton(3), Qt::LeftButton);
        QCOMPARE(ai->currentText(), QStringLiteral("Aus"));
        QVERIFY(child<QLabel>("cameraAiHint")->text().isEmpty());
    }

    // «Positionen»: Speichern (Dialog), Abrufen, Überschreiben, Umbenennen, Löschen mit Rückfrage
    void presets()
    {
        auto *tree = child<QTreeWidget>("cameraPresets");
        QCOMPARE(tree->headerItem()->text(1), QStringLiteral("Position"));
        auto *save = child<QPushButton>("cameraPresetSave");
        QVERIFY(save->isEnabled());
        QVERIFY(!child<QPushButton>("cameraPresetRecall")->isEnabled());
        QVERIFY(!child<QPushButton>("cameraPresetRemove")->isEnabled());

        QTest::mouseClick(save, Qt::LeftButton);
        PresetNameDialog *dialog = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((dialog = m_window->findChild<PresetNameDialog *>()) && dialog->isVisible(), 3000);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Position speichern"));
        auto *ok = dialog->findChild<QPushButton *>(QStringLiteral("presetNameOk"));
        QVERIFY(!ok->isEnabled());
        QVERIFY(!dialog->findChild<QCheckBox *>(QStringLiteral("presetWithControls"))->isChecked());
        QTest::keyClicks(dialog->findChild<QLineEdit *>(QStringLiteral("presetName")), QStringLiteral("Totale"));
        QVERIFY(ok->isEnabled());
        QTest::mouseClick(ok, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(m_window->statusBar()->currentMessage(), QStringLiteral("Gespeichert: Totale"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(presetItem(QStringLiteral("Totale")), 5000);
        QTreeWidgetItem *item = presetItem(QStringLiteral("Totale"));
        QVERIFY2(item->text(1).contains(QStringLiteral("Zoom")), qPrintable(item->text(1)));
        QCOMPARE(item->text(2), QStringLiteral("–"));

        tree->setCurrentItem(item);
        QVERIFY(child<QPushButton>("cameraPresetRecall")->isEnabled());
        QVERIFY(child<QPushButton>("cameraPresetOverwrite")->isEnabled());
        qsizetype from = m_answers.size();
        QTest::mouseClick(child<QPushButton>("cameraPresetRecall"), Qt::LeftButton);
        QCOMPARE(waitForAnswer(QStringLiteral("POST"), QStringLiteral("/recall"), from).result.status, 200);

        from = m_answers.size();
        QTest::mouseClick(child<QPushButton>("cameraPresetOverwrite"), Qt::LeftButton);
        QCOMPARE(waitForAnswer(QStringLiteral("POST"), QStringLiteral("/save-current"), from).result.status, 200);

        // Umbenennen über askText (QInputDialog)
        QTimer::singleShot(200, this, [] {
            if (auto *input = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                input->setTextValue(QStringLiteral("Totale breit"));
                input->accept();
            }
        });
        QTest::mouseClick(child<QPushButton>("cameraPresetRename"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(presetItem(QStringLiteral("Totale breit")), 5000);
        QCOMPARE(m_window->statusBar()->currentMessage(), QStringLiteral("Gespeichert: Totale breit"));
        QVERIFY2(presetItem(QStringLiteral("Totale breit"))->isSelected(), "Wahl bleibt beim selben Preset");

        // Ganze Presets nach dem Start: eine zweite Seite mit eigener Verbindung holt sie einzeln
        {
            CoreClient::Options o;
            o.socketPath = m_socket;
            o.program = QStringLiteral("tst_fensterkameras");
            o.version = version();
            CoreClient client(o);
            CameraPage second(darkTheme());
            second.setClient(&client);
            client.start();
            auto *secondChoice = second.findChild<QComboBox *>(QStringLiteral("cameraChoice"));
            QTRY_COMPARE_WITH_TIMEOUT(secondChoice->count(), 2, 10000);
            secondChoice->setCurrentIndex(secondChoice->findData(kTiny2));
            QCOMPARE(second.currentCameraId(), kTiny2);
            auto *secondTree = second.findChild<QTreeWidget *>(QStringLiteral("cameraPresets"));
            QTRY_COMPARE_WITH_TIMEOUT(secondTree->topLevelItemCount(), 1, 5000);
            QTRY_VERIFY_WITH_TIMEOUT(secondTree->topLevelItem(0)->text(1).contains(QStringLiteral("Zoom")), 5000);
            client.stop();
        }

        // Löschen fragt zuerst; «Abbrechen» lässt das Preset
        QTimer::singleShot(200, this, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                QCOMPARE(box->text(), QStringLiteral("Position «Totale breit» löschen?"));
                box->reject();
            }
        });
        QTest::mouseClick(child<QPushButton>("cameraPresetRemove"), Qt::LeftButton);
        QTest::qWait(300);
        QVERIFY(presetItem(QStringLiteral("Totale breit")));
        QTimer::singleShot(200, this, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                for (QAbstractButton *b : box->buttons()) {
                    if (b->text() == QStringLiteral("Löschen"))
                        b->click();
                }
            }
        });
        QTest::mouseClick(child<QPushButton>("cameraPresetRemove"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!presetItem(QStringLiteral("Totale breit")), 5000);
        QCOMPARE(m_window->statusBar()->currentMessage(), QStringLiteral("Gelöscht: Totale breit"));
        QVERIFY(!child<QPushButton>("cameraPresetRemove")->isEnabled());
    }

    // Dialog «Kamera einrichten»: Name und Grenze, «Standard», Tempo aus «Bewegen», «Vergessen»
    void setupDialog()
    {
        QTest::mouseClick(child<QPushButton>("cameraSetup"), Qt::LeftButton);
        QPointer<CameraSetupDialog> dialog;
        QTRY_VERIFY_WITH_TIMEOUT((dialog = m_window->findChild<CameraSetupDialog *>()) && dialog->isVisible(), 3000);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Kamera einrichten – OBSBOT Tiny 2"));
        auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("cameraSetupName"));
        auto *tiltMin = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("cameraSetupTiltMin"));
        QCOMPARE(name->text(), QStringLiteral("OBSBOT Tiny 2"));
        QVERIFY(tiltMin->isEnabled());
        QCOMPARE(tiltMin->text(), QStringLiteral("Standard"));
        QVERIFY(dialog->findChild<QCheckBox *>(QStringLiteral("cameraSetupAiOff"))->isChecked());
        QVERIFY(dialog->findChild<QCheckBox *>(QStringLiteral("cameraSetupAiOff"))->isEnabled());
        QCOMPARE(dialog->findChild<QSpinBox *>(QStringLiteral("cameraSetupSpeed"))->suffix(), QStringLiteral(" °/s"));

        name->setText(QStringLiteral("Kamera oben"));
        tiltMin->setValue(-50);
        qsizetype from = m_answers.size();
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("cameraSetupOk")), Qt::LeftButton);
        const Answer put = waitForAnswer(QStringLiteral("PUT"), QStringLiteral("/config"), from);
        QCOMPARE(put.result.status, 200);
        QCOMPARE(*put.call.body, (QJsonObject{{QStringLiteral("name"), QStringLiteral("Kamera oben")},
                                              {QStringLiteral("tilt_min"), -50}}));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog, 3000); // WA_DeleteOnClose
        QTRY_VERIFY_WITH_TIMEOUT(child<QComboBox>("cameraChoice")->currentText().startsWith(QStringLiteral("Kamera oben")),
                                 5000);

        // Tempo in «Bewegen» ist dieselbe Einstellung
        from = m_answers.size();
        child<QSpinBox>("cameraSpeed")->setValue(20);
        QCOMPARE(waitForAnswer(QStringLiteral("PUT"), QStringLiteral("/config"), from).result.status, 200);

        // Ein gemerkter Wert erscheint im Dialog und lässt sich vergessen
        from = m_answers.size();
        // Die Tiny 2 der Attrappe hat keine Helligkeit; der Bildwinkel wird gemerkt (docs/09)
        m_client->callCamera(cameraControlsPutCall(kTiny2, {{QStringLiteral("obsbot_fov"), 1}}));
        QCOMPARE(waitForAnswer(QStringLiteral("PUT"), QStringLiteral("/controls"), from).result.status, 200);
        QTest::mouseClick(child<QPushButton>("cameraSetup"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT((dialog = m_window->findChild<CameraSetupDialog *>()) && dialog->isVisible(), 3000);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Kamera einrichten – Kamera oben"));
        QCOMPARE(dialog->findChild<QDoubleSpinBox *>(QStringLiteral("cameraSetupTiltMin"))->value(), -50.0);
        QCOMPARE(dialog->findChild<QSpinBox *>(QStringLiteral("cameraSetupSpeed"))->value(), 20);
        auto *remembered = dialog->findChild<QTreeWidget *>(QStringLiteral("cameraRemembered"));
        QTRY_VERIFY_WITH_TIMEOUT(remembered->topLevelItemCount() >= 1, 5000);
        const auto fovItem = [&]() -> QTreeWidgetItem * {
            for (int i = 0; i < remembered->topLevelItemCount(); ++i) {
                if (remembered->topLevelItem(i)->data(0, Qt::UserRole).toString() == QStringLiteral("obsbot_fov"))
                    return remembered->topLevelItem(i);
            }
            return nullptr;
        };
        QTRY_VERIFY_WITH_TIMEOUT(fovItem() && fovItem()->text(1) == QStringLiteral("Mittel"), 5000);
        QCOMPARE(fovItem()->text(2), QStringLiteral("Weit"));
        auto *forget = dialog->findChild<QPushButton *>(QStringLiteral("cameraSetupForget"));
        QVERIFY(!forget->isEnabled());
        remembered->setCurrentItem(fovItem());
        QVERIFY(forget->isEnabled());
        QTest::mouseClick(forget, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!fovItem(), 5000);

        // Zurück auf den Standard: Name leer = Modell, Grenze «Standard» = null
        dialog->findChild<QLineEdit *>(QStringLiteral("cameraSetupName"))->clear();
        tiltMin = dialog->findChild<QDoubleSpinBox *>(QStringLiteral("cameraSetupTiltMin"));
        tiltMin->setValue(tiltMin->minimum());
        from = m_answers.size();
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("cameraSetupOk")), Qt::LeftButton);
        const Answer reset = waitForAnswer(QStringLiteral("PUT"), QStringLiteral("/config"), from);
        QCOMPARE(reset.result.status, 200);
        QCOMPARE(*reset.call.body, (QJsonObject{{QStringLiteral("name"), QJsonValue()},
                                                {QStringLiteral("tilt_min"), QJsonValue()}}));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(
            child<QComboBox>("cameraChoice")->currentText().startsWith(QStringLiteral("OBSBOT Tiny 2")), 5000);
    }

    // Bild für den Prüfer (nur mit DSS_TEST_BILDER): Tiny 2 wach mit einer Position
    void picture()
    {
        const QString dir = qEnvironmentVariable("DSS_TEST_BILDER");
        if (dir.isEmpty())
            QSKIP("DSS_TEST_BILDER nicht gesetzt");
        const qsizetype from = m_answers.size();
        m_client->callCamera(cameraPresetCreateCall(QStringLiteral("Schreibtisch"), kTiny2, true));
        QCOMPARE(waitForAnswer(QStringLiteral("POST"), QStringLiteral("camera-presets"), from).result.status, 200);
        QTRY_VERIFY_WITH_TIMEOUT(presetItem(QStringLiteral("Schreibtisch")), 5000);
        m_window->statusBar()->clearMessage();
        QTest::qWait(300);
        QVERIFY(m_window->grab().save(dir + QStringLiteral("/kameras-e2.png")));
    }

    // Schlafende Tiny 2: Kreuz, Zoom, KI grau, Hinweis mit «Wecken»; Abrufen erlaubt, Speichern nicht
    void sleepingCamera()
    {
        // Eine Position der wachen Kamera für «Abrufen» im Schlaf
        const qsizetype from = m_answers.size();
        m_client->callCamera(cameraPresetCreateCall(QStringLiteral("Nah"), kTiny2, false));
        QCOMPARE(waitForAnswer(QStringLiteral("POST"), QStringLiteral("camera-presets"), from).result.status, 200);
        QTRY_VERIFY_WITH_TIMEOUT(presetItem(QStringLiteral("Nah")), 5000);

        auto *wake = child<QPushButton>("cameraWake");
        QCOMPARE(wake->text(), QStringLiteral("Schlafen"));
        QTest::mouseClick(wake, Qt::LeftButton);
        QVERIFY(waitForTiny2(QStringLiteral("schläft")));
        QTRY_COMPARE_WITH_TIMEOUT(wake->text(), QStringLiteral("Wecken"), 3000);
        auto *hint = child<QLabel>("cameraMoveHint");
        QCOMPARE(hint->text(), QStringLiteral("Kamera schläft"));
        QCOMPARE(hint->property("state").toString(), QStringLiteral("warning"));
        QVERIFY(child<QPushButton>("cameraHintWake")->isVisible());
        QVERIFY(!crossButton(1)->isEnabled());
        QVERIFY(!child<QToolButton>("cameraZoomIn")->isEnabled());
        QVERIFY(!child<QComboBox>("cameraAi")->isEnabled());
        QCOMPARE(child<QLabel>("cameraAiHint")->text(), QStringLiteral("Nur bei wacher Kamera"));
        QVERIFY(!child<QPushButton>("cameraPresetSave")->isEnabled());
        QVERIFY(child<QSpinBox>("cameraSpeed")->isEnabled()); // Einstellung, geht auch im Schlaf
        QTreeWidgetItem *item = presetItem(QStringLiteral("Nah"));
        QVERIFY(item);
        child<QTreeWidget>("cameraPresets")->setCurrentItem(item);
        QVERIFY(child<QPushButton>("cameraPresetRecall")->isEnabled());
        QVERIFY(!child<QPushButton>("cameraPresetOverwrite")->isEnabled());
        QVERIFY(child<QPushButton>("cameraPresetRename")->isEnabled());
        QCOMPARE(child<QLabel>("cameraRecallHint")->text(), QStringLiteral("weckt die Kamera zuerst"));

        QTest::mouseClick(child<QPushButton>("cameraHintWake"), Qt::LeftButton);
        QVERIFY(waitForTiny2(QStringLiteral("wach")));
        QTRY_VERIFY_WITH_TIMEOUT(crossButton(1)->isEnabled(), 3000);
        QVERIFY(child<QLabel>("cameraMoveHint")->text().isEmpty());
    }

    // C922: Wecken/Schlafen und KI grau mit «Diese Kamera hat das nicht.», Tempo in %
    void c922()
    {
        QVERIFY(chooseCamera(kameradienst::kC922));
        auto *wake = child<QPushButton>("cameraWake");
        QVERIFY(!wake->isEnabled());
        QCOMPARE(wake->toolTip(), QStringLiteral("Diese Kamera hat das nicht."));
        auto *ai = child<QComboBox>("cameraAi");
        QVERIFY(!ai->isEnabled());
        QCOMPARE(ai->toolTip(), QStringLiteral("Diese Kamera hat das nicht."));
        QCOMPARE(child<QSpinBox>("cameraSpeed")->suffix(), QStringLiteral(" %"));
        QVERIFY(child<QToolButton>("cameraHome")->isEnabled());
        QVERIFY(chooseCamera(kTiny2));
    }

    // Dienst weg: ganzer Reiter grau, Zustand rot in zwei Zeilen, Hinweis über den Spalten
    void serviceGone()
    {
        m_service->stop();
        QTRY_VERIFY_WITH_TIMEOUT(!child<QSplitter>("cameraColumns")->isEnabled(), 10000);
        auto *state = child<QLabel>("cameraState");
        QVERIFY2(state->property("state").toString() != QStringLiteral("ok"), qPrintable(state->text()));
        QVERIFY(!child<QLabel>("cameraBlockedHint")->text().isEmpty());
        QVERIFY(!child<QPushButton>("cameraWake")->isEnabled());
        QVERIFY(!child<QPushButton>("cameraSetup")->isEnabled());
        QVERIFY(!child<QComboBox>("cameraChoice")->isEnabled());
    }
};

int main(int argc, char *argv[])
{
    if (qgetenv("XDG_SESSION_TYPE") != "x11" || qEnvironmentVariableIsEmpty("DISPLAY")) {
        qInfo("übersprungen: keine X11-Sitzung (XDG_SESSION_TYPE/DISPLAY)");
        return 77;
    }
    QApplication app(argc, argv);
    applyAppearance(darkTheme(), QStringLiteral(DSS_TEST_APP_FONT_DIR));
    TestFensterKameras test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_fensterkameras.moc"
