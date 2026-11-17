#include "camerapage.h"

#include "actionview.h"
#include "camerasetupdialog.h"
#include "coreclient.h"
#include "dialogs.h"
#include "panels.h"
#include "presetnamedialog.h"
#include "symbols.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int kPresetIdRole = Qt::UserRole;

// Grosser Richtungsknopf für Steuerkreuz und Zoom (Entscheid 1 des Nutzers, 2026-11-17): Symbol und
// Ersatztext wie symbolButton (panels.h), die Grösse aus der Schrift statt der 56 px des Entwurfs.
QToolButton *directionButton(const QString &symbolFile, const QString &fallback, const QString &toolTip,
                             QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(fallback);
    const int line = button->fontMetrics().height();
    const QSize iconSize(line * 3 / 2, line * 3 / 2);
    if (!symbolFile.isEmpty()) {
        const QIcon icon = loadSymbol(symbolFile, iconSize);
        if (!icon.isNull())
            button->setIcon(icon);
    }
    button->setIconSize(iconSize);
    button->setFixedSize(line * 3, line * 3);
    button->setToolTip(toolTip);
    button->setAccessibleName(toolTip);
    button->setFocusPolicy(Qt::StrongFocus);
    button->setEnabled(false);
    return button;
}

// Farbe über die Eigenschaft state im Theme (wie headerbar.cpp, setConnectionState)
void setState(QWidget *w, const QString &state)
{
    if (w->property("state").toString() == state)
        return;
    w->setProperty("state", state);
    w->style()->unpolish(w);
    w->style()->polish(w);
}

// Hinweis als Text ohne Kasten (Entscheid 3), Höhe für eine Zeile immer da (Regel 9)
QLabel *hintLine(const QString &objectName, QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setObjectName(objectName);
    label->setTextFormat(Qt::PlainText);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    label->setMinimumHeight(label->fontMetrics().lineSpacing());
    return label;
}

} // namespace

CameraPage::CameraPage(const Theme &theme, QWidget *parent) : QWidget(parent), m_theme(theme)
{
    setObjectName(QStringLiteral("cameraPage"));
    setAttribute(Qt::WA_StyledBackground);
    m_clock.start();
    m_holdTimer = new QTimer(this);
    m_holdTimer->setSingleShot(true);
    connect(m_holdTimer, &QTimer::timeout, this, &CameraPage::onHoldTimer);

    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);
    column->addWidget(createHeader());

    // Hinweis über den Spalten, solange der Dienst nicht antwortet (Entwurf Seite 4 E)
    m_blockedHint = hintLine(QStringLiteral("cameraBlockedHint"), this);
    column->addWidget(m_blockedHint);

    // Drei Spalten mit Teilern wie «Live» im Reiter «Chat-Bot»; keine lässt sich zuschieben.
    m_columns = new QSplitter(Qt::Horizontal, this);
    m_columns->setObjectName(QStringLiteral("cameraColumns"));
    m_columns->setChildrenCollapsible(false);
    // Links mit Bildlauf wie das Dock «Eigenschaften»: Die Spalte streckt das Fenster nicht über
    // 1280x800 hinaus (scripts/bildschirmfoto), bei kleiner Höhe läuft sie senkrecht.
    auto *leftScroll = new QScrollArea(m_columns);
    leftScroll->setObjectName(QStringLiteral("cameraLeftScroll"));
    leftScroll->setWidgetResizable(true);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *left = new QWidget;
    left->setObjectName(QStringLiteral("cameraLeftColumn"));
    auto *leftColumn = new QVBoxLayout(left);
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(6);
    leftColumn->addWidget(dockPanel(QStringLiteral("Bewegen"), createMovePanel(), left));
    leftColumn->addWidget(dockPanel(QStringLiteral("KI-Verfolgung"), createAiPanel(), left));
    leftColumn->addWidget(dockPanel(QStringLiteral("Positionen"), createPresetsPanel(), left), 1);
    leftScroll->setWidget(left);
    leftScroll->setMinimumWidth(left->minimumSizeHint().width()
                                + leftScroll->verticalScrollBar()->sizeHint().width());
    m_columns->addWidget(leftScroll);

    // Mitte und rechts: Bereiche mit Titel, Inhalt ab e3
    QWidget *image = dockPanel(dss::cameraImageTitle(nullptr), new QWidget, m_columns);
    image->setObjectName(QStringLiteral("cameraImagePanel"));
    m_imageTitle = image->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"));
    m_columns->addWidget(image);
    QWidget *light = dockPanel(QStringLiteral("Licht"), new QWidget, m_columns);
    light->setObjectName(QStringLiteral("cameraLightPanel"));
    m_columns->addWidget(light);
    m_columns->setStretchFactor(0, 0);
    m_columns->setStretchFactor(1, 1);
    m_columns->setStretchFactor(2, 0);
    column->addWidget(m_columns, 1);

    refresh();
}

QWidget *CameraPage::createHeader()
{
    // Kopfzeile mit fester Höhe (Regel 9) wie #chatBotHeader: Ein neuer Zustand ändert nur Texte.
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("cameraHeader"));
    header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *row = new QHBoxLayout(header);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(6);

    m_choice = new QComboBox(header);
    m_choice->setObjectName(QStringLiteral("cameraChoice"));
    m_choice->setAccessibleName(QStringLiteral("Kamera"));
    m_choice->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_choice->setMinimumContentsLength(28);
    connect(m_choice, &QComboBox::currentIndexChanged, this, [this] {
        switchCamera(m_choice->currentData().toString());
        refresh();
    });
    row->addWidget(fieldLabel(QStringLiteral("Kamera"), m_choice, header));
    row->addWidget(m_choice);

    // Feste Breite für den längeren Text (wie #chatBotChatCounter)
    m_wake = new QPushButton(QStringLiteral("Wecken"), header);
    m_wake->setObjectName(QStringLiteral("cameraWake"));
    const int wakeWidth = m_wake->sizeHint().width();
    m_wake->setText(QStringLiteral("Schlafen"));
    m_wake->setMinimumWidth(std::max(wakeWidth, m_wake->sizeHint().width()));
    connect(m_wake, &QPushButton::clicked, this, [this] {
        const dss::CameraEntry *camera = currentCamera();
        if (camera)
            send(dss::cameraWakeCall(camera->id, camera->state != QLatin1String("awake")));
    });
    row->addWidget(m_wake);
    m_setup = new QPushButton(QStringLiteral("Einrichten …"), header);
    m_setup->setObjectName(QStringLiteral("cameraSetup"));
    connect(m_setup, &QPushButton::clicked, this, &CameraPage::openSetup);
    row->addWidget(m_setup);
    row->addStretch(1);

    // Zustand des Dienstes in zwei Zeilen, Platz für beide von Anfang an (wie die Zeile «OBS»)
    auto *state = new QWidget(header);
    auto *stateColumn = new QVBoxLayout(state);
    stateColumn->setContentsMargins(0, 0, 0, 0);
    stateColumn->setSpacing(0);
    m_state = new QLabel(state);
    m_state->setObjectName(QStringLiteral("cameraState"));
    m_state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_stateDetail = new QLabel(state);
    m_stateDetail->setObjectName(QStringLiteral("cameraStateDetail"));
    m_stateDetail->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_stateDetail->setTextFormat(Qt::PlainText);
    stateColumn->addWidget(m_state);
    stateColumn->addWidget(m_stateDetail);
    state->setMinimumHeight(fontMetrics().lineSpacing() * 2);
    state->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    row->addWidget(state, 1);
    return header;
}

QWidget *CameraPage::createMovePanel()
{
    auto *box = new QWidget;
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(8, 6, 8, 8);
    column->setSpacing(6);

    // Hinweis mit «Wecken» daneben; der Knopf behält seinen Platz, auch wenn er fehlt (Regel 9)
    auto *hintRow = new QHBoxLayout;
    hintRow->setSpacing(6);
    m_moveHint = hintLine(QStringLiteral("cameraMoveHint"), box);
    hintRow->addWidget(m_moveHint, 1);
    m_hintWake = new QPushButton(QStringLiteral("Wecken"), box);
    m_hintWake->setObjectName(QStringLiteral("cameraHintWake"));
    QSizePolicy keep = m_hintWake->sizePolicy();
    keep.setRetainSizeWhenHidden(true);
    m_hintWake->setSizePolicy(keep);
    m_hintWake->hide();
    connect(m_hintWake, &QPushButton::clicked, this, [this] {
        if (!m_cameraId.isEmpty())
            send(dss::cameraWakeCall(m_cameraId, true));
    });
    hintRow->addWidget(m_hintWake);
    column->addLayout(hintRow);

    auto *body = new QHBoxLayout;
    body->setSpacing(8);
    // Steuerkreuz 3 × 3: pan +1 = Bild nach rechts, tilt +1 = nach oben (docs/09-kamera-dienst.md)
    struct Direction
    {
        const char *symbol;
        const char *fallback;
        const char *toolTip;
        int pan;
        int tilt;
    };
    static const Direction directions[] = {
        {"nach-links-oben.svg", "↖", "Nach links oben", -1, 1},
        {"nach-oben.svg", "↑", "Nach oben", 0, 1},
        {"nach-rechts-oben.svg", "↗", "Nach rechts oben", 1, 1},
        {"nach-links.svg", "←", "Nach links", -1, 0},
        {nullptr, nullptr, nullptr, 0, 0},
        {"nach-rechts.svg", "→", "Nach rechts", 1, 0},
        {"nach-links-unten.svg", "↙", "Nach links unten", -1, -1},
        {"nach-unten.svg", "↓", "Nach unten", 0, -1},
        {"nach-rechts-unten.svg", "↘", "Nach rechts unten", 1, -1},
    };
    auto *cross = new QGridLayout;
    cross->setSpacing(4);
    for (int i = 0; i < 9; ++i) {
        const Direction &d = directions[i];
        QToolButton *button = nullptr;
        if (!d.symbol) {
            button = directionButton(QString(), QStringLiteral("Mitte"), QStringLiteral("Mitte"), box);
            button->setObjectName(QStringLiteral("cameraHome"));
            m_home = button;
            connect(button, &QToolButton::clicked, this, [this] {
                if (!m_cameraId.isEmpty())
                    send(dss::cameraHomeCall(m_cameraId));
            });
        } else {
            button = directionButton(QString::fromLatin1(d.symbol), QString::fromUtf8(d.fallback),
                                     QString::fromUtf8(d.toolTip), box);
            button->setObjectName(QStringLiteral("cameraMove%1").arg(i));
            const int pan = d.pan;
            const int tilt = d.tilt;
            connect(button, &QToolButton::pressed, this, [this, pan, tilt] { pressDirection(pan, tilt, 0); });
            connect(button, &QToolButton::released, this, &CameraPage::releaseDirection);
        }
        button->installEventFilter(this);
        m_crossButtons.append(button);
        cross->addWidget(button, i / 3, i % 3);
    }
    body->addLayout(cross);

    auto *side = new QGridLayout;
    side->setHorizontalSpacing(8);
    side->setVerticalSpacing(8);
    QToolButton *zoomOut = directionButton(QString(), QStringLiteral("−"), QStringLiteral("Zoom heraus"), box);
    zoomOut->setObjectName(QStringLiteral("cameraZoomOut"));
    QToolButton *zoomIn = directionButton(QString(), QStringLiteral("+"), QStringLiteral("Zoom hinein"), box);
    zoomIn->setObjectName(QStringLiteral("cameraZoomIn"));
    for (QToolButton *b : {zoomOut, zoomIn}) {
        const int zoom = b == zoomIn ? 1 : -1;
        connect(b, &QToolButton::pressed, this, [this, zoom] { pressDirection(0, 0, zoom); });
        connect(b, &QToolButton::released, this, &CameraPage::releaseDirection);
        m_crossButtons.append(b);
    }
    m_zoomText = new QLabel(QStringLiteral("100 %"), box);
    m_zoomText->setObjectName(QStringLiteral("cameraZoomText"));
    m_zoomText->setAlignment(Qt::AlignCenter);
    m_zoomText->setMinimumWidth(m_zoomText->sizeHint().width());
    auto *zoomRow = new QHBoxLayout;
    zoomRow->setSpacing(6);
    zoomRow->addWidget(zoomOut);
    zoomRow->addWidget(m_zoomText);
    zoomRow->addWidget(zoomIn);
    zoomRow->addStretch(1);
    side->addWidget(new QLabel(QStringLiteral("Zoom"), box), 0, 0);
    side->addLayout(zoomRow, 0, 1);

    // «Tempo» und «Schritt»: Einstellungen der Kamera (Entscheid 2), wie im Dialog «Kamera einrichten»
    m_speed = new QSpinBox(box);
    m_speed->setObjectName(QStringLiteral("cameraSpeed"));
    m_speed->setKeyboardTracking(false);
    m_speed->setRange(1, 100);
    m_speed->setSuffix(QStringLiteral(" °/s"));
    keepValueVisible(m_speed);
    connect(m_speed, &QSpinBox::valueChanged, this, [this](int value) {
        send(dss::cameraConfigPutCall(m_cameraId, {{QStringLiteral("speed"), value}}));
    });
    m_step = new QDoubleSpinBox(box);
    m_step->setObjectName(QStringLiteral("cameraStep"));
    m_step->setKeyboardTracking(false);
    m_step->setDecimals(1);
    m_step->setRange(1, 90);
    m_step->setSuffix(QStringLiteral(" °"));
    keepValueVisible(m_step);
    connect(m_step, &QDoubleSpinBox::valueChanged, this, [this](double value) {
        const double rounded = qRound(value * 10) / 10.0;
        const QJsonValue v = rounded == qRound(rounded) ? QJsonValue(qRound(rounded)) : QJsonValue(rounded);
        send(dss::cameraConfigPutCall(m_cameraId, {{QStringLiteral("step_deg"), v}}));
    });
    side->addWidget(fieldLabel(QStringLiteral("Tempo"), m_speed, box), 1, 0);
    side->addWidget(m_speed, 1, 1, Qt::AlignLeft);
    side->addWidget(fieldLabel(QStringLiteral("Schritt"), m_step, box), 2, 0);
    side->addWidget(m_step, 2, 1, Qt::AlignLeft);
    side->setRowStretch(3, 1);
    body->addLayout(side, 1);
    column->addLayout(body);
    return box;
}

QWidget *CameraPage::createAiPanel()
{
    auto *box = new QWidget;
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(8, 6, 8, 8);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    m_aiHint = hintLine(QStringLiteral("cameraAiHint"), box);
    grid->addWidget(m_aiHint, 0, 0, 1, 2);
    m_ai = new QComboBox(box);
    m_ai->setObjectName(QStringLiteral("cameraAi"));
    m_ai->setAccessibleName(QStringLiteral("KI-Verfolgung"));
    m_ai->setPlaceholderText(QStringLiteral("–"));
    m_ai->setMinimumContentsLength(16);
    m_ai->setEnabled(false);
    // Nur eine Wahl des Nutzers schickt die Anfrage; der Stand kommt aus controls (refresh)
    connect(m_ai, &QComboBox::activated, this, [this](int index) {
        if (!m_cameraId.isEmpty())
            send(dss::cameraAiCall(m_cameraId, m_ai->itemData(index).toInt()));
    });
    grid->addWidget(fieldLabel(QStringLiteral("Modus"), m_ai, box), 1, 0);
    grid->addWidget(m_ai, 1, 1, Qt::AlignLeft);
    grid->setColumnStretch(1, 1);
    return box;
}

QWidget *CameraPage::createPresetsPanel()
{
    auto *box = new QWidget;
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    m_presets = new QTreeWidget(box);
    m_presets->setObjectName(QStringLiteral("cameraPresets"));
    m_presets->setAccessibleName(QStringLiteral("Positionen"));
    m_presets->setColumnCount(3);
    m_presets->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Position"), QStringLiteral("Bildwerte")});
    m_presets->setRootIsDecorated(false);
    m_presets->setUniformRowHeights(true);
    m_presets->setSelectionMode(QAbstractItemView::SingleSelection);
    const int digit = m_presets->fontMetrics().horizontalAdvance(QLatin1Char('0'));
    m_presets->header()->setStretchLastSection(true);
    m_presets->setColumnWidth(0, digit * 18);
    m_presets->setColumnWidth(1, digit * 24);
    connect(m_presets, &QTreeWidget::itemSelectionChanged, this, &CameraPage::refresh);
    // Doppelklick = Abrufen (Plan 8)
    connect(m_presets, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (m_view.recallEnabled)
            recallPreset();
    });
    column->addWidget(m_presets, 1);

    m_recall = new QPushButton(QStringLiteral("Abrufen"));
    m_recall->setObjectName(QStringLiteral("cameraPresetRecall"));
    m_save = new QPushButton(QStringLiteral("Aktuelle Position speichern …"));
    m_save->setObjectName(QStringLiteral("cameraPresetSave"));
    m_overwrite = new QPushButton(QStringLiteral("Überschreiben"));
    m_overwrite->setObjectName(QStringLiteral("cameraPresetOverwrite"));
    m_rename = new QPushButton(QStringLiteral("Umbenennen"));
    m_rename->setObjectName(QStringLiteral("cameraPresetRename"));
    m_remove = new QPushButton(QStringLiteral("Löschen"));
    m_remove->setObjectName(QStringLiteral("cameraPresetRemove"));
    m_recallHint = new QLabel;
    m_recallHint->setObjectName(QStringLiteral("cameraRecallHint"));
    connect(m_recall, &QPushButton::clicked, this, &CameraPage::recallPreset);
    connect(m_save, &QPushButton::clicked, this, &CameraPage::savePreset);
    connect(m_overwrite, &QPushButton::clicked, this, &CameraPage::overwritePreset);
    connect(m_rename, &QPushButton::clicked, this, &CameraPage::renamePreset);
    connect(m_remove, &QPushButton::clicked, this, &CameraPage::removePreset);
    // Fünf Knöpfe: Umbruch auf zwei Zeilen wie im Entwurf (Entscheid 1)
    column->addWidget(buttonBar({m_recall, m_save, m_overwrite}, {}, box));
    column->addWidget(buttonBar({m_rename, m_remove}, {m_recallHint}, box));
    return box;
}

void CameraPage::setClient(dss::CoreClient *client)
{
    m_client = client;
    connect(client, &dss::CoreClient::cameraServiceChanged, this, &CameraPage::onServiceChanged);
    connect(client, &dss::CoreClient::cameraEventReceived, this, &CameraPage::onEvent);
    connect(client, &dss::CoreClient::cameraResultReceived, this, &CameraPage::onResult);
    onServiceChanged();
}

void CameraPage::onServiceChanged()
{
    const std::optional<dss::CameraServiceInfo> info = m_client ? m_client->cameraService() : std::nullopt;
    const bool wasUsable = m_service.usable;
    m_service = dss::cameraServiceView(info);
    if (!m_service.usable) {
        stopHolding(false); // Dienst weg: der Totmann des Dienstes hält an
        m_presetsFetched.clear();
        m_pending.clear();
    }
    const QString before = m_cameraId;
    applyParts(m_data.setService(info));
    if (m_service.usable && !wasUsable && m_cameraId == before)
        fetchCamera();
    fetchPresets();
    refresh();
}

void CameraPage::onEvent(const dss::CameraEventInfo &event)
{
    applyParts(m_data.applyEvent(event));
}

void CameraPage::onResult(const dss::CameraCall &call, const dss::CameraResult &result)
{
    const int parts = m_data.applyResult(call, result);
    if (const auto done = m_pending.take(call.requestId)) {
        done(result);
    } else if (call.method != QLatin1String("GET")) {
        if (const QString error = dss::cameraResultError(result); !error.isEmpty())
            emit statusMessage(QStringLiteral("Kamera-Dienst: ") + error);
    }
    applyParts(parts);
}

void CameraPage::applyParts(int parts)
{
    if (parts & dss::CameraData::Cameras)
        refreshChoice();
    if (parts & dss::CameraData::Presets)
        fetchPresets();
    if (m_setupDialog && (parts & (dss::CameraData::Config | dss::CameraData::Controls)))
        m_setupDialog->refresh();
    if (parts)
        refresh();
}

void CameraPage::refreshChoice()
{
    const QList<dss::CameraEntry> &cameras = m_data.cameras();
    const QString next = dss::cameraChoiceAfterUpdate(cameras, m_cameraId);
    {
        const QSignalBlocker block(m_choice);
        bool sameIds = m_choice->count() == cameras.size();
        for (qsizetype i = 0; sameIds && i < cameras.size(); ++i)
            sameIds = m_choice->itemData(int(i)).toString() == cameras.at(i).id;
        if (!sameIds) {
            m_choice->clear();
            for (const dss::CameraEntry &c : cameras)
                m_choice->addItem(dss::cameraChoiceText(c), c.id);
        } else {
            for (qsizetype i = 0; i < cameras.size(); ++i)
                m_choice->setItemText(int(i), dss::cameraChoiceText(cameras.at(i)));
        }
        m_choice->setCurrentIndex(m_choice->findData(next));
    }
    if (next != m_cameraId)
        switchCamera(next);
}

void CameraPage::switchCamera(const QString &cameraId)
{
    if (cameraId == m_cameraId)
        return;
    stopHolding(true);
    m_cameraId = cameraId;
    {
        const QSignalBlocker block(m_presets);
        m_presets->clearSelection();
    }
    fetchCamera();
}

void CameraPage::fetchCamera()
{
    // Bildwerte (mit der KI-Verfolgung) und Einstellungen der gewählten Kamera; das Ereignis beim
    // Anmelden bringt sie nicht (docs/09-kamera-dienst.md, «Ereignisse»).
    if (!m_service.usable || m_cameraId.isEmpty())
        return;
    send(dss::cameraControlsGetCall(m_cameraId));
    send(dss::cameraConfigGetCall(m_cameraId));
}

void CameraPage::fetchPresets()
{
    // camera_service bringt nur Kennung und Name; ganze Presets einzeln (GET camera-presets/<id>)
    if (!m_service.usable)
        return;
    for (const QString &id : m_data.cameraPresetsWithoutDetails()) {
        if (m_presetsFetched.contains(id))
            continue;
        m_presetsFetched.insert(id);
        send(dss::cameraPresetGetCall(id));
    }
}

void CameraPage::send(const dss::CameraCall &call, std::function<void(const dss::CameraResult &)> done)
{
    if (!m_client)
        return;
    const qint64 id = m_client->callCamera(call);
    if (id == 0)
        return;
    if (done)
        m_pending.insert(id, std::move(done));
    // Entscheid Lotse: KI-Anzeige nach einer Bewegung selbst auf «Aus» (ai_off_before_move)
    if (m_data.noteCallSent(call))
        refresh();
}

void CameraPage::pressDirection(int pan, int tilt, int zoom)
{
    if (!m_view.moveEnabled || m_cameraId.isEmpty() || m_hold.pressed())
        return;
    m_hold.press(m_cameraId, pan, tilt, zoom, m_clock.elapsed());
    scheduleHold();
}

void CameraPage::releaseDirection()
{
    if (!m_hold.pressed())
        return;
    m_holdTimer->stop();
    if (const std::optional<dss::CameraCall> call = m_hold.release(m_clock.elapsed()))
        send(*call);
}

void CameraPage::stopHolding(bool sendStop)
{
    m_holdTimer->stop();
    if (sendStop && m_hold.moving())
        send(dss::cameraStopCall(m_hold.cameraId()));
    m_hold.cancel();
}

void CameraPage::scheduleHold()
{
    const int due = m_hold.nextDueMs(m_clock.elapsed());
    if (due < 0)
        m_holdTimer->stop();
    else
        m_holdTimer->start(due);
}

void CameraPage::onHoldTimer()
{
    if (const std::optional<dss::CameraCall> call = m_hold.tick(m_clock.elapsed()))
        send(*call);
    scheduleHold();
}

bool CameraPage::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        const auto *key = static_cast<QKeyEvent *>(event);
        int pan = 0;
        int tilt = 0;
        switch (key->key()) {
        case Qt::Key_Left: pan = -1; break;
        case Qt::Key_Right: pan = 1; break;
        case Qt::Key_Up: tilt = 1; break;
        case Qt::Key_Down: tilt = -1; break;
        default: return QWidget::eventFilter(watched, event);
        }
        if (!key->isAutoRepeat()) {
            if (event->type() == QEvent::KeyPress)
                pressDirection(pan, tilt, 0);
            else
                releaseDirection();
        }
        return true; // Pfeiltasten bewegen die Kamera, nicht den Fokus
    }
    return QWidget::eventFilter(watched, event);
}

const dss::CameraEntry *CameraPage::currentCamera() const
{
    return m_data.camera(m_cameraId);
}

QString CameraPage::selectedPresetId() const
{
    const QList<QTreeWidgetItem *> selected = m_presets->selectedItems();
    return selected.isEmpty() ? QString() : selected.first()->data(0, kPresetIdRole).toString();
}

QString CameraPage::selectedPresetName() const
{
    const QList<QTreeWidgetItem *> selected = m_presets->selectedItems();
    return selected.isEmpty() ? QString() : selected.first()->text(0);
}

void CameraPage::refresh()
{
    const dss::CameraEntry *camera = currentCamera();
    const QJsonArray controls = m_data.controls(m_cameraId);

    // Liste «Positionen» nur neu, wenn sich Zeilen ändern; die Wahl bleibt beim selben Preset
    const QList<dss::CameraPresetRow> rows = dss::cameraPresetRows(m_data.cameraPresets(), m_cameraId, controls);
    if (rows != m_presetRows) {
        const QString selected = selectedPresetId();
        const QSignalBlocker block(m_presets);
        m_presets->clear();
        for (const dss::CameraPresetRow &row : rows) {
            auto *item = new QTreeWidgetItem({row.name, row.position, row.withControls});
            item->setData(0, kPresetIdRole, row.id);
            m_presets->addTopLevelItem(item);
            if (row.id == selected) {
                m_presets->setCurrentItem(item);
                item->setSelected(true);
            }
        }
        m_presetRows = rows;
    }
    const bool presetSelected = !selectedPresetId().isEmpty();
    m_view = dss::cameraControlView(m_service, camera, presetSelected, controls);
    if (!m_view.moveEnabled && m_hold.pressed())
        stopHolding(m_service.usable);

    // Kopfzeile
    m_choice->setEnabled(m_view.chooserEnabled);
    m_wake->setText(m_view.wakeText);
    m_wake->setEnabled(m_view.wakeEnabled);
    m_wake->setToolTip(m_view.wakeToolTip);
    m_setup->setEnabled(m_view.setupEnabled);
    m_state->setText(m_service.text);
    m_stateDetail->setText(m_service.detail);
    m_stateDetail->setToolTip(m_service.detail);
    setState(m_state, m_service.state);
    setState(m_stateDetail, m_service.state);
    m_blockedHint->setText(m_service.blockedHint);
    m_blockedHint->setToolTip(m_service.blockedHint); // einzeilig, der ganze Text beim Darüberfahren
    m_columns->setEnabled(m_service.usable);

    // Bewegen
    m_moveHint->setText(m_view.moveHint);
    m_moveHint->setToolTip(m_view.moveHint);
    setState(m_moveHint, m_view.moveHintState);
    m_hintWake->setVisible(m_view.moveHintWake);
    m_hintWake->setEnabled(m_view.wakeEnabled);
    for (QToolButton *b : std::as_const(m_crossButtons))
        b->setEnabled(m_view.moveEnabled);
    m_zoomText->setText(dss::cameraZoomText(controls));
    const QJsonObject config = m_data.config(m_cameraId);
    const QJsonObject values = config.value(QLatin1String("config")).toObject();
    const QJsonObject ranges = config.value(QLatin1String("ranges")).toObject();
    {
        const QSignalBlocker block(m_speed);
        const auto [low, high] = dss::cameraConfigRange(config, QStringLiteral("speed"), 1, 100);
        m_speed->setRange(int(low), int(high));
        m_speed->setSuffix(dss::cameraSpeedSuffix(camera));
        if (values.contains(QLatin1String("speed")))
            m_speed->setValue(values.value(QLatin1String("speed")).toInt());
    }
    {
        const QSignalBlocker block(m_step);
        const auto [low, high] = dss::cameraConfigRange(config, QStringLiteral("step_deg"), 1, 90);
        m_step->setRange(low, high);
        if (values.contains(QLatin1String("step_deg")))
            m_step->setValue(values.value(QLatin1String("step_deg")).toDouble());
    }
    m_speed->setEnabled(m_view.setupEnabled && ranges.contains(QLatin1String("speed")));
    m_step->setEnabled(m_view.setupEnabled && ranges.contains(QLatin1String("step_deg")));

    // KI-Verfolgung
    if (m_view.aiMenu != m_aiMenu) {
        const QSignalBlocker block(m_ai);
        m_ai->clear();
        for (const auto &[value, label] : m_view.aiMenu)
            m_ai->addItem(label, value);
        m_aiMenu = m_view.aiMenu;
    }
    {
        const QSignalBlocker block(m_ai);
        m_ai->setCurrentIndex(m_ai->findData(m_view.aiValue));
    }
    m_ai->setEnabled(m_view.aiEnabled);
    m_ai->setToolTip(m_view.aiHint);
    m_aiHint->setText(m_view.aiHint);
    m_aiHint->setToolTip(m_view.aiHint);

    // Positionen
    m_presets->setEnabled(m_view.presetsEnabled);
    m_recall->setEnabled(m_view.recallEnabled);
    m_save->setEnabled(m_view.saveEnabled);
    m_overwrite->setEnabled(m_view.saveEnabled && presetSelected);
    m_rename->setEnabled(m_view.editEnabled);
    m_remove->setEnabled(m_view.editEnabled);
    m_recallHint->setText(m_view.recallHint);

    if (m_imageTitle)
        m_imageTitle->setText(dss::cameraImageTitle(camera));
}

void CameraPage::reportResult(const dss::CameraResult &result, const QString &okText)
{
    const QString error = dss::cameraResultError(result);
    emit statusMessage(error.isEmpty() ? okText : QStringLiteral("Kamera-Dienst: ") + error);
}

void CameraPage::recallPreset()
{
    const QString id = selectedPresetId();
    if (!id.isEmpty())
        send(dss::cameraPresetRecallCall(id));
}

void CameraPage::savePreset()
{
    const dss::CameraEntry *camera = currentCamera();
    if (!camera)
        return;
    auto *dialog = new PresetNameDialog(
        QStringLiteral("Position speichern"),
        QStringLiteral("Gespeichert werden Position und Zoom der Kamera «%1». Mit Bildwerten stellt das Abrufen "
                       "auch Helligkeit, Weissabgleich usw. wieder her.")
            .arg(camera->name),
        true, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    const QString cameraId = camera->id;
    connect(dialog, &QDialog::accepted, this, [this, dialog, cameraId] {
        const QString name = dialog->name();
        send(dss::cameraPresetCreateCall(name, cameraId, dialog->withControls()),
             [this, name](const dss::CameraResult &r) { reportResult(r, dss::savedStatusText(name)); });
    });
    dialog->open();
}

void CameraPage::overwritePreset()
{
    const QString id = selectedPresetId();
    const QString name = selectedPresetName();
    if (id.isEmpty())
        return;
    // Bildwerte wie bisher im Preset (Kästchen nur beim Speichern)
    bool withControls = false;
    for (const QJsonValue &v : m_data.cameraPresets()) {
        const QJsonObject o = v.toObject();
        if (o.value(QLatin1String("id")).toString() == id)
            withControls = o.value(QLatin1String("controls")).isObject();
    }
    send(dss::cameraPresetOverwriteCall(id, withControls),
         [this, name](const dss::CameraResult &r) { reportResult(r, dss::savedStatusText(name)); });
}

void CameraPage::renamePreset()
{
    const QString id = selectedPresetId();
    QString name = selectedPresetName();
    if (id.isEmpty() || !askText(this, QStringLiteral("Position umbenennen"), QStringLiteral("Neuer Name"), &name))
        return;
    name = name.trimmed();
    send(dss::cameraPresetRenameCall(id, name),
         [this, name](const dss::CameraResult &r) { reportResult(r, dss::savedStatusText(name)); });
}

void CameraPage::removePreset()
{
    const QString id = selectedPresetId();
    const QString name = selectedPresetName();
    if (id.isEmpty()
        || !confirm(this, QStringLiteral("Position löschen?"), QStringLiteral("Position «%1» löschen?").arg(name),
                    QStringLiteral("Löschen"), QStringLiteral("Abbrechen")))
        return;
    send(dss::cameraPresetDeleteCall(id),
         [this, name](const dss::CameraResult &r) { reportResult(r, dss::deletedStatusText(name)); });
}

void CameraPage::openSetup()
{
    if (!m_client || !currentCamera() || m_setupDialog)
        return;
    m_setupDialog = new CameraSetupDialog(m_client, &m_data, m_cameraId, this);
    m_setupDialog->setAttribute(Qt::WA_DeleteOnClose);
    m_setupDialog->open();
}
