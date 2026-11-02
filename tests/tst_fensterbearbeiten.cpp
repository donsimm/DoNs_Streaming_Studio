// Bearbeiten im echten Fenster (P08, Teile C bis E): MainWindow wie in dss-app, verbunden mit
// einem eigenen dss-core (eigener Datenordner, eigene Verbindung), in der X11-Sitzung (CLAUDE.md
// Regel 6); ohne X11 «übersprungen» (Exit-Code 77). Geklickt und getippt wird mit QTest; die
// Dialoge werden wie vom Nutzer beantwortet. Geprüft wird, was beim Kern ankommt (zweite
// Verbindung) und was das Fenster danach zeigt.
// Nicht hier: Dateidialoge (Bild, Video, Schriftdatei, Bild ersetzen) und der Farbdialog sind
// Dialoge der GTK-Plattform und lassen sich nicht fernsteuern; die Änderungen dahinter prüft
// tst_bearbeiten. Ziehen mit der Maus (QDrag) ebenso; die Rechnung prüfen tst_overlayliste und
// tst_windowedit.
// Daten wie im Handtest: Bild und Video aus prototypes/media, Be Vietnam Pro.

#include "coreclient.h"
#include "dockviews.h"
#include "fontcatalog.h"
#include "mainwindow.h"
#include "overlaystore.h"
#include "prozesshilfe.h"
#include "symbols.h"
#include "theme.h"
#include "version.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QToolButton>

#include <functional>
#include <memory>

using namespace dss;

namespace {

const QString kMediaDir = QStringLiteral(DSS_TEST_MEDIA_DIR);

// Beantwortet den nächsten modalen Dialog, sobald er offen ist (der Aufruf, der ihn öffnet,
// blockiert bis dahin).
void answerDialog(std::function<void(QWidget *)> answer)
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

// Auswahl in einer Liste wie mit der Maus: Qt setzt zuerst den Eintrag, dann kommt activated.
void choose(QComboBox *combo, int index)
{
    combo->setCurrentIndex(index);
    emit combo->activated(index);
}

// Doppelklick wie mit der Maus: Qt meldet ihn nur nach einem Klick auf denselben Eintrag
// (QAbstractItemView::mouseDoubleClickEvent prüft den zuletzt gedrückten Eintrag).
void doubleClick(QAbstractItemView *view, const QPoint &position)
{
    QTest::mouseClick(view->viewport(), Qt::LeftButton, {}, position);
    QTest::mouseDClick(view->viewport(), Qt::LeftButton, {}, position);
}

void clickButton(QWidget *dialog, const QString &text)
{
    for (QPushButton *b : dialog->findChildren<QPushButton *>()) {
        if (b->text() == text) {
            b->click();
            return;
        }
    }
    qWarning("Knopf «%s» nicht gefunden", qPrintable(text));
}

} // namespace

class TestFensterBearbeiten : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_data;
    std::unique_ptr<QTemporaryDir> m_run;
    std::unique_ptr<QProcess> m_core;
    std::unique_ptr<CoreClient> m_client; // das Fenster
    std::unique_ptr<CoreClient> m_check;  // zweite Verbindung: was beim Kern angekommen ist
    std::unique_ptr<MainWindow> m_window;
    QUuid m_scene;
    QUuid m_folder;

    template <typename T>
    T *child(const char *name)
    {
        T *w = m_window->findChild<T *>(QString::fromLatin1(name));
        if (!w)
            qWarning("%s nicht gefunden", name);
        return w;
    }

    Overlay checked(const QUuid &id)
    {
        const std::optional<Overlay> o = m_check->overlay(id);
        return o ? *o : Overlay();
    }

    // Quelle in Zeile row des Docks «Quellen» (0 = vorderste).
    Source sourceAtRow(int row)
    {
        const Overlay o = checked(m_scene);
        return o.sources.value(o.sources.size() - 1 - row);
    }

    void selectSourceRow(int row)
    {
        auto *list = child<SourceList>("sourceList");
        list->setCurrentRow(row);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        {
            OverlayStore store(m_data.path());
            QString error;
            QVERIFY(store.load(&error));
            m_scene = store.createOverlay(QStringLiteral("Test Szene"), 1920, 1080, &error);
            QVERIFY(!store.createOverlay(QStringLiteral("Chat Ecke"), 800, 600, &error).isNull());
            QVERIFY(!store.addImageSource(m_scene, kMediaDir + QStringLiteral("/bild1.png"), &error).isNull());
            QVERIFY(!store.addVideoSource(m_scene, kMediaDir + QStringLiteral("/alpha.webm"), true, &error).isNull());
            QVERIFY(!store.addTextSource(m_scene, QStringLiteral("Willkommen"), defaultTextFontFile(), &error).isNull());
            m_folder = store.createFolder(QStringLiteral("Szenen"), &error);
            QVERIFY(store.moveOverlayInList(m_scene, m_folder, 0, &error));
            QVERIFY(store.moveFolderInList(m_folder, 0, &error));
        }
        m_run = testhelp::runtimeDir();
        const QString socket = m_run->filePath(QStringLiteral("kern.sock"));
        m_core = testhelp::startCore(m_data.path(), socket);
        QString log;
        QVERIFY2(testhelp::waitForLog(m_core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        CoreClient::Options o;
        o.socketPath = socket;
        o.program = QStringLiteral("dss-app"); // wie das Fenster
        o.version = version();
        m_client = std::make_unique<CoreClient>(o);
        o.program = QStringLiteral("tst_fensterbearbeiten");
        m_check = std::make_unique<CoreClient>(o);

        const Theme theme = darkTheme();
        qApp->setStyleSheet(styleSheet(theme, symbolPath));
        m_window = std::make_unique<MainWindow>(theme, false, m_client.get());
        m_window->resize(1280, 800);
        m_window->show();
        QVERIFY(QTest::qWaitForWindowExposed(m_window.get()));
        m_client->start();
        m_check->start();
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isConnected() && m_check->isConnected(), 5000);
        m_check->requestOverlay(m_scene);
        QTRY_VERIFY_WITH_TIMEOUT(m_check->overlay(m_scene).has_value(), 3000);
    }

    void cleanupTestCase()
    {
        if (m_window)
            m_window->close(); // bittet den Kern, sich zu beenden (einziges Fenster)
        m_window.reset();
        m_client.reset();
        m_check.reset();
        if (m_core && m_core->state() != QProcess::NotRunning && !m_core->waitForFinished(3000)) {
            m_core->terminate();
            m_core->waitForFinished(3000);
        }
    }

    void treeShowsFolders()
    {
        auto *tree = child<OverlayTree>("overlayTree");
        QTRY_COMPARE_WITH_TIMEOUT(tree->topLevelItemCount(), 2, 3000);
        QTreeWidgetItem *folder = tree->topLevelItem(0);
        QCOMPARE(folder->text(0), QStringLiteral("Szenen"));
        QVERIFY(folder->data(0, OverlayTree::kFolderRole).toBool());
        QCOMPARE(folder->childCount(), 1);
        QCOMPARE(folder->child(0)->text(0), QStringLiteral("Test Szene"));
        QVERIFY(folder->isExpanded());
        QCOMPARE(tree->topLevelItem(1)->text(0), QStringLiteral("Chat Ecke"));
        // erstes Overlay der Liste ist das im Ordner
        m_window->selectFirstOverlay();
        QCOMPARE(tree->currentItem()->text(0), QStringLiteral("Test Szene"));
        auto *list = child<SourceList>("sourceList");
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 3, 3000);
        QCOMPARE(list->item(0)->text(), QStringLiteral("Text: Willkommen")); // vorderste oben
        QCOMPARE(list->item(2)->text(), QStringLiteral("bild1.png"));
        QVERIFY(child<QToolButton>("overlayAdd")->isEnabled() == bool(DSS_APP_EDITING));
    }

    void readOnlyWithoutEditing()
    {
        if (DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_EDITING ist an");
        selectSourceRow(0);
        QCOMPARE(child<QStackedWidget>("propertyPages")->currentIndex(), 3);
        for (QWidget *w : {static_cast<QWidget *>(child<QSpinBox>("propertyX")),
                           static_cast<QWidget *>(child<QLineEdit>("propertyName")),
                           static_cast<QWidget *>(child<QPlainTextEdit>("textContent")),
                           static_cast<QWidget *>(child<QToolButton>("overlayAdd")),
                           static_cast<QWidget *>(child<QToolButton>("sourceAdd"))})
            QVERIFY(!w->isEnabled());
    }

    void textProperties()
    {
        if (!DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_EDITING ist aus");
        selectSourceRow(0);
        const QUuid text = sourceAtRow(0).id;
        QCOMPARE(child<QStackedWidget>("propertyPages")->currentIndex(), 3);
        auto *family = child<QComboBox>("textFamily");
        auto *weight = child<QComboBox>("textWeight");
        QTRY_COMPARE_WITH_TIMEOUT(family->currentText(), QStringLiteral("Be Vietnam Pro"), 3000);
        QCOMPARE(weight->currentText(), QStringLiteral("Regular (400)"));
        QCOMPARE(family->itemText(family->count() - 1), QStringLiteral("Datei wählen …"));
        QVERIFY(child<QCheckBox>("textItalic")->isEnabled());

        // Lage mit Pfeiltaste und getippt
        auto *x = child<QSpinBox>("propertyX");
        QVERIFY(x->isEnabled());
        x->setFocus();
        QTest::keyClick(x, Qt::Key_Up);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().x, 1, 3000);
        x->selectAll();
        QTest::keyClicks(x, QStringLiteral("321"));
        QCOMPARE(checked(m_scene).sources.last().x, 1); // noch nicht bestätigt
        QTest::keyClick(x, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().x, 321, 3000);
        // 20 Schritte mit der Pfeiltaste: der letzte Wert kommt an
        for (int i = 0; i < 20; ++i)
            QTest::keyClick(x, Qt::Key_Up);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().x, 341, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(x->value(), 341, 3000);
        // Drehung: 359.9 Grad = 3599 Zehntel, das Feld läuft rundum
        auto *rotation = child<QDoubleSpinBox>("propertyRotation");
        rotation->setValue(359.9);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().rotation, 3599, 3000);
        rotation->setFocus();
        QTest::keyClick(rotation, Qt::Key_Up);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().rotation, 0, 3000);
        // Grösse: 0 lässt sich nicht eingeben (Minimum 1)
        auto *width = child<QSpinBox>("propertyWidth");
        QCOMPARE(width->minimum(), 1);

        // Name: leer gilt nicht, sonst übernommen
        auto *name = child<QLineEdit>("propertyName");
        name->setFocus();
        name->selectAll();
        QTest::keyClick(name, Qt::Key_Delete);
        QTest::keyClick(name, Qt::Key_Return);
        QCOMPARE(name->text(), QStringLiteral("Text: Willkommen"));
        QCOMPARE(m_window->statusBar()->currentMessage(),
                 QStringLiteral("Der Name der Quelle darf nicht leer sein."));
        // QTest::keyClicks kennt nur ASCII (qasciikey.cpp); den Umlaut als Text setzen.
        name->setText(QStringLiteral("Begrüssung"));
        QTest::keyClick(name, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().name, QStringLiteral("Begrüssung"), 3000);
        QTRY_COMPARE_WITH_TIMEOUT(child<SourceList>("sourceList")->item(0)->text(), QStringLiteral("Begrüssung"), 3000);

        // Inhalt mehrzeilig, Zeichen für Zeichen getippt
        auto *content = child<QPlainTextEdit>("textContent");
        content->setFocus();
        content->moveCursor(QTextCursor::End);
        QTest::keyClick(content, Qt::Key_Return);
        QTest::keyClicks(content, QStringLiteral("im Stream"));
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().text.content,
                                  QStringLiteral("Willkommen\nim Stream"), 3000);
        QCOMPARE(content->toPlainText(), QStringLiteral("Willkommen\nim Stream"));

        // Schnitt, kursiv, Grösse, Ausrichtung
        choose(weight, weight->findData(700));
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().text.weight, 700, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(weight->currentText(), QStringLiteral("Bold (700)"), 3000);
        QTest::mouseClick(child<QCheckBox>("textItalic"), Qt::LeftButton, {},
                          QPoint(8, child<QCheckBox>("textItalic")->height() / 2));
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().text.italic, true, 3000);
        QCOMPARE(checked(m_scene).sources.last().text.weight, 700);
        QCOMPARE(family->currentText(), QStringLiteral("Be Vietnam Pro")); // Kopie im Datenordner
        child<QSpinBox>("textSize")->setValue(96);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().text.fontSize, 96, 3000);
        auto *align = child<QComboBox>("textAlign");
        choose(align, 2);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().text.align, TextAlign::Right, 3000);
        // andere Familie: nächster Schnitt (DejaVu Sans hat kein Kursiv → aufrecht, Bold)
        choose(family, family->findData(QStringLiteral("DejaVu Sans")));
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.last().text.italic, false, 3000);
        QCOMPARE(checked(m_scene).sources.last().text.weight, 700);
        QCOMPARE(family->currentText(), QStringLiteral("DejaVu Sans"));
        QVERIFY(!child<QCheckBox>("textItalic")->isEnabled());
        QCOMPARE(checked(m_scene).sources.last().id, text);
    }

    void videoAndImagePages()
    {
        if (!DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_EDITING ist aus");
        selectSourceRow(1); // Video
        QCOMPARE(child<QStackedWidget>("propertyPages")->currentIndex(), 2);
        auto *loop = child<QCheckBox>("videoLoop");
        QVERIFY(loop->isChecked());
        QTest::mouseClick(loop, Qt::LeftButton, {}, QPoint(8, loop->height() / 2));
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(1).loop, false, 3000);
        selectSourceRow(2); // Bild
        QCOMPARE(child<QStackedWidget>("propertyPages")->currentIndex(), 1);
        QVERIFY(child<QPushButton>("imageReplace")->isEnabled());
    }

    void sourceList()
    {
        if (!DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_EDITING ist aus");
        auto *list = child<SourceList>("sourceList");
        // Auge der Zeile 1 (Video) anklicken: ausgeblendet, Auswahl bleibt
        selectSourceRow(2);
        const QUuid video = sourceAtRow(1).id;
        QStyleOptionViewItem option;
        option.initFrom(list);
        option.rect = list->visualItemRect(list->item(1));
        option.features |= QStyleOptionViewItem::HasDecoration | QStyleOptionViewItem::HasDisplay;
        option.icon = list->item(1)->icon();
        option.text = list->item(1)->text();
        option.decorationSize = list->iconSize().isValid() ? list->iconSize() : QSize(16, 16);
        const QRect eye = list->style()->subElementRect(QStyle::SE_ItemViewItemDecoration, &option, list);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, eye.center());
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(1).visible, false, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(list->item(1)->toolTip(), QStringLiteral("ausgeblendet"), 3000);
        QCOMPARE(list->currentRow(), 2);
        QTest::mouseClick(list->viewport(), Qt::LeftButton, {}, eye.center());
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(1).visible, true, 3000);

        // Bild (hinten) nach vorne, dann zurück; ganz hinten ist «nach hinten» aus
        auto *forward = child<QToolButton>("sourceForward");
        auto *backward = child<QToolButton>("sourceBackward");
        const QUuid image = sourceAtRow(2).id;
        QVERIFY(!backward->isEnabled());
        QVERIFY(forward->isEnabled());
        forward->click();
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(1).id, image, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(list->currentRow(), 1, 3000); // Auswahl folgt der Quelle
        backward->click();
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).id, image, 3000);
        QCOMPARE(sourceAtRow(1).id, video);

        // Text hinzufügen: neue Quelle vorne und gewählt
        auto *add = child<QToolButton>("sourceAdd");
        QVERIFY(add->isEnabled());
        add->menu()->actions().at(2)->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 4, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(list->currentRow(), 0, 3000);
        QCOMPARE(list->item(0)->text(), QStringLiteral("Text: Text"));
        QCOMPARE(sourceAtRow(0).text.content, QStringLiteral("Text"));
        QCOMPARE(child<QComboBox>("textFamily")->currentText(), QStringLiteral("Be Vietnam Pro"));

        // Umbenennen per Doppelklick
        answerDialog([](QWidget *dialog) {
            if (auto *input = qobject_cast<QInputDialog *>(dialog)) {
                input->setTextValue(QStringLiteral("Zweiter Text"));
                input->accept();
            }
        });
        doubleClick(list, list->visualItemRect(list->item(0)).center());
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(0).name, QStringLiteral("Zweiter Text"), 3000);

        // Entfernen: zuerst abbrechen, dann bestätigen
        answerDialog([](QWidget *dialog) { clickButton(dialog, QStringLiteral("Abbrechen")); });
        child<QToolButton>("sourceRemove")->click();
        QTest::qWait(200);
        QCOMPARE(checked(m_scene).sources.size(), 4);
        answerDialog([](QWidget *dialog) { clickButton(dialog, QStringLiteral("Entfernen")); });
        child<QToolButton>("sourceRemove")->click();
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.size(), 3, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), 3, 3000);
    }

    void overlays()
    {
        if (!DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_EDITING ist aus");
        auto *tree = child<OverlayTree>("overlayTree");
        // Anlegen mit dem Dialog (Vorgabe 1920x1080), danach gewählt
        QString proposed;
        answerDialog([&proposed](QWidget *dialog) {
            if (auto *name = dialog->findChild<QLineEdit *>()) {
                proposed = name->text();
                name->setText(QStringLiteral("Fenster Test"));
            }
            clickButton(dialog, QStringLiteral("Anlegen"));
        });
        child<QToolButton>("overlayAdd")->click();
        QTRY_COMPARE_WITH_TIMEOUT(m_check->overlays().size(), 3, 3000);
        QCOMPARE(proposed, QStringLiteral("Neues Overlay"));
        const OverlaySummary created = m_check->overlays().last();
        QCOMPARE(created.name, QStringLiteral("Fenster Test"));
        QCOMPARE(created.width, 1920);
        QCOMPARE(created.height, 1080);
        QTRY_COMPARE_WITH_TIMEOUT(tree->currentItem()->text(0), QStringLiteral("Fenster Test"), 3000);
        QCOMPARE(child<SourceList>("sourceList")->count(), 0);

        // Umbenennen: Frage nach der Adresse, Standard «behalten» (Enter)
        bool askedAddress = false;
        answerDialog([&askedAddress](QWidget *dialog) {
            if (auto *input = qobject_cast<QInputDialog *>(dialog)) {
                input->setTextValue(QStringLiteral("Fenster Neu"));
                input->accept();
            }
            answerDialog([&askedAddress](QWidget *question) {
                askedAddress = qobject_cast<QMessageBox *>(question) != nullptr;
                QTest::keyClick(question, Qt::Key_Return);
            });
        });
        doubleClick(tree, tree->visualItemRect(tree->currentItem()).center());
        QTRY_COMPARE_WITH_TIMEOUT(m_check->listData().summary(created.id)->name, QStringLiteral("Fenster Neu"), 3000);
        QVERIFY(askedAddress);
        QCOMPARE(m_check->listData().summary(created.id)->address, QStringLiteral("fenster-test"));

        // Kopieren: Kopie gewählt
        child<QToolButton>("overlayCopy")->click();
        QTRY_COMPARE_WITH_TIMEOUT(m_check->overlays().size(), 4, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(tree->currentItem()->text(0), QStringLiteral("Fenster Neu Kopie"), 3000);

        // Löschen mit Bestätigung
        answerDialog([](QWidget *dialog) { clickButton(dialog, QStringLiteral("Löschen")); });
        child<QToolButton>("overlayRemove")->click();
        QTRY_COMPARE_WITH_TIMEOUT(m_check->overlays().size(), 3, 3000);

        // Ordner zuklappen wird gespeichert
        QTreeWidgetItem *folder = tree->topLevelItem(0);
        folder->setExpanded(false);
        QTRY_COMPARE_WITH_TIMEOUT(m_check->listData().layout.folder(m_folder)->expanded, false, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!tree->topLevelItem(0)->isExpanded(), 3000);
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
    TestFensterBearbeiten test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_fensterbearbeiten.moc"
