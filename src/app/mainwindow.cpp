#include "mainwindow.h"

#include "canvaswidget.h"
#include "configpaths.h"
#include "coreclient.h"
#include "dockviews.h"
#include "docks.h"
#include "headerbar.h"
#include "symbols.h"
#if DSS_APP_RENDERER
#include "sourcerenderer.h"
#endif
#include "version.h"
#include "windowedit.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QToolButton>
#include <QTreeWidget>
#include <QStyle>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

#include <tuple>

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
    setWindowTitle(dss::windowTitle());
    resize(1280, 800);
    createMenus();
    createHeader();
    createCentralArea();
    createDocks();
    createStatusBar();
    if (m_transformMenu) {
        connectTransformShortcuts();
    } else {
        // Ohne DSS_APP_TRANSFORM keine Knöpfe im Bereich «Transformation»
        for (QToolButton *b : {m_propertiesDock.copyTransform, m_propertiesDock.pasteTransform,
                               m_propertiesDock.resetTransform})
            b->hide();
    }
    if (!editingAllowed()) {
        // Nur Anzeige: nichts ziehen (Schalter DSS_APP_EDITING oder ohne Kern).
        m_overlaysDock.tree->setDragDropMode(QAbstractItemView::NoDragDrop);
        m_sourcesDock.list->setDragDropMode(QAbstractItemView::NoDragDrop);
    }
    if (m_keepLayout)
        restoreLayout();
    if (m_client)
        connectCore();
}

void MainWindow::createMenus()
{
    // Datei → Beenden, Bearbeiten → Transformieren (P09b), Docks und Hilfe → Über tun etwas;
    // die übrigen Menüs sind noch leer und ausgegraut (Auftrag P05). Eigene Menüleiste, weil sie mit dem Kopfbereich
    // zusammen als Menü-Widget des Fensters steht (createHeader); menuBar() würde sie ersetzen.
    m_menuBar = new QMenuBar(this);
    QMenuBar *bar = m_menuBar;
    QMenu *file = bar->addMenu(QStringLiteral("&Datei"));
    QAction *quit = file->addAction(QStringLiteral("&Beenden"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);
    quit->setMenuRole(QAction::QuitRole);

    QMenu *edit = bar->addMenu(QStringLiteral("&Bearbeiten"));
#if DSS_APP_TRANSFORM
    createTransformMenu(edit);
#else
    edit->setEnabled(false);
#endif
    bar->addMenu(QStringLiteral("&Ansicht"))->setEnabled(false);
    m_docksMenu = bar->addMenu(QStringLiteral("D&ocks"));
    bar->addMenu(QStringLiteral("&Werkzeuge"))->setEnabled(false);

    QMenu *help = bar->addMenu(QStringLiteral("&Hilfe"));
    help->addAction(QStringLiteral("Ü&ber …"), this, &MainWindow::showAbout);
}

void MainWindow::createHeader()
{
    // Kopfbereich fest über die ganze Fensterbreite (E-23, P09): Menüleiste und Kopfbereich
    // zusammen als Menü-Widget. QMainWindow legt es über die Bereiche der Docks; links oder rechts
    // angedockte Docks beginnen darunter. https://doc.qt.io/qt-6.8/qmainwindow.html#setMenuWidget
    auto *top = new QWidget(this);
    top->setObjectName(QStringLiteral("menuAndHeader"));
    auto *column = new QVBoxLayout(top);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(m_menuBar);
    auto *headerRow = new QWidget(top);
    auto *headerLayout = new QVBoxLayout(headerRow);
    headerLayout->setContentsMargins(4, 4, 4, 6); // wie vorher im mittleren Bereich
    m_header = new HeaderBar(m_theme, headerRow);
    headerLayout->addWidget(m_header);
    column->addWidget(headerRow);
    setMenuWidget(top);
}

void MainWindow::createCentralArea()
{
    auto *central = new QWidget(this);
    auto *column = new QVBoxLayout(central);
    column->setContentsMargins(4, 0, 4, 0);
    column->setSpacing(6);

    m_modules = new QStackedWidget(central);
    m_canvas = new CanvasWidget(m_theme, m_modules);
    m_modules->addWidget(m_canvas);
#if DSS_APP_RENDERER
    m_renderer = new SourceRenderer(this);
    m_canvas->setRenderer(m_renderer);
#endif

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
    m_controlDock = createControlDock(this);
    const QList<QDockWidget *> docks = {m_overlaysDock.dock, m_sourcesDock.dock,
                                        m_propertiesDock.dock, m_controlDock.dock};
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
    connect(m_client, &dss::CoreClient::errorReceived, this, &MainWindow::onCoreError);
    connect(m_overlaysDock.tree, &QTreeWidget::currentItemChanged, this,
            &MainWindow::onOverlayItemChanged);
    connect(m_sourcesDock.list, &QListWidget::currentItemChanged, this,
            &MainWindow::onSourceItemChanged);
    // Wählen auf der Leinwand: dieselbe Auswahl wie im Dock «Quellen» (P09, Teil C).
    connect(m_canvas, &CanvasWidget::sourceClicked, this, &MainWindow::onCanvasSourceClicked);
    m_statusMessage->setText(m_client->statusText());
    refreshGauges(dss::SystemStats());
    if (editingAllowed())
        connectEditing();
    refreshActions();
}

void MainWindow::onCoreError(const dss::ErrorInfo &error)
{
    // Fehler des Kerns in der Statuszeile (Teil A), zum Beispiel «Speichern fehlgeschlagen».
    showStatus(error.message);
}

void MainWindow::showStatus(const QString &text)
{
    // Überdeckt die Zeile «Bereit»/Zustand des Kerns für 10 s, die Anzeigen rechts bleiben.
    // https://doc.qt.io/qt-6.8/qstatusbar.html#showMessage
    statusBar()->showMessage(text, 10000);
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
        m_selectOnResult.clear();
    } else if (editingAllowed()) {
        // Auswahl der Schriften für Textquellen (Teil F)
        m_client->requestFonts();
    }
#if DSS_APP_RENDERER
    // Dateien der Quellen liegen im Datenordner des Kerns (hello.data_dir, P09).
    if (connected)
        m_renderer->setDataDir(m_client->coreHello().dataDir);
    refreshOverlay();
#endif
    refreshActions();
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
    // Baum neu füllen, wie der Kern die Liste schickt (Reihenfolge und Ordner, Teil B), und die
    // Auswahl über die Kennung behalten.
    const dss::OverlayListData &data = m_client->listData();
    m_filling = true;
    OverlayTree *tree = m_overlaysDock.tree;
    tree->clear();
    QTreeWidgetItem *selected = nullptr;
    const auto overlayItem = [&](const QUuid &id) {
        const dss::OverlaySummary *o = data.summary(id);
        auto *item = new QTreeWidgetItem;
        item->setText(0, o ? o->name : id.toString(QUuid::WithoutBraces));
        item->setData(0, OverlayTree::kIdRole, id);
        item->setData(0, OverlayTree::kFolderRole, false);
        // Auf ein Overlay lässt sich etwas fallen lassen (es landet daneben), auf einen Ordner hinein.
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled
                       | Qt::ItemIsDropEnabled);
        if (o)
            item->setToolTip(0, QStringLiteral("%1 · %2x%3 · %4 Quelle(n)")
                                    .arg(o->address).arg(o->width).arg(o->height).arg(o->sourceCount));
        if (id == m_selectedOverlay)
            selected = item;
        return item;
    };
    QList<std::pair<QTreeWidgetItem *, bool>> folders;
    for (const dss::ListEntry &entry : data.layout.entries) {
        if (!entry.isFolder) {
            tree->addTopLevelItem(overlayItem(entry.overlayId));
            continue;
        }
        auto *item = new QTreeWidgetItem;
        item->setText(0, entry.folder.name);
        item->setData(0, OverlayTree::kIdRole, entry.folder.id);
        item->setData(0, OverlayTree::kFolderRole, true);
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        item->setToolTip(0, QStringLiteral("Ordner · %1 Overlay(s)").arg(entry.folder.overlays.size()));
        // Ordner zeigen das Aufklappzeichen auch leer, damit klar ist, dass etwas hineinpasst.
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        for (const QUuid &id : entry.folder.overlays)
            item->addChild(overlayItem(id));
        tree->addTopLevelItem(item);
        folders.append({item, entry.folder.expanded});
        if (entry.folder.id == m_selectedFolder)
            selected = item;
    }
    for (const auto &[item, expanded] : folders)
        item->setExpanded(expanded);
    if (selected) {
        tree->setCurrentItem(selected);
    } else {
        m_selectedOverlay = QUuid();
        m_selectedFolder = QUuid();
        m_selectedSource = QUuid();
    }
    m_filling = false;
    // Kaputte liste.json: einmal melden (Reihenfolge und Ordner lassen sich dann nicht ändern).
    if (data.listError != m_lastListError) {
        m_lastListError = data.listError;
        if (!data.listError.isEmpty())
            showStatus(data.listError);
    }
    refreshOverlay();
}

void MainWindow::onOverlayItemChanged(QTreeWidgetItem *current)
{
    if (m_filling)
        return;
    const bool folder = current && current->data(0, OverlayTree::kFolderRole).toBool();
    const QUuid id = current ? current->data(0, OverlayTree::kIdRole).toUuid() : QUuid();
    m_selectedFolder = folder ? id : QUuid();
    m_selectedOverlay = folder ? QUuid() : id;
    m_selectedSource = QUuid();
    if (!m_selectedOverlay.isNull())
        m_client->requestOverlay(m_selectedOverlay);
    refreshOverlay();
}

std::optional<dss::Overlay> MainWindow::selectedOverlay() const
{
    if (!m_client || m_selectedOverlay.isNull())
        return std::nullopt;
    std::optional<dss::Overlay> overlay = m_client->overlay(m_selectedOverlay);
    // Beim Ziehen auf der Leinwand die Vorschau zeigen (P09, Teil C).
    if (overlay && m_geometryPreview && m_geometryPreview->overlayId == m_selectedOverlay)
        dss::applyGeometry(&*overlay, m_geometryPreview->sourceId, m_geometryPreview->geometry);
    return overlay;
}

void MainWindow::refreshOverlay()
{
    // Quellen und Leinwand des gewählten Overlays; ohne Overlay leer bzw. Platzhalter.
    const std::optional<dss::Overlay> overlay = selectedOverlay();
    m_filling = true;
    SourceList *list = m_sourcesDock.list;
    list->clear();
    QListWidgetItem *selected = nullptr;
    if (overlay) {
        const QIcon shown = loadSymbol(QStringLiteral("auge.svg"), QSize(16, 16));
        const QIcon hidden = loadSymbol(QStringLiteral("auge-zu.svg"), QSize(16, 16));
        for (const dss::SourceRow &row : dss::sourceRows(*overlay)) {
            auto *item = new QListWidgetItem(row.visible ? shown : hidden, row.name, list);
            item->setData(SourceList::kIdRole, row.id);
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
    m_canvas->setOverlay(overlay);
    m_canvas->setMediaSizes(m_client ? m_client->mediaSizes() : dss::MediaSizes());
    m_canvas->setSelectedSource(m_selectedSource);
#if DSS_APP_RENDERER
    m_renderer->setOverlay(overlay ? &*overlay : nullptr);
#endif
    // Auflösung im Dock «Steuerung» (P09); während eine Änderung unterwegs ist, bleibt der Wert.
    if (!m_client || !m_client->isChangePending(dss::overlaySizeKey(m_selectedOverlay))) {
        const QSignalBlocker blockWidth(m_controlDock.overlayWidth);
        const QSignalBlocker blockHeight(m_controlDock.overlayHeight);
        m_controlDock.overlayWidth->setValue(overlay ? overlay->width : dss::kNewOverlayWidth);
        m_controlDock.overlayHeight->setValue(overlay ? overlay->height : dss::kNewOverlayHeight);
    }
    refreshProperties();
}

void MainWindow::onSourceItemChanged(QListWidgetItem *current)
{
    if (m_filling)
        return;
    m_selectedSource = current ? current->data(Qt::UserRole).toUuid() : QUuid();
    m_canvas->setSelectedSource(m_selectedSource);
    refreshProperties();
}

void MainWindow::refreshProperties()
{
    const std::optional<dss::Overlay> overlay = selectedOverlay();
    const dss::Source *source = overlay ? dss::findSource(*overlay, m_selectedSource) : nullptr;
    PropertiesDock &d = m_propertiesDock;
    // Felder setzen ohne Änderungen auszulösen. Ein Feld, dessen Änderung noch unterwegs ist,
    // bleibt, wie der Nutzer es eingestellt hat; es wird nach der Antwort nachgeführt
    // (onChangeFinished). Gleiche Werte werden nicht neu gesetzt (Schreibmarke bleibt).
    const auto pending = [this, source](const QString &field) {
        return source && m_client->isChangePending(dss::changeKey(source->id, field));
    };
    const auto setSpin = [&](QSpinBox *spin, int value, const QString &field) {
        const QSignalBlocker block(spin);
        if (!pending(field) && spin->value() != value)
            spin->setValue(value);
    };
    if (!d.name->hasFocus() || !source)
        d.name->setText(source ? source->name : QString());
    setSpin(d.x, source ? source->x : 0, QStringLiteral("x"));
    setSpin(d.y, source ? source->y : 0, QStringLiteral("y"));
    setSpin(d.width, source ? source->width : 1, QStringLiteral("width"));
    setSpin(d.height, source ? source->height : 1, QStringLiteral("height"));
    {
        const QSignalBlocker block(d.rotation);
        const double degrees = source ? dss::rotationDegrees(source->rotation) : 0.0;
        if (!pending(QStringLiteral("rotation")) && d.rotation->value() != degrees)
            d.rotation->setValue(degrees);
    }
    // Spiegeln (P09b): Kästchen im Bereich «Transformation»
    for (const auto &[box, value, field] :
         {std::tuple{d.flipHorizontal, source && source->flipHorizontal, QStringLiteral("flipHorizontal")},
          std::tuple{d.flipVertical, source && source->flipVertical, QStringLiteral("flipVertical")}}) {
        const QSignalBlocker block(box);
        if (!pending(field))
            box->setChecked(value);
    }
    int page = PropertiesDock::kEmptyPage;
    QString hint = QStringLiteral("Keine Quelle gewählt");
    if (source) {
        switch (source->kind) {
        case dss::SourceKind::Image:
            page = PropertiesDock::kImagePage;
            hint = QStringLiteral("Bild");
            break;
        case dss::SourceKind::Video:
            page = PropertiesDock::kVideoPage;
            hint = QStringLiteral("Video");
            break;
        case dss::SourceKind::Text:
            page = PropertiesDock::kTextPage;
            hint = QStringLiteral("Text");
            break;
        }
        if (!editingAllowed())
            hint += QStringLiteral(" · nur Anzeige");
    }
    d.hint->setText(hint);
    d.kindPages->setCurrentIndex(page);
    if (source && source->kind == dss::SourceKind::Video) {
        const QSignalBlocker block(d.loop);
        d.loop->setChecked(source->loop);
        const QSignalBlocker blockHold(d.holdAtEnd);
        d.holdAtEnd->setChecked(source->holdAtEnd);
    }
    if (source && source->kind == dss::SourceKind::Text) {
        const dss::TextProperties &t = source->text;
        {
            const QSignalBlocker block(d.content);
            if (!pending(QStringLiteral("content")) && d.content->toPlainText() != t.content)
                d.content->setPlainText(t.content);
        }
        setSpin(d.fontSize, t.fontSize, QStringLiteral("fontSize"));
        const QList<dss::FontFace> faces = m_client->fonts();
        const QString family = dss::familyOfFont(faces, t.fontFile);
        {
            const QSignalBlocker block(d.family);
            d.family->setCurrentIndex(family.isEmpty() ? -1 : d.family->findData(family));
        }
        {
            const QSignalBlocker block(d.weight);
            d.weight->clear();
            for (const dss::WeightChoice &c : dss::weightChoices(faces, family))
                d.weight->addItem(c.label, c.weight);
            d.weight->setCurrentIndex(d.weight->findData(t.weight));
        }
        {
            const QSignalBlocker block(d.italic);
            d.italic->setChecked(t.italic);
        }
        d.italic->setEnabled(editingAllowed() && dss::italicSelectable(faces, family));
        int r = 0, g = 0, b = 0, a = 0;
        dss::colorParts(t.color, &r, &g, &b, &a);
        QPixmap swatch(14, 14);
        swatch.fill(QColor(r, g, b, a));
        d.color->setIcon(QIcon(swatch));
        d.color->setText(t.color);
        {
            const QSignalBlocker block(d.align);
            d.align->setCurrentIndex(d.align->findData(int(t.align)));
        }
    }
    refreshActions();
}

void MainWindow::refreshFonts()
{
    // Familien wie in fontFamilies (mitgelieferte zuerst), dann «Datei wählen …» (Entscheid 5).
    QComboBox *family = m_propertiesDock.family;
    const QSignalBlocker block(family);
    family->clear();
    const QList<dss::FontFace> faces = m_client->fonts();
    for (const QString &name : dss::fontFamilies(faces))
        family->addItem(name, name);
    family->insertSeparator(family->count());
    family->addItem(QStringLiteral("Datei wählen …"));
    if (!m_client->fontError().isEmpty())
        showStatus(m_client->fontError());
    refreshProperties();
}

void MainWindow::refreshActions()
{
    // Knöpfe und Felder nur, wenn bearbeitet werden kann und etwas gewählt ist.
    const bool edit = editingAllowed() && m_client && m_client->isConnected();
    const std::optional<dss::Overlay> overlay = selectedOverlay();
    const dss::Source *source = overlay ? dss::findSource(*overlay, m_selectedSource) : nullptr;
    m_canvas->setEditable(edit && DSS_APP_CANVAS_EDIT);
    m_overlaysDock.add->setEnabled(edit);
    m_overlaysDock.remove->setEnabled(edit && overlay);
    m_overlaysDock.copy->setEnabled(edit && overlay);
    m_sourcesDock.add->setEnabled(edit && overlay);
    m_sourcesDock.remove->setEnabled(edit && source);
    m_sourcesDock.forward->setEnabled(
        edit && source && dss::sourceStepIndex(*overlay, source->id, +1).has_value());
    m_sourcesDock.backward->setEnabled(
        edit && source && dss::sourceStepIndex(*overlay, source->id, -1).has_value());
    const PropertiesDock &d = m_propertiesDock;
    for (QWidget *w : std::initializer_list<QWidget *>{d.name, d.x, d.y, d.width, d.height,
                                                       d.rotation, d.flipHorizontal,
                                                       d.flipVertical, d.replaceImage, d.loop,
                                                       d.content, d.family, d.weight,
                                                       d.fontSize, d.color, d.align})
        w->setEnabled(edit && source);
    // Transformieren (P09b): Kopieren geht auch ohne Bearbeiten, Einfügen nach dem Kopieren.
    if (m_transformMenu) {
        for (QAction *a : std::as_const(m_transformActions))
            a->setEnabled(edit && source);
        m_copyTransform->setEnabled(source != nullptr);
        m_pasteTransform->setEnabled(edit && source && m_copiedTransform);
        d.copyTransform->setEnabled(m_copyTransform->isEnabled());
        d.pasteTransform->setEnabled(m_pasteTransform->isEnabled());
        d.resetTransform->setEnabled(m_resetTransform->isEnabled());
    }
    // «Am Ende anhalten» ist mit «Wiederholen» ausgegraut (Entscheid 9).
    d.holdAtEnd->setEnabled(edit && source && !source->loop);
    m_controlDock.overlayWidth->setEnabled(edit && overlay);
    m_controlDock.overlayHeight->setEnabled(edit && overlay);
    if (!(edit && source))
        d.italic->setEnabled(false);
    // Ausrichten auf der Leinwand bleibt in P08 ausgegraut.
    for (QToolButton *b : d.alignButtons)
        b->setEnabled(false);
}

void MainWindow::selectFirstOverlay()
{
    selectOverlay(dss::firstOverlay(m_client->listData().layout));
}

void MainWindow::scrollPropertiesToKindPage()
{
    // Oberkante der Felder je Art an den oberen Rand (nur verschieben, kein Umbau).
    m_propertiesDock.scroll->verticalScrollBar()->setValue(m_propertiesDock.kindPages->y());
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
