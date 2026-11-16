#include "mainwindow.h"

#include "canvasimage.h"
#include "canvaswidget.h"
#include "chatbotpage.h"
#include "configpaths.h"
#include "coreclient.h"
#include "dockviews.h"
#include "docks.h"
#include "headerbar.h"
#include "obssourcedialog.h"
#include "symbols.h"
#if DSS_APP_SETTINGS_DIALOG
#include "settingsdialog.h"
#endif
#if DSS_APP_CANVAS_IMAGE
#include "overlayimage.h"
#endif
#if DSS_APP_RENDERER
#include "mediapreview.h"
#include "sourcerenderer.h"
#endif
#include "version.h"
#include "windowdata.h"
#include "windowedit.h"

#include <QAction>
#include <QCheckBox>
#include <QClipboard>
#include <QGuiApplication>
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
#include <QSlider>
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

// Liste und Zustand einer Zeile «Ton …» im Dock «Steuerung» (P12b b5, P14 t)
void showAudioRouteRow(const dss::AudioRouteRow &row, QComboBox *box, QLabel *state, const QString &boxToolTip,
                       bool coreConnected)
{
    {
        const QSignalBlocker block(box);
        // Neu füllen nur, wenn sich die Liste geändert hat (offene Liste bleibt offen).
        bool same = box->count() == row.choices.size();
        for (int i = 0; same && i < box->count(); ++i)
            same = box->itemData(i).toString() == row.choices.at(i).name && box->itemText(i) == row.choices.at(i).label;
        if (!same) {
            box->clear();
            for (const dss::AudioOutputChoice &c : row.choices)
                box->addItem(c.label, c.name);
        }
        box->setCurrentIndex(row.current);
    }
    box->setToolTip(QStringLiteral("%1: %2").arg(boxToolTip, box->currentText()));
    box->setEnabled(row.canEdit);
    // Ohne Kern ausgegraut wie die Zeilen «Ausgabe» und «OBS».
    state->setEnabled(coreConnected);
    state->setText(row.text);
    state->setToolTip(row.toolTip);
    const QString stateKey = row.problem ? QStringLiteral("problem") : QStringLiteral("normal");
    if (state->property("state").toString() != stateKey) {
        state->setProperty("state", stateKey);
        // Das Stylesheet wertet [state="…"] nur beim Polieren aus.
        state->style()->unpolish(state);
        state->style()->polish(state);
    }
}

} // namespace

MainWindow::MainWindow(const Theme &theme, bool keepLayout, dss::CoreClient *client,
                       QWidget *parent)
    : QMainWindow(parent), m_theme(theme), m_keepLayout(keepLayout), m_client(client)
{
    m_canvasImageDir = dss::canvasImageDir();
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
#if DSS_APP_CANVAS_IMAGE
    // Bild des gewählten Overlays (P10-vor, Teil B 2). Strg+Umschalt+B: weder im Fenster noch in
    // Xfce belegt (geprüft 2026-11-03, xfconf-query -c xfce4-keyboard-shortcuts -l -v).
    m_canvasImageAction = file->addAction(QStringLiteral("Leinwand als &Bild speichern"), this,
                                          &MainWindow::saveCanvasImage);
    m_canvasImageAction->setObjectName(QStringLiteral("canvasImageAction"));
    m_canvasImageAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    m_canvasImageAction->setEnabled(false);
    file->addSeparator();
#endif
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
    QMenu *tools = bar->addMenu(QStringLiteral("&Werkzeuge"));
#if DSS_APP_SETTINGS_DIALOG
    // Einstellungen des Kerns (P10-vor, Teil B 1); nur mit Verbindung zum Kern.
    m_settingsAction = tools->addAction(QStringLiteral("&Einstellungen …"), this, &MainWindow::showSettings);
    m_settingsAction->setObjectName(QStringLiteral("settingsAction"));
    m_settingsAction->setEnabled(false);
#else
    tools->setEnabled(false);
#endif

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

    // P13: Reiter «Chat-Bot» (chatbotpage.h); ohne Kern zeigt die Kopfzeile «Twitch: –».
    m_chatBot = new ChatBotPage(m_modules);
    m_chatBot->setHeader(dss::chatBotHeaderView(dss::ConnectionState::Off, std::nullopt));
    m_modules->addWidget(m_chatBot);
    column->addWidget(m_modules, 1);

    connect(m_header, &HeaderBar::moduleSelected, this, &MainWindow::onModuleSelected);
    setCentralWidget(central);
}

void MainWindow::onModuleSelected(int module)
{
    m_modules->setCurrentIndex(module);
    // P13 (Entscheid 2026-11-15): Docks des Editors nur im Reiter «Overlays». Die Anordnung beim
    // Verlassen gilt fürs Speichern, solange die Docks ausgeblendet sind.
    const bool editor = module == HeaderBar::Overlays;
    const QList<QDockWidget *> docks = editorDocks();
    QList<bool> visible;
    for (QDockWidget *d : docks)
        visible.append(!d->isHidden()); // isVisible wäre vor show() immer false
    if (!editor && !m_moduleDocks.docksHidden())
        m_editorLayout = saveState(kLayoutVersion);
    const QList<bool> next = m_moduleDocks.switchTo(editor, visible);
    for (qsizetype i = 0; i < next.size(); ++i)
        docks.at(i)->setVisible(next.at(i));
    // Im Reiter «Chat-Bot» kein Dock über das Menü einblenden.
    m_docksMenu->setEnabled(editor);
}

QList<QDockWidget *> MainWindow::editorDocks() const
{
    QList<QDockWidget *> docks = {m_overlaysDock.dock, m_sourcesDock.dock, m_propertiesDock.dock,
                                  m_controlDock.dock};
    if (m_mediaDock.dock)
        docks.append(m_mediaDock.dock);
    return docks;
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
    QList<QDockWidget *> docks = {m_overlaysDock.dock, m_sourcesDock.dock,
                                  m_propertiesDock.dock, m_controlDock.dock};
    QList<int> widths = {230, 230, 370, 440};
#if DSS_APP_MEDIA_DOCK
    // P10b (Teil B 2): ohne gespeicherte Anordnung eigenes Dock zwischen «Quellen» und
    // «Eigenschaften»; mit gespeicherter Anordnung Reiter neben «Quellen» (restoreLayout).
    setupMediaDock();
    docks.insert(2, m_mediaDock.dock);
    widths = {200, 200, 300, 330, 400};
#endif
    addDockWidget(Qt::BottomDockWidgetArea, docks.first());
    for (qsizetype i = 1; i < docks.size(); ++i)
        splitDockWidget(docks.at(i - 1), docks.at(i), Qt::Horizontal);
    resizeDocks(docks, widths, Qt::Horizontal);
    resizeDocks({docks.first()}, {215}, Qt::Vertical);
    for (QDockWidget *d : docks)
        m_docksMenu->addAction(d->toggleViewAction());
#if DSS_APP_BROWSER_SOURCE
    // Schalter «Ausgabe» links neben jedem Overlay (E-24), wie ein Kästchen.
    m_overlaysDock.tree->setOutputIcons(loadSymbol(QStringLiteral("kaestchen-an.svg"), QSize(12, 12)),
                                        loadSymbol(QStringLiteral("kaestchen-aus.svg"), QSize(12, 12)));
#endif
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
    // Laufende Übernahmen (P10-vor, Punkt 67): immer da, leer ohne Übernahme (keine Änderung
    // der Anordnung beim Wechsel).
    m_importStatus = new QLabel(this);
    m_importStatus->setObjectName(QStringLiteral("importStatus"));
    statusBar()->addPermanentWidget(m_importStatus);
    // Meldungen des Overlay-Servers (P10, Teil C 2): immer da, leer ohne Meldung.
    m_serverStatus = new QLabel(this);
    m_serverStatus->setObjectName(QStringLiteral("serverStatus"));
    statusBar()->addPermanentWidget(m_serverStatus);
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
    if (state.isEmpty())
        return;
    if (!restoreState(state, kLayoutVersion)) {
        qWarning("Anordnung der Docks aus %s nicht lesbar, Standard gilt", qPrintable(layoutFile()));
        return;
    }
    // P10b (Teil B 2): Eine Anordnung ohne das Dock «Medien» (gespeichert vor P10b oder ohne
    // DSS_APP_MEDIA_DOCK) lässt es, wo es beim Aufbau war (QMainWindow::restoreState); hier kommt
    // es als Reiter zu «Quellen», bei Reitern «Overlays»/«Quellen» also als dritter Reiter.
    // https://doc.qt.io/qt-6.8/qmainwindow.html#restoreState
    // https://doc.qt.io/qt-6.8/qmainwindow.html#tabifyDockWidget
    if (m_mediaDock.dock && !settings.value(QStringLiteral("fenster/medien"), false).toBool())
        tabifyDockWidget(m_sourcesDock.dock, m_mediaDock.dock);
}

void MainWindow::saveLayout()
{
    QDir().mkpath(QFileInfo(layoutFile()).absolutePath());
    QSettings settings(layoutFile(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("fenster/geometrie"), saveGeometry());
    // P13: Im Reiter «Chat-Bot» sind die Docks ausgeblendet; gespeichert wird die Anordnung von «Overlays».
    settings.setValue(QStringLiteral("fenster/anordnung"),
                      m_moduleDocks.docksHidden() ? m_editorLayout : saveState(kLayoutVersion));
    // P10b: Enthält die Anordnung das Dock «Medien»? (restoreLayout)
    settings.setValue(QStringLiteral("fenster/medien"), m_mediaDock.dock != nullptr);
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
    connect(m_client, &dss::CoreClient::obsStatusChanged, this, &MainWindow::refreshConnections); // Tooltip
    // P10c: Klick auf den OBS-Knopf; ohne Verbindung zum Kern ist die Aktion gesperrt.
    connect(m_header, &HeaderBar::obsClicked, this, [this] {
        if (m_settingsAction)
            m_settingsAction->trigger();
    });
    connectChatBot();
    connect(m_client, &dss::CoreClient::overlayListChanged, this, &MainWindow::refreshOverlayList);
    connect(m_client, &dss::CoreClient::overlayChanged, this, [this](const QUuid &id) {
        if (id == m_selectedOverlay)
            refreshOverlay();
    });
    connect(m_client, &dss::CoreClient::errorReceived, this, &MainWindow::onCoreError);
    // P14 d: Laufzustand (Markierung «live», Leinwand, Bereich «Chat-Bot»)
    m_liveTimer = new QTimer(this);
    m_liveTimer->setSingleShot(true);
    connect(m_liveTimer, &QTimer::timeout, this, &MainWindow::refreshLiveRow);
    connect(m_client, &dss::CoreClient::liveChanged, this, &MainWindow::refreshOverlay);
    connect(m_propertiesDock.hideLive, &QPushButton::clicked, this, &MainWindow::hideLive);
    connect(m_client, &dss::CoreClient::pendingImportsChanged, this, [this] {
        m_importStatus->setText(dss::importStatusText(m_client->pendingImports()));
    });
#if DSS_APP_BROWSER_SOURCE
    connect(m_client, &dss::CoreClient::browserSourceChanged, this, &MainWindow::refreshBrowserSource);
    connect(m_client, &dss::CoreClient::serverStatusChanged, this, [this] {
        refreshServerStatus();
        refreshBrowserSource(); // verbundene Seiten in «Ausgabe: …»
    });
    connect(m_controlDock.copyAddress, &QPushButton::clicked, this, &MainWindow::copyBrowserSource);
    connect(m_client, &dss::CoreClient::obsSourcesChanged, this, &MainWindow::refreshObsSource);
    connect(m_client, &dss::CoreClient::connectionStatesChanged, this, &MainWindow::refreshObsSource);
    connect(m_controlDock.obsSourceButton, &QPushButton::clicked, this, &MainWindow::showObsSourceDialog);
#endif
    // P12b b5: Zeile «Ton Videos»
    connect(m_client, &dss::CoreClient::settingsChanged, this, &MainWindow::refreshAudioRoute);
    connect(m_client, &dss::CoreClient::audioOutputsChanged, this, &MainWindow::refreshAudioRoute);
    connect(m_client, &dss::CoreClient::settingsUpdateFinished, this, [this](const dss::ChangeResult &result) {
        if (!m_audioRouteRequests.removeOne(result.requestId))
            return; // Antwort an den Dialog «Einstellungen»
        if (!result.ok)
            showStatus(result.message);
        else if (!result.warning.isEmpty())
            showStatus(result.warning);
        // Felder nachführen, die während der Änderung nicht gesetzt wurden; bei Fehler zurück auf
        // den Wert des Kerns.
        refreshAudioRoute();
    });
    connect(m_controlDock.audioOutput, &QComboBox::activated, this, [this](int index) {
        dss::AudioRouteChange change;
        change.output = m_controlDock.audioOutput->itemData(index).toString();
        sendAudioRoute(QString::fromLatin1(dss::kAudioPurposeVideo), change);
    });
    connect(m_controlDock.audioOffset, &QSpinBox::valueChanged, this, [this](int value) {
        dss::AudioRouteChange change;
        change.offsetMs = value;
        sendAudioRoute(QString::fromLatin1(dss::kAudioPurposeVideo), change);
    });
    // P14 t: Zeile «Ton Chat-Bot»
    connect(m_controlDock.actionsAudioOutput, &QComboBox::activated, this, [this](int index) {
        dss::AudioRouteChange change;
        change.output = m_controlDock.actionsAudioOutput->itemData(index).toString();
        sendAudioRoute(QString::fromLatin1(dss::kAudioPurposeActions), change);
    });
    connect(m_overlaysDock.tree, &QTreeWidget::currentItemChanged, this,
            &MainWindow::onOverlayItemChanged);
    // Gewählt ist die markierte Zeile, nicht die aktuelle (P10b): Nach dem Neufüllen der Liste
    // kann Zeile 0 aktuell sein, ohne markiert zu sein (keine Quelle gewählt). Ein Klick darauf
    // ändert dann nur die Markierung, currentItemChanged käme nicht, und die Eigenschaften
    // blieben leer.
    connect(m_sourcesDock.list, &QListWidget::itemSelectionChanged, this, [this] {
        const QList<QListWidgetItem *> marked = m_sourcesDock.list->selectedItems();
        onSourceItemChanged(marked.isEmpty() ? nullptr : marked.first());
    });
    // Wählen auf der Leinwand: dieselbe Auswahl wie im Dock «Quellen» (P09, Teil C).
    connect(m_canvas, &CanvasWidget::sourceClicked, this, &MainWindow::onCanvasSourceClicked);
    m_statusMessage->setText(m_client->statusText());
    refreshGauges(dss::SystemStats());
    if (editingAllowed())
        connectEditing();
#if DSS_APP_MEDIA_DOCK
    if (m_mediaDock.dock)
        connectMedia();
#endif
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

void MainWindow::showSettings()
{
#if DSS_APP_SETTINGS_DIALOG
    if (!m_client)
        return;
    if (!m_settingsDialog)
        m_settingsDialog = new SettingsDialog(m_client, this);
    m_settingsDialog->showFromCore();
#endif
}

void MainWindow::showTwitchSettings()
{
#if DSS_APP_SETTINGS_DIALOG
    // Wie der OBS-Knopf: ohne Verbindung zum Kern gesperrt.
    if (!m_settingsAction || !m_settingsAction->isEnabled())
        return;
    showSettings();
    m_settingsDialog->focusTwitchSection();
#endif
}

void MainWindow::connectChatBot()
{
    // P13: Twitch-Knopf, Kopfzeile des Reiters «Chat-Bot» und Bereich «Ereignisse» (Plan 1.5, 2.7)
    connect(m_header, &HeaderBar::twitchClicked, this, &MainWindow::showTwitchSettings);
    connect(m_client, &dss::CoreClient::twitchStatusChanged, this, &MainWindow::refreshConnections);
    connect(m_client, &dss::CoreClient::connectedChanged, this, &MainWindow::refreshChatBotHeader);
    connect(m_client, &dss::CoreClient::eventHistoryReceived, this, [this](const QList<dss::events::Event> &events) {
        m_eventRows.reset(events);
        m_chatBot->resetEvents(m_eventRows.rows());
    });
    connect(m_client, &dss::CoreClient::eventReceived, this, [this](const dss::events::Event &event) {
        const int removed = m_eventRows.append(event);
        m_chatBot->appendEvent(m_eventRows.rows().last(), removed);
    });
    connect(m_client, &dss::CoreClient::eventsDropped, this,
            [this](qint64 count) { showStatus(dss::eventsDroppedText(count)); });
    // P14 e2-11: Schalter «Chat-Bot aktiv». Den Stand zeigt erst actions_status; bei Ablehnung
    // (Meldung über onChangeFinished) wieder den Stand des Kerns.
    connect(m_client, &dss::CoreClient::actionsStatusChanged, this, &MainWindow::refreshChatBotHeader);
    connect(m_chatBot, &ChatBotPage::enabledClicked, this, [this](bool enabled) {
        m_actionsEnabledRequest = m_client->sendActionsEdit(dss::actionsSetEnabledEdit(enabled));
        if (m_actionsEnabledRequest == 0)
            refreshChatBotHeader();
    });
    connect(m_client, &dss::CoreClient::changeFinished, this, [this](const dss::ChangeResult &result) {
        if (m_actionsEnabledRequest == 0 || result.requestId != m_actionsEnabledRequest)
            return;
        m_actionsEnabledRequest = 0;
        if (!result.ok)
            refreshChatBotHeader();
    });
    m_client->subscribeEvents(dss::chatBotEventFilters());
}

void MainWindow::refreshChatBotHeader()
{
    const bool connected = m_client->isConnected();
    const dss::ConnectionState channel = connected
        ? m_client->connectionStates().value(dss::Connection::Twitch, dss::ConnectionState::Off)
        : dss::ConnectionState::Off;
    m_chatBot->setHeader(dss::chatBotHeaderView(channel, connected ? m_client->twitchStatus() : std::nullopt,
                                                 m_client->actionsStatus()));
}

void MainWindow::saveCanvasImage()
{
#if DSS_APP_CANVAS_IMAGE
    const std::optional<dss::Overlay> overlay = canvasOverlay(); // wie die Leinwand, mit Laufzustand
    if (!overlay)
        return;
    QString path;
    QString error;
    if (dss::saveOverlayImage(*m_renderer, *overlay, m_canvasImageDir, QDateTime::currentDateTime(),
                              &path, &error))
        showStatus(QStringLiteral("Leinwand gespeichert: %1").arg(path));
    else
        showStatus(error);
#endif
}

void MainWindow::onCoreConnectedChanged(bool connected)
{
    if (m_settingsAction)
        m_settingsAction->setEnabled(connected);
    m_coreStatus->setText(dss::coreStatusText(connected));
    m_coreStatus->setProperty("state", connected ? QStringLiteral("connected")
                                                 : QStringLiteral("disconnected"));
    // Das Stylesheet wertet [state="…"] nur beim Polieren aus.
    m_coreStatus->style()->unpolish(m_coreStatus);
    m_coreStatus->style()->polish(m_coreStatus);
#if DSS_APP_BROWSER_SOURCE
    refreshServerStatus();
#endif
    if (!connected) {
        // Ohne Verbindung keine Werte und keinen Verlauf zeigen.
        m_cpuHistory.clear();
        m_ramHistory.clear();
        m_lanHistory.clear();
        refreshGauges(dss::SystemStats());
        m_selectOnResult.clear();
        m_audioRouteRequests.clear();
    } else {
        // Ausgang und Versatz für die Zeile «Ton Videos» (P12b b5)
        m_client->requestSettings();
        if (editingAllowed()) {
            // Auswahl der Schriften für Textquellen (Teil F)
            m_client->requestFonts();
        }
    }
    refreshAudioRoute();
#if DSS_APP_RENDERER
    // Dateien der Quellen liegen im Datenordner des Kerns (hello.data_dir, P09).
    if (connected)
        m_renderer->setDataDir(m_client->coreHello().dataDir);
    if (connected && m_mediaPreviews)
        m_mediaPreviews->setDataDir(m_client->coreHello().dataDir); // P10b: Vorschau im Dock «Medien»
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
    m_header->setConnectionToolTip(dss::Connection::Obs,
                                   dss::obsToolTip(states.value(dss::Connection::Obs, dss::ConnectionState::Off),
                                                   m_client->obsStatus()));
    // P13: Problem des Bots im Tooltip, Zustand von Kanal und Bot in der Kopfzeile «Chat-Bot»
    m_header->setConnectionToolTip(dss::Connection::Twitch,
                                   dss::twitchToolTip(states.value(dss::Connection::Twitch, dss::ConnectionState::Off),
                                                      m_client->twitchStatus()));
    refreshChatBotHeader();
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
        if (o) {
            QString tip = QStringLiteral("%1 · %2x%3 · %4 Quelle(n)")
                              .arg(o->address).arg(o->width).arg(o->height).arg(o->sourceCount);
#if DSS_APP_BROWSER_SOURCE
            item->setData(0, OverlayTree::kOutputRole, o->output);
            tip += QLatin1Char('\n') + dss::outputToolTip(o->output);
#endif
            item->setToolTip(0, tip);
        }
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
        const dss::LiveElements live = m_client ? m_client->live(m_selectedOverlay) : dss::LiveElements();
        for (const dss::SourceRow &row : dss::sourceRows(*overlay)) {
            // Auge wie in der Datei; der Laufzustand ändert sie nicht und zeigt sich als «live»
            auto *item = new QListWidgetItem(row.visible ? shown : hidden, row.name, list);
            item->setData(SourceList::kIdRole, row.id);
            item->setData(SourceList::kLiveRole, live.contains(row.id));
            item->setToolTip(live.contains(row.id)
                                 ? dss::visibilityToolTip(row.visible) + QStringLiteral(", live durch den Chat-Bot")
                                 : dss::visibilityToolTip(row.visible));
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
    // P14 d: Leinwand und Zeichner zeigen den Laufzustand wie die Seite (K-07)
    const std::optional<dss::Overlay> drawn = canvasOverlay();
    m_canvas->setOverlay(drawn);
    m_canvas->setMediaSizes(m_client ? m_client->mediaSizes() : dss::MediaSizes());
    m_canvas->setSelectedSource(m_selectedSource);
#if DSS_APP_RENDERER
    m_renderer->setOverlay(drawn ? &*drawn : nullptr);
#endif
    if (m_canvasImageAction)
        m_canvasImageAction->setEnabled(overlay.has_value()); // ohne Overlay ausgegraut
    // Auflösung im Dock «Steuerung» (P09); während eine Änderung unterwegs ist, bleibt der Wert.
    if (!m_client || !m_client->isChangePending(dss::overlaySizeKey(m_selectedOverlay))) {
        const QSignalBlocker blockWidth(m_controlDock.overlayWidth);
        const QSignalBlocker blockHeight(m_controlDock.overlayHeight);
        m_controlDock.overlayWidth->setValue(overlay ? overlay->width : dss::kNewOverlayWidth);
        m_controlDock.overlayHeight->setValue(overlay ? overlay->height : dss::kNewOverlayHeight);
    }
#if DSS_APP_BROWSER_SOURCE
    // Adresse der Browserquelle zum gewählten Overlay (fragt nur bei einem anderen Overlay neu).
    if (m_client)
        m_client->requestBrowserSource(m_selectedOverlay);
    refreshBrowserSource();
#endif
    refreshProperties();
}

std::optional<dss::Overlay> MainWindow::canvasOverlay() const
{
    std::optional<dss::Overlay> overlay = selectedOverlay();
    if (overlay && m_client)
        overlay = dss::applyLiveState(*overlay, m_client->live(overlay->id));
    return overlay;
}

void MainWindow::refreshLiveRow()
{
    const dss::LiveElements live = m_client ? m_client->live(m_selectedOverlay) : dss::LiveElements();
    const dss::LiveRow row = dss::liveRow(live, m_selectedSource, m_client && m_client->isConnected());
    m_propertiesDock.liveState->setText(row.text);
    m_propertiesDock.hideLive->setEnabled(row.canHide);
    if (!m_liveTimer)
        return;
    const int next = dss::liveRefreshMs(live);
    if (next < 0)
        m_liveTimer->stop();
    else
        m_liveTimer->start(next);
}

void MainWindow::hideLive()
{
    if (!m_client || m_selectedOverlay.isNull() || m_selectedSource.isNull())
        return;
    if (m_client->sendLiveReset(m_selectedOverlay, {m_selectedSource}) == 0)
        showStatus(QStringLiteral("Keine Verbindung zum Kern, Änderung nicht geschickt"));
}

void MainWindow::refreshBrowserSource()
{
    if (!m_client)
        return;
    const dss::BrowserSourceField field =
        dss::browserSourceField(m_selectedOverlay, m_client->browserSource());
    QLineEdit *edit = m_controlDock.browserSource;
    if (edit->text() != field.text)
        edit->setText(field.text);
    edit->setPlaceholderText(field.placeholder);
    edit->setToolTip(field.text.isEmpty() ? field.placeholder : field.text); // Feld schneidet ab
    edit->setCursorPosition(0);
    const dss::OverlaySummary *summary = m_client->listData().summary(m_selectedOverlay);
    const std::optional<dss::ServerStatusInfo> status = m_client->serverStatus();
    std::optional<int> pages;
    if (status && status->enabled)
        pages = status->pages.value(m_selectedOverlay, 0);
    m_controlDock.outputState->setText(
        dss::outputStateText(summary ? std::optional<bool>(summary->output) : std::nullopt, pages));
    refreshObsSource();
    refreshActions();
}

void MainWindow::refreshObsSource()
{
    if (!m_client)
        return;
    const dss::ObsSourceRow row = dss::obsSourceRow(
        !m_selectedOverlay.isNull(),
        m_client->connectionStates().value(dss::Connection::Obs, dss::ConnectionState::Off),
        m_client->obsSource(m_selectedOverlay));
    QLabel *state = m_controlDock.obsSourceState;
    state->setText(row.text);
    state->setToolTip(row.text);
    const QString stateKey = row.problem ? QStringLiteral("problem") : QStringLiteral("normal");
    if (state->property("state").toString() != stateKey) {
        state->setProperty("state", stateKey);
        // Das Stylesheet wertet [state="…"] nur beim Polieren aus.
        state->style()->unpolish(state);
        state->style()->polish(state);
    }
    QPushButton *button = m_controlDock.obsSourceButton;
    button->setText(row.buttonText);
    button->setToolTip(row.buttonToolTip);
    button->setEnabled(row.canAct);
}

void MainWindow::refreshAudioRoute()
{
    const std::optional<dss::SettingsInfo> settings = m_client ? m_client->settings() : std::nullopt;
    const std::optional<dss::AudioOutputsInfo> outputs = m_client ? m_client->audioOutputs() : std::nullopt;
    const bool connected = m_client && m_client->isConnected();
    const dss::AudioRouteRow row = dss::audioRouteRow(settings, outputs, QString::fromLatin1(dss::kAudioPurposeVideo));
    showAudioRouteRow(row, m_controlDock.audioOutput, m_controlDock.audioOutputState,
                      QStringLiteral("Ausgang für den Ton der Videos"), connected);
    QSpinBox *offset = m_controlDock.audioOffset;
    {
        const QSignalBlocker block(offset);
        offset->setRange(row.offsetMinMs, row.offsetMaxMs);
        // Während eine eigene Änderung unterwegs ist, bleibt der eingestellte Wert.
        if (m_audioRouteRequests.isEmpty() && offset->value() != row.offsetMs)
            offset->setValue(row.offsetMs);
    }
    offset->setEnabled(row.canEdit);
    // P14 t: Zeile «Ton Chat-Bot», ohne Versatz
    showAudioRouteRow(dss::audioRouteRow(settings, outputs, QString::fromLatin1(dss::kAudioPurposeActions)),
                      m_controlDock.actionsAudioOutput, m_controlDock.actionsAudioOutputState,
                      QStringLiteral("Ausgang für den Ton des Chat-Bots"), connected);
}

void MainWindow::sendAudioRoute(const QString &purpose, const dss::AudioRouteChange &change)
{
    dss::SettingsUpdate update;
    update.audioRoutes.insert(purpose, change);
    if (const qint64 id = m_client->sendSettingsUpdate(update); id > 0)
        m_audioRouteRequests.append(id);
    else
        showStatus(QStringLiteral("Keine Verbindung zum Kern, Änderung nicht geschickt"));
}

void MainWindow::showObsSourceDialog()
{
    if (m_selectedOverlay.isNull())
        return;
    if (!m_obsSourceDialog) {
        m_obsSourceDialog = new ObsSourceDialog(m_client, this);
        connect(m_obsSourceDialog, &ObsSourceDialog::succeeded, this, &MainWindow::showStatus);
    }
    const dss::ObsSourceInfo source = m_client->obsSource(m_selectedOverlay);
    const dss::OverlaySummary *summary = m_client->listData().summary(m_selectedOverlay);
    const bool addToScene = dss::obsSourceRow(
        true, m_client->connectionStates().value(dss::Connection::Obs, dss::ConnectionState::Off), source).addsToScene;
    m_obsSourceDialog->showFor(m_selectedOverlay,
                               addToScene ? source.inputName : (summary ? summary->name : QString()), addToScene);
}

void MainWindow::refreshServerStatus()
{
    const dss::ServerMessages messages =
        dss::serverMessages(m_client && m_client->isConnected(), m_client ? m_client->serverStatus() : std::nullopt);
    m_controlDock.serverMessage->setText(messages.text);
    m_serverStatus->setText(messages.shortText);
    m_serverStatus->setToolTip(messages.text);
}

void MainWindow::copyBrowserSource()
{
    const dss::BrowserSourceField field =
        dss::browserSourceField(m_selectedOverlay, m_client->browserSource());
    if (!field.canCopy)
        return;
    QGuiApplication::clipboard()->setText(field.text);
    showStatus(QStringLiteral("Adresse der Browserquelle kopiert"));
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
    refreshLiveRow();
    if (source && source->kind == dss::SourceKind::Video) {
        const QSignalBlocker block(d.loop);
        d.loop->setChecked(source->loop);
        const QSignalBlocker blockHold(d.holdAtEnd);
        d.holdAtEnd->setChecked(source->holdAtEnd);
        // P12b b5: Regler bleibt, solange seine Änderung unterwegs ist (wie die Zahlenfelder)
        if (!pending(QStringLiteral("volume"))) {
            const QSignalBlocker blockVolume(d.volume);
            d.volume->setValue(source->volume);
            d.volumeValue->setText(QStringLiteral("%1 %").arg(source->volume));
        }
        const QSignalBlocker blockMuted(d.muted);
        if (!pending(QStringLiteral("muted")))
            d.muted->setChecked(source->muted);
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
                                                       d.volume, d.volumeValue, d.muted,
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
#if DSS_APP_BROWSER_SOURCE
    // Browserquelle (Teil C 1): ohne gewähltes Overlay ausgegraut. Kopieren geht auch ohne
    // Bearbeiten, ein neuer Token ist eine Änderung.
    const bool connected = m_client && m_client->isConnected();
    const bool canCopy = connected && overlay
                         && dss::browserSourceField(m_selectedOverlay, m_client->browserSource()).canCopy;
    m_controlDock.browserSource->setEnabled(connected && overlay);
    m_controlDock.copyAddress->setEnabled(canCopy);
    m_controlDock.newToken->setEnabled(edit && overlay);
    m_controlDock.outputState->setEnabled(connected && overlay);
    m_controlDock.obsSourceState->setEnabled(connected && overlay);
#endif
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
