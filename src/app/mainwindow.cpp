#include "mainwindow.h"

#include "canvaswidget.h"
#include "configpaths.h"
#include "docks.h"
#include "headerbar.h"
#include "symbols.h"
#include "version.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QDockWidget>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {

// Version der gespeicherten Anordnung; bei einem Umbau der Docks erhöhen, dann gilt
// wieder die Standardanordnung. https://doc.qt.io/qt-6.8/qmainwindow.html#saveState
constexpr int kLayoutVersion = 1;

// Datei der Fenstereinstellungen: ~/.config/dons-streaming-studio/fenster.ini
// (bzw. $XDG_CONFIG_HOME/…), derselbe Ordner wie die eigenen Symbole (dss::configDir()).
QString layoutFile()
{
    return dss::configDir() + QStringLiteral("/fenster.ini");
}

} // namespace

MainWindow::MainWindow(const Theme &theme, bool keepLayout, QWidget *parent)
    : QMainWindow(parent), m_theme(theme), m_keepLayout(keepLayout)
{
    setWindowTitle(QStringLiteral("DoNs Streaming Studio"));
    resize(1280, 800);
    createMenus();
    createCentralArea();
    createDocks();
    createStatusBar();
    if (m_keepLayout)
        restoreLayout();
}

void MainWindow::createMenus()
{
    // Nur Datei → Beenden, Docks und Hilfe → Über tun etwas; die übrigen Menüs sind
    // noch leer und ausgegraut (Auftrag P05).
    QMenu *file = menuBar()->addMenu(QStringLiteral("&Datei"));
    QAction *quit = file->addAction(QStringLiteral("&Beenden"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);
    quit->setMenuRole(QAction::QuitRole);

    for (const QString &title : {QStringLiteral("&Bearbeiten"), QStringLiteral("&Ansicht")})
        menuBar()->addMenu(title)->setEnabled(false);
    m_docksMenu = menuBar()->addMenu(QStringLiteral("D&ocks"));
    menuBar()->addMenu(QStringLiteral("&Werkzeuge"))->setEnabled(false);

    QMenu *help = menuBar()->addMenu(QStringLiteral("&Hilfe"));
    help->addAction(QStringLiteral("Ü&ber …"), this, &MainWindow::showAbout);
}

void MainWindow::createCentralArea()
{
    auto *central = new QWidget(this);
    auto *column = new QVBoxLayout(central);
    column->setContentsMargins(4, 4, 4, 0);
    column->setSpacing(6);

    m_header = new HeaderBar(central);
    column->addWidget(m_header);

    m_modules = new QStackedWidget(central);
    m_modules->addWidget(new CanvasWidget(m_theme, m_modules));

    auto *chatBotPage = new QWidget(m_modules);
    chatBotPage->setObjectName(QStringLiteral("chatBotPage"));
    chatBotPage->setAttribute(Qt::WA_StyledBackground);
    auto *chatBotLayout = new QVBoxLayout(chatBotPage);
    chatBotLayout->addWidget(new QLabel(QStringLiteral("Noch nicht vorhanden"), chatBotPage), 0,
                             Qt::AlignCenter);
    m_modules->addWidget(chatBotPage);
    column->addWidget(m_modules, 1);

    connect(m_header, &HeaderBar::moduleSelected, m_modules, &QStackedWidget::setCurrentIndex);
    setCentralWidget(central);
}

void MainWindow::setConnectionState(dss::Connection connection, dss::ConnectionState state)
{
    m_header->setConnectionState(connection, state);
}

void MainWindow::createDocks()
{
    // Docks unten über die ganze Breite, nebeneinander wie auf dem Bild.
    setCorner(Qt::BottomLeftCorner, Qt::BottomDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::BottomDockWidgetArea);
    const QList<QDockWidget *> docks = {createOverlaysDock(this), createSourcesDock(this),
                                        createPropertiesDock(this), createControlDock(this)};
    addDockWidget(Qt::BottomDockWidgetArea, docks.first());
    for (qsizetype i = 1; i < docks.size(); ++i)
        splitDockWidget(docks.at(i - 1), docks.at(i), Qt::Horizontal);
    resizeDocks(docks, {230, 230, 370, 440}, Qt::Horizontal);
    resizeDocks({docks.first()}, {215}, Qt::Vertical);
    for (QDockWidget *d : docks)
        m_docksMenu->addAction(d->toggleViewAction());
}

void MainWindow::createStatusBar()
{
    statusBar()->setSizeGripEnabled(false);
    statusBar()->addWidget(new QLabel(QStringLiteral("Bereit"), this));
    // Einziger Zoom in P05 ist «Einpassen».
    auto *zoom = new QComboBox(this);
    zoom->addItem(QStringLiteral("Einpassen"));
    zoom->setToolTip(QStringLiteral("Zoom"));
    statusBar()->addPermanentWidget(zoom);
    auto *core = new QLabel(QStringLiteral("Kern-Dienst: nicht verbunden"), this);
    core->setObjectName(QStringLiteral("coreStatus"));
    core->setProperty("state", QStringLiteral("disconnected"));
    statusBar()->addPermanentWidget(core);
}

void MainWindow::restoreLayout()
{
    const QSettings settings(layoutFile(), QSettings::IniFormat);
    const QByteArray geometry = settings.value(QStringLiteral("fenster/geometrie")).toByteArray();
    const QByteArray state = settings.value(QStringLiteral("fenster/anordnung")).toByteArray();
    if (!geometry.isEmpty() && !restoreGeometry(geometry))
        qWarning("Fenstergrösse aus %s nicht lesbar, Standard gilt", qPrintable(layoutFile()));
    if (!state.isEmpty() && !restoreState(state, kLayoutVersion))
        qWarning("Anordnung der Docks aus %s nicht lesbar, Standard gilt", qPrintable(layoutFile()));
}

void MainWindow::saveLayout()
{
    QDir().mkpath(QFileInfo(layoutFile()).absolutePath());
    QSettings settings(layoutFile(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("fenster/geometrie"), saveGeometry());
    settings.setValue(QStringLiteral("fenster/anordnung"), saveState(kLayoutVersion));
    settings.sync();
    if (settings.status() != QSettings::NoError)
        qWarning("Fensteranordnung konnte nicht gespeichert werden: %s", qPrintable(layoutFile()));
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_keepLayout)
        saveLayout();
    QMainWindow::closeEvent(event);
}

void MainWindow::showAbout()
{
    QString text = QStringLiteral("<b>DoNs Streaming Studio</b><br>Version %1<br>Qt %2")
                       .arg(dss::version(), QString::fromLatin1(qVersion()));
    // Markenhinweis, sobald der Nutzer ein eigenes NDI-Symbol einsetzt (offener Punkt aus
    // «Entschieden vor P06»). Wortlaut nach den Lizenzangaben des NDI SDK:
    // https://docs.ndi.video/docs/sdk/licensing («About Box … NDI® is a registered trademark
    // of Vizrt NDI AB»).
    if (hasUserSymbol(QStringLiteral("ndi.svg")))
        text += QStringLiteral("<br><br>%1").arg(dss::ndiTrademarkNotice());
    QMessageBox::about(this, QStringLiteral("Über DoNs Streaming Studio"), text);
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    if (m_firstPaintReported)
        return;
    m_firstPaintReported = true;
    // Erst nach dem Zeichnen melden, wie in den Prototypen von P01.
    QTimer::singleShot(0, this, [this] {
        qInfo("Fenster gezeichnet");
        emit firstPainted();
    });
}
