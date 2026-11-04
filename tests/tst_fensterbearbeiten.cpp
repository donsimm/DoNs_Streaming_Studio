// Bearbeiten im echten Fenster (P08, Teile C bis E): MainWindow wie in dss-app, verbunden mit
// einem eigenen dss-core (eigener Datenordner, eigene Verbindung), in der X11-Sitzung (CLAUDE.md
// Regel 6); ohne X11 «übersprungen» (Exit-Code 77). Geklickt und getippt wird mit QTest; die
// Dialoge werden wie vom Nutzer beantwortet. Geprüft wird, was beim Kern ankommt (zweite
// Verbindung) und was das Fenster danach zeigt.
// Nicht hier: Dateidialoge (Bild, Video, Schriftdatei, Bild ersetzen) und der Farbdialog sind
// Dialoge der GTK-Plattform und lassen sich nicht fernsteuern; die Änderungen dahinter prüft
// tst_bearbeiten. Ziehen in den Docks «Overlays» und «Quellen» ebenso; die Rechnung prüfen
// tst_overlayliste und tst_windowedit.
// P10b: Dock «Medien» in beiden Lagen, Umbenennen, Ordner, Hinzufügen (mit dem Dateidialog von Qt
// statt GTK, Qt::AA_DontUseNativeDialogs), Fortschritt und Abbrechen, Löschen. Ziehen aus dem Dock
// mit QDrag wie mit der Maus (dragWithMouse): zwischen Ordnern, auf die Leinwand, in die Liste
// «Quellen». Dazu muss das Fenster an der Stelle zuoberst auf dem Bildschirm liegen.
// Daten wie im Handtest: Bild und Video aus prototypes/media, Be Vietnam Pro.

#include "browsersource.h"
#include "canvaswidget.h"
#include "coreclient.h"
#include "configpaths.h"
#include "coresettings.h"
#include "dockviews.h"
#include "fontcatalog.h"
#include "headerbar.h"
#include "mainwindow.h"
#include "overlaystore.h"
#include "prozesshilfe.h"
#include "settingsdialog.h"
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
#include <QClipboard>
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
#include <QFileDialog>
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
#include <QHeaderView>
#include <QSettings>
#include <QTcpServer>
#include <QWindow>
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

// P10b: Ziehen mit der Maus wie der Nutzer, mit echtem QDrag. Drücken und Bewegen auf dem
// Fenster lassen die Ansicht das Ziehen beginnen; QDrag::exec läuft in einer eigenen Schleife und
// kehrt erst nach dem Loslassen zurück. Darin bewegt ein Zeitgeber die Maus zum Ziel und lässt
// los. Die Ereignisse gehen über QTest mit QWindow (QWindowSystemInterface) wie vom Fenstersystem;
// das Ziel sucht Qt am Bildschirm (xcb), das Fenster muss dort also zuoberst liegen.
// https://doc.qt.io/qt-6.8/qdrag.html#exec, https://doc.qt.io/qt-6.8/qtest.html#mouseMove-1
bool dragWithMouse(QWidget *from, QPoint fromPos, QWidget *to, QPoint toPos)
{
    QWindow *window = from->window()->windowHandle();
    const QPoint a = from->mapTo(from->window(), fromPos);
    const QPoint b = to->mapTo(to->window(), toPos);
    bool released = false;
    QTimer::singleShot(200, window, [&] {
        for (int i = 1; i <= 6; ++i) {
            QTest::mouseMove(window, a + (b - a) * i / 6);
            QTest::qWait(30);
        }
        QTest::qWait(80);
        QTest::mouseRelease(window, Qt::LeftButton, {}, b);
        released = true;
    });
    QTest::mousePress(window, Qt::LeftButton, {}, a);
    QTest::mouseMove(window, a + QPoint(QApplication::startDragDistance() + 4, 0));
    QTest::mouseMove(window, a + QPoint(QApplication::startDragDistance() + 8, 2));
    const QDeadlineTimer deadline(5000);
    while (!released && !deadline.hasExpired())
        QTest::qWait(20);
    return released;
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

    // P10b: Änderung über die zweite Verbindung und ihr Ergebnis
    ChangeResult checkChange(const ChangeRequest &r)
    {
        QSignalSpy spy(m_check.get(), &CoreClient::changeFinished);
        const qint64 id = m_check->sendChange(r);
        QDeadlineTimer deadline(30000);
        while (!deadline.hasExpired()) {
            for (const QList<QVariant> &args : std::as_const(spy)) {
                if (args.at(0).value<ChangeResult>().requestId == id)
                    return args.at(0).value<ChangeResult>();
            }
            spy.wait(50);
        }
        return {};
    }

    // Eintrag im Dock «Medien» mit diesem Namen (Spalte Name), nullptr wenn keiner
    QTreeWidgetItem *mediaItem(const QString &name)
    {
        for (QTreeWidgetItemIterator it(child<MediaTree>("mediaTree")); *it; ++it) {
            if ((*it)->text(MediaTree::NameColumn) == name)
                return *it;
        }
        return nullptr;
    }

    const MediaFileData *checkedMedia(const QString &name)
    {
        for (const MediaFileData &f : m_check->mediaList().files) {
            if (f.entry.name == name)
                return &f;
        }
        return nullptr;
    }

    // Fenster und zweite Verbindung haben dieselbe Medienliste (das Fenster hat neu gefüllt)
    bool mediaSynced()
    {
        return m_client->mediaList() == m_check->mediaList() && m_client->imports() == m_check->imports();
    }

    const MediaFolder *checkedFolder(const QString &name)
    {
        for (const MediaFolder &f : m_check->mediaList().folders) {
            if (f.name == name)
                return &f;
        }
        return nullptr;
    }

    // Grosse Datei für Übernahmen, die einige Sekunden dauern (P10-vor, Punkt 67), einmal im
    // Bau-Ordner erzeugt (zufälliger Inhalt, Endung .png; der Kern prüft beim Übernehmen nur die
    // Endung).
    QString largeFile()
    {
        const qint64 mib = qEnvironmentVariableIsSet("DSS_TEST_GROSS_MIB")
                               ? qEnvironmentVariableIntValue("DSS_TEST_GROSS_MIB") : 1024;
        if (mib <= 0 || !QDir().mkpath(QStringLiteral(DSS_TEST_LARGE_DIR)))
            return {};
        const QString big = QStringLiteral(DSS_TEST_LARGE_DIR "/gross-%1mib.png").arg(mib);
        if (QFileInfo(big).size() != mib << 20) {
            QFile f(big);
            if (!f.open(QIODevice::WriteOnly))
                return {};
            QRandomGenerator random(67);
            QByteArray chunk(1 << 20, Qt::Uninitialized);
            for (qint64 i = 0; i < mib; ++i) {
                random.fillRange(reinterpret_cast<quint32 *>(chunk.data()), chunk.size() / 4);
                if (f.write(chunk) != chunk.size())
                    return {};
            }
        }
        return big;
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

    void sourceSelectionFollowsMark()
    {
        // P10b: Gewählt ist die markierte Zeile, nicht die aktuelle. In 6 von 50 Läufen war nach
        // dem Löschen der gewählten Quelle Zeile 0 aktuell, aber nicht markiert; ein Markieren
        // dieser Zeile (wie ein Klick) zeigte dann keine Eigenschaften.
        if (!DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_EDITING ist aus");
        auto *list = child<SourceList>("sourceList");
        auto *name = child<QLineEdit>("propertyName");
        selectOverlayNamed(QStringLiteral("Test Szene"));
        QVERIFY(list->count() >= 2);
        selectSourceRow(1);
        QCOMPARE(name->text(), sourceAtRow(1).name);
        // Zeile 0 markieren, Zeile 1 bleibt aktuell
        list->selectionModel()->select(list->model()->index(0, 0), QItemSelectionModel::ClearAndSelect);
        QCOMPARE(list->currentRow(), 1);
        QCOMPARE(name->text(), sourceAtRow(0).name);
        // Nichts markiert: keine Quelle gewählt
        list->clearSelection();
        QVERIFY(name->text().isEmpty());
        selectSourceRow(0);
        QCOMPARE(name->text(), sourceAtRow(0).name);
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
    // P10, Teil C: Feld «Browserquelle», «Kopieren», «Neuer Token», Schalter «Ausgabe» in der
    // Liste, Anzeige «Ausgabe: …», ohne Overlay ausgegraut, Meldung «Port belegt» sichtbar.
    void browserSource()
    {
        if (!DSS_APP_BROWSER_SOURCE || !DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_BROWSER_SOURCE oder DSS_APP_EDITING ist aus");
        auto *tree = child<OverlayTree>("overlayTree");
        auto *field = child<QLineEdit>("browserSource");
        auto *copy = child<QPushButton>("browserSourceCopy");
        auto *newToken = child<QPushButton>("browserSourceNewToken");
        auto *state = child<QLabel>("outputState");
        auto *message = child<QLabel>("serverMessage");
        auto *status = child<QLabel>("serverStatus");
        QVERIFY(field && copy && newToken && state && message && status);
        const auto itemNamed = [tree](const QString &name) -> QTreeWidgetItem * {
            for (QTreeWidgetItemIterator it(tree); *it; ++it)
                if ((*it)->text(0) == name)
                    return *it;
            return nullptr;
        };
        selectOverlayNamed(QStringLiteral("Test Szene"));
        m_client->requestSettings();
        QTRY_VERIFY_WITH_TIMEOUT(m_client->settings().has_value(), 3000);
        const int port = m_client->settings()->settings.port;
        QTRY_VERIFY_WITH_TIMEOUT(field->text().contains(QStringLiteral(":%1/o/test-szene/").arg(port)), 3000);
        QVERIFY(field->text().startsWith(QStringLiteral("http://")));
        QVERIFY(field->isEnabled() && copy->isEnabled() && newToken->isEnabled());
#if DSS_CORE_OVERLAY_SERVER
        QVERIFY(message->text().isEmpty());
        QVERIFY(status->text().isEmpty());
#else
        // Kern ohne Server: Hinweis im Dock und in der Statuszeile; Adresse und Token gehen trotzdem.
        QTRY_VERIFY_WITH_TIMEOUT(message->text().contains(QStringLiteral("ohne Overlay-Server")), 3000);
        QVERIFY(status->text().contains(QStringLiteral("ohne Overlay-Server")));
#endif

        // Kopieren in die Zwischenablage (Inhalt des Nutzers danach zurück)
        QClipboard *clipboard = QGuiApplication::clipboard();
        const QString saved = clipboard->text();
        copy->click();
        QCOMPARE(clipboard->text(), field->text());
        clipboard->setText(saved);

        // Schalter «Ausgabe» links neben dem Eintrag; die Auswahl bleibt
        QTRY_VERIFY_WITH_TIMEOUT(state->text().startsWith(QStringLiteral("Ausgabe: ein")), 3000);
        QRect onSwitch = tree->outputSwitchRect(itemNamed(QStringLiteral("Test Szene")));
        QVERIFY(!onSwitch.isEmpty());
        QVERIFY(onSwitch.right() < tree->visualItemRect(itemNamed(QStringLiteral("Test Szene"))).left());
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, onSwitch.center());
        QTRY_VERIFY_WITH_TIMEOUT(!m_check->listData().summary(m_scene)->output, 3000);
        // Ohne Server kennt der Kern keine verbundenen Seiten (keine Anzahl).
        const QString offText = DSS_CORE_OVERLAY_SERVER
                                    ? QStringLiteral("Ausgabe: aus (Browserquelle leer), keine Browserquelle verbunden")
                                    : QStringLiteral("Ausgabe: aus (Browserquelle leer)");
        QTRY_COMPARE_WITH_TIMEOUT(state->text(), offText, 3000);
        QCOMPARE(itemNamed(QStringLiteral("Test Szene"))->data(0, OverlayTree::kOutputRole).toBool(), false);
        QCOMPARE(tree->currentItem()->text(0), QStringLiteral("Test Szene"));
        // Ein anderes Overlay umschalten, ohne es zu wählen
        const QUuid chat = itemNamed(QStringLiteral("Chat Ecke"))->data(0, OverlayTree::kIdRole).toUuid();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, {},
                          tree->outputSwitchRect(itemNamed(QStringLiteral("Chat Ecke"))).center());
        QTRY_VERIFY_WITH_TIMEOUT(!m_check->listData().summary(chat)->output, 3000);
        QCOMPARE(tree->currentItem()->text(0), QStringLiteral("Test Szene"));
        for (const QString &name : {QStringLiteral("Test Szene"), QStringLiteral("Chat Ecke")}) {
            QTest::mouseClick(tree->viewport(), Qt::LeftButton, {}, tree->outputSwitchRect(itemNamed(name)).center());
            QTest::qWait(300); // neue Liste vom Kern abwarten (die Einträge werden neu angelegt)
        }
        QTRY_VERIFY_WITH_TIMEOUT(m_check->listData().summary(m_scene)->output && m_check->listData().summary(chat)->output, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(state->text().startsWith(QStringLiteral("Ausgabe: ein")), 3000);
        // Ordner haben keinen Schalter
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->data(0, OverlayTree::kFolderRole).toBool())
                QVERIFY(tree->outputSwitchRect(*it).isEmpty());

        // Neuer Token: Abbrechen lässt die Adresse, Bestätigen gibt eine neue
        const QString before = field->text();
        QString question;
        answerDialog([&question](QWidget *dialog) {
            if (auto *box = qobject_cast<QMessageBox *>(dialog))
                question = box->text();
            clickButton(dialog, QStringLiteral("Abbrechen"));
        });
        newToken->click();
        QCOMPARE(question, newTokenQuestion());
        QTest::qWait(300);
        QCOMPARE(field->text(), before);
        answerDialog([](QWidget *dialog) { clickButton(dialog, QStringLiteral("Neuer Token")); });
        newToken->click();
        QTRY_VERIFY_WITH_TIMEOUT(field->text() != before && field->text().contains(QStringLiteral("/o/test-szene/")), 3000);

#if DSS_CORE_OVERLAY_SERVER
        // Port belegt: Meldung im Dock und in der Statuszeile, danach wieder weg
        // Port im erlaubten Bereich (kPortMin bis kPortMax), den dieser Test selbst belegt
        QTcpServer blocker;
        for (int p = 30000 + int(QCoreApplication::applicationPid() % 2000); !blocker.isListening() && p <= kPortMax; ++p)
            static_cast<void>(blocker.listen(QHostAddress::LocalHost, quint16(p)));
        QVERIFY(blocker.isListening());
        SettingsUpdate update;
        update.port = blocker.serverPort();
        QVERIFY(m_client->sendSettingsUpdate(update) > 0);
        QTRY_VERIFY_WITH_TIMEOUT(message->text().contains(QStringLiteral("Port %1 ist belegt").arg(blocker.serverPort())), 5000);
        QVERIFY(message->text().contains(QStringLiteral("tst_fensterbearbeiten")));
        QVERIFY(status->text().startsWith(QStringLiteral("Browserquelle: Port %1 ist belegt").arg(blocker.serverPort())));
        QCOMPARE(status->toolTip(), message->text());
        QVERIFY(status->isVisible());
        update.port = port;
        QVERIFY(m_client->sendSettingsUpdate(update) > 0);
        QTRY_VERIFY_WITH_TIMEOUT(message->text().isEmpty() && status->text().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(field->text().contains(QStringLiteral(":%1/o/test-szene/").arg(port)), 3000);
#endif

        // Ohne gewähltes Overlay (Ordner gewählt) ausgegraut
        for (QTreeWidgetItemIterator it(tree); *it; ++it)
            if ((*it)->data(0, OverlayTree::kFolderRole).toBool()) {
                tree->setCurrentItem(*it);
                break;
            }
        QTRY_VERIFY_WITH_TIMEOUT(!field->isEnabled() && !copy->isEnabled() && !newToken->isEnabled(), 3000);
        QCOMPARE(field->text(), QString());
        QCOMPARE(state->text(), QStringLiteral("Ausgabe: –"));
        selectOverlayNamed(QStringLiteral("Test Szene"));
        QTRY_VERIFY_WITH_TIMEOUT(field->isEnabled() && !field->text().isEmpty(), 3000);
    }

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
        QVERIFY(hint->text().contains(QStringLiteral("Server startet mit den neuen Werten neu")));
        auto *reset = dialog->findChild<QPushButton *>(QStringLiteral("settingsReset"));
        QVERIFY(reset && reset->isVisible());
        // Werte des Kerns: Port aus kern.ini des Tests (seit P10 eigener Port je Testkern,
        // prozesshilfe.h) und die erkannte Adresse
        QTRY_VERIFY_WITH_TIMEOUT(ok->isEnabled(), 3000);
        QVERIFY(m_client->settings().has_value());
        QCOMPARE(port->value(), m_client->settings()->settings.port);
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
        QVERIFY(!reset->isEnabled()); // Datei gültig: nichts zurückzusetzen

        // P10, Punkt 78: Adresse, die nicht zu diesem PC gehört (192.0.2.1, Beispielnetz nach
        // RFC 5737); Punkt 76: die Meldung steht nur im Dialog, nicht in der Statuszeile.
        address->setText(QStringLiteral("192.0.2.1"));
        QTest::mouseClick(ok, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(problem->text().startsWith(QStringLiteral("192.0.2.1 gehört nicht zu diesem PC. "
                                                                           "Adressen dieses PCs: ")),
                                 3000);
        QVERIFY(dialog->isVisible());
        QTest::qWait(200);
        QVERIFY2(!m_window->statusBar()->currentMessage().contains(QStringLiteral("192.0.2.1")),
                 qPrintable(m_window->statusBar()->currentMessage()));

        // Gültig: Kern schreibt, alle Verbindungen bekommen die neuen Werte, Dialog schliesst
        port->setValue(9000);
        address->setText(detectHomeAddress());
        QTest::mouseClick(ok, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(!dialog->isVisible(), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(m_check->settings()
                                     && m_check->settings()->settings
                                            == (CoreSettings{9000, detectHomeAddress()}),
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

    // P10, Punkt 77: Dialog mit einem eigenen Kern, dessen kern.ini ungültig ist: «OK» ausgegraut,
    // «Auf Standardwerte zurücksetzen» frei; danach die neuen Werte im Dialog und der Hinweis.
    void settingsDialogReset()
    {
        QTemporaryDir data;
        QTemporaryDir config;
        std::unique_ptr<QTemporaryDir> run = testhelp::runtimeDir();
        const QString socket = run->filePath(QStringLiteral("r.sock"));
        const QString dir = config.filePath(QFileInfo(coreSettingsPath()).dir().dirName());
        QVERIFY(QDir().mkpath(dir));
        {
            QFile f(dir + QStringLiteral("/kern.ini"));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("[overlay-server]\nport=99999\n");
        }
        // Port nach dem Zurücksetzen: Standard dieses Testkerns (--standardport, Punkt 83), nicht
        // 8081 der Version in Arbeit.
        std::unique_ptr<QProcess> core = testhelp::startCore(data.path(), socket, config.path());
        QString log;
        QVERIFY2(testhelp::waitForLog(core.get(), QStringLiteral("Verbindung offen"), &log), qPrintable(log));
        CoreClient::Options o;
        o.socketPath = socket;
        o.program = QStringLiteral("tst_fensterbearbeiten");
        o.version = version();
        CoreClient client(o);
        client.start();
        QTRY_VERIFY_WITH_TIMEOUT(client.isConnected(), 5000);
        SettingsDialog dialog(&client, m_window.get());
        dialog.showFromCore();
        auto *problem = dialog.findChild<QLabel *>(QStringLiteral("settingsProblem"));
        auto *ok = dialog.findChild<QPushButton *>(QStringLiteral("settingsOk"));
        auto *reset = dialog.findChild<QPushButton *>(QStringLiteral("settingsReset"));
        auto *port = dialog.findChild<QSpinBox *>(QStringLiteral("settingsPort"));
        auto *address = dialog.findChild<QLineEdit *>(QStringLiteral("settingsAddress"));
        QTRY_VERIFY_WITH_TIMEOUT(problem->text().contains(QStringLiteral("Port 99999 ungültig")), 3000);
        QVERIFY(!ok->isEnabled());
        QVERIFY(reset->isEnabled());
        QTest::mouseClick(reset, Qt::LeftButton);
        QTRY_VERIFY_WITH_TIMEOUT(problem->text().startsWith(QStringLiteral("Auf Standardwerte zurückgesetzt. Die ungültige "
                                                                           "Datei heisst jetzt kern.ini.ungueltig-")),
                                 3000);
        QVERIFY(dialog.isVisible());
        QVERIFY(ok->isEnabled());
        QVERIFY(!reset->isEnabled());
        QCOMPARE(port->value(), testhelp::testPort(config.path()));
        QCOMPARE(address->text(), detectHomeAddress());
        QCOMPARE(QDir(dir).entryList(QDir::Files).size(), 2); // kern.ini und die umbenannte
        dialog.reject();
        client.stop();
        core->terminate();
        QVERIFY(core->waitForFinished(5000));
    }
#endif

    // P10-vor, Teil A 1 (Punkt 67): Während der Kern eine grosse Datei übernimmt, bleibt das
    // Fenster bedienbar: Hinweis in der Statuszeile, Änderungen kommen an, der Kern antwortet.
    // Grösse 1 GiB (Auftrag); DSS_TEST_GROSS_MIB setzt eine andere (Wiederholungsläufe). Die Datei
    // (Zufallszahlen mit festem Startwert) liegt im Bauordner auf der Platte und wird
    // wiederverwendet; die Kopie des Kerns landet im Datenordner des Tests.
    // --- Dock «Medien» (P10b, E-28) ---

    // Teil B 2 ohne gespeicherte Anordnung: eigenes Dock zwischen «Quellen» und «Eigenschaften»;
    // Teil B 1: Spalten, Vorschau, Ordner zuerst, Sortieren per Spaltenkopf.
    void mediaDockDefaultLayout()
    {
        if (!DSS_APP_MEDIA_DOCK)
            QSKIP("Schalter DSS_APP_MEDIA_DOCK ist aus");
        auto *media = child<QDockWidget>("mediaDock");
        auto *sources = child<QDockWidget>("sourcesDock");
        auto *properties = child<QDockWidget>("propertiesDock");
        QVERIFY(media && sources && properties);
        QCOMPARE(m_window->dockWidgetArea(media), Qt::BottomDockWidgetArea);
        QVERIFY(m_window->tabifiedDockWidgets(media).isEmpty());
        QVERIFY(media->isVisible() && !media->isFloating());
        QVERIFY2(sources->geometry().right() < media->geometry().left()
                     && media->geometry().right() < properties->geometry().left(),
                 "Medien nicht zwischen Quellen und Eigenschaften");
        QCOMPARE(media->windowTitle(), QStringLiteral("Medien"));
        // im Menü «Docks» ein- und ausblendbar
        QMenu *docks = nullptr;
        for (QMenu *m : m_window->findChildren<QMenu *>()) {
            if (m->title() == QStringLiteral("D&ocks"))
                docks = m;
        }
        QVERIFY(docks);
        QAction *toggle = nullptr;
        for (QAction *a : docks->actions()) {
            if (a->text() == QStringLiteral("Medien"))
                toggle = a;
        }
        QVERIFY(toggle);
        toggle->trigger();
        QVERIFY(!media->isVisible());
        toggle->trigger();
        QVERIFY(media->isVisible());

        auto *tree = child<MediaTree>("mediaTree");
        QCOMPARE(tree->columnCount(), 3);
        QCOMPARE(tree->headerItem()->text(0), QStringLiteral("Name"));
        QCOMPARE(tree->headerItem()->text(1), QStringLiteral("Typ"));
        QCOMPARE(tree->headerItem()->text(2), QStringLiteral("Grösse"));
        // Dateien aus initTestCase (ohne medien.json: Ersatznamen) und neue mit Namen
        ChangeRequest folder;
        folder.type = QString::fromLatin1(msg::kCreateMediaFolder);
        folder.name = QStringLiteral("Logos");
        QVERIFY(checkChange(folder).ok);
        ChangeRequest add;
        add.type = QString::fromLatin1(msg::kAddMedia);
        add.file = kMediaDir + QStringLiteral("/video.webm");
        QVERIFY(checkChange(add).ok);
        add.file = kMediaDir + QStringLiteral("/bild2.png");
        QVERIFY(checkChange(add).ok);
        QTRY_VERIFY_WITH_TIMEOUT(mediaItem(QStringLiteral("bild2.png")) && mediaItem(QStringLiteral("video.webm"))
                                     && mediaItem(QStringLiteral("Logos")), 3000);
        QTreeWidgetItem *video = mediaItem(QStringLiteral("video.webm"));
        QCOMPARE(video->text(MediaTree::KindColumn), QStringLiteral("Video"));
        QCOMPARE(video->text(MediaTree::SizeColumn), QStringLiteral("1.6 MiB"));
        QVERIFY(video->font(0).italic()); // ungenutzt
        QVERIFY(video->toolTip(0).contains(QStringLiteral("Ungenutzt")));
        QTreeWidgetItem *logos = mediaItem(QStringLiteral("Logos"));
        QCOMPARE(logos->text(MediaTree::KindColumn), QStringLiteral("Ordner"));
        // genutzte Datei (Bild der Szene): nicht kursiv, «Verwendet in» im Hinweis
        const QString usedImage = sourceAtRow(2).file;
        QTreeWidgetItem *used = nullptr;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->data(0, MediaTree::kFileRole).toString() == usedImage)
                used = *it;
        }
        QVERIFY(used);
        QVERIFY(!used->font(0).italic());
        // auch in «Chat Ecke», wenn transformCommands vorher lief
        QVERIFY2(used->toolTip(0).contains(QStringLiteral("\nVerwendet in Overlay «"))
                     && used->toolTip(0).contains(QStringLiteral("Overlay «Test Szene» (Quelle «bild1.png»")),
                 qPrintable(used->toolTip(0)));
#if DSS_APP_RENDERER
        // Vorschau: Bild und erstes Bild des Videos, nicht leer
        const auto opaquePixels = [](const QIcon &icon) {
            const QImage image = icon.pixmap(QSize(40, 24)).toImage();
            int n = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    n += qAlpha(image.pixel(x, y)) > 0 ? 1 : 0;
            return n;
        };
        QTRY_VERIFY_WITH_TIMEOUT(opaquePixels(mediaItem(QStringLiteral("video.webm"))->icon(0)) > 300, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(opaquePixels(mediaItem(QStringLiteral("bild2.png"))->icon(0)) > 100, 5000);
#endif
        // Ordner zuerst; Name aufsteigend, dann per Klick auf «Grösse» nach Grösse
        QCOMPARE(tree->topLevelItem(0)->text(0), QStringLiteral("Logos"));
        const auto order = [&] {
            QStringList names;
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                names << tree->topLevelItem(i)->text(0);
            return names;
        };
        QHeaderView *header = tree->header();
        const auto clickHeader = [&](int section) {
            QTest::mouseClick(header->viewport(), Qt::LeftButton, {},
                              QPoint(header->sectionViewportPosition(section) + 10, header->height() / 2));
        };
        clickHeader(MediaTree::SizeColumn);
        QCOMPARE(header->sortIndicatorSection(), int(MediaTree::SizeColumn));
        QStringList bySize = order();
        QCOMPARE(bySize.first(), QStringLiteral("Logos"));
        QVERIFY2(bySize.indexOf(QStringLiteral("bild2.png")) < bySize.indexOf(QStringLiteral("video.webm")),
                 qPrintable(bySize.join(QLatin1Char(','))));
        const Qt::SortOrder firstOrder = header->sortIndicatorOrder();
        clickHeader(MediaTree::SizeColumn);
        QVERIFY(header->sortIndicatorOrder() != firstOrder);
        const QStringList reversed = order();
        QCOMPARE(reversed.first(), QStringLiteral("Logos")); // Ordner bleiben zuerst
        QVERIFY(reversed.indexOf(QStringLiteral("bild2.png")) != bySize.indexOf(QStringLiteral("bild2.png")));
        tree->sortByColumn(MediaTree::NameColumn, Qt::AscendingOrder);
    }

    // Teil B 2 mit gespeicherter Anordnung (fenster.ini): «Medien» als Reiter neben «Quellen»;
    // sind «Overlays» und «Quellen» Reiter, als dritter. Ein zweiter Start lässt es dort.
    void mediaDockSavedLayout_data()
    {
        QTest::addColumn<QString>("source");
        QTest::newRow("Reiter Overlays und Quellen") << QString();
        // Kopie der echten fenster.ini des Nutzers, wie im Handtest (nur lesen)
        QTest::newRow("echte fenster.ini (Kopie)")
            << QDir::homePath() + QStringLiteral("/.config/dons-streaming-studio/fenster.ini");
    }

    void mediaDockSavedLayout()
    {
        if (!DSS_APP_MEDIA_DOCK)
            QSKIP("Schalter DSS_APP_MEDIA_DOCK ist aus");
        QFETCH(QString, source);
        QTemporaryDir config;
        const QByteArray oldConfig = qgetenv("XDG_CONFIG_HOME");
        qputenv("XDG_CONFIG_HOME", config.path().toUtf8());
        const auto restore = qScopeGuard([&] { qputenv("XDG_CONFIG_HOME", oldConfig); });
        const QString ini = configDir() + QStringLiteral("/fenster.ini");
        QVERIFY(QDir().mkpath(configDir()));
        const Theme theme = darkTheme();
        if (source.isEmpty()) {
            // Anordnung wie vor P10b: Overlays und Quellen als Reiter, ohne Dock «Medien»
            MainWindow old(theme, false, nullptr);
            old.resize(1280, 800);
            old.tabifyDockWidget(old.findChild<QDockWidget *>(QStringLiteral("overlaysDock")),
                                 old.findChild<QDockWidget *>(QStringLiteral("sourcesDock")));
            old.removeDockWidget(old.findChild<QDockWidget *>(QStringLiteral("mediaDock")));
            QSettings settings(ini, QSettings::IniFormat);
            settings.setValue(QStringLiteral("fenster/geometrie"), old.saveGeometry());
            settings.setValue(QStringLiteral("fenster/anordnung"), old.saveState(1));
        } else {
            if (!QFile::exists(source))
                QSKIP("keine fenster.ini des Nutzers");
            QVERIFY(QFile::copy(source, ini));
            QSettings settings(ini, QSettings::IniFormat);
            QVERIFY(!settings.contains(QStringLiteral("fenster/medien"))); // gespeichert vor P10b
        }
        bool overlaysTabbed = false;
        {
            MainWindow w(theme, true, nullptr);
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            auto *media = w.findChild<QDockWidget *>(QStringLiteral("mediaDock"));
            auto *sources = w.findChild<QDockWidget *>(QStringLiteral("sourcesDock"));
            auto *overlays = w.findChild<QDockWidget *>(QStringLiteral("overlaysDock"));
            QVERIFY(w.tabifiedDockWidgets(sources).contains(media));
            overlaysTabbed = w.tabifiedDockWidgets(sources).contains(overlays);
            if (source.isEmpty())
                QVERIFY(overlaysTabbed);
            if (overlaysTabbed) {
                const QList<QDockWidget *> tabs = w.tabifiedDockWidgets(media);
                QVERIFY(tabs.contains(overlays) && tabs.contains(sources));
                QCOMPARE(tabs.size(), 2); // drei Reiter
            }
            // Bild für den Bericht (nur mit DSS_TEST_BILDER, ein Ordner)
            if (const QString dir = qEnvironmentVariable("DSS_TEST_BILDER"); !dir.isEmpty()) {
                media->raise();
                QTest::qWait(200);
                w.grab().save(dir + QStringLiteral("/medien-reiter-%1.png").arg(QTest::currentDataTag()));
            }
            w.close(); // speichert die Anordnung
        }
        QSettings settings(ini, QSettings::IniFormat);
        QCOMPARE(settings.value(QStringLiteral("fenster/medien")).toBool(), true);
        {
            MainWindow w(theme, true, nullptr);
            w.show();
            QVERIFY(QTest::qWaitForWindowExposed(&w));
            auto *media = w.findChild<QDockWidget *>(QStringLiteral("mediaDock"));
            auto *sources = w.findChild<QDockWidget *>(QStringLiteral("sourcesDock"));
            QVERIFY(w.tabifiedDockWidgets(sources).contains(media));
            QCOMPARE(w.tabifiedDockWidgets(media).size(), overlaysTabbed ? 2 : 1);
            // lösbar wie die anderen Docks
            QVERIFY(media->features() & QDockWidget::DockWidgetFloatable);
            QVERIFY(media->features() & QDockWidget::DockWidgetMovable);
            w.close();
        }
    }

    // Teil B 3 und 5: Umbenennen (F2, Doppelklick), Ordner anlegen, umbenennen, löschen; Löschen
    // ausgegraut bei genutzter Datei, mit «verwendet in» in der Zeile unter der Liste.
    void mediaRenameFoldersRemove()
    {
        if (!DSS_APP_MEDIA_DOCK || !DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_MEDIA_DOCK oder DSS_APP_EDITING ist aus");
        auto *tree = child<MediaTree>("mediaTree");
        auto *remove = child<QToolButton>("mediaRemove");
        auto *info = child<QLabel>("mediaInfo");
        ChangeRequest add;
        add.type = QString::fromLatin1(msg::kAddMedia);
        add.file = kMediaDir + QStringLiteral("/bild3.png");
        QVERIFY(checkChange(add).ok);
        QTRY_VERIFY_WITH_TIMEOUT(mediaItem(QStringLiteral("bild3.png")), 3000);

        // F2 → Dialog → neuer Name beim Kern
        tree->setCurrentItem(mediaItem(QStringLiteral("bild3.png")));
        tree->setFocus();
        answerDialog([](QWidget *d) {
            d->findChild<QLineEdit *>()->setText(QStringLiteral("Hintergrund"));
            clickButton(d, QStringLiteral("OK"));
        });
        QTest::keyClick(tree, Qt::Key_F2);
        QTRY_VERIFY_WITH_TIMEOUT(checkedMedia(QStringLiteral("Hintergrund")), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(mediaItem(QStringLiteral("Hintergrund")), 3000);
        QVERIFY(tree->currentItem() && tree->currentItem()->text(0) == QStringLiteral("Hintergrund"));

        // Ordner anlegen im Knopf; der neue Ordner landet im gewählten Ordner bzw. dem der Datei
        tree->setCurrentItem(nullptr);
        answerDialog([](QWidget *d) {
            d->findChild<QLineEdit *>()->setText(QStringLiteral("Szenen-Medien"));
            clickButton(d, QStringLiteral("OK"));
        });
        child<QToolButton>("mediaAddFolder")->click();
        QTRY_VERIFY_WITH_TIMEOUT(checkedFolder(QStringLiteral("Szenen-Medien")), 3000);
        QCOMPARE(checkedFolder(QStringLiteral("Szenen-Medien"))->parent, QUuid());
        QTRY_VERIFY_WITH_TIMEOUT(mediaItem(QStringLiteral("Szenen-Medien")), 3000);
        tree->setCurrentItem(mediaItem(QStringLiteral("Szenen-Medien")));
        answerDialog([](QWidget *d) {
            d->findChild<QLineEdit *>()->setText(QStringLiteral("Unterordner"));
            clickButton(d, QStringLiteral("OK"));
        });
        child<QToolButton>("mediaAddFolder")->click();
        QTRY_VERIFY_WITH_TIMEOUT(checkedFolder(QStringLiteral("Unterordner")), 3000);
        const QUuid outer = checkedFolder(QStringLiteral("Szenen-Medien"))->id;
        QCOMPARE(checkedFolder(QStringLiteral("Unterordner"))->parent, outer);
        // Doppelklick auf einen Ordner → umbenennen
        QTRY_VERIFY_WITH_TIMEOUT(mediaItem(QStringLiteral("Unterordner")), 3000);
        tree->scrollToItem(mediaItem(QStringLiteral("Unterordner")));
        answerDialog([](QWidget *d) {
            d->findChild<QLineEdit *>()->setText(QStringLiteral("Alt"));
            clickButton(d, QStringLiteral("OK"));
        });
        doubleClick(tree, tree->visualItemRect(mediaItem(QStringLiteral("Unterordner"))).center());
        QTRY_VERIFY_WITH_TIMEOUT(checkedFolder(QStringLiteral("Alt")), 3000);
        QTRY_VERIFY_WITH_TIMEOUT(mediaItem(QStringLiteral("Alt")) && mediaItem(QStringLiteral("Alt"))->parent()
                                     && mediaItem(QStringLiteral("Alt"))->parent()->text(0) == QStringLiteral("Szenen-Medien"),
                                 3000);
        // Zuklappen wird gespeichert
        mediaItem(QStringLiteral("Szenen-Medien"))->setExpanded(false);
        QTRY_VERIFY_WITH_TIMEOUT(!checkedFolder(QStringLiteral("Szenen-Medien"))->expanded, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(mediaSynced(), 3000);
        mediaItem(QStringLiteral("Szenen-Medien"))->setExpanded(true);
        QTRY_VERIFY_WITH_TIMEOUT(checkedFolder(QStringLiteral("Szenen-Medien"))->expanded, 3000);

        // Löschen: genutzte Datei ausgegraut, mit «verwendet in»
        const QString usedImage = sourceAtRow(2).file;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) {
            if ((*it)->data(0, MediaTree::kFileRole).toString() == usedImage)
                tree->setCurrentItem(*it);
        }
        QVERIFY(!remove->isEnabled());
        QVERIFY2(info->text().startsWith(QStringLiteral("Verwendet in Overlay «"))
                     && info->text().contains(QStringLiteral("Overlay «Test Szene» (Quelle «bild1.png»")),
                 qPrintable(info->text()));
        QVERIFY2(remove->toolTip().contains(QStringLiteral("lässt sich nicht löschen, verwendet in Overlay «")),
                 qPrintable(remove->toolTip()));
        // ungenutzte Datei: löschen nach Rückfrage, Datei weg
        tree->setCurrentItem(mediaItem(QStringLiteral("Hintergrund")));
        QCOMPARE(info->text(), QStringLiteral("Ungenutzt"));
        QVERIFY(remove->isEnabled());
        const QString file = checkedMedia(QStringLiteral("Hintergrund"))->entry.file;
        QVERIFY(QFile::exists(m_data.filePath(file)));
        answerDialog([](QWidget *d) { clickButton(d, QStringLiteral("Löschen")); });
        remove->click();
        QTRY_VERIFY_WITH_TIMEOUT(!checkedMedia(QStringLiteral("Hintergrund")), 3000);
        QVERIFY(!QFile::exists(m_data.filePath(file)));
        QTRY_VERIFY_WITH_TIMEOUT(!mediaItem(QStringLiteral("Hintergrund")), 3000);
        // Ordner löschen: Inhalt kommt eine Ebene höher
        tree->setCurrentItem(mediaItem(QStringLiteral("Szenen-Medien")));
        QVERIFY(remove->isEnabled());
        answerDialog([](QWidget *d) {
            QVERIFY(d->findChild<QLabel *>(QStringLiteral("qt_msgbox_label"))
                    && d->findChild<QLabel *>(QStringLiteral("qt_msgbox_label"))->text().contains(QStringLiteral("auf die oberste Ebene")));
            clickButton(d, QStringLiteral("Löschen"));
        });
        remove->click();
        QTRY_VERIFY_WITH_TIMEOUT(!checkedFolder(QStringLiteral("Szenen-Medien")), 3000);
        QCOMPARE(checkedFolder(QStringLiteral("Alt"))->parent, QUuid());
    }

    // Teil B 4 und Teil A 3: mehrere Dateien mit «Hinzufügen» in den gewählten Ordner, ohne Quelle;
    // eine grosse Datei mit Fortschritt in der Liste, abgebrochen mit «Löschen».
    void mediaAddProgressCancel()
    {
        if (!DSS_APP_MEDIA_DOCK || !DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_MEDIA_DOCK oder DSS_APP_EDITING ist aus");
        auto *tree = child<MediaTree>("mediaTree");
        auto *remove = child<QToolButton>("mediaRemove");
        ChangeRequest folder;
        folder.type = QString::fromLatin1(msg::kCreateMediaFolder);
        folder.name = QStringLiteral("Neu hinzugefügt");
        QVERIFY(checkChange(folder).ok);
        QTRY_VERIFY_WITH_TIMEOUT(mediaItem(QStringLiteral("Neu hinzugefügt")), 3000);
        const QUuid folderId = checkedFolder(QStringLiteral("Neu hinzugefügt"))->id;
        tree->setCurrentItem(mediaItem(QStringLiteral("Neu hinzugefügt")));
        const qsizetype sourcesBefore = checked(m_scene).sources.size();

        // Dateidialog von Qt (der von GTK lässt sich nicht fernsteuern); zwei Dateien auf einmal
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
        const auto restore = qScopeGuard([] { QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false); });
        answerDialog([](QWidget *d) {
            auto *dialog = qobject_cast<QFileDialog *>(d);
            QVERIFY(dialog);
            QCOMPARE(dialog->fileMode(), QFileDialog::ExistingFiles);
            dialog->setDirectory(kMediaDir);
            dialog->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))
                ->setText(QStringLiteral("\"bild4.png\" \"bild5.png\""));
            QMetaObject::invokeMethod(dialog, "accept"); // geschützt; wie der Knopf «Öffnen»
        });
        child<QToolButton>("mediaAdd")->click();
        QTRY_VERIFY_WITH_TIMEOUT(checkedMedia(QStringLiteral("bild4.png")) && checkedMedia(QStringLiteral("bild5.png")), 5000);
        QCOMPARE(checkedMedia(QStringLiteral("bild4.png"))->entry.folder, folderId);
        QCOMPARE(checkedMedia(QStringLiteral("bild5.png"))->entry.folder, folderId);
        QCOMPARE(checked(m_scene).sources.size(), sourcesBefore); // keine Quelle
        QTRY_VERIFY_WITH_TIMEOUT(mediaItem(QStringLiteral("bild4.png")) && mediaItem(QStringLiteral("bild4.png"))->parent()
                                     == mediaItem(QStringLiteral("Neu hinzugefügt")), 3000);

        // Grosse Datei: Zeile mit «übernehme … N %», «Löschen» bricht ab, nichts bleibt in media/
        const QString big = largeFile();
        QVERIFY(!big.isEmpty());
        const QStringList mediaBefore = QDir(m_data.filePath(QStringLiteral("media"))).entryList(QDir::Files);
        ChangeRequest add;
        add.type = QString::fromLatin1(msg::kAddMedia);
        add.file = big;
        add.folderId = folderId;
        QSignalSpy finished(m_client.get(), &CoreClient::changeFinished);
        QVERIFY(m_client->sendChange(add) > 0);
        const QString name = QFileInfo(big).fileName();
        QTRY_VERIFY_WITH_TIMEOUT(mediaItem(name), 3000);
        QTreeWidgetItem *row = mediaItem(name);
        QVERIFY(row->data(0, MediaTree::kImportRole).toLongLong() > 0);
        QCOMPARE(row->parent(), mediaItem(QStringLiteral("Neu hinzugefügt")));
        QVERIFY2(row->text(MediaTree::SizeColumn).startsWith(QStringLiteral("übernehme … ")), qPrintable(row->text(2)));
        // Fortschritt steigt (der Kern schickt ihn alle 200 ms)
        const auto percent = [&] {
            QTreeWidgetItem *r = mediaItem(name);
            return r ? r->text(MediaTree::SizeColumn).section(QLatin1Char(' '), 2, 2).toInt() : -1;
        };
        const int first = percent();
        QTRY_VERIFY_WITH_TIMEOUT(percent() > first || !mediaItem(name), 10000);
        QVERIFY2(mediaItem(name), "Übernahme schon fertig, bevor der Fortschritt stieg");
        tree->setCurrentItem(mediaItem(name));
        QVERIFY(remove->isEnabled());
        QVERIFY2(remove->toolTip().startsWith(QStringLiteral("Übernehmen von «")), qPrintable(remove->toolTip()));
        remove->click();
        QTRY_VERIFY_WITH_TIMEOUT(!mediaItem(name), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(finished.cbegin(), finished.cend(), [](const QList<QVariant> &a) {
                                     const ChangeResult r = a.at(0).value<ChangeResult>();
                                     return !r.ok && r.code == QLatin1String(err::kCanceled);
                                 }), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(QDir(m_data.filePath(QStringLiteral("media"))).entryList(QDir::Files), mediaBefore, 10000);
        QVERIFY(m_check->mediaList().files.size() == mediaBefore.size());
    }

    // Teil B 3 und 6 mit QDrag wie mit der Maus: Datei in einen Ordner, Ordner in einen Ordner,
    // Bild auf die Leinwand (Mitte an der Maus, Originalgrösse), Video in die Liste «Quellen» (an
    // der Stelle, Mitte der Leinwand). Nichts wird kopiert.
    void mediaDragAndDrop()
    {
        if (!DSS_APP_MEDIA_DOCK || !DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_MEDIA_DOCK oder DSS_APP_EDITING ist aus");
        m_window->raise();
        m_window->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(m_window.get()));
        auto *tree = child<MediaTree>("mediaTree");
        // Höhere Docks: Ziehen von Zeile zu Zeile ohne Rollen (die früheren Fälle legen Ordner an)
        auto *dock = child<QDockWidget>("mediaDock");
        const int dockHeight = dock->height();
        m_window->resizeDocks({dock}, {460}, Qt::Vertical);
        const auto restoreHeight = qScopeGuard([&] { m_window->resizeDocks({dock}, {dockHeight}, Qt::Vertical); });
        ChangeRequest r;
        r.type = QString::fromLatin1(msg::kCreateMediaFolder);
        r.name = QStringLiteral("Ziel");
        QVERIFY(checkChange(r).ok);
        r.name = QStringLiteral("Wandert");
        QVERIFY(checkChange(r).ok);
        r.type = QString::fromLatin1(msg::kAddMedia);
        r.name.clear();
        r.file = kMediaDir + QStringLiteral("/bild2.png");
        QVERIFY(checkChange(r).ok);
        r.file = kMediaDir + QStringLiteral("/video.webm");
        QVERIFY(checkChange(r).ok); // nach mediaDockDefaultLayout schon da: Name bleibt
        QTRY_VERIFY_WITH_TIMEOUT(mediaItem(QStringLiteral("Ziel")) && mediaItem(QStringLiteral("Wandert"))
                                     && mediaItem(QStringLiteral("bild2.png")) && mediaItem(QStringLiteral("video.webm")),
                                 3000);
        QTRY_VERIFY_WITH_TIMEOUT(mediaSynced(), 3000);
        tree->collapseAll();
        QTRY_VERIFY_WITH_TIMEOUT(mediaSynced(), 3000); // Zuklappen geht an den Kern
        const auto center = [&](const QString &name) {
            QTreeWidgetItem *item = mediaItem(name);
            tree->scrollToItem(item);
            return tree->visualItemRect(item).center();
        };
        // Datei auf einen Ordner
        QVERIFY(dragWithMouse(tree->viewport(), center(QStringLiteral("bild2.png")), tree->viewport(),
                              center(QStringLiteral("Ziel"))));
        const QUuid target = checkedFolder(QStringLiteral("Ziel"))->id;
        QTRY_COMPARE_WITH_TIMEOUT(checkedMedia(QStringLiteral("bild2.png"))->entry.folder, target, 3000);
        // Ordner auf einen Ordner
        QTRY_VERIFY_WITH_TIMEOUT(mediaSynced(), 3000);
        QVERIFY(dragWithMouse(tree->viewport(), center(QStringLiteral("Wandert")), tree->viewport(),
                              center(QStringLiteral("Ziel"))));
        QTRY_COMPARE_WITH_TIMEOUT(checkedFolder(QStringLiteral("Wandert"))->parent, target, 3000);

        // Bild auf die Leinwand: Mitte an der Maus, 256x256 wie die Datei
        selectOverlayNamed(QStringLiteral("Test Szene"));
        const qsizetype before = checked(m_scene).sources.size();
        const QStringList mediaBefore = QDir(m_data.filePath(QStringLiteral("media"))).entryList(QDir::Files);
        CanvasWidget *canvas = m_window->canvas();
        const QPoint drop = canvas->toWidget(QPointF(600, 400)).toPoint();
        const QPointF at = canvas->overlayPoint(drop);
        QTRY_VERIFY_WITH_TIMEOUT(mediaSynced(), 3000);
        mediaItem(QStringLiteral("Ziel"))->setExpanded(true);
        QTRY_VERIFY_WITH_TIMEOUT(checkedFolder(QStringLiteral("Ziel"))->expanded && mediaSynced(), 3000);
        QVERIFY(dragWithMouse(tree->viewport(), center(QStringLiteral("bild2.png")), canvas, drop));
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.size(), before + 1, 5000);
        Source added = checked(m_scene).sources.last(); // zuoberst
        QCOMPARE(added.name, QStringLiteral("bild2.png"));
        QCOMPARE(added.file, checkedMedia(QStringLiteral("bild2.png"))->entry.file);
#if DSS_MEDIA_SIZE
        QCOMPARE(QSize(added.width, added.height), QSize(256, 256));
        QCOMPARE(QPoint(added.x, added.y), QPoint(qRound(at.x()) - 128, qRound(at.y()) - 128));
#endif
        // neue Quelle ist gewählt
        QTRY_COMPARE_WITH_TIMEOUT(child<SourceList>("sourceList")->currentRow(), 0, 3000);

        // Video in die Liste «Quellen» zwischen Zeile 0 und 1: Mitte der Leinwand, Zeile 1
        auto *list = child<SourceList>("sourceList");
        QTRY_COMPARE_WITH_TIMEOUT(list->count(), int(before + 1), 3000);
        const QRect row1 = list->visualItemRect(list->item(1));
        const QPoint between(row1.center().x(), row1.top() + 2); // oberer Rand von Zeile 1
        QVERIFY(dragWithMouse(tree->viewport(), center(QStringLiteral("video.webm")), list->viewport(), between));
        QTRY_COMPARE_WITH_TIMEOUT(checked(m_scene).sources.size(), before + 2, 5000);
        const Overlay o = checked(m_scene);
        const Source video = o.sources.at(o.sources.size() - 2); // Zeile 1
        QCOMPARE(video.name, QStringLiteral("video.webm"));
        QCOMPARE(video.kind, SourceKind::Video);
#if DSS_MEDIA_SIZE
        QCOMPARE(QRect(video.x, video.y, video.width, video.height), QRect(480, 270, 960, 540));
#endif
        // nichts kopiert
        QCOMPARE(QDir(m_data.filePath(QStringLiteral("media"))).entryList(QDir::Files), mediaBefore);
        // jetzt verwendet: nicht mehr kursiv, Löschen ausgegraut
        QTRY_VERIFY_WITH_TIMEOUT(mediaItem(QStringLiteral("video.webm")) && !mediaItem(QStringLiteral("video.webm"))->font(0).italic(), 3000);
        tree->setCurrentItem(mediaItem(QStringLiteral("video.webm")));
        QVERIFY(!child<QToolButton>("mediaRemove")->isEnabled());
        // Aufräumen für spätere Fälle: beide Quellen wieder weg
        for (const QUuid &id : {added.id, video.id}) {
            ChangeRequest rm;
            rm.type = QString::fromLatin1(msg::kRemoveSource);
            rm.overlayId = m_scene;
            rm.sourceId = id;
            QVERIFY(checkChange(rm).ok);
        }
    }

    void usableWhileImportingLargeFile()
    {
        if (!DSS_APP_EDITING)
            QSKIP("Schalter DSS_APP_EDITING ist aus");
        const QString big = largeFile();
        QVERIFY(!big.isEmpty());
        const qint64 mib = QFileInfo(big).size() >> 20;
        selectOverlayNamed(QStringLiteral("Test Szene"));
        const qsizetype sourcesBefore = checked(m_scene).sources.size();
        selectSourceRow(0);
        // Nach Kennung, nicht nach Zeile: Die neue Quelle kommt ganz nach vorne (Zeile 0), und die
        // zweite Verbindung sieht sie, bevor das Fenster change_result hat (2 von 50 Läufen
        // scheiterten so, als die Prüfung noch sourceAtRow(0) las).
        const QUuid edited = sourceAtRow(0).id;
        const auto editedX = [&] {
            const Source *s = findSource(checked(m_scene), edited);
            return s ? s->x : -1;
        };
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
                const int before = editedX();
                x->setFocus();
                t.restart();
                QTest::keyClick(x, Qt::Key_Up);
                QTRY_COMPARE_WITH_TIMEOUT(editedX(), before + 1, 2000);
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
