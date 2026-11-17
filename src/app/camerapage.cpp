#include "camerapage.h"

#include "actionview.h"
#include "camerasetupdialog.h"
#include "coreclient.h"
#include "dialogs.h"
#include "lightsdialog.h"
#include "panels.h"
#include "presetnamedialog.h"
#include "symbols.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSlider>
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

// Hinweis als Text ohne Kasten (Entscheid 3) wie die Familie (#hintLabel, grau; Farbe der Kopfzeile
// über state), Höhe für lines Zeilen immer da (Regel 9). Mehr als eine Zeile bricht um.
QLabel *hintLine(QWidget *parent, int lines = 1)
{
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("hintLabel"));
    label->setTextFormat(Qt::PlainText);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    if (lines > 1) {
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        label->setFixedHeight(label->fontMetrics().lineSpacing() * lines);
    } else {
        label->setMinimumHeight(label->fontMetrics().lineSpacing());
    }
    return label;
}

// Zeile, die ihren ganzen Text zeigt, solange Platz ist, sonst am Ende «…» (P15 f2a); der Anfang
// bleibt immer sichtbar, den ganzen Text zeigt der Tooltip. Erhöht die Mindestbreite nicht: Der
// Text ändert nur, was gezeichnet wird, nicht das Layout (Regel 9).
class ElidedLabel : public QLabel
{
public:
    using QLabel::QLabel;

    QSize minimumSizeHint() const override
    {
        return {fontMetrics().horizontalAdvance(QStringLiteral("…")), QLabel::minimumSizeHint().height()};
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        const QRect r = contentsRect();
        if (fontMetrics().horizontalAdvance(text()) <= r.width()) {
            QLabel::paintEvent(event);
            return;
        }
        QPainter painter(this);
        style()->drawItemText(&painter, r, static_cast<int>(alignment()), palette(), isEnabled(),
                              fontMetrics().elidedText(text(), Qt::ElideRight, r.width()), foregroundRole());
    }
};

// Bildlauf wie das Dock «Eigenschaften» (docks.cpp, #propertiesScroll)
QScrollArea *scrollColumn(const QString &objectName, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setObjectName(objectName);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return scroll;
}

// Reglerzeile «Regler + Wert fester Breite» (Entscheid 1 des Nutzers, 2026-11-17), wie «Lautstärke»
// im Dock «Eigenschaften»: Beschriftung in Spalte column, Regler daneben (dehnbar), Wert rechtsbündig
// mit der Breite des längsten Werts, damit ein neuer Wert die Anordnung nicht ändert (Regel 9).
QSlider *sliderRow(QGridLayout *grid, int row, int column, QLabel *label, QLabel **value, QWidget *parent)
{
    auto *slider = new QSlider(Qt::Horizontal, parent);
    slider->setAccessibleName(label->text());
    slider->setEnabled(false);
    label->setBuddy(slider);
    *value = new QLabel(QStringLiteral("–"), parent);
    (*value)->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    int width = 0;
    for (const QString &longest : {QStringLiteral("−00000"), QStringLiteral("0000 K"), QStringLiteral("100 %")})
        width = std::max(width, (*value)->fontMetrics().horizontalAdvance(longest));
    (*value)->setMinimumWidth(width);
    grid->addWidget(label, row, column);
    grid->addWidget(slider, row, column + 1);
    grid->addWidget(*value, row, column + 2);
    return slider;
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
    m_bundleTimer = new QTimer(this);
    m_bundleTimer->setSingleShot(true);
    connect(m_bundleTimer, &QTimer::timeout, this, &CameraPage::onBundleTimer);

    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);
    column->addWidget(createHeader());

    // Drei Spalten mit Teilern wie «Live» im Reiter «Chat-Bot»; keine lässt sich zuschieben.
    m_columns = new QSplitter(Qt::Horizontal, this);
    m_columns->setObjectName(QStringLiteral("cameraColumns"));
    m_columns->setChildrenCollapsible(false);
    // Links mit Bildlauf wie das Dock «Eigenschaften»: Die Spalte streckt das Fenster nicht über
    // 1280x800 hinaus (scripts/bildschirmfoto), bei kleiner Höhe läuft sie senkrecht.
    QScrollArea *leftScroll = scrollColumn(QStringLiteral("cameraLeftScroll"), m_columns);
    auto *left = new QWidget;
    left->setObjectName(QStringLiteral("cameraLeftColumn"));
    auto *leftColumn = new QVBoxLayout(left);
    leftColumn->setContentsMargins(0, 0, 0, 0);
    leftColumn->setSpacing(6);
    QWidget *move = dockPanel(QStringLiteral("Bewegen"), createMovePanel(), left);
    move->setObjectName(QStringLiteral("cameraMovePanel"));
    leftColumn->addWidget(move);
    QWidget *ai = dockPanel(QStringLiteral("KI-Verfolgung"), createAiPanel(), left);
    ai->setObjectName(QStringLiteral("cameraAiPanel"));
    leftColumn->addWidget(ai);
    QWidget *presets = dockPanel(QStringLiteral("Positionen"), createPresetsPanel(), left);
    presets->setObjectName(QStringLiteral("cameraPresetsPanel"));
    leftColumn->addWidget(presets, 1);
    leftScroll->setWidget(left);
    leftScroll->setMinimumWidth(left->minimumSizeHint().width()
                                + leftScroll->verticalScrollBar()->sizeHint().width());
    m_columns->addWidget(leftScroll);

    // Mitte «Bildwerte» mit Bildlauf, rechts «Licht» (P15 e3); rechts ebenfalls mit Bildlauf, damit
    // viele Lampen das Fenster nicht über 1280x800 hinaus strecken.
    QWidget *image = dockPanel(dss::cameraImageTitle(nullptr), createImagePanel(), m_columns);
    image->setObjectName(QStringLiteral("cameraImagePanel"));
    m_imageTitle = image->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"));
    m_columns->addWidget(image);
    QWidget *light = dockPanel(QStringLiteral("Licht"), createLightPanel(), m_columns);
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
    // Zwei Zeilen (P15 e4, Entscheid Lotse): oben Auswahl und Knöpfe, darunter links der Hinweis
    // «… gesperrt …» (statt einer eigenen Zeile über den Spalten); rechts der Zustand des Dienstes
    // in beiden Zeilen. Der Zustand bekommt die Breite, die er braucht (nie abgeschnitten), der
    // Hinweis links nimmt den Rest.
    // P15 f2a: Die zweite Zeile (Hinweis und Meldung) reicht über beide Spalten, so nutzt eine lange
    // Meldung den Platz unter Auswahl und Knöpfen statt die Seite zu verbreitern. Reicht auch das
    // nicht, endet sie mit «…» (ElidedLabel, ganzer Text im Tooltip); die Kopfzeile erhöht die
    // Mindestbreite der Seite nicht.
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("cameraHeader"));
    header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *grid = new QGridLayout(header);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(0);
    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    grid->addLayout(row, 0, 0);

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
    // «Kamera einrichten …» steht im Menü «Werkzeuge» (Wunsch Gerätetest P15, N3)
    row->addStretch(1);

    // Hinweis, solange der Dienst nicht antwortet (Entwurf Seite 4 E), rot über state
    m_blockedHint = hintLine(header);
    setStyleState(m_blockedHint, QStringLiteral("error"));

    // Zustand des Dienstes in zwei Zeilen, Platz für beide von Anfang an (wie die Zeile «OBS»)
    m_state = new QLabel(header);
    m_state->setObjectName(QStringLiteral("cameraState"));
    m_state->setAlignment(Qt::AlignRight | Qt::AlignBottom);
    m_stateDetail = new ElidedLabel(header);
    m_stateDetail->setObjectName(QStringLiteral("cameraStateDetail"));
    m_stateDetail->setAlignment(Qt::AlignRight | Qt::AlignTop);
    m_stateDetail->setTextFormat(Qt::PlainText);
    m_stateDetail->setMinimumHeight(fontMetrics().lineSpacing());
    grid->addWidget(m_state, 0, 1);
    auto *second = new QHBoxLayout;
    second->setSpacing(grid->horizontalSpacing());
    second->addWidget(m_blockedHint, 1);
    second->addWidget(m_stateDetail, 0);
    grid->addLayout(second, 1, 0, 1, 2);
    grid->setColumnStretch(0, 1);
    return header;
}

QWidget *CameraPage::createMovePanel()
{
    auto *box = new QWidget;
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(8, 6, 8, 8);
    column->setSpacing(6);

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

    // Hinweis mit «Wecken» daneben in der freien Fläche unter «Tempo» und «Schritt» (Entscheid 10
    // des Nutzers, 2026-11-17). Höhe für zwei Zeilen und die Breite des Knopfs bleiben auch ohne
    // Hinweis und ohne Knopf (Regel 9).
    auto *hintRow = new QHBoxLayout;
    hintRow->setSpacing(6);
    m_moveHint = hintLine(box, 2);
    hintRow->addWidget(m_moveHint, 1, Qt::AlignTop);
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
    hintRow->addWidget(m_hintWake, 0, Qt::AlignTop);
    side->addLayout(hintRow, 3, 0, 1, 2);
    side->setRowStretch(4, 1);
    body->addLayout(side, 1);
    body->setAlignment(cross, Qt::AlignTop);
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
    // Eine Zeile (Entscheid 10: Leerzone knapp): Modus, Auswahl, rechts daneben der Hinweis
    m_aiHint = hintLine(box);
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
    grid->addWidget(fieldLabel(QStringLiteral("Modus"), m_ai, box), 0, 0);
    grid->addWidget(m_ai, 0, 1);
    grid->addWidget(m_aiHint, 0, 2);
    grid->setColumnStretch(2, 1);
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
    m_recallHint->setObjectName(QStringLiteral("hintLabel"));
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

QWidget *CameraPage::createImagePanel()
{
    QScrollArea *scroll = scrollColumn(QStringLiteral("cameraImageScroll"), nullptr);
    auto *form = new QWidget(scroll);
    form->setObjectName(QStringLiteral("cameraImageForm"));
    auto *formColumn = new QVBoxLayout(form);
    formColumn->setContentsMargins(0, 0, 0, 0);
    formColumn->setSpacing(0);

    // Je Bereich aus cameraControlSections() alle Zeilen fest (Regel 9); «Zurücksetzen» in der
    // Titelzeile, davor der Hinweis des Bereichs («Diese Kamera hat das nicht.»).
    for (const QString &title : dss::cameraControlSections()) {
        SectionWidgets s;
        s.reset = symbolButton(QStringLiteral("zuruecksetzen.svg"), QStringLiteral("Z"),
                               QStringLiteral("%1 zurücksetzen").arg(title), nullptr);
        s.reset->setObjectName(QStringLiteral("cameraSectionReset_") + title);
        QGridLayout *grid = nullptr;
        QWidget *sec = section(title, form, &grid, {s.reset}, &s.hint);
        sec->setObjectName(QStringLiteral("cameraSection_") + title);
        connect(s.reset, &QToolButton::clicked, this, [this, title] {
            if (!m_cameraId.isEmpty())
                send(dss::cameraSectionResetCall(m_cameraId, m_controlRows, title));
        });
        m_sectionWidgets.insert(title, s);

        int row = 0;
        for (const dss::CameraControlSpec &spec : dss::cameraControlSpecs()) {
            if (spec.section != title)
                continue;
            const QString key = spec.key;
            ControlWidgets w;
            // Marke «•» vor gemerkten Werten (Entscheid 3), Platz immer da
            w.mark = new QLabel(QStringLiteral("•"), sec);
            w.mark->setObjectName(QStringLiteral("cameraRememberedMark"));
            w.mark->setProperty("controlKey", key); // für tst_fensterkameras
            w.mark->setToolTip(QStringLiteral("Gemerkt: Der Dienst setzt den Wert, sobald die Kamera erscheint."));
            QSizePolicy keep = w.mark->sizePolicy();
            keep.setRetainSizeWhenHidden(true);
            w.mark->setSizePolicy(keep);
            w.mark->hide();
            grid->addWidget(w.mark, row, 0);
            QWidget *control = nullptr;
            switch (spec.kind) {
            case dss::CameraControlKind::Slider:
                w.label = new QLabel(spec.label, sec);
                w.slider = sliderRow(grid, row, 1, w.label, &w.value, sec);
                w.slider->setObjectName(QStringLiteral("cameraControl_") + key);
                connect(w.slider, &QSlider::valueChanged, this, [this, key](int value) {
                    m_controlWidgets.value(key).value->setText(dss::cameraSliderText(key, value));
                    if (!m_cameraId.isEmpty())
                        putBundled(dss::cameraControlsPutCall(m_cameraId, {}).path, {{key, value}});
                });
                connect(w.slider, &QSlider::sliderReleased, this, &CameraPage::flushBundled);
                control = w.slider;
                break;
            case dss::CameraControlKind::Choice:
                w.label = new QLabel(spec.label, sec);
                w.choice = new QComboBox(sec);
                w.choice->setObjectName(QStringLiteral("cameraControl_") + key);
                w.choice->setAccessibleName(spec.label);
                w.choice->setPlaceholderText(QStringLiteral("–"));
                w.choice->setMinimumContentsLength(14);
                w.choice->setEnabled(false);
                w.label->setBuddy(w.choice);
                connect(w.choice, &QComboBox::activated, this, [this, key](int index) {
                    if (!m_cameraId.isEmpty())
                        send(dss::cameraControlsPutCall(
                            m_cameraId, {{key, m_controlWidgets.value(key).choice->itemData(index).toInt()}}));
                });
                grid->addWidget(w.label, row, 1);
                grid->addWidget(w.choice, row, 2, 1, 2, Qt::AlignLeft);
                control = w.choice;
                break;
            case dss::CameraControlKind::Check:
                // Kästchen über die Spalten von Regler und Wert (wie «Wiederholen» in «Eigenschaften»)
                w.check = new QCheckBox(spec.label, sec);
                w.check->setObjectName(QStringLiteral("cameraControl_") + key);
                w.check->setEnabled(false);
                connect(w.check, &QCheckBox::clicked, this, [this, key](bool checked) {
                    if (!m_cameraId.isEmpty())
                        send(dss::cameraControlsPutCall(
                            m_cameraId, {{key, dss::cameraCheckValue(m_data.controls(m_cameraId), key, checked)}}));
                });
                grid->addWidget(w.check, row, 1, 1, 3);
                control = w.check;
                break;
            case dss::CameraControlKind::Text:
                w.label = new QLabel(spec.label, sec);
                w.value = new QLabel(QStringLiteral("–"), sec);
                w.value->setObjectName(QStringLiteral("cameraControl_") + key);
                grid->addWidget(w.label, row, 1);
                grid->addWidget(w.value, row, 2, 1, 2);
                control = w.value;
                break;
            }
            // Kontextmenü «Zurücksetzen» an gemerkten Werten (Plan 8)
            for (QWidget *target : {static_cast<QWidget *>(w.label), control}) {
                if (!target)
                    continue;
                target->setContextMenuPolicy(Qt::CustomContextMenu);
                connect(target, &QWidget::customContextMenuRequested, this, [this, key, target](const QPoint &pos) {
                    showControlMenu(key, target->mapToGlobal(pos));
                });
            }
            m_controlWidgets.insert(key, w);
            ++row;
        }
        grid->setColumnStretch(2, 1);
        formColumn->addWidget(sec);
    }
    formColumn->addStretch(1);
    scroll->setWidget(form);
    return scroll;
}

QWidget *CameraPage::createLightPanel()
{
    QScrollArea *scroll = scrollColumn(QStringLiteral("cameraLightScroll"), nullptr);
    auto *box = new QWidget(scroll);
    box->setObjectName(QStringLiteral("cameraLightColumn"));
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    // Leiste oben (Entwurf 1B): «Alle ein», «Alle aus», rechts «Lampen …»
    m_allOn = new QPushButton(QStringLiteral("Alle ein"));
    m_allOn->setObjectName(QStringLiteral("cameraLightsAllOn"));
    m_allOff = new QPushButton(QStringLiteral("Alle aus"));
    m_allOff->setObjectName(QStringLiteral("cameraLightsAllOff"));
    m_lamps = new QPushButton(QStringLiteral("Lampen …"));
    m_lamps->setObjectName(QStringLiteral("cameraLamps"));
    connect(m_allOn, &QPushButton::clicked, this, [this] { send(dss::lightsAllCall(true)); });
    connect(m_allOff, &QPushButton::clicked, this, [this] { send(dss::lightsAllCall(false)); });
    connect(m_lamps, &QPushButton::clicked, this, &CameraPage::openLights);
    column->addWidget(buttonBar({m_allOn, m_allOff}, {m_lamps}, box));

    // Je Lampe ein Bereich (rebuildLights), darunter «Licht-Presets»
    m_lightSections = new QVBoxLayout;
    m_lightSections->setContentsMargins(0, 0, 0, 0);
    m_lightSections->setSpacing(0);
    column->addLayout(m_lightSections);
    column->addWidget(createLightPresetsSection(box), 1);
    scroll->setWidget(box);
    scroll->setMinimumWidth(box->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width());
    return scroll;
}

QWidget *CameraPage::createLightPresetsSection(QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    // Titel und Linie wie die Bereiche, die Liste darunter über die ganze Breite (Entwurf 1B)
    QGridLayout *grid = nullptr;
    column->addWidget(section(QStringLiteral("Licht-Presets"), box, &grid));

    m_lightPresets = new QTreeWidget(box);
    m_lightPresets->setObjectName(QStringLiteral("cameraLightPresets"));
    m_lightPresets->setAccessibleName(QStringLiteral("Licht-Presets"));
    m_lightPresets->setColumnCount(1);
    m_lightPresets->setHeaderLabels({QStringLiteral("Name")});
    m_lightPresets->setRootIsDecorated(false);
    m_lightPresets->setUniformRowHeights(true);
    m_lightPresets->setSelectionMode(QAbstractItemView::SingleSelection);
    m_lightPresets->header()->setStretchLastSection(true);
    m_lightPresets->setMinimumHeight(m_lightPresets->fontMetrics().lineSpacing() * 6);
    connect(m_lightPresets, &QTreeWidget::itemSelectionChanged, this, &CameraPage::refresh);
    // Doppelklick = Abrufen wie bei «Positionen»
    connect(m_lightPresets, &QTreeWidget::itemDoubleClicked, this, [this] {
        if (m_lightRecall->isEnabled())
            send(dss::lightPresetRecallCall(selectedLightPresetId()));
    });
    column->addWidget(m_lightPresets, 1);

    m_lightRecall = new QPushButton(QStringLiteral("Abrufen"));
    m_lightRecall->setObjectName(QStringLiteral("cameraLightPresetRecall"));
    m_lightSave = new QPushButton(QStringLiteral("Aktuelles Licht speichern …"));
    m_lightSave->setObjectName(QStringLiteral("cameraLightPresetSave"));
    m_lightOverwrite = new QPushButton(QStringLiteral("Überschreiben"));
    m_lightOverwrite->setObjectName(QStringLiteral("cameraLightPresetOverwrite"));
    m_lightRename = new QPushButton(QStringLiteral("Umbenennen"));
    m_lightRename->setObjectName(QStringLiteral("cameraLightPresetRename"));
    m_lightRemove = new QPushButton(QStringLiteral("Löschen"));
    m_lightRemove->setObjectName(QStringLiteral("cameraLightPresetRemove"));
    connect(m_lightRecall, &QPushButton::clicked, this, [this] {
        if (const QString id = selectedLightPresetId(); !id.isEmpty())
            send(dss::lightPresetRecallCall(id));
    });
    connect(m_lightSave, &QPushButton::clicked, this, &CameraPage::saveLightPreset);
    connect(m_lightOverwrite, &QPushButton::clicked, this, [this] {
        const QString id = selectedLightPresetId();
        const QString name = selectedLightPresetName();
        if (!id.isEmpty())
            send(dss::lightPresetOverwriteCall(id),
                 [this, name](const dss::CameraResult &r) { reportResult(r, dss::savedStatusText(name)); });
    });
    connect(m_lightRename, &QPushButton::clicked, this, &CameraPage::renameLightPreset);
    connect(m_lightRemove, &QPushButton::clicked, this, &CameraPage::removeLightPreset);
    // Fünf Knöpfe: Umbruch auf zwei Zeilen wie «Positionen» (Entscheid 1)
    column->addWidget(buttonBar({m_lightRecall, m_lightSave, m_lightOverwrite}, {}, box));
    column->addWidget(buttonBar({m_lightRename, m_lightRemove}, {}, box));
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
        m_lightPresetsFetched.clear();
        m_pending.clear();
    }
    const QString before = m_cameraId;
    int parts = m_data.setService(info);
    if (m_service.usable != wasUsable)
        parts |= dss::CameraData::Cameras | dss::CameraData::Lights; // Auswahl ohne/mit Zustand (Entscheid 9)
    applyParts(parts);
    if (m_service.usable && !wasUsable && m_cameraId == before)
        fetchCamera();
    fetchPresets();
    fetchLightPresets();
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
    if (parts & dss::CameraData::Presets) {
        fetchPresets();
        fetchLightPresets();
    }
    if (m_setupDialog && (parts & (dss::CameraData::Config | dss::CameraData::Controls)))
        m_setupDialog->refresh();
    if (m_lightsDialog && (parts & dss::CameraData::Lights))
        m_lightsDialog->refresh();
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
                m_choice->addItem(dss::cameraChoiceText(c, m_service.usable), c.id);
        } else {
            for (qsizetype i = 0; i < cameras.size(); ++i)
                m_choice->setItemText(int(i), dss::cameraChoiceText(cameras.at(i), m_service.usable));
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
    flushBundled(); // Regler der bisherigen Kamera zuerst
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

void CameraPage::fetchLightPresets()
{
    // Wie fetchPresets: camera_service bringt nur Kennung und Name der Licht-Presets
    if (!m_service.usable)
        return;
    for (const QString &id : m_data.lightPresetsWithoutDetails()) {
        if (m_lightPresetsFetched.contains(id))
            continue;
        m_lightPresetsFetched.insert(id);
        send(dss::lightPresetGetCall(id));
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
        sendHoldCall(*call);
}

void CameraPage::stopHolding(bool sendStop)
{
    m_holdTimer->stop();
    if (sendStop && m_hold.moving())
        sendHoldCall(dss::cameraStopCall(m_hold.cameraId()));
    m_hold.cancel();
}

void CameraPage::sendHoldCall(const dss::CameraCall &call)
{
    // Entscheid Lotse (P15 e3): Lehnt der Dienst die Fahrt ab, steht die Meldung nur einmal je
    // Halten in der Statuszeile, nicht bei jeder Wiederholung.
    const int serial = m_hold.serial();
    send(call, [this, serial](const dss::CameraResult &result) {
        const QString error = dss::cameraResultError(result);
        if (!error.isEmpty() && m_hold.reportFailure(serial))
            emit statusMessage(QStringLiteral("Kamera-Dienst: ") + error);
    });
}

void CameraPage::putBundled(const QString &path, const QJsonObject &values)
{
    if (const std::optional<dss::CameraCall> call = m_bundler.put(path, values, m_clock.elapsed()))
        send(*call);
    const int due = m_bundler.nextDueMs(m_clock.elapsed());
    if (due >= 0 && !m_bundleTimer->isActive())
        m_bundleTimer->start(due);
}

void CameraPage::onBundleTimer()
{
    for (const dss::CameraCall &call : m_bundler.tick(m_clock.elapsed()))
        send(call);
    const int due = m_bundler.nextDueMs(m_clock.elapsed());
    if (due >= 0)
        m_bundleTimer->start(due);
}

void CameraPage::flushBundled()
{
    m_bundleTimer->stop();
    for (const dss::CameraCall &call : m_bundler.flush(m_clock.elapsed()))
        send(call);
    // Was noch nicht fällig ist, geht mit dem Takt (P15 e4, CameraBundler::flush)
    const int due = m_bundler.nextDueMs(m_clock.elapsed());
    if (due >= 0)
        m_bundleTimer->start(due);
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
        sendHoldCall(*call);
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
    if (m_view.setupEnabled != m_setupEnabled) {
        m_setupEnabled = m_view.setupEnabled;
        emit setupEnabledChanged(m_setupEnabled);
    }
    m_state->setText(m_service.text);
    m_stateDetail->setText(m_service.detail);
    m_stateDetail->setToolTip(m_service.detail);
    setStyleState(m_state, m_service.state);
    setStyleState(m_stateDetail, m_service.state);
    m_blockedHint->setText(m_service.blockedHint);
    m_blockedHint->setToolTip(m_service.blockedHint); // einzeilig, der ganze Text beim Darüberfahren
    m_columns->setEnabled(m_service.usable);

    // Bewegen
    m_moveHint->setText(m_view.moveHint);
    m_moveHint->setToolTip(m_view.moveHint);
    setStyleState(m_moveHint, m_view.moveHintState);
    m_hintWake->setVisible(m_view.moveHintWake);
    m_hintWake->setEnabled(m_view.wakeEnabled);
    for (QToolButton *b : std::as_const(m_crossButtons))
        b->setEnabled(m_view.moveEnabled);
    m_zoomText->setText(m_view.zoomText);
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
    refreshImage(camera, controls);
    refreshLights();
}

void CameraPage::refreshImage(const dss::CameraEntry *camera, const QJsonArray &controls)
{
    // Ein Ereignis setzt nur Werte, Freigaben, Tooltips und Marken (Regel 9). Einen Regler, den der
    // Nutzer gerade zieht, lässt der Stand des Dienstes in Ruhe.
    m_controlRows = dss::cameraControlRows(controls, camera, m_service);
    for (const dss::CameraControlRow &row : std::as_const(m_controlRows)) {
        const ControlWidgets w = m_controlWidgets.value(row.spec.key);
        w.mark->setVisible(row.remembered);
        if (w.label) {
            w.label->setEnabled(row.enabled);
            w.label->setToolTip(row.toolTip);
        }
        switch (row.spec.kind) {
        case dss::CameraControlKind::Slider:
            if (!w.slider->isSliderDown()) {
                const QSignalBlocker block(w.slider);
                if (row.available) {
                    w.slider->setRange(row.minimum, row.maximum);
                    w.slider->setSingleStep(row.step);
                    w.slider->setPageStep(std::max(row.step, (row.maximum - row.minimum) / 10));
                    w.slider->setValue(row.value);
                } else {
                    // Ohne Wert ganz links, nie die Stellung einer anderen Kamera (Entscheid 11)
                    w.slider->setRange(0, 0);
                }
                w.value->setText(row.valueText);
            }
            w.slider->setEnabled(row.enabled);
            w.slider->setToolTip(row.toolTip);
            w.value->setEnabled(row.enabled);
            break;
        case dss::CameraControlKind::Choice: {
            const QSignalBlocker block(w.choice);
            bool same = w.choice->count() == row.menu.size();
            for (qsizetype i = 0; same && i < row.menu.size(); ++i)
                same = w.choice->itemData(int(i)).toInt() == row.menu.at(i).first
                       && w.choice->itemText(int(i)) == row.menu.at(i).second;
            if (!same) {
                w.choice->clear();
                for (const auto &[value, label] : row.menu)
                    w.choice->addItem(label, value);
            }
            w.choice->setCurrentIndex(row.available ? w.choice->findData(row.value) : -1);
            w.choice->setEnabled(row.enabled);
            w.choice->setToolTip(row.toolTip);
            break;
        }
        case dss::CameraControlKind::Check: {
            const QSignalBlocker block(w.check);
            w.check->setChecked(row.available && row.value != 0);
            w.check->setEnabled(row.enabled);
            w.check->setToolTip(row.toolTip);
            break;
        }
        case dss::CameraControlKind::Text:
            w.value->setText(row.valueText);
            w.value->setEnabled(row.enabled);
            w.value->setToolTip(row.toolTip);
            break;
        }
    }
    for (auto it = m_sectionWidgets.cbegin(); it != m_sectionWidgets.cend(); ++it) {
        it->reset->setEnabled(dss::cameraSectionResettable(m_controlRows, it.key()));
        it->hint->setText(dss::cameraSectionHint(m_controlRows, it.key()));
    }
}

void CameraPage::showControlMenu(const QString &key, const QPoint &globalPos)
{
    // «Zurücksetzen» nur an gemerkten, bedienbaren Werten: null = Standard des Geräts, vergessen
    for (const dss::CameraControlRow &row : std::as_const(m_controlRows)) {
        if (row.spec.key != key || !row.remembered || !row.enabled || m_cameraId.isEmpty())
            continue;
        QMenu menu(this);
        const QString cameraId = m_cameraId;
        menu.addAction(QStringLiteral("Zurücksetzen"), this, [this, cameraId, key] {
            send(dss::cameraControlsPutCall(cameraId, {{key, QJsonValue::Null}}));
        });
        menu.exec(globalPos);
        return;
    }
}

void CameraPage::rebuildLights()
{
    const QStringList key = dss::lightLayoutKey(m_data.lights());
    if ((m_lightsBuilt && key == m_lightLayout) || m_lightsDialog)
        return;
    flushBundled();
    for (const LightWidgets &w : std::as_const(m_lightWidgets))
        delete w.section;
    m_lightWidgets.clear();

    QStringList headers{QStringLiteral("Name")};
    for (const dss::LightRow &row : dss::lightRows(m_data.lights(), m_service)) {
        LightWidgets w;
        w.id = row.id;
        QGridLayout *grid = nullptr;
        w.section = section(row.name, m_lightSections->parentWidget(), &grid, {}, &w.state);
        w.section->setObjectName(QStringLiteral("cameraLight_") + row.id);
        w.on = new QCheckBox(QStringLiteral("Ein"), w.section);
        w.on->setObjectName(QStringLiteral("cameraLightOn_") + row.id);
        w.on->setEnabled(false);
        grid->addWidget(w.on, 0, 1, 1, 2);
        w.brightnessLabel = new QLabel(QStringLiteral("Helligkeit"), w.section);
        w.brightness = sliderRow(grid, 1, 0, w.brightnessLabel, &w.brightnessValue, w.section);
        w.brightness->setObjectName(QStringLiteral("cameraLightBrightness_") + row.id);
        // Namen mit der Lampe, sonst heissen alle Regler gleich (Entscheid Lotse, P15 e4)
        w.brightness->setAccessibleName(QStringLiteral("Helligkeit ") + row.name);
        w.brightness->setRange(dss::kLightBrightnessMin, dss::kLightBrightnessMax);
        w.brightness->setPageStep(10);
        w.kelvinLabel = new QLabel(QStringLiteral("Farbtemperatur"), w.section);
        w.kelvin = sliderRow(grid, 2, 0, w.kelvinLabel, &w.kelvinValue, w.section);
        w.kelvin->setObjectName(QStringLiteral("cameraLightKelvin_") + row.id);
        w.kelvin->setAccessibleName(QStringLiteral("Farbtemperatur ") + row.name);
        w.kelvin->setRange(dss::kLightKelvinMin, dss::kLightKelvinMax);
        w.kelvin->setSingleStep(dss::kLightKelvinStep);
        w.kelvin->setPageStep(dss::kLightKelvinStep * 10);
        grid->setColumnStretch(1, 1);

        const QString path = dss::lightPutCall(row.id, {}).path;
        const QString id = row.id;
        connect(w.on, &QCheckBox::clicked, this, [this, id](bool on) {
            send(dss::lightPutCall(id, {{QStringLiteral("on"), on}}));
        });
        QLabel *brightnessValue = w.brightnessValue;
        connect(w.brightness, &QSlider::valueChanged, this, [this, path, brightnessValue](int value) {
            brightnessValue->setText(dss::lightBrightnessText(value));
            putBundled(path, {{QStringLiteral("brightness"), value}});
        });
        QLabel *kelvinValue = w.kelvinValue;
        connect(w.kelvin, &QSlider::valueChanged, this, [this, path, kelvinValue](int value) {
            // Schritte von 50 K (Plan 2.6); der Dienst bekommt Mired
            const int kelvin = qRound(value / double(dss::kLightKelvinStep)) * dss::kLightKelvinStep;
            kelvinValue->setText(dss::lightKelvinText(kelvin));
            putBundled(path, {{QStringLiteral("temperature"), dss::lightMiredFromKelvin(kelvin)}});
        });
        connect(w.brightness, &QSlider::sliderReleased, this, &CameraPage::flushBundled);
        connect(w.kelvin, &QSlider::sliderReleased, this, &CameraPage::flushBundled);
        m_lightSections->addWidget(w.section);
        m_lightWidgets.append(w);
        headers << row.name;
    }
    m_lightPresets->setColumnCount(int(headers.size()));
    m_lightPresets->setHeaderLabels(headers);
    const int digit = m_lightPresets->fontMetrics().horizontalAdvance(QLatin1Char('0'));
    for (int i = 0; i + 1 < headers.size(); ++i)
        m_lightPresets->setColumnWidth(i, digit * (i == 0 ? 12 : 18));
    m_lightPresetRows.clear(); // Spalten neu: Liste neu füllen
    m_lightLayout = key;
    m_lightsBuilt = true;
}

QString CameraPage::selectedLightPresetId() const
{
    const QList<QTreeWidgetItem *> selected = m_lightPresets->selectedItems();
    return selected.isEmpty() ? QString() : selected.first()->data(0, kPresetIdRole).toString();
}

QString CameraPage::selectedLightPresetName() const
{
    const QList<QTreeWidgetItem *> selected = m_lightPresets->selectedItems();
    return selected.isEmpty() ? QString() : selected.first()->text(0);
}

void CameraPage::refreshLights()
{
    rebuildLights();
    for (const dss::LightRow &row : dss::lightRows(m_data.lights(), m_service)) {
        const auto it = std::find_if(m_lightWidgets.cbegin(), m_lightWidgets.cend(),
                                     [&row](const LightWidgets &w) { return w.id == row.id; });
        if (it == m_lightWidgets.cend())
            continue; // neue Lampe: Bereich erst nach dem Dialog «Lampen»
        it->state->setText(row.stateText);
        setStyleState(it->state, row.stateKey);
        {
            const QSignalBlocker block(it->on);
            it->on->setChecked(row.on);
        }
        it->on->setEnabled(row.enabled);
        if (!it->brightness->isSliderDown()) {
            const QSignalBlocker block(it->brightness);
            it->brightness->setValue(row.brightness);
            it->brightnessValue->setText(row.brightnessText);
        }
        if (!it->kelvin->isSliderDown()) {
            const QSignalBlocker block(it->kelvin);
            it->kelvin->setValue(row.kelvin);
            it->kelvinValue->setText(row.kelvinText);
        }
        for (QWidget *w : std::initializer_list<QWidget *>{it->brightnessLabel, it->brightness, it->brightnessValue,
                                                           it->kelvinLabel, it->kelvin, it->kelvinValue})
            w->setEnabled(row.enabled);
    }

    // Licht-Presets nur neu, wenn sich Zeilen ändern; die Wahl bleibt beim selben Preset
    const QList<dss::LightPresetRow> rows = dss::lightPresetRows(m_data.lightPresets(), m_data.lights());
    if (rows != m_lightPresetRows) {
        const QString selected = selectedLightPresetId();
        const QSignalBlocker block(m_lightPresets);
        m_lightPresets->clear();
        for (const dss::LightPresetRow &row : rows) {
            auto *item = new QTreeWidgetItem(QStringList{row.name} + row.cells);
            item->setData(0, kPresetIdRole, row.id);
            m_lightPresets->addTopLevelItem(item);
            if (row.id == selected) {
                m_lightPresets->setCurrentItem(item);
                item->setSelected(true);
            }
        }
        m_lightPresetRows = rows;
    }
    const dss::LightPanelView view =
        dss::lightPanelView(m_service, m_data.lights(), !selectedLightPresetId().isEmpty());
    m_allOn->setEnabled(view.allEnabled);
    m_allOff->setEnabled(view.allEnabled);
    m_lamps->setEnabled(view.lampsEnabled);
    m_lightPresets->setEnabled(view.presetsEnabled);
    m_lightRecall->setEnabled(view.recallEnabled);
    m_lightSave->setEnabled(view.saveEnabled);
    m_lightOverwrite->setEnabled(view.overwriteEnabled);
    m_lightRename->setEnabled(view.editEnabled);
    m_lightRemove->setEnabled(view.editEnabled);
}

void CameraPage::saveLightPreset()
{
    // Wie «Position speichern», nur mit Name (Entwurf Seite 5, Entscheid 1)
    auto *dialog = new PresetNameDialog(
        QStringLiteral("Licht speichern"),
        QStringLiteral("Gespeichert wird der Stand aller Lampen. Eine Lampe, die nicht antwortet, bleibt beim "
                       "Abrufen unverändert."),
        false, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    connect(dialog, &QDialog::accepted, this, [this, dialog] {
        const QString name = dialog->name();
        send(dss::lightPresetCreateCall(name),
             [this, name](const dss::CameraResult &r) { reportResult(r, dss::savedStatusText(name)); });
    });
    dialog->open();
}

void CameraPage::renameLightPreset()
{
    const QString id = selectedLightPresetId();
    QString name = selectedLightPresetName();
    if (id.isEmpty() || !askText(this, QStringLiteral("Licht-Preset umbenennen"), QStringLiteral("Neuer Name"), &name))
        return;
    name = name.trimmed();
    send(dss::lightPresetRenameCall(id, name),
         [this, name](const dss::CameraResult &r) { reportResult(r, dss::savedStatusText(name)); });
}

void CameraPage::removeLightPreset()
{
    const QString id = selectedLightPresetId();
    const QString name = selectedLightPresetName();
    if (id.isEmpty()
        || !confirm(this, QStringLiteral("Licht-Preset löschen?"), QStringLiteral("Licht-Preset «%1» löschen?").arg(name),
                    QStringLiteral("Löschen"), QStringLiteral("Abbrechen")))
        return;
    send(dss::lightPresetDeleteCall(id),
         [this, name](const dss::CameraResult &r) { reportResult(r, dss::deletedStatusText(name)); });
}

void CameraPage::openLights()
{
    if (!m_client || m_lightsDialog)
        return;
    m_lightsDialog = new LightsDialog(m_client, &m_data, this);
    m_lightsDialog->setAttribute(Qt::WA_DeleteOnClose);
    // Nach dem Dialog die Bereiche der Lampen neu (Plan 8); der Dialog wird erst danach gelöscht
    connect(m_lightsDialog, &QDialog::finished, this, [this] {
        m_lightsDialog = nullptr;
        refreshLights();
    });
    m_lightsDialog->open();
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
