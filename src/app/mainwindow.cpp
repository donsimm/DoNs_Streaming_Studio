#include "mainwindow.h"

#include "canvaswidget.h"
#include "configpaths.h"
#include "coreclient.h"
#include "docks.h"
#include "headerbar.h"
#include "symbols.h"
#include "version.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QSpinBox>
#include <QStyle>
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

MainWindow::MainWindow(const Theme &theme, bool keepLayout, dss::CoreClient *client,
                       QWidget *parent)
    : QMainWindow(parent), m_theme(theme), m_keepLayout(keepLayout), m_client(client)
{
    setWindowTitle(QStringLiteral("DoNs Streaming Studio"));
    resize(1280, 800);
    createMenus();
    createCentralArea();
    createDocks();
    createStatusBar();
    if (m_keepLayout)
        restoreLayout();
    if (m_client)
        connectCore();
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

    m_header = new HeaderBar(m_theme, central);
    column->addWidget(m_header);

    m_modules = new QStackedWidget(central);
    m_canvas = new CanvasWidget(m_theme, m_modules);
    m_modules->addWidget(m_canvas);

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
    m_overlaysDock = createOverlaysDock(this);
    m_sourcesDock = createSourcesDock(this);
    m_propertiesDock = createPropertiesDock(this);
    const QList<QDockWidget *> docks = {m_overlaysDock.dock, m_sourcesDock.dock,
                                        m_propertiesDock.dock, createControlDock(this)};
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
    m_statusMessage = new QLabel(QStringLiteral("Bereit"), this);
    statusBar()->addWidget(m_statusMessage, 1);
    // Einziger Zoom in P05 ist «Einpassen».
    auto *zoom = new QComboBox(this);
    zoom->addItem(QStringLiteral("Einpassen"));
    zoom->setToolTip(QStringLiteral("Zoom"));
    statusBar()->addPermanentWidget(zoom);
    m_coreStatus = new QLabel(dss::coreStatusText(false), this);
    m_coreStatus->setObjectName(QStringLiteral("coreStatus"));
    m_coreStatus->setProperty("state", QStringLiteral("disconnected"));
    statusBar()->addPermanentWidget(m_coreStatus);
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
    // Normales Schliessen: den Kern bitten, sich zu beenden (Entscheid 1 in P07). Er tut es
    // nur, wenn kein anderes Fenster verbunden ist. Höchstens 1 s warten.
    if (m_client && m_client->isConnected()) {
        const std::optional<dss::QuitReply> reply = m_client->requestQuit(1000);
        if (!reply)
            qWarning("Kern hat auf die Bitte zum Beenden nicht geantwortet");
        else if (reply->accepted)
            qInfo("Kern beendet sich");
        else
            qInfo("Kern läuft weiter: %d andere Fenster verbunden", reply->otherWindows);
    } else if (m_client) {
        m_client->stop();
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::connectCore()
{
    connect(m_client, &dss::CoreClient::statusTextChanged, m_statusMessage, &QLabel::setText);
    connect(m_client, &dss::CoreClient::connectedChanged, this, &MainWindow::onCoreConnectedChanged);
    connect(m_client, &dss::CoreClient::statsReceived, this, &MainWindow::onStats);
    connect(m_client, &dss::CoreClient::connectionStatesChanged, this, &MainWindow::refreshConnections);
    connect(m_client, &dss::CoreClient::overlayListChanged, this, &MainWindow::refreshOverlayList);
    connect(m_client, &dss::CoreClient::overlayChanged, this, [this](const QUuid &id) {
        if (id == m_selectedOverlay)
            refreshOverlay();
    });
    connect(m_overlaysDock.list, &QListWidget::currentItemChanged, this,
            &MainWindow::onOverlayItemChanged);
    connect(m_sourcesDock.list, &QListWidget::currentItemChanged, this,
            &MainWindow::onSourceItemChanged);
    m_statusMessage->setText(m_client->statusText());
    refreshGauges(dss::SystemStats());
}

void MainWindow::onCoreConnectedChanged(bool connected)
{
    m_coreStatus->setText(dss::coreStatusText(connected));
    m_coreStatus->setProperty("state", connected ? QStringLiteral("connected")
                                                 : QStringLiteral("disconnected"));
    // Das Stylesheet wertet [state="…"] nur beim Polieren aus.
    m_coreStatus->style()->unpolish(m_coreStatus);
    m_coreStatus->style()->polish(m_coreStatus);
    if (!connected) {
        // Ohne Verbindung keine Werte und keinen Verlauf zeigen.
        m_cpuHistory.clear();
        m_ramHistory.clear();
        m_lanHistory.clear();
        refreshGauges(dss::SystemStats());
    }
}

void MainWindow::onStats(const dss::SystemStats &stats)
{
    m_cpuHistory.add(stats.cpuPercent);
    m_ramHistory.add(stats.ramUsedKiB ? std::optional<double>(double(*stats.ramUsedKiB))
                                      : std::nullopt);
    m_lanHistory.add(stats.lan ? std::optional<double>(stats.lan->receivedKbitPerSecond
                                                       + stats.lan->sentKbitPerSecond)
                               : std::nullopt);
    refreshGauges(stats);
}

void MainWindow::refreshGauges(const dss::SystemStats &stats)
{
    m_header->setGauge(HeaderBar::Cpu, dss::cpuText(stats.cpuPercent),
                       QStringLiteral("CPU-Auslastung aller Kerne"), m_cpuHistory.values(),
                       m_cpuHistory.capacity(), 100.0);
    m_header->setGauge(HeaderBar::Ram, dss::ramText(stats.ramUsedKiB, stats.ramTotalKiB),
                       QStringLiteral("RAM belegt / gesamt"), m_ramHistory.values(),
                       m_ramHistory.capacity(),
                       stats.ramTotalKiB ? double(*stats.ramTotalKiB) : m_ramHistory.maximum());
    m_header->setGauge(HeaderBar::Lan, dss::lanText(stats.lan), dss::lanToolTip(stats.lan),
                       m_lanHistory.values(), m_lanHistory.capacity(),
                       dss::lanScaleMaximum(m_lanHistory));
}

void MainWindow::refreshConnections()
{
    const dss::ConnectionStates states = m_client->connectionStates();
    for (dss::Connection c : dss::kAllConnections)
        setConnectionState(c, states.value(c, dss::ConnectionState::Off));
}

void MainWindow::refreshOverlayList()
{
    // Liste neu füllen und die Auswahl über die Kennung behalten.
    m_filling = true;
    QListWidget *list = m_overlaysDock.list;
    list->clear();
    QListWidgetItem *selected = nullptr;
    for (const dss::OverlaySummary &o : m_client->overlays()) {
        auto *item = new QListWidgetItem(o.name, list);
        item->setData(Qt::UserRole, o.id);
        item->setToolTip(QStringLiteral("%1 · %2x%3 · %4 Quelle(n)")
                             .arg(o.address).arg(o.width).arg(o.height).arg(o.sourceCount));
        if (o.id == m_selectedOverlay)
            selected = item;
    }
    if (selected) {
        list->setCurrentItem(selected);
    } else {
        m_selectedOverlay = QUuid();
        m_selectedSource = QUuid();
    }
    m_filling = false;
    refreshOverlay();
}

void MainWindow::onOverlayItemChanged(QListWidgetItem *current)
{
    if (m_filling)
        return;
    m_selectedOverlay = current ? current->data(Qt::UserRole).toUuid() : QUuid();
    m_selectedSource = QUuid();
    if (!m_selectedOverlay.isNull())
        m_client->requestOverlay(m_selectedOverlay);
    refreshOverlay();
}

void MainWindow::refreshOverlay()
{
    // Quellen und Leinwand des gewählten Overlays; ohne Overlay leer bzw. Platzhalter.
    const std::optional<dss::Overlay> overlay =
        m_selectedOverlay.isNull() ? std::nullopt : m_client->overlay(m_selectedOverlay);
    m_filling = true;
    QListWidget *list = m_sourcesDock.list;
    list->clear();
    QListWidgetItem *selected = nullptr;
    if (overlay) {
        const QIcon shown = loadSymbol(QStringLiteral("auge.svg"), QSize(16, 16));
        const QIcon hidden = loadSymbol(QStringLiteral("auge-zu.svg"), QSize(16, 16));
        for (const dss::SourceRow &row : dss::sourceRows(*overlay)) {
            auto *item = new QListWidgetItem(row.visible ? shown : hidden, row.name, list);
            item->setData(Qt::UserRole, row.id);
            item->setToolTip(dss::visibilityToolTip(row.visible));
            if (row.id == m_selectedSource)
                selected = item;
        }
    }
    if (selected)
        list->setCurrentItem(selected);
    else
        m_selectedSource = QUuid();
    m_filling = false;
    m_canvas->setCanvasSize(overlay ? QSize(overlay->width, overlay->height) : QSize());
    refreshProperties();
}

void MainWindow::onSourceItemChanged(QListWidgetItem *current)
{
    if (m_filling)
        return;
    m_selectedSource = current ? current->data(Qt::UserRole).toUuid() : QUuid();
    refreshProperties();
}

void MainWindow::refreshProperties()
{
    const std::optional<dss::Overlay> overlay =
        m_selectedOverlay.isNull() ? std::nullopt : m_client->overlay(m_selectedOverlay);
    const dss::Source *source = overlay ? dss::findSource(*overlay, m_selectedSource) : nullptr;
    // Nur Anzeige, die Felder bleiben ausgegraut (Bearbeiten kommt mit P08).
    m_propertiesDock.name->setText(source ? source->name : QString());
    m_propertiesDock.x->setValue(source ? source->x : 0);
    m_propertiesDock.y->setValue(source ? source->y : 0);
    m_propertiesDock.width->setValue(source ? source->width : 1);
    m_propertiesDock.height->setValue(source ? source->height : 1);
    m_propertiesDock.rotation->setValue(source ? dss::rotationDegrees(source->rotation) : 0.0);
    m_propertiesDock.hint->setText(source ? QStringLiteral("Nur Anzeige")
                                          : QStringLiteral("Keine Quelle gewählt"));
}

void MainWindow::selectOverlayRow(int row)
{
    m_overlaysDock.list->setCurrentRow(row);
}

void MainWindow::selectSourceRow(int row)
{
    m_sourcesDock.list->setCurrentRow(row);
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
