// Reiter «Kameras» im echten Fenster (P15 e2): MainWindow wie in dss-app, verbunden mit einem eigenen
// dss-core, der mit dem echten Kamera-Dienst in der Attrappe verbunden ist (eigener Prozess auf
// 127.0.0.1, freier Port, kameradiensthilfe.h), in der X11-Sitzung (CLAUDE.md Regel 6). Prüft
// Kopfzeile, Steuerkreuz (Klick = Schritt, Halten = Fahrt mit stop, Pfeiltasten), KI-Verfolgung,
// «Positionen» (Speichern, Abrufen, Überschreiben, Umbenennen, Löschen), ganze Presets nach dem
// Start, schlafende Kamera, C922, Dialog «Kamera einrichten» und den Verlust des Dienstes. Seit P15 e3
// auch die Mitte «Bildwerte» (Regler gebündelt, Kästchen, Auswahl, Marke «gemerkt», Kontextmenü und
// «Zurücksetzen» je Bereich) und rechts «Licht» mit Attrappen-Lampen (Dialog «Lampen», Regler,
// «Alle aus», «Licht-Presets»).
// Mit DSS_TEST_BILDER (Ordnerpfad) legt der Test kameras-e3.png, kameras-e3-c922.png,
// kameras-e3-weg.png, kameras-e3-lampen.png und kameras-e3-einrichten.png in diesem Ordner ab.
// Muster: tst_fensterchatbot (Fenster), tst_kernkamera (Kern mit Dienst).

#include "camerapage.h"
#include "camerasetupdialog.h"
#include "coreclient.h"
#include "kameradiensthilfe.h"
#include "lightsdialog.h"
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
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
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
    kameradienst::FakeLights m_lamps;
    QStringList m_lightIds; // Kennungen der angelegten Lampen (links, rechts, weg)

    // Bild für den Prüfer, nur mit DSS_TEST_BILDER (Ordnerpfad)
    static void savePicture(QWidget *w, const QString &file)
    {
        const QString dir = qEnvironmentVariable("DSS_TEST_BILDER");
        if (dir.isEmpty())
            return;
        QTest::qWait(300);
        QVERIFY(w->grab().save(dir + QLatin1Char('/') + file));
    }

    QLabel *rememberedMark(const QString &key)
    {
        for (QLabel *l : m_window->findChildren<QLabel *>(QStringLiteral("cameraRememberedMark"))) {
            if (l->property("controlKey").toString() == key)
                return l;
        }
        return nullptr;
    }

    // Text rechts in der Titelzeile eines Bereichs (section, Objektname des Bereichs)
    QLabel *sectionText(const QString &sectionName)
    {
        auto *s = m_window->findChild<QWidget *>(sectionName);
        return s ? s->findChild<QLabel *>(QStringLiteral("propertySectionText")) : nullptr;
    }

    // Kästchen über mehrere Spalten: die Mitte liegt ausserhalb von Kästchen und Text, darum links klicken
    static void clickCheck(QCheckBox *box)
    {
        QTest::mouseClick(box, Qt::LeftButton, {}, QPoint(box->height() / 2, box->height() / 2));
    }

    // Regler ziehen: drei Werte, dann loslassen (sliderReleased)
    static void drag(QSlider *slider, std::initializer_list<int> values)
    {
        slider->setSliderDown(true);
        for (int v : values)
            slider->setValue(v);
        slider->setSliderDown(false);
    }

    QTreeWidgetItem *lightPresetItem(const QString &name)
    {
        auto *tree = child<QTreeWidget>("cameraLightPresets");
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            if (tree->topLevelItem(i)->text(0) == name)
                return tree->topLevelItem(i);
        }
        return nullptr;
    }

    LightsDialog *openLightsDialog()
    {
        QTest::mouseClick(child<QPushButton>("cameraLamps"), Qt::LeftButton);
        LightsDialog *dialog = nullptr;
        if (!QTest::qWaitFor([&] { return (dialog = m_window->findChild<LightsDialog *>()) && dialog->isVisible(); },
                             3000))
            return nullptr;
        // Adressen aus GET export geladen
        if (!QTest::qWaitFor([&] { return dialog->findChild<QPushButton *>(QStringLiteral("lightsAdd"))->isEnabled(); },
                             5000))
            return nullptr;
        return dialog;
    }

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

    qsizetype countAnswers(const QString &pathEnd, qsizetype from, const QString &method = QString()) const
    {
        qsizetype n = 0;
        for (qsizetype i = from; i < m_answers.size(); ++i)
            n += m_answers.at(i).call.path.endsWith(pathEnd)
                         && (method.isEmpty() || m_answers.at(i).call.method == method)
                     ? 1
                     : 0;
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
        m_lamps.stop();
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
        // Nacharbeit e2 (Entscheide 6 und 7 des Nutzers): kein Kästchen «Presets wecken die Kamera»,
        // «Schritt» wie im Reiter
        QVERIFY(!dialog->findChild<QCheckBox *>(QStringLiteral("cameraSetupPresetsWake")));
        QStringList labels;
        for (QLabel *l : dialog->findChildren<QLabel *>())
            labels << l->text();
        QVERIFY(labels.contains(QStringLiteral("Schritt")));
        QVERIFY(!labels.contains(QStringLiteral("Schrittweite")));
        for (QCheckBox *c : dialog->findChildren<QCheckBox *>())
            QVERIFY2(!c->text().contains(QStringLiteral("Preset")), qPrintable(c->text()));

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
        savePicture(dialog, QStringLiteral("kameras-e3-einrichten.png"));
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

    // P15 e3, Mitte «Bildwerte»: feste Zeilen, Regler gebündelt, Kästchen, Auswahl, Marke «gemerkt»,
    // Kontextmenü und «Zurücksetzen» je Bereich (C922 der Attrappe; die Tiny 2 hat nur «OBSBOT»)
    void imageValues()
    {
        // Tiny 2: alle Zeilen da, ohne Wert grau mit dem Hinweis in der Titelzeile
        auto *brightness = child<QSlider>("cameraControl_brightness");
        QVERIFY(brightness && !brightness->isEnabled());
        QCOMPARE(sectionText(QStringLiteral("cameraSection_Bild"))->text(), QStringLiteral("Diese Kamera hat das nicht."));
        QVERIFY(sectionText(QStringLiteral("cameraSection_OBSBOT"))->text().isEmpty());
        QVERIFY(child<QComboBox>("cameraControl_obsbot_fov")->isEnabled());
        QVERIFY(!m_window->findChild<QWidget *>(QStringLiteral("cameraControl_obsbot_ai"))); // KI nur links
        const int rows = int(m_window->findChildren<QLabel *>(QStringLiteral("cameraRememberedMark")).size());
        QCOMPARE(rows, int(cameraControlSpecs().size()));

        QVERIFY(chooseCamera(kameradienst::kC922));
        QTRY_VERIFY_WITH_TIMEOUT(brightness->isEnabled(), 5000);
        QCOMPARE(m_window->findChildren<QLabel *>(QStringLiteral("cameraRememberedMark")).size(), rows);
        QCOMPARE(sectionText(QStringLiteral("cameraSection_OBSBOT"))->text(), QStringLiteral("Diese Kamera hat das nicht."));
        QVERIFY(!child<QComboBox>("cameraControl_obsbot_fov")->isEnabled());
        QVERIFY(!child<QToolButton>("cameraSectionReset_Bild")->isEnabled());

        // Ziehen: der erste Wert sofort, die anderen gebündelt, beim Loslassen der letzte
        qsizetype from = m_answers.size();
        drag(brightness, {100, 110, 120});
        const QString putMethod = QStringLiteral("PUT");
        QTRY_COMPARE_WITH_TIMEOUT(countAnswers(QStringLiteral("/controls"), from, putMethod), 2, 5000);
        QTest::qWait(300);
        QCOMPARE(countAnswers(QStringLiteral("/controls"), from, putMethod), 2);
        QList<int> sent;
        for (qsizetype i = from; i < m_answers.size(); ++i) {
            if (m_answers.at(i).call.method == putMethod &&m_answers.at(i).call.path.endsWith(QStringLiteral("/controls")))
                sent << m_answers.at(i).call.body->value(QStringLiteral("brightness")).toInt();
        }
        QCOMPARE(sent, (QList<int>{100, 120}));
        QTRY_VERIFY_WITH_TIMEOUT(rememberedMark(QStringLiteral("brightness"))->isVisible(), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(brightness->value(), 120, 5000);
        QVERIFY(child<QToolButton>("cameraSectionReset_Bild")->isEnabled());

        // «Zurücksetzen» in der Titelzeile: alle bedienbaren Werte des Bereichs mit null
        from = m_answers.size();
        QTest::mouseClick(child<QToolButton>("cameraSectionReset_Bild"), Qt::LeftButton);
        Answer put = waitForAnswer(QStringLiteral("PUT"), QStringLiteral("/controls"), from);
        QCOMPARE(put.result.status, 200);
        QCOMPARE(*put.call.body, (QJsonObject{{QStringLiteral("brightness"), QJsonValue()}}));
        QTRY_VERIFY_WITH_TIMEOUT(!rememberedMark(QStringLiteral("brightness"))->isVisible(), 5000);

        // Kästchen (Typ bool): Weissabgleich von Hand, danach ist die Temperatur bedienbar
        auto *autoWhite = child<QCheckBox>("cameraControl_white_balance_automatic");
        auto *temperature = child<QSlider>("cameraControl_white_balance_temperature");
        QVERIFY(autoWhite->isChecked());
        QVERIFY(!temperature->isEnabled());
        QCOMPARE(temperature->toolTip(), QStringLiteral("Bei eingeschalteter Automatik nicht einstellbar"));
        from = m_answers.size();
        clickCheck(autoWhite);
        put = waitForAnswer(QStringLiteral("PUT"), QStringLiteral("/controls"), from);
        QCOMPARE(put.result.status, 200);
        QCOMPARE(*put.call.body, (QJsonObject{{QStringLiteral("white_balance_automatic"), false}}));
        QTRY_VERIFY_WITH_TIMEOUT(temperature->isEnabled(), 5000);

        // Auswahl: Netzfrequenz «50 Hz», gemerkt; Kontextmenü «Zurücksetzen» schickt null
        auto *power = child<QComboBox>("cameraControl_power_line_frequency");
        const int fifty = power->findText(QStringLiteral("50 Hz"));
        QVERIFY(fifty >= 0);
        from = m_answers.size();
        emit power->activated(fifty);
        put = waitForAnswer(QStringLiteral("PUT"), QStringLiteral("/controls"), from);
        QCOMPARE(*put.call.body, (QJsonObject{{QStringLiteral("power_line_frequency"), 1}}));
        QTRY_VERIFY_WITH_TIMEOUT(rememberedMark(QStringLiteral("power_line_frequency"))->isVisible(), 5000);
        QCOMPARE(power->currentText(), QStringLiteral("50 Hz"));
        savePicture(m_window.get(), QStringLiteral("kameras-e3-c922.png"));
        from = m_answers.size();
        QTimer::singleShot(200, this, [] {
            if (auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                QCOMPARE(menu->actions().size(), 1);
                QCOMPARE(menu->actions().first()->text(), QStringLiteral("Zurücksetzen"));
                menu->actions().first()->trigger();
                menu->close();
            }
        });
        emit power->customContextMenuRequested(QPoint(2, 2));
        put = waitForAnswer(QStringLiteral("PUT"), QStringLiteral("/controls"), from);
        QCOMPARE(*put.call.body, (QJsonObject{{QStringLiteral("power_line_frequency"), QJsonValue()}}));
        QTRY_VERIFY_WITH_TIMEOUT(!rememberedMark(QStringLiteral("power_line_frequency"))->isVisible(), 5000);

        // Weissabgleich wieder automatisch für die folgenden Fälle
        from = m_answers.size();
        QTest::mouseClick(child<QToolButton>("cameraSectionReset_Weissabgleich"), Qt::LeftButton);
        QCOMPARE(waitForAnswer(QStringLiteral("PUT"), QStringLiteral("/controls"), from).result.status, 200);
        QTRY_VERIFY_WITH_TIMEOUT(autoWhite->isChecked(), 5000);
        QVERIFY(chooseCamera(kTiny2));
    }

    // P15 e3, Dialog «Lampen»: Hinzufügen (zwei Attrappen-Lampen und eine ohne Antwort), Verbindung
    // prüfen, Bereiche erst nach dem Dialog
    void lightsDialog()
    {
        QVERIFY(m_lamps.start(2));
        auto *allOn = child<QPushButton>("cameraLightsAllOn");
        QVERIFY(!allOn->isEnabled()); // noch keine Lampe
        QVERIFY(child<QPushButton>("cameraLamps")->isEnabled());

        QPointer<LightsDialog> dialog = openLightsDialog();
        QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Lampen"));
        auto *list = dialog->findChild<QTreeWidget *>(QStringLiteral("lightsList"));
        QCOMPARE(list->topLevelItemCount(), 0);
        auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("lightsName"));
        auto *host = dialog->findChild<QLineEdit *>(QStringLiteral("lightsHost"));
        const QStringList hosts{m_lamps.hosts().at(0), m_lamps.hosts().at(1),
                                QStringLiteral("127.0.0.1:%1").arg(kameradienst::freePort())};
        const QStringList names{QStringLiteral("Key Light links"), QStringLiteral("Key Light rechts"),
                                QStringLiteral("Lampe weg")};
        for (int i = 0; i < 3; ++i) {
            QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("lightsAdd")), Qt::LeftButton);
            QCOMPARE(name->text(), QStringLiteral("Neue Lampe"));
            QTest::keyClicks(name, names.at(i)); // ersetzt den gewählten Text
            QTest::keyClicks(host, hosts.at(i));
        }
        QCOMPARE(list->topLevelItemCount(), 3);
        QCOMPARE(list->topLevelItem(1)->text(0), names.at(1));
        QCOMPARE(list->topLevelItem(1)->text(1), hosts.at(1));
        QCOMPARE(list->topLevelItem(1)->text(2), QStringLiteral("–"));
        QVERIFY(!dialog->findChild<QPushButton *>(QStringLiteral("lightsCheck"))->isEnabled());
        QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("lightsCheckResult"))->text(), QStringLiteral("Prüfbar nach OK"));

        // Noch kein Bereich, solange der Dialog offen ist
        const qsizetype from = m_answers.size();
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("lightsOk")), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!dialog, 10000);
        m_lightIds.clear();
        for (qsizetype i = from; i < m_answers.size(); ++i) {
            if (m_answers.at(i).call.method == QStringLiteral("POST") && m_answers.at(i).call.path == QStringLiteral("lights")) {
                QCOMPARE(m_answers.at(i).result.status, 200);
                m_lightIds << m_answers.at(i).result.body.value(QStringLiteral("light")).toObject().value(QStringLiteral("id")).toString();
            }
        }
        QCOMPARE(m_lightIds.size(), 3);

        // Bereiche je Lampe nach dem Dialog; Zustand nach der Abfrage des Dienstes (alle 5 s)
        QTRY_VERIFY_WITH_TIMEOUT(m_window->findChild<QWidget *>(QStringLiteral("cameraLight_") + m_lightIds.at(2)), 5000);
        for (int i = 0; i < 2; ++i) {
            QLabel *state = sectionText(QStringLiteral("cameraLight_") + m_lightIds.at(i));
            QTRY_COMPARE_WITH_TIMEOUT(state->text(), QStringLiteral("erreichbar"), 12000);
            QCOMPARE(state->property("state").toString(), QStringLiteral("ok"));
        }
        QLabel *gone = sectionText(QStringLiteral("cameraLight_") + m_lightIds.at(2));
        QCOMPARE(gone->text(), QStringLiteral("nicht erreichbar"));
        QCOMPARE(gone->property("state").toString(), QStringLiteral("error"));
        QVERIFY(!child<QCheckBox>(qPrintable(QStringLiteral("cameraLightOn_") + m_lightIds.at(2)))->isEnabled());
        auto *on = child<QCheckBox>(qPrintable(QStringLiteral("cameraLightOn_") + m_lightIds.at(0)));
        QTRY_VERIFY_WITH_TIMEOUT(on->isEnabled() && on->isChecked(), 5000);
        QCOMPARE(child<QSlider>(qPrintable(QStringLiteral("cameraLightKelvin_") + m_lightIds.at(0)))->value(), 4500);
        QVERIFY(allOn->isEnabled());

        // Verbindung prüfen an einer gespeicherten Lampe, danach die Lampe ohne Antwort löschen
        dialog = openLightsDialog();
        QVERIFY(dialog);
        list = dialog->findChild<QTreeWidget *>(QStringLiteral("lightsList"));
        QCOMPARE(list->topLevelItemCount(), 3);
        QCOMPARE(list->topLevelItem(0)->text(1), hosts.at(0));
        QCOMPARE(list->topLevelItem(0)->text(2), QStringLiteral("erreichbar"));
        list->setCurrentItem(list->topLevelItem(0));
        auto *check = dialog->findChild<QPushButton *>(QStringLiteral("lightsCheck"));
        QVERIFY(check->isEnabled());
        QTest::mouseClick(check, Qt::LeftButton);
        auto *result = dialog->findChild<QLabel *>(QStringLiteral("lightsCheckResult"));
        QTRY_COMPARE_WITH_TIMEOUT(result->text(), QStringLiteral("✔ Lampe antwortet"), 5000);
        QCOMPARE(result->property("state").toString(), QStringLiteral("ok"));
        savePicture(dialog, QStringLiteral("kameras-e3-lampen.png"));
        list->setCurrentItem(list->topLevelItem(2));
        QTest::mouseClick(check, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(result->text(), QStringLiteral("Lampe antwortet nicht"), 5000);
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("lightsRemove")), Qt::LeftButton);
        QCOMPARE(list->topLevelItemCount(), 2);
        const qsizetype removeFrom = m_answers.size();
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("lightsOk")), Qt::LeftButton);
        const Answer removed = waitForAnswer(QStringLiteral("DELETE"), QStringLiteral("lights/") + m_lightIds.at(2), removeFrom);
        QCOMPARE(removed.result.status, 200);
        QTRY_VERIFY_WITH_TIMEOUT(!dialog, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!m_window->findChild<QWidget *>(QStringLiteral("cameraLight_") + m_lightIds.at(2)), 5000);
        m_lightIds.removeLast();
    }

    // P15 e3, rechts «Licht»: Ein, Regler gebündelt (Kelvin in Schritten von 50 K, an den Dienst in
    // Mired), «Alle aus», «Alle ein»
    void lights()
    {
        const QString id = m_lightIds.at(0);
        auto *brightness = child<QSlider>(qPrintable(QStringLiteral("cameraLightBrightness_") + id));
        qsizetype from = m_answers.size();
        drag(brightness, {50, 55, 60});
        QTRY_COMPARE_WITH_TIMEOUT(countAnswers(QStringLiteral("lights/") + id, from), 2, 5000);
        QTest::qWait(300);
        QList<int> sent;
        for (qsizetype i = from; i < m_answers.size(); ++i) {
            if (m_answers.at(i).call.path == QStringLiteral("lights/") + id) {
                QCOMPARE(m_answers.at(i).result.status, 200);
                sent << m_answers.at(i).call.body->value(QStringLiteral("brightness")).toInt();
            }
        }
        QCOMPARE(sent, (QList<int>{50, 60}));

        auto *kelvin = child<QSlider>(qPrintable(QStringLiteral("cameraLightKelvin_") + id));
        from = m_answers.size();
        kelvin->setValue(3220); // auf 3200 K gerundet
        Answer put = waitForAnswer(QStringLiteral("PUT"), QStringLiteral("lights/") + id, from);
        QCOMPARE(put.result.status, 200);
        QCOMPARE(*put.call.body, (QJsonObject{{QStringLiteral("temperature"), lightMiredFromKelvin(3200)}}));

        from = m_answers.size();
        QTest::mouseClick(child<QPushButton>("cameraLightsAllOff"), Qt::LeftButton);
        put = waitForAnswer(QStringLiteral("PUT"), QStringLiteral("lights/all"), from);
        QCOMPARE(put.result.status, 200);
        auto *on = child<QCheckBox>(qPrintable(QStringLiteral("cameraLightOn_") + id));
        QTRY_VERIFY_WITH_TIMEOUT(!on->isChecked(), 5000);
        QTest::qWait(1000);
        QVERIFY2(!on->isChecked(), "nach «Alle aus» wieder ein");
        from = m_answers.size();
        clickCheck(on);
        put = waitForAnswer(QStringLiteral("PUT"), QStringLiteral("lights/") + id, from);
        QCOMPARE(*put.call.body, (QJsonObject{{QStringLiteral("on"), true}}));
        QTRY_VERIFY_WITH_TIMEOUT(on->isChecked(), 5000);
    }

    // P15 e3, «Licht-Presets»: Speichern (Dialog nur mit Name), Abrufen, Überschreiben, Umbenennen,
    // Löschen mit Rückfrage; Spalten je Lampe
    void lightPresets()
    {
        auto *tree = child<QTreeWidget>("cameraLightPresets");
        QCOMPARE(tree->columnCount(), 3);
        QCOMPARE(tree->headerItem()->text(1), QStringLiteral("Key Light links"));
        QVERIFY(!child<QPushButton>("cameraLightPresetRecall")->isEnabled());

        QTest::mouseClick(child<QPushButton>("cameraLightPresetSave"), Qt::LeftButton);
        PresetNameDialog *dialog = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((dialog = m_window->findChild<PresetNameDialog *>()) && dialog->isVisible(), 3000);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Licht speichern"));
        QVERIFY(!dialog->findChild<QCheckBox *>(QStringLiteral("presetWithControls")));
        QTest::keyClicks(dialog->findChild<QLineEdit *>(QStringLiteral("presetName")), QStringLiteral("Stream"));
        QTest::mouseClick(dialog->findChild<QPushButton *>(QStringLiteral("presetNameOk")), Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(m_window->statusBar()->currentMessage(), QStringLiteral("Gespeichert: Stream"), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(lightPresetItem(QStringLiteral("Stream")), 5000);
        QTreeWidgetItem *item = lightPresetItem(QStringLiteral("Stream"));
        QTRY_COMPARE_WITH_TIMEOUT(item->text(1), QStringLiteral("ein · 60 % · 3200 K"), 5000);

        tree->setCurrentItem(item);
        QVERIFY(child<QPushButton>("cameraLightPresetRecall")->isEnabled());
        qsizetype from = m_answers.size();
        QTest::mouseClick(child<QPushButton>("cameraLightPresetRecall"), Qt::LeftButton);
        QCOMPARE(waitForAnswer(QStringLiteral("POST"), QStringLiteral("light-presets/stream/recall"), from).result.status, 200);
        from = m_answers.size();
        QTest::mouseClick(child<QPushButton>("cameraLightPresetOverwrite"), Qt::LeftButton);
        QCOMPARE(waitForAnswer(QStringLiteral("POST"), QStringLiteral("/save-current"), from).result.status, 200);

        QTimer::singleShot(200, this, [] {
            if (auto *input = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                input->setTextValue(QStringLiteral("Live"));
                input->accept();
            }
        });
        QTest::mouseClick(child<QPushButton>("cameraLightPresetRename"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(lightPresetItem(QStringLiteral("Live")), 5000);
        QVERIFY(lightPresetItem(QStringLiteral("Live"))->isSelected());

        QTimer::singleShot(200, this, [] {
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                QCOMPARE(box->text(), QStringLiteral("Licht-Preset «Live» löschen?"));
                for (QAbstractButton *b : box->buttons()) {
                    if (b->text() == QStringLiteral("Löschen"))
                        b->click();
                }
            }
        });
        QTest::mouseClick(child<QPushButton>("cameraLightPresetRemove"), Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!lightPresetItem(QStringLiteral("Live")), 5000);
        QCOMPARE(m_window->statusBar()->currentMessage(), QStringLiteral("Gelöscht: Live"));

        // Für das Bild ein Preset, das bleibt
        from = m_answers.size();
        m_client->callCamera(lightPresetCreateCall(QStringLiteral("Abend")));
        QCOMPARE(waitForAnswer(QStringLiteral("POST"), QStringLiteral("light-presets"), from).result.status, 200);
        QTRY_VERIFY_WITH_TIMEOUT(lightPresetItem(QStringLiteral("Abend")), 5000);
    }

    // Bild für den Prüfer (nur mit DSS_TEST_BILDER): Tiny 2 wach mit Positionen und Lampen; die
    // Fensterhöhe bleibt bei der Mindestgrösse 1280x800 (Test bildschirmfoto)
    void picture()
    {
        QVERIFY(m_window->minimumSizeHint().height() <= 800);
        QVERIFY(m_window->minimumSizeHint().width() <= 1280);
        const QString dir = qEnvironmentVariable("DSS_TEST_BILDER");
        if (dir.isEmpty())
            QSKIP("DSS_TEST_BILDER nicht gesetzt");
        const qsizetype from = m_answers.size();
        m_client->callCamera(cameraPresetCreateCall(QStringLiteral("Schreibtisch"), kTiny2, true));
        QCOMPARE(waitForAnswer(QStringLiteral("POST"), QStringLiteral("camera-presets"), from).result.status, 200);
        QTRY_VERIFY_WITH_TIMEOUT(presetItem(QStringLiteral("Schreibtisch")), 5000);
        m_window->statusBar()->clearMessage();
        savePicture(m_window.get(), QStringLiteral("kameras-e3.png"));
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
        QVERIFY(!child<QPushButton>("cameraLamps")->isEnabled());
        m_window->statusBar()->clearMessage();
        savePicture(m_window.get(), QStringLiteral("kameras-e3-weg.png"));
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
