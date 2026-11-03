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

#include "canvaswidget.h"
#include "coreclient.h"
#include "coresettings.h"
#include "dockviews.h"
#include "fontcatalog.h"
#include "headerbar.h"
#include "mainwindow.h"
#include "overlaystore.h"
#include "prozesshilfe.h"
#if DSS_APP_CANVAS_IMAGE
#include "sourcerenderer.h"
#endif
#include "symbols.h"
#include "theme.h"
#include "version.h"
#include "windowedit.h"
#include "windowsize.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QToolButton>

#include <algorithm>
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

// Maus auf der Leinwand mit Umschalttasten (QTest::mouseMove kennt keine Umschalttasten).
void sendMouse(QWidget *w, QEvent::Type type, QPointF pos, Qt::MouseButton button,
               Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers)
{
    QMouseEvent e(type, pos, w->mapToGlobal(pos), button, buttons, modifiers);
    QApplication::sendEvent(w, &e);
}

// Ziehen von a nach b (Pixel des Widgets) in einigen Schritten.
} // namespace

namespace QTest {
template <>
char *toString(const dss::SourceGeometry &g)
{
    return qstrdup(QStringLiteral("(%1, %2, %3x%4, %5, h%6 v%7)")
                       .arg(g.x).arg(g.y).arg(g.width).arg(g.height).arg(g.rotation)
                       .arg(g.flipHorizontal).arg(g.flipVertical).toUtf8().constData());
}
} // namespace QTest

namespace {

void drag(QWidget *w, QPointF a, QPointF b, Qt::KeyboardModifiers modifiers = {})
{
    sendMouse(w, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton, modifiers);
    for (int i = 1; i <= 4; ++i)
        sendMouse(w, QEvent::MouseMove, a + (b - a) * i / 4.0, Qt::NoButton, Qt::LeftButton, modifiers);
    sendMouse(w, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton, modifiers);
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

    void selectOverlayNamed(const QString &name)
    {
        auto *tree = child<OverlayTree>("overlayTree");
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->text(0) == name) {
                tree->setCurrentItem(*it);
                break;
            }
        }
        QTRY_VERIFY_WITH_TIMEOUT(child<SourceList>("sourceList")->count() > 0, 3000);
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

    // P09b, E-26: Name oben, Bereich «Transformation» mit Spiegeln, darunter der Bereich der Art
    // mit kleinem Titel und feiner Linie; Spiegeln per Kästchen kommt beim Kern an.
    void propertySections()
    {
        if (!DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_EDITING ist aus");
        selectOverlayNamed(QStringLiteral("Test Szene"));
        auto *pages = child<QStackedWidget>("propertyPages");
        auto *transform = child<QWidget>("propertyTransform");
        auto *name = child<QLineEdit>("propertyName");
        const auto titles = [](QWidget *w) {
            QStringList r;
            for (const QLabel *l : w->findChildren<QLabel *>(QStringLiteral("propertySectionTitle"),
                                                              Qt::FindDirectChildrenOnly))
                r.append(l->text());
            return r;
        };
        QCOMPARE(titles(transform), QStringList{QStringLiteral("Transformation")});
        const QStringList kinds = {QString(), QStringLiteral("Bild"), QStringLiteral("Video"),
                                   QStringLiteral("Text")};
        for (int i = 1; i < pages->count(); ++i)
            QCOMPARE(titles(pages->widget(i)), QStringList{kinds.at(i)});
        // Reihenfolge von oben: Name, Transformation, Art
        QVERIFY(name->mapTo(m_window.get(), QPoint()).y() < transform->mapTo(m_window.get(), QPoint()).y());
        QVERIFY(transform->mapTo(m_window.get(), QPoint()).y() < pages->mapTo(m_window.get(), QPoint()).y());
        auto *line = transform->findChild<QFrame *>(QStringLiteral("propertySectionLine"));
        QVERIFY(line);
        QCOMPARE(line->height(), 1);
        for (const QString &x : {QStringLiteral("propertyX"), QStringLiteral("propertyY"),
                                QStringLiteral("propertyWidth"), QStringLiteral("propertyHeight"),
                                QStringLiteral("propertyRotation"),
                                QStringLiteral("propertyFlipHorizontal"),
                                QStringLiteral("propertyFlipVertical")})
            QVERIFY2(transform->isAncestorOf(child<QWidget>(x.toLatin1().constData())), qPrintable(x));

        selectSourceRow(2); // Bild
        auto *flipH = child<QCheckBox>("propertyFlipHorizontal");
        auto *flipV = child<QCheckBox>("propertyFlipVertical");
        QVERIFY(flipH->isEnabled() && flipV->isEnabled());
        const bool before = sourceAtRow(2).flipHorizontal;
        QCOMPARE(flipH->isChecked(), before);
        QTest::mouseClick(flipH, Qt::LeftButton, {}, QPoint(8, flipH->height() / 2));
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).flipHorizontal, !before, 3000);
        QTest::mouseClick(flipV, Qt::LeftButton, {}, QPoint(8, flipV->height() / 2));
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).flipVertical, true, 3000);
        // Änderung von aussen (zweite Verbindung) zeigt das Kästchen
        SourceChanges c;
        c.flipHorizontal = before;
        c.flipVertical = false;
        m_check->sendChange(sourceUpdate(m_scene, sourceAtRow(2).id, c));
        QTRY_COMPARE_WITH_TIMEOUT(flipV->isChecked(), false, 3000);
        QCOMPARE(flipH->isChecked(), before);
    }

    // P09 Entscheid 1 (E-23): Kopfbereich über die ganze Breite, links angedockte Docks darunter.
    void headerAboveLeftDock()
    {
        const QByteArray state = m_window->saveState();
        auto *dock = child<QDockWidget>("overlaysDock");
        m_window->addDockWidget(Qt::LeftDockWidgetArea, dock);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->dockWidgetArea(dock) == Qt::LeftDockWidgetArea, 2000);
        QCoreApplication::processEvents();
        auto *header = m_window->findChild<HeaderBar *>();
        QVERIFY(header);
        const QRect headerRect(header->mapTo(m_window.get(), QPoint(0, 0)), header->size());
        const QRect dockRect(dock->mapTo(m_window.get(), QPoint(0, 0)), dock->size());
        QVERIFY2(headerRect.bottom() < dockRect.top(),
                 qPrintable(QStringLiteral("Kopf %1..%2, Dock ab %3")
                                .arg(headerRect.top()).arg(headerRect.bottom()).arg(dockRect.top())));
        QCOMPARE(m_window->menuWidget()->width(), m_window->width());
        QVERIFY(m_window->restoreState(state));
        QTRY_VERIFY_WITH_TIMEOUT(m_window->dockWidgetArea(dock) == Qt::BottomDockWidgetArea, 2000);
    }

    // P09b Entscheid 2: Leistungsanzeige 600 px (200 je Anzeige) rechts im Kopfbereich, höchstens
    // ein Drittel der Fensterbreite; zwischen Knöpfen und Anzeige bleibt Platz frei.
    void headerGaugesWidth()
    {
        const QSize before = m_window->size();
        auto *header = m_window->findChild<HeaderBar *>();
        QVERIFY(header);
        for (const auto &[windowWidth, total] : {std::pair{1920, 600}, std::pair{1280, 426}}) {
            m_window->resize(windowWidth, 800);
            const auto *cpu = header->findChild<QWidget *>(QStringLiteral("gaugeCpu"));
            const auto *ram = header->findChild<QWidget *>(QStringLiteral("gaugeRam"));
            const auto *lan = header->findChild<QWidget *>(QStringLiteral("gaugeLan"));
            const auto *left = header->findChild<QWidget *>(QStringLiteral("headerLeft"));
            QVERIFY(cpu && ram && lan && left);
            // resize() setzt die Breite sofort; die Bestätigung des Fenstermanagers für die
            // vorherige Grösse kann danach eintreffen und die Breite kurz zurücksetzen (P10a:
            // 1 von 40 Läufen, Anzeige 600 bei Sollbreite 1280). Deshalb warten, bis Fenster und
            // Anzeige zur Sollbreite passen, und danach ohne Ereignisverarbeitung prüfen.
            const auto gaugesSum = [&] { return cpu->width() + ram->width() + lan->width(); };
            QTRY_VERIFY2_WITH_TIMEOUT(m_window->width() == windowWidth && gaugesSum() == total,
                                      qPrintable(QStringLiteral("Fenster %1, Anzeige %2")
                                                     .arg(m_window->width()).arg(gaugesSum())),
                                      3000);
            QCOMPARE(cpu->width(), dss::headerGaugeWidth(total, 0));
            QCOMPARE(ram->x(), cpu->x() + cpu->width());
            QCOMPARE(lan->x(), ram->x() + ram->width());
            // rechts bündig mit dem Innenrand des Kopfbereichs (Rahmen, dann 8 px)
            QCOMPARE(lan->x() + lan->width(), header->contentsRect().right() + 1 - 8);
            QVERIFY2(left->x() + left->width() <= cpu->x(),
                     qPrintable(QStringLiteral("links bis %1, Anzeige ab %2")
                                    .arg(left->x() + left->width()).arg(cpu->x())));
            // P10a Teil A 1: Verbindungsknöpfe mittig zwischen Reitern und Anzeige (Rest rechts)
            const auto *buttons = header->findChild<QWidget *>(QStringLiteral("headerConnections"));
            QVERIFY(buttons);
            const int gapLeft = buttons->x() - (left->x() + left->width());
            const int gapRight = cpu->x() - (buttons->x() + buttons->width());
            QVERIFY2(gapLeft > 0 && (gapRight == gapLeft || gapRight == gapLeft + 1),
                     qPrintable(QStringLiteral("Abstand links %1, rechts %2").arg(gapLeft).arg(gapRight)));
            for (const QToolButton *b : buttons->findChildren<QToolButton *>())
                QCOMPARE(b->iconSize(), QSize(19, 19));
        }
        m_window->resize(before);
    }

    // P09 Entscheid 9: «Am Ende anhalten» kommt beim Kern an und ist mit «Wiederholen» ausgegraut.
    void videoHoldAtEnd()
    {
        if (!DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_EDITING ist aus");
        selectOverlayNamed(QStringLiteral("Test Szene"));
        selectSourceRow(1); // Video
        auto *loop = child<QCheckBox>("videoLoop");
        auto *hold = child<QCheckBox>("videoHoldAtEnd");
        if (!loop->isChecked()) {
            QTest::mouseClick(loop, Qt::LeftButton, {}, QPoint(8, loop->height() / 2));
            QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(1).loop, true, 3000);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!hold->isEnabled(), 3000);
        QTest::mouseClick(loop, Qt::LeftButton, {}, QPoint(8, loop->height() / 2));
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(1).loop, false, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(hold->isEnabled(), 3000);
        QVERIFY(!hold->isChecked());
        QTest::mouseClick(hold, Qt::LeftButton, {}, QPoint(8, hold->height() / 2));
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(1).holdAtEnd, true, 3000);
        QVERIFY(hold->isChecked());
    }

    // P09 Entscheid 5: Auflösung im Dock «Steuerung»; Quellen behalten ihre Pixelwerte.
    void overlayResolution()
    {
        if (!DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_EDITING ist aus");
        selectOverlayNamed(QStringLiteral("Test Szene"));
        auto *width = child<QSpinBox>("overlayWidth");
        auto *height = child<QSpinBox>("overlayHeight");
        QTRY_VERIFY_WITH_TIMEOUT(width->isEnabled() && height->isEnabled(), 3000);
        QCOMPARE(width->value(), 1920);
        QCOMPARE(height->value(), 1080);
        const QList<Source> before = checked(m_scene).sources;
        width->setFocus();
        width->selectAll();
        QTest::keyClicks(width, QStringLiteral("1280"));
        QTest::keyClick(width, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).width, 1280, 3000);
        height->setFocus();
        height->selectAll();
        QTest::keyClicks(height, QStringLiteral("720"));
        QTest::keyClick(height, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).height, 720, 3000);
        const QList<Source> after = checked(m_scene).sources;
        QCOMPARE(after.size(), before.size());
        for (qsizetype i = 0; i < after.size(); ++i) {
            QCOMPARE(after.at(i).x, before.at(i).x);
            QCOMPARE(after.at(i).width, before.at(i).width);
        }
        // Zurück, damit die übrigen Fälle 1920x1080 sehen.
        width->setValue(1920);
        height->setValue(1080);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).width, 1920, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).height, 1080, 3000);
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
    // P09 Teil C: Wählen und Bearbeiten auf der Leinwand. Lage der Quellen über die zweite
    // Verbindung, damit sie sich nicht überdecken.
    void canvasSelectAndEdit()
    {
        if (!DSS_APP_EDITING || !DSS_APP_CANVAS_EDIT)
            QSKIP("Schalter DSS_APP_EDITING oder DSS_APP_CANVAS_EDIT ist aus");
        selectOverlayNamed(QStringLiteral("Test Szene"));
        const Source image = sourceAtRow(2);
        const Source video = sourceAtRow(1);
        const Source text = sourceAtRow(0);
        const auto place = [&](const Source &s, int x, int y, int w, int h) {
            SourceChanges c;
            c.x = x;
            c.y = y;
            c.width = w;
            c.height = h;
            c.rotation = 0;
            c.flipHorizontal = false; // P09b
            c.flipVertical = false;
            m_check->sendChange(sourceUpdate(m_scene, s.id, c));
        };
        place(image, 100, 100, 400, 200);
        place(video, 1200, 600, 300, 300);
        place(text, 1500, 100, 300, 100);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.first().x, 100, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(0).x, 1500, 3000);
        CanvasWidget *canvas = m_window->canvas();
        QTRY_COMPARE_WITH_TIMEOUT(canvas->toWidget(QPointF(0, 0)) != canvas->toWidget(QPointF(1, 1)), true, 2000);
        auto *list = child<SourceList>("sourceList");
        auto *x = child<QSpinBox>("propertyX");
        const auto at = [canvas](double ox, double oy) { return canvas->toWidget(QPointF(ox, oy)).toPoint(); };

        // Klick wählt die Quelle unter dem Zeiger, wie im Dock «Quellen»
        QTest::mouseClick(canvas, Qt::LeftButton, {}, at(300, 200));
        QTRY_COMPARE_WITH_TIMEOUT(list->currentRow(), 2, 2000);
        QCOMPARE(x->value(), 100);
        QTest::mouseClick(canvas, Qt::LeftButton, {}, at(1350, 750));
        QTRY_COMPARE_WITH_TIMEOUT(list->currentRow(), 1, 2000);
        // Klick daneben hebt die Auswahl auf
        QTest::mouseClick(canvas, Qt::LeftButton, {}, at(900, 900));
        QTRY_VERIFY_WITH_TIMEOUT(list->currentItem() == nullptr, 2000);
        // Auswahl im Dock wählt auch auf der Leinwand: Ziehen am Griff der Quelle geht sofort
        selectSourceRow(2);

        // Verschieben: Weg in Pixeln des Overlays, auf ganze Pixel
        {
            const QPoint a = at(300, 200);
            const QPoint b = at(340, 230);
            const QPointF d = canvas->overlayPoint(b) - canvas->overlayPoint(a);
            drag(canvas, a, b);
            const int ex = 100 + int(std::lround(d.x()));
            const int ey = 100 + int(std::lround(d.y()));
            QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).x, ex, 3000);
            QCOMPARE(sourceAtRow(2).y, ey);
            QCOMPARE(x->value(), ex); // Eigenschaften zeigen den Wert
            place(image, 100, 100, 400, 200);
            QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).x, 100, 3000);
        }
        // Ecke ohne Shift: Verhältnis der Datei bleibt (P09b, Entscheid 3: bild1.png ist
        // 256x256, der Kasten 400x200), mit Shift frei. Der Kern schickt die Originalgrösse mit;
        // ohne DSS_MEDIA_SIZE kennt er sie nicht, dann gilt das Verhältnis des Kastens.
#if DSS_MEDIA_SIZE
        QTRY_COMPARE_WITH_TIMEOUT(m_check->mediaSizes().value(image.file), QSize(256, 256), 3000);
        const int aspectWidth = 1; // Breite : Höhe
        const int aspectHeight = 1;
        const int expectedWidth = 600;
#else
        QVERIFY(m_check->mediaSizes().isEmpty());
        const int aspectWidth = 2;
        const int aspectHeight = 1;
        const int expectedWidth = 800;
#endif
        drag(canvas, at(500, 300), at(700, 500));
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).width != 400, true, 3000);
        {
            const Source s = sourceAtRow(2);
            QCOMPARE(s.x, 100);
            QCOMPARE(s.y, 100);
            QVERIFY2(std::abs(s.width * aspectHeight - s.height * aspectWidth) <= 1,
                     qPrintable(QStringLiteral("%1x%2").arg(s.width).arg(s.height)));
            QVERIFY(std::abs(s.width - expectedWidth) <= 3);
        }
        // Text hat keine Originalgrösse: Verhältnis des Kastens beim Beginn (300x100 = 3:1)
        selectSourceRow(0);
        drag(canvas, at(1800, 200), at(1860, 260));
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(0).width != 300, true, 3000);
        {
            const Source s = sourceAtRow(0);
            QVERIFY2(std::abs(s.width - 3 * s.height) <= 1 && std::abs(s.width - 480) <= 3,
                     qPrintable(QStringLiteral("%1x%2").arg(s.width).arg(s.height)));
        }
        place(text, 1500, 100, 300, 100);
        selectSourceRow(2);
        place(image, 100, 100, 400, 200);
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).width, 400, 3000);
        drag(canvas, at(500, 300), at(700, 500), Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).width != 400, true, 3000);
        {
            const Source s = sourceAtRow(2);
            QVERIFY(std::abs(s.width - 600) <= 3);
            QVERIFY(std::abs(s.height - 400) <= 3);
        }
        // Kante: nur die Breite
        place(image, 100, 100, 400, 200);
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).width, 400, 3000);
        drag(canvas, at(500, 200), at(600, 260));
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).width != 400, true, 3000);
        QVERIFY(std::abs(sourceAtRow(2).width - 500) <= 3);
        QCOMPARE(sourceAtRow(2).height, 200);
        QCOMPARE(sourceAtRow(2).y, 100);
        // P09b: rechte Kante über die linke hinaus spiegelt waagrecht, die linke Kante bleibt
        place(image, 100, 100, 400, 200);
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).width, 400, 3000);
        drag(canvas, at(500, 200), at(0, 200));
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).flipHorizontal, true, 3000);
        {
            const Source s = sourceAtRow(2);
            QVERIFY2(std::abs(s.width - 100) <= 3 && std::abs(s.x + s.width - 100) <= 1,
                     qPrintable(QStringLiteral("x %1 Breite %2").arg(s.x).arg(s.width)));
            QCOMPARE(s.flipVertical, false);
            QCOMPARE(s.height, 200);
        }
        // Drehen am Drehgriff mit Shift: Zeiger rechts der Mitte, knapp unter ihr (90.2°) → auf
        // ganze Grad eingerastet 90° (P09b)
        place(image, 100, 100, 400, 200);
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).width, 400, 3000);
        {
            const QPointF handle = canvas->toWidget(QPointF(300, 100)) - QPointF(0, 30);
            drag(canvas, handle, at(600, 201), Qt::ShiftModifier);
            QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).rotation, 900, 3000);
            QCOMPARE(sourceAtRow(2).x, 100);
            QCOMPARE(child<QDoubleSpinBox>("propertyRotation")->value(), 90.0);
        }
        // Ohne Shift: Zehntelgrad, nicht eingerastet
        {
            const QPointF handle = canvas->toWidget(QPointF(400, 200)) + QPointF(30, 0); // gedreht: rechts
            drag(canvas, handle, at(450, 250));
            QTRY_VERIFY_WITH_TIMEOUT(sourceAtRow(2).rotation != 900, 3000);
            const int r = sourceAtRow(2).rotation;
            QVERIFY2(r > 900 && r < 1800 && r % 10 != 0, qPrintable(QString::number(r)));
        }
        // Pfeiltasten: 1 Pixel, mit Shift 10
        place(image, 100, 100, 400, 200);
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).rotation, 0, 3000);
        canvas->setFocus();
        QTest::keyClick(canvas, Qt::Key_Right);
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).x, 101, 3000);
        QTest::keyClick(canvas, Qt::Key_Down, Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).y, 110, 3000);
        QTest::keyClick(canvas, Qt::Key_Left, Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).x, 91, 3000);
        QCOMPARE(x->value(), 91);

        // Zeigerformen über Griffen
        place(image, 100, 100, 400, 200);
        QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(2).x, 100, 3000);
        const auto hover = [&](QPointF p) {
            sendMouse(canvas, QEvent::MouseMove, p, Qt::NoButton, Qt::NoButton, {});
            return canvas->cursor().shape();
        };
        QCOMPARE(hover(at(300, 200)), Qt::SizeAllCursor);
        QCOMPARE(hover(at(500, 300)), Qt::SizeFDiagCursor);
        QCOMPARE(hover(at(500, 100)), Qt::SizeBDiagCursor);
        QCOMPARE(hover(at(500, 200)), Qt::SizeHorCursor);
        QCOMPARE(hover(at(300, 300)), Qt::SizeVerCursor);
        QCOMPARE(hover(canvas->toWidget(QPointF(300, 100)) - QPointF(0, 30)), Qt::OpenHandCursor);
        QCOMPARE(hover(at(900, 900)), Qt::ArrowCursor);
    }

    // Während des Ziehens zeigen Leinwand und Eigenschaften die neue Lage, bevor losgelassen ist.
    void canvasPreviewWhileDragging()
    {
        if (!DSS_APP_EDITING || !DSS_APP_CANVAS_EDIT)
            QSKIP("Schalter DSS_APP_EDITING oder DSS_APP_CANVAS_EDIT ist aus");
        selectOverlayNamed(QStringLiteral("Test Szene"));
        selectSourceRow(2);
        CanvasWidget *canvas = m_window->canvas();
        auto *x = child<QSpinBox>("propertyX");
        const QPointF a = canvas->toWidget(QPointF(300, 200));
        const QPointF b = canvas->toWidget(QPointF(500, 200));
        sendMouse(canvas, QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton, {});
        sendMouse(canvas, QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton, {});
        QVERIFY(std::abs(x->value() - 300) <= 2);
        sendMouse(canvas, QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton, {});
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(sourceAtRow(2).x - 300) <= 2, 3000);
        QCOMPARE(x->value(), sourceAtRow(2).x);
    }

    // P09b, Entscheid 5 (E-25): Befehle «Transformieren» im Menü «Bearbeiten» und per Rechtsklick,
    // an zwei Quellen und über zwei Overlays (kopieren in «Test Szene», einfügen in «Chat Ecke»).
    void transformCommands()
    {
        if (!DSS_APP_EDITING || !DSS_APP_TRANSFORM || !DSS_APP_CANVAS_EDIT)
            QSKIP("Schalter DSS_APP_EDITING, DSS_APP_TRANSFORM oder DSS_APP_CANVAS_EDIT ist aus");
        selectOverlayNamed(QStringLiteral("Test Szene"));
        auto *menu = m_window->findChild<QMenu *>(QStringLiteral("transformMenu"));
        QVERIFY(menu);
        QStringList texts;
        for (const QAction *a : menu->actions())
            texts.append(a->isSeparator() ? QStringLiteral("-") : QString(a->text()).remove(QLatin1Char('&')));
        QCOMPARE(texts, (QStringList{
            QStringLiteral("Transformation kopieren"), QStringLiteral("Transformation einfügen"),
            QStringLiteral("Transformation zurücksetzen"), QStringLiteral("-"),
            QStringLiteral("90° im Uhrzeigersinn drehen"), QStringLiteral("90° gegen den Uhrzeigersinn drehen"),
            QStringLiteral("180° drehen"), QStringLiteral("-"), QStringLiteral("Waagrecht spiegeln"),
            QStringLiteral("Senkrecht spiegeln"), QStringLiteral("-"), QStringLiteral("Auf Leinwand einpassen"),
            QStringLiteral("Auf Leinwand strecken"), QStringLiteral("Zentrieren"),
            QStringLiteral("Senkrecht zentrieren"), QStringLiteral("Waagrecht zentrieren")}));
        const auto action = [this](const char *name) {
            QAction *a = m_window->findChild<QAction *>(QString::fromLatin1(name));
            if (!a)
                qFatal("Aktion %s fehlt", name);
            return a;
        };
        const Source image = sourceAtRow(2);
        const Source video = sourceAtRow(1);
        // Lage über die zweite Verbindung setzen und warten, bis auch das Fenster sie hat (die
        // Befehle rechnen mit dem Stand des Fensters).
        const auto place = [&](const Source &s, const QUuid &overlay, int x, int y, int w, int h, int r) {
            SourceChanges c;
            c.x = x;
            c.y = y;
            c.width = w;
            c.height = h;
            c.rotation = r;
            c.flipHorizontal = false;
            c.flipVertical = false;
            m_check->sendChange(sourceUpdate(overlay, s.id, c));
            const SourceGeometry expected{x, y, w, h, r, false, false};
            const auto inWindow = [&] {
                const std::optional<Overlay> o = m_client->overlay(overlay);
                const Source *found = o ? findSource(*o, s.id) : nullptr;
                return found && geometryOf(*found) == expected;
            };
            QTRY_VERIFY_WITH_TIMEOUT(inWindow(), 3000);
        };
        place(image, m_scene, 100, 100, 400, 200, 0);
        place(video, m_scene, 1200, 600, 300, 300, 0);
        const auto geometryAt = [this](int row) { return geometryOf(sourceAtRow(row)); };

        // Ohne Auswahl ausgegraut, Einfügen erst nach dem Kopieren
        child<SourceList>("sourceList")->setCurrentRow(-1);
        QTRY_VERIFY_WITH_TIMEOUT(!action("transformReset")->isEnabled(), 2000);
        QVERIFY(!action("transformCopy")->isEnabled());
        selectSourceRow(2);
        QTRY_VERIFY_WITH_TIMEOUT(action("transformReset")->isEnabled(), 2000);
        QVERIFY(!action("transformPaste")->isEnabled());
        QVERIFY(!child<QToolButton>("transformPasteButton")->isEnabled()); // P10a: Knopf ebenso

        // Bild: 90° im Uhrzeigersinn, linke obere Ecke des umschliessenden Kastens bleibt (100, 100)
        action("transformRotate90Clockwise")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryAt(2), (SourceGeometry{0, 200, 400, 200, 900, false, false}), 3000);
        action("transformFlipHorizontal")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryAt(2), (SourceGeometry{0, 200, 400, 200, 900, true, false}), 3000);
        action("transformCopy")->trigger();
        QVERIFY(action("transformPaste")->isEnabled());

        // Video: einfügen, dann die übrigen Befehle
        selectSourceRow(1);
        action("transformPaste")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryAt(1), (SourceGeometry{0, 200, 400, 200, 900, true, false}), 3000);
        action("transformReset")->trigger();
#if DSS_MEDIA_SIZE
        const SourceGeometry reset{0, 0, 512, 512, 0, false, false}; // alpha.webm ist 512x512
        const SourceGeometry fit{420, 0, 1080, 1080, 0, false, false};
#else
        const SourceGeometry reset{0, 0, 400, 200, 0, false, false}; // Grösse unbekannt: bleibt
        const SourceGeometry fit{0, 60, 1920, 960, 0, false, false};
#endif
        QTRY_COMPARE_WITH_TIMEOUT(geometryAt(1), reset, 3000);
        action("transformFit")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryAt(1), fit, 3000);
        action("transformStretch")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryAt(1), (SourceGeometry{0, 0, 1920, 1080, 0, false, false}), 3000);
        place(video, m_scene, 10, 20, 300, 300, 450);
        action("transformCenterVertically")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryAt(1), (SourceGeometry{10, 390, 300, 300, 450, false, false}), 3000);
        action("transformCenterHorizontally")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryAt(1), (SourceGeometry{810, 390, 300, 300, 450, false, false}), 3000);
        action("transformRotate180")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryAt(1).rotation, 2250, 3000);
        action("transformRotate90CounterClockwise")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryAt(1).rotation, 1350, 3000);
        action("transformFlipVertical")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryAt(1).flipVertical, true, 3000);
        place(video, m_scene, 1200, 600, 300, 300, 0);

        // Über zwei Overlays: Bildquelle in «Chat Ecke» (800x600), kopierte Werte einfügen
        QUuid chat;
        for (const OverlaySummary &o : m_check->overlays()) {
            if (o.name == QStringLiteral("Chat Ecke"))
                chat = o.id;
        }
        QVERIFY(!chat.isNull());
        ChangeRequest add;
        add.type = QString::fromLatin1(msg::kAddImageSource);
        add.overlayId = chat;
        add.file = kMediaDir + QStringLiteral("/bild1.png");
        m_check->sendChange(add);
        QTRY_COMPARE_WITH_TIMEOUT(checked(chat).sources.size(), qsizetype(1), 3000);
        selectOverlayNamed(QStringLiteral("Chat Ecke"));
        selectSourceRow(0);
        QTRY_VERIFY_WITH_TIMEOUT(action("transformPaste")->isEnabled(), 2000);
        action("transformPaste")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryOf(checked(chat).sources.first()),
                                  (SourceGeometry{0, 200, 400, 200, 900, true, false}), 3000);
        // Zentrieren mit der Auflösung dieses Overlays: (800 - 400) / 2, (600 - 200) / 2
        action("transformCenter")->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(geometryOf(checked(chat).sources.first()),
                                  (SourceGeometry{200, 200, 400, 200, 900, true, false}), 3000);

        // Rechtsklick auf der Leinwand wählt die Quelle darunter und zeigt «Transformieren»;
        // ebenso im Dock «Quellen».
        selectOverlayNamed(QStringLiteral("Test Szene"));
        selectSourceRow(1);
        const auto popupTexts = [](QWidget *target, QPoint position) {
            QStringList seen;
            QTimer::singleShot(300, target, [&seen] {
                if (auto *popup = qobject_cast<QMenu *>(QApplication::activePopupWidget())) {
                    for (const QAction *a : popup->actions())
                        seen.append(QString(a->text()).remove(QLatin1Char('&')));
                    popup->close();
                }
            });
            QContextMenuEvent event(QContextMenuEvent::Mouse, position, target->mapToGlobal(position));
            QApplication::sendEvent(target, &event);
            return seen;
        };
        CanvasWidget *canvas = m_window->canvas();
        // Bild: 400x200 um 90° gedreht bei (0, 200), Mitte (200, 300)
        const QStringList canvasMenu =
            popupTexts(canvas, canvas->toWidget(QPointF(200, 300)).toPoint());
        QCOMPARE(child<SourceList>("sourceList")->currentRow(), 2);
        QVERIFY2(canvasMenu.contains(QStringLiteral("Transformieren")), qPrintable(canvasMenu.join(u'|')));
        QVERIFY(canvasMenu.contains(QStringLiteral("Umbenennen …")));
        auto *list = child<SourceList>("sourceList");
        const QStringList listMenu = popupTexts(list->viewport(), list->visualItemRect(list->item(1)).center());
        QCOMPARE(list->currentRow(), 1);
        QVERIFY2(listMenu.contains(QStringLiteral("Transformieren")), qPrintable(listMenu.join(u'|')));
        place(image, m_scene, 100, 100, 400, 200, 0);
    }

    // P10a Teil A 2 und 3: Knöpfe im Bereich «Transformation» und Tastenkürzel wie OBS; die Kürzel
    // gelten mit Fokus auf der Leinwand oder im Dock «Quellen», nicht in Textfeldern.
    void transformButtonsAndShortcuts()
    {
        if (!DSS_APP_EDITING || !DSS_APP_TRANSFORM)
            QSKIP("Schalter DSS_APP_EDITING oder DSS_APP_TRANSFORM ist aus");
        m_window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
        selectOverlayNamed(QStringLiteral("Test Szene"));
        const Source image = sourceAtRow(2);
        const auto placeImage = [&](int x, int y, int w, int h) {
            SourceChanges c;
            c.x = x;
            c.y = y;
            c.width = w;
            c.height = h;
            c.rotation = 0;
            c.flipHorizontal = false;
            c.flipVertical = false;
            // Auf die Antwort des Kerns warten: sonst kann die Änderung erst nach einer Änderung
            // des Fensters abgehen und sie überschreiben.
            bool answered = false;
            const qint64 id = m_check->sendChange(sourceUpdate(m_scene, image.id, c));
            const QMetaObject::Connection connection = connect(
                m_check.get(), &CoreClient::changeFinished, this,
                [&](const ChangeResult &r) { answered = answered || r.requestId == id; });
            QTRY_VERIFY_WITH_TIMEOUT(answered, 3000);
            disconnect(connection);
            const SourceGeometry expected{x, y, w, h, 0, false, false};
            QTRY_VERIFY_WITH_TIMEOUT(geometryOf(*findSource(*m_client->overlay(m_scene), image.id))
                                         == expected, 3000);
        };
        placeImage(100, 100, 400, 200);
        const auto expectedAfter = [&](dss::TransformCommand command) {
            const Source *s = findSource(*m_client->overlay(m_scene), image.id);
            return dss::transformGeometry(*s, command, QSize(1920, 1080),
                                          dss::originalSize(*s, m_client->mediaSizes()));
        };
        const auto action = [this](const char *name) {
            return m_window->findChild<QAction *>(QString::fromLatin1(name));
        };

        // Kürzel wie OBS im Menü
        QCOMPARE(action("transformCopy")->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
        QCOMPARE(action("transformPaste")->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V));
        QCOMPARE(action("transformReset")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_R));
        QCOMPARE(action("transformFit")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_F));
        QCOMPARE(action("transformStretch")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_S));
        QCOMPARE(action("transformCenter")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_D));

        // Knöpfe: rechts in der Titelzeile, Hinweis mit Kürzel, Einfügen erst nach dem Kopieren
        auto *copy = child<QToolButton>("transformCopyButton");
        auto *paste = child<QToolButton>("transformPasteButton");
        auto *reset = child<QToolButton>("transformResetButton");
        auto *section = child<QWidget>("propertyTransform");
        QVERIFY(copy->isVisible() && paste->isVisible() && reset->isVisible());
        // Klicks gehen über das Fenster (Qt 6), die Knöpfe müssen im sichtbaren Teil liegen.
        child<QScrollArea>("propertiesScroll")->ensureWidgetVisible(reset);
        QTRY_COMPARE_WITH_TIMEOUT(QApplication::widgetAt(reset->mapToGlobal(reset->rect().center())),
                                  static_cast<QWidget *>(reset), 2000);
        QVERIFY(copy->x() < paste->x() && paste->x() < reset->x());
        QCOMPARE(reset->x() + reset->width(), section->contentsRect().right() + 1 - 8);
        QCOMPARE(copy->toolTip(), QStringLiteral("Transformation kopieren (%1)")
                                      .arg(action("transformCopy")->shortcut().toString(QKeySequence::NativeText)));
        QVERIFY(paste->toolTip().startsWith(QStringLiteral("Transformation einfügen (")));
        QVERIFY(reset->toolTip().startsWith(QStringLiteral("Transformation zurücksetzen (")));
        child<SourceList>("sourceList")->setCurrentRow(-1);
        QTRY_VERIFY_WITH_TIMEOUT(!copy->isEnabled() && !reset->isEnabled(), 2000);
        selectSourceRow(2);
        QTRY_VERIFY_WITH_TIMEOUT(copy->isEnabled() && reset->isEnabled(), 2000);
        // Einfügen folgt dem Befehl (ausgegraut ohne Kopie, geprüft in transformCommands)
        QCOMPARE(paste->isEnabled(), action("transformPaste")->isEnabled());
        QTest::mouseClick(copy, Qt::LeftButton);
        QVERIFY(paste->isEnabled());
        SourceGeometry copied = geometryOf(sourceAtRow(2));
        QCOMPARE(copied, (SourceGeometry{100, 100, 400, 200, 0, false, false}));
        const SourceGeometry resetExpected = expectedAfter(dss::TransformCommand::Reset);
        QTest::mouseClick(reset, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(geometryOf(sourceAtRow(2)), resetExpected, 3000);
        QTest::mouseClick(paste, Qt::LeftButton);
        QTRY_COMPARE_WITH_TIMEOUT(geometryOf(sourceAtRow(2)), copied, 3000);

        // Kürzel mit Fokus auf der Leinwand
        CanvasWidget *canvas = m_window->canvas();
        canvas->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(canvas->hasFocus(), 2000);
        SourceGeometry expected = expectedAfter(dss::TransformCommand::CenterOnCanvas);
        QTest::keyClick(canvas, Qt::Key_D, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(geometryOf(sourceAtRow(2)), expected, 3000);
        expected = expectedAfter(dss::TransformCommand::StretchToCanvas);
        QTest::keyClick(canvas, Qt::Key_S, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(geometryOf(sourceAtRow(2)), expected, 3000);
        expected = expectedAfter(dss::TransformCommand::FitToCanvas);
        QTest::keyClick(canvas, Qt::Key_F, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(geometryOf(sourceAtRow(2)), expected, 3000);
        // Kopieren mit Strg+Umschalt+C, dann zurücksetzen mit Strg+R
        QTest::keyClick(canvas, Qt::Key_C, Qt::ControlModifier | Qt::ShiftModifier);
        copied = expected;
        expected = expectedAfter(dss::TransformCommand::Reset);
        QTest::keyClick(canvas, Qt::Key_R, Qt::ControlModifier);
        QTRY_COMPARE_WITH_TIMEOUT(geometryOf(sourceAtRow(2)), expected, 3000);

        // Kürzel mit Fokus im Dock «Quellen»: Einfügen mit Strg+Umschalt+V
        auto *list = child<SourceList>("sourceList");
        list->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(list->hasFocus(), 2000);
        QTest::keyClick(list, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(geometryOf(sourceAtRow(2)), copied, 3000);
        QCOMPARE(list->currentRow(), 2);

        // In Textfeldern wirken die Kürzel nicht (Name, Zahlenfeld X)
        placeImage(100, 100, 400, 200);
        for (QWidget *field : {static_cast<QWidget *>(child<QLineEdit>("propertyName")),
                               static_cast<QWidget *>(child<QSpinBox>("propertyX"))}) {
            field->setFocus();
            QTRY_VERIFY_WITH_TIMEOUT(field->hasFocus(), 2000);
            for (Qt::Key key : {Qt::Key_D, Qt::Key_S, Qt::Key_F, Qt::Key_R})
                QTest::keyClick(field, key, Qt::ControlModifier);
            QTest::keyClick(field, Qt::Key_V, Qt::ControlModifier | Qt::ShiftModifier);
        }
        QTest::qWait(500);
        QCOMPARE(geometryOf(*findSource(checked(m_scene), image.id)),
                 (SourceGeometry{100, 100, 400, 200, 0, false, false}));
    }

#if DSS_APP_CANVAS_IMAGE
    // P10-vor, Teil B 2: Datei → Leinwand als Bild speichern, Kürzel Strg+Umschalt+B, ohne
    // gewähltes Overlay ausgegraut; Meldung mit dem Dateinamen in der Statuszeile.
    void canvasImage()
    {
        QTemporaryDir pictures;
        const QString dir = pictures.filePath(QStringLiteral("DoNs Streaming Studio"));
        m_window->setCanvasImageDir(dir); // nie ~/Bilder im Test
        auto *action = child<QAction>("canvasImageAction");
        QVERIFY(action);
        QCOMPARE(action->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+B")));
        QMenu *file = nullptr;
        for (QMenu *m : m_window->findChildren<QMenu *>())
            if (m->title() == QStringLiteral("&Datei"))
                file = m;
        QVERIFY(file && file->actions().contains(action));
        QCOMPARE(action->text(), QStringLiteral("Leinwand als &Bild speichern"));

        // Ohne Overlay ausgegraut: Ordner «Szenen» wählen
        auto *tree = child<OverlayTree>("overlayTree");
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->text(0) == QStringLiteral("Szenen"))
                tree->setCurrentItem(*it);
        QTRY_VERIFY_WITH_TIMEOUT(!action->isEnabled(), 3000);
        QTest::keyClick(m_window->canvas(), Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier);
        QTest::qWait(200);
        QVERIFY(!QDir(dir).exists());

        selectOverlayNamed(QStringLiteral("Test Szene"));
        QTRY_VERIFY_WITH_TIMEOUT(action->isEnabled(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(m_window->renderer()->imagesLoaded(), 5000);
        // Kürzel mit Fokus auf der Leinwand
        m_window->canvas()->setFocus();
        QTRY_VERIFY_WITH_TIMEOUT(m_window->canvas()->hasFocus(), 2000);
        QTest::keyClick(m_window->canvas(), Qt::Key_B, Qt::ControlModifier | Qt::ShiftModifier);
        QTRY_COMPARE_WITH_TIMEOUT(QDir(dir).entryList(QDir::Files).size(), 1, 3000);
        const QString name = QDir(dir).entryList(QDir::Files).first();
        QVERIFY2(QRegularExpression(QStringLiteral("\\Atest-szene_\\d{4}-\\d\\d-\\d\\d_\\d\\d-\\d\\d-\\d\\d\\.png\\z"))
                     .match(name).hasMatch(),
                 qPrintable(name));
        QVERIFY2(m_window->statusBar()->currentMessage().contains(name),
                 qPrintable(m_window->statusBar()->currentMessage()));
        const QImage image(QDir(dir).filePath(name));
        QCOMPARE(image.size(), QSize(1920, 1080)); // Auflösung des Overlays, nicht der Leinwand
        QVERIFY(image.hasAlphaChannel());
        QCOMPARE(qAlpha(image.pixel(1919, 1079)), 0);
        // Menüeintrag wirkt ebenso
        action->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(QDir(dir).entryList(QDir::Files).size(), 2, 3000);
    }
#endif

#if DSS_APP_SETTINGS_DIALOG
    // P10-vor, Teil B 1: Werkzeuge → Einstellungen … liest die Werte vom Kern, schickt Änderungen
    // als Nachricht und zeigt die Meldung des Kerns.
    void settingsDialog()
    {
        auto *action = child<QAction>("settingsAction");
        QVERIFY(action);
        QVERIFY(action->isEnabled());
        QMenu *tools = nullptr;
        for (QMenu *m : m_window->findChildren<QMenu *>())
            if (m->title() == QStringLiteral("&Werkzeuge"))
                tools = m;
        QVERIFY(tools && tools->isEnabled() && tools->actions().contains(action));
        QCOMPARE(action->text(), QStringLiteral("&Einstellungen …"));

        action->trigger();
        QDialog *dialog = nullptr;
        QTRY_VERIFY_WITH_TIMEOUT((dialog = m_window->findChild<QDialog *>(QStringLiteral("settingsDialog")))
                                     && dialog->isVisible(), 3000);
        auto *port = dialog->findChild<QSpinBox *>(QStringLiteral("settingsPort"));
        auto *address = dialog->findChild<QLineEdit *>(QStringLiteral("settingsAddress"));
        auto *problem = dialog->findChild<QLabel *>(QStringLiteral("settingsProblem"));
        auto *ok = dialog->findChild<QPushButton *>(QStringLiteral("settingsOk"));
        auto *hint = dialog->findChild<QLabel *>(QStringLiteral("settingsHint"));
        QVERIFY(port && address && problem && ok && hint);
        QVERIFY(hint->text().contains(QStringLiteral("erst mit der Browserquelle (P10)")));
        // Werte des Kerns: Standard der Version und die erkannte Adresse
        QTRY_VERIFY_WITH_TIMEOUT(ok->isEnabled(), 3000);
        QCOMPARE(port->value(), defaultCoreSettings().port);
        QCOMPARE(address->text(), detectHomeAddress());
        QCOMPARE(port->minimum(), 1024);
        QCOMPARE(port->maximum(), 32767);

        // Ungültige Adresse: Meldung des Kerns im Dialog, Dialog bleibt offen, nichts geändert
        const int problemHeight = problem->height();
        address->setText(QStringLiteral("192.168.1"));
        QTest::mouseClick(ok, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(problem->text().contains(QStringLiteral("Adresse «192.168.1» ungültig")), 3000);
        QVERIFY(dialog->isVisible());
        QCOMPARE(problem->height(), problemHeight); // Meldung ändert das Layout nicht
        QVERIFY(ok->isEnabled());

        // Gültig: Kern schreibt, alle Verbindungen bekommen die neuen Werte, Dialog schliesst
        port->setValue(9000);
        address->setText(QStringLiteral("10.0.0.2"));
        QTest::mouseClick(ok, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->isVisible(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(m_check->settings()
                                     && m_check->settings()->settings
                                            == (CoreSettings{9000, QStringLiteral("10.0.0.2")}),
                                 3000);
        QVERIFY(m_client->settings()->file.endsWith(QStringLiteral("/kern.ini")));

        // Wieder öffnen zeigt den neuen Stand; Abbrechen ändert nichts
        action->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(dialog->isVisible() && port->value() == 9000, 3000);
        QVERIFY(problem->text().isEmpty());
        port->setValue(9100);
        clickButton(dialog, QStringLiteral("Abbrechen"));
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->isVisible(), 3000);
        QTest::qWait(300);
        QCOMPARE(m_check->settings()->settings.port, 9000);
    }
#endif

    // P10-vor, Teil A 1 (Punkt 67): Während der Kern eine grosse Datei übernimmt, bleibt das
    // Fenster bedienbar: Hinweis in der Statuszeile, Änderungen kommen an, der Kern antwortet.
    // Grösse 1 GiB (Auftrag); DSS_TEST_GROSS_MIB setzt eine andere (Wiederholungsläufe). Die Datei
    // (Zufallszahlen mit festem Startwert) liegt im Bauordner auf der Platte und wird
    // wiederverwendet; die Kopie des Kerns landet im Datenordner des Tests.
    void usableWhileImportingLargeFile()
    {
        if (!DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_EDITING ist aus");
        const qint64 mib = qEnvironmentVariableIsSet("DSS_TEST_GROSS_MIB")
                               ? qEnvironmentVariableIntValue("DSS_TEST_GROSS_MIB") : 1024;
        QVERIFY(mib > 0);
        QVERIFY(QDir().mkpath(QStringLiteral(DSS_TEST_LARGE_DIR)));
        const QString big = QStringLiteral(DSS_TEST_LARGE_DIR "/gross-%1mib.png").arg(mib);
        if (QFileInfo(big).size() != mib << 20) {
            QFile f(big);
            QVERIFY(f.open(QIODevice::WriteOnly));
            QRandomGenerator random(67);
            QByteArray chunk(1 << 20, Qt::Uninitialized);
            for (qint64 i = 0; i < mib; ++i) {
                random.fillRange(reinterpret_cast<quint32 *>(chunk.data()), chunk.size() / 4);
                QCOMPARE(f.write(chunk), chunk.size());
            }
        }
        selectOverlayNamed(QStringLiteral("Test Szene"));
        const qsizetype sourcesBefore = checked(m_scene).sources.size();
        selectSourceRow(0);
        auto *x = child<QSpinBox>("propertyX");
        auto *importStatus = child<QLabel>("importStatus");
        QVERIFY(x && importStatus);

        // Ereignisschleife des Fensters: grösste Lücke zwischen zwei Takten von 10 ms
        QElapsedTimer tick;
        qint64 maxGap = 0;
        QTimer ticker;
        ticker.setInterval(10);
        connect(&ticker, &QTimer::timeout, this, [&] {
            maxGap = std::max(maxGap, tick.restart());
        });
        tick.start();
        ticker.start();

        QSignalSpy finished(m_client.get(), &CoreClient::changeFinished);
        ChangeRequest add;
        add.type = QString::fromLatin1(msg::kAddImageSource);
        add.overlayId = m_scene;
        add.file = big;
        QElapsedTimer importTimer;
        importTimer.start();
        const qint64 id = m_client->sendChange(add);
        QVERIFY(id > 0);
        QTRY_VERIFY_WITH_TIMEOUT(importStatus->text().contains(QFileInfo(big).fileName()), 2000);

        QList<qint64> replies;   // Antwortzeit des Kerns auf get_overlay_list (ms)
        QList<qint64> edits;     // Pfeiltaste im Feld X bis zur Ankunft beim Kern (ms)
        QSignalSpy lists(m_check.get(), &CoreClient::overlayListChanged);
        while (!m_client->pendingImports().isEmpty() && importTimer.elapsed() < 300000) {
            lists.clear();
            QElapsedTimer t;
            t.start();
            m_check->requestOverlayList();
            QVERIFY(lists.wait(2000));
            replies.append(t.elapsed());
            if (replies.size() % 5 == 1 && !m_client->pendingImports().isEmpty()) {
                const int before = sourceAtRow(0).x;
                x->setFocus();
                t.restart();
                QTest::keyClick(x, Qt::Key_Up);
                QTRY_COMPARE_WITH_TIMEOUT(sourceAtRow(0).x, before + 1, 2000);
                edits.append(t.elapsed());
            }
            QTest::qWait(100);
        }
        const qint64 importMs = importTimer.elapsed();
        ticker.stop();
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(finished.cbegin(), finished.cend(), [id](const QList<QVariant> &a) {
                                     return a.at(0).value<ChangeResult>().requestId == id;
                                 }), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.size(), sourcesBefore + 1, 5000);
        QVERIFY(importStatus->text().isEmpty());
        std::sort(replies.begin(), replies.end());
        std::sort(edits.begin(), edits.end());
        qInfo("Übernehmen %lld MiB: %lld ms; %lld Antworten des Kerns min %lld / Median %lld / max %lld ms; "
              "%lld Änderungen im Fenster max %lld ms; grösste Lücke der Ereignisschleife %lld ms",
              static_cast<long long>(mib), static_cast<long long>(importMs),
              static_cast<long long>(replies.size()), static_cast<long long>(replies.value(0)),
              static_cast<long long>(replies.value(replies.size() / 2)),
              static_cast<long long>(replies.value(replies.size() - 1)),
              static_cast<long long>(edits.size()), static_cast<long long>(edits.value(edits.size() - 1)),
              static_cast<long long>(maxGap));
        QVERIFY2(replies.size() >= 3, "Übernehmen zu kurz für eine Messung");
        QVERIFY(!edits.isEmpty());
        QVERIFY2(replies.last() < 500, qPrintable(QString::number(replies.last())));
        QVERIFY2(maxGap < 500, qPrintable(QString::number(maxGap)));
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
