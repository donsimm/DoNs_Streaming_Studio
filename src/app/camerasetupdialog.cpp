#include "camerasetupdialog.h"

#include "cameraview.h"
#include "coreclient.h"
#include "panels.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

// Titel und Linie wie die Bereiche im Dialog «Einstellungen» (settingsdialog.cpp, «OBS»)
void addSectionTitle(QVBoxLayout *column, const QString &title, QWidget *parent)
{
    auto *label = new QLabel(title, parent);
    label->setObjectName(QStringLiteral("propertySectionTitle"));
    column->addSpacing(6);
    column->addWidget(label);
    auto *line = new QFrame(parent);
    line->setObjectName(QStringLiteral("propertySectionLine"));
    line->setFixedHeight(1);
    column->addWidget(line);
}

// Zahl für JSON: ganze Werte als ganze Zahl (wie der Dienst sie schreibt), sonst eine Stelle
QJsonValue number(double value)
{
    const double rounded = qRound(value * 10) / 10.0;
    return rounded == qRound(rounded) ? QJsonValue(qRound(rounded)) : QJsonValue(rounded);
}

} // namespace

CameraSetupDialog::CameraSetupDialog(dss::CoreClient *client, const dss::CameraData *data,
                                     const QString &cameraId, QWidget *parent)
    : QDialog(parent), m_client(client), m_data(data), m_cameraId(cameraId)
{
    setObjectName(QStringLiteral("cameraSetupDialog"));
    const dss::CameraEntry *camera = data->camera(cameraId);
    setWindowTitle(QStringLiteral("Kamera einrichten – %1").arg(camera ? camera->name : cameraId));
    const bool tiny2 = camera && dss::cameraIsTiny2(*camera);
    auto *column = new QVBoxLayout(this);
    column->setSpacing(4);

    auto *nameForm = new QFormLayout;
    m_name = new QLineEdit(this);
    m_name->setObjectName(QStringLiteral("cameraSetupName"));
    m_name->setMaxLength(40); // NAME_MAX im Dienst (motion.py)
    nameForm->addRow(QStringLiteral("Name"), m_name);
    column->addLayout(nameForm);

    addSectionTitle(column, QStringLiteral("Grenzen"), this);
    auto *limits = new QGridLayout;
    limits->setHorizontalSpacing(8);
    m_panMin = limitBox("cameraSetupPanMin");
    m_panMax = limitBox("cameraSetupPanMax");
    m_tiltMin = limitBox("cameraSetupTiltMin");
    m_tiltMax = limitBox("cameraSetupTiltMax");
    // Vier Felder mit «von»/«bis»: Namen für Screenreader wie die Zeile im Raster
    m_panMin->setAccessibleName(QStringLiteral("Schwenken von"));
    m_panMax->setAccessibleName(QStringLiteral("Schwenken bis"));
    m_tiltMin->setAccessibleName(QStringLiteral("Neigen von"));
    m_tiltMax->setAccessibleName(QStringLiteral("Neigen bis"));
    limits->addWidget(new QLabel(QStringLiteral("Schwenken"), this), 0, 0);
    limits->addWidget(new QLabel(QStringLiteral("von"), this), 0, 1);
    limits->addWidget(m_panMin, 0, 2);
    limits->addWidget(new QLabel(QStringLiteral("bis"), this), 0, 3);
    limits->addWidget(m_panMax, 0, 4);
    limits->addWidget(new QLabel(QStringLiteral("Neigen"), this), 1, 0);
    limits->addWidget(new QLabel(QStringLiteral("von"), this), 1, 1);
    limits->addWidget(m_tiltMin, 1, 2);
    limits->addWidget(new QLabel(QStringLiteral("bis"), this), 1, 3);
    limits->addWidget(m_tiltMax, 1, 4);
    limits->setColumnStretch(5, 1);
    column->addLayout(limits);
    auto *limitsHint = new QLabel(QStringLiteral("Unter der unteren Grenze schläft die Tiny 2 von selbst ein. Der Dienst "
                                                 "begrenzt jedes Ziel, auch aus Positionen und vom Stream Deck."),
                                  this);
    limitsHint->setObjectName(QStringLiteral("hintLabel"));
    limitsHint->setWordWrap(true);
    column->addWidget(limitsHint);

    addSectionTitle(column, QStringLiteral("Richtung"), this);
    auto *direction = new QHBoxLayout;
    direction->setSpacing(8);
    m_invertPan = new QCheckBox(QStringLiteral("Schwenken umkehren"), this);
    m_invertPan->setObjectName(QStringLiteral("cameraSetupInvertPan"));
    m_invertTilt = new QCheckBox(QStringLiteral("Neigen umkehren"), this);
    m_invertTilt->setObjectName(QStringLiteral("cameraSetupInvertTilt"));
    direction->addWidget(m_invertPan);
    direction->addWidget(m_invertTilt);
    direction->addStretch(1);
    column->addLayout(direction);

    addSectionTitle(column, QStringLiteral("Bewegen"), this);
    auto *move = new QFormLayout;
    m_speed = new QSpinBox(this);
    m_speed->setObjectName(QStringLiteral("cameraSetupSpeed"));
    m_speed->setSuffix(dss::cameraSpeedSuffix(camera));
    keepValueVisible(m_speed);
    m_step = new QDoubleSpinBox(this);
    m_step->setObjectName(QStringLiteral("cameraSetupStep"));
    m_step->setDecimals(1);
    m_step->setSuffix(QStringLiteral(" °"));
    keepValueVisible(m_step);
    m_speed->setEnabled(false);
    m_step->setEnabled(false);
    move->addRow(QStringLiteral("Tempo"), m_speed);
    move->addRow(QStringLiteral("Schrittweite"), m_step);
    column->addLayout(move);

    addSectionTitle(column, QStringLiteral("Verhalten"), this);
    // [OFFEN] Der Dienst hat dafür kein Feld: Abrufen weckt die Tiny 2 immer (docs/09-kamera-dienst.md,
    // «Kamera-Presets»). Darum angekreuzt und nicht bedienbar.
    m_presetsWake = new QCheckBox(QStringLiteral("Presets wecken die Kamera"), this);
    m_presetsWake->setObjectName(QStringLiteral("cameraSetupPresetsWake"));
    m_presetsWake->setChecked(tiny2);
    m_presetsWake->setEnabled(false);
    m_aiOff = new QCheckBox(QStringLiteral("KI-Verfolgung vor dem Bewegen aus"), this);
    m_aiOff->setObjectName(QStringLiteral("cameraSetupAiOff"));
    m_aiOff->setEnabled(false);
    if (!tiny2) {
        // Entwurf Seite 5: bei der C922 ausgegraut
        m_presetsWake->setToolTip(QStringLiteral("Diese Kamera hat das nicht."));
        m_aiOff->setToolTip(QStringLiteral("Diese Kamera hat das nicht."));
    }
    column->addWidget(m_presetsWake);
    column->addWidget(m_aiOff);

    addSectionTitle(column, QStringLiteral("Gemerkte Werte"), this);
    m_remembered = new QTreeWidget(this);
    m_remembered->setObjectName(QStringLiteral("cameraRemembered"));
    m_remembered->setAccessibleName(QStringLiteral("Gemerkte Werte"));
    m_remembered->setColumnCount(3);
    m_remembered->setHeaderLabels(
        {QStringLiteral("Wert"), QStringLiteral("gemerkt"), QStringLiteral("Standard des Geräts")});
    m_remembered->setRootIsDecorated(false);
    m_remembered->setUniformRowHeights(true);
    m_remembered->setSelectionMode(QAbstractItemView::SingleSelection);
    const int digit = m_remembered->fontMetrics().horizontalAdvance(QLatin1Char('0'));
    m_remembered->header()->setStretchLastSection(true);
    m_remembered->setColumnWidth(0, digit * 26);
    m_remembered->setColumnWidth(1, digit * 16);
    m_remembered->setMinimumHeight(m_remembered->fontMetrics().lineSpacing() * 8);
    m_forget = new QPushButton(QStringLiteral("Vergessen"));
    m_forget->setObjectName(QStringLiteral("cameraSetupForget"));
    auto *forgetHint = new QLabel(QStringLiteral("Der Dienst setzt den Wert beim Erscheinen der Kamera nicht mehr."));
    forgetHint->setObjectName(QStringLiteral("hintLabel"));
    auto *rememberedBox = new QWidget(this);
    auto *rememberedColumn = new QVBoxLayout(rememberedBox);
    rememberedColumn->setContentsMargins(0, 0, 0, 0);
    rememberedColumn->setSpacing(0);
    rememberedColumn->addWidget(m_remembered, 1);
    rememberedColumn->addWidget(buttonBar({m_forget, forgetHint}, {}, rememberedBox));
    column->addWidget(rememberedBox, 1);
    connect(m_remembered, &QTreeWidget::itemSelectionChanged, this, &CameraSetupDialog::refresh);
    connect(m_forget, &QPushButton::clicked, this, [this] {
        const QList<QTreeWidgetItem *> selected = m_remembered->selectedItems();
        if (selected.isEmpty())
            return;
        const QString key = selected.first()->data(0, Qt::UserRole).toString();
        m_problem->clear();
        m_forgetRequest = m_client->callCamera(dss::cameraControlsPutCall(m_cameraId, {{key, QJsonValue::Null}}));
    });

    // Meldung des Dienstes: Platz für zwei Zeilen von Anfang an (wie #settingsProblem, Regel 9)
    m_problem = new QLabel(this);
    m_problem->setObjectName(QStringLiteral("cameraSetupProblem"));
    m_problem->setWordWrap(true);
    m_problem->setTextFormat(Qt::PlainText);
    m_problem->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_problem->setFixedHeight(fontMetrics().lineSpacing() * 2);
    column->addWidget(m_problem);

    auto *buttons = new QDialogButtonBox(this);
    m_ok = buttons->addButton(QStringLiteral("OK"), QDialogButtonBox::AcceptRole);
    m_ok->setObjectName(QStringLiteral("cameraSetupOk"));
    m_ok->setDefault(true);
    buttons->addButton(QStringLiteral("Abbrechen"), QDialogButtonBox::RejectRole);
    column->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &CameraSetupDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_client, &dss::CoreClient::cameraResultReceived, this,
            [this](const dss::CameraCall &call, const dss::CameraResult &result) {
                const QString error = dss::cameraResultError(result);
                if (call.requestId == m_request) {
                    m_request = 0;
                    if (error.isEmpty()) {
                        QDialog::accept();
                        return;
                    }
                    m_problem->setText(error);
                    refresh();
                } else if (call.requestId == m_forgetRequest) {
                    m_forgetRequest = 0;
                    m_problem->setText(error);
                }
            });
    refresh();
    resize(fontMetrics().averageCharWidth() * 90, sizeHint().height());
}

QDoubleSpinBox *CameraSetupDialog::limitBox(const char *objectName)
{
    auto *box = new QDoubleSpinBox(this);
    box->setObjectName(QString::fromLatin1(objectName));
    box->setDecimals(1);
    box->setSuffix(QStringLiteral(" °"));
    // Der kleinste Wert steht für «Standard» (null an den Dienst)
    box->setSpecialValueText(QStringLiteral("Standard"));
    keepValueVisible(box);
    box->setEnabled(false); // bis die Einstellungen da sind (fill)
    return box;
}

void CameraSetupDialog::setLimit(QDoubleSpinBox *box, const QString &key)
{
    const QJsonObject config = m_data->config(m_cameraId);
    const auto [low, high] = dss::cameraConfigRange(config, key, -180, 180);
    box->setRange(low - 1, high);
    const QJsonValue value = config.value(QLatin1String("config")).toObject().value(key);
    box->setValue(value.isDouble() ? value.toDouble() : box->minimum());
    box->setEnabled(config.value(QLatin1String("ranges")).toObject().contains(key));
}

void CameraSetupDialog::fill()
{
    const QJsonObject config = m_data->config(m_cameraId);
    const QJsonObject values = config.value(QLatin1String("config")).toObject();
    if (values.isEmpty())
        return;
    const QJsonObject ranges = config.value(QLatin1String("ranges")).toObject();
    const dss::CameraEntry *camera = m_data->camera(m_cameraId);
    const bool tiny2 = camera && dss::cameraIsTiny2(*camera);
    m_name->setText(values.value(QLatin1String("name")).toString());
    setLimit(m_panMin, QStringLiteral("pan_min"));
    setLimit(m_panMax, QStringLiteral("pan_max"));
    setLimit(m_tiltMin, QStringLiteral("tilt_min"));
    setLimit(m_tiltMax, QStringLiteral("tilt_max"));
    m_invertPan->setChecked(values.value(QLatin1String("invert_pan")).toBool());
    m_invertTilt->setChecked(values.value(QLatin1String("invert_tilt")).toBool());
    const auto [speedLow, speedHigh] = dss::cameraConfigRange(config, QStringLiteral("speed"), 1, 100);
    m_speed->setRange(int(speedLow), int(speedHigh));
    m_speed->setValue(values.value(QLatin1String("speed")).toInt());
    m_speed->setEnabled(ranges.contains(QLatin1String("speed")));
    const auto [stepLow, stepHigh] = dss::cameraConfigRange(config, QStringLiteral("step_deg"), 1, 90);
    m_step->setRange(stepLow, stepHigh);
    m_step->setValue(values.value(QLatin1String("step_deg")).toDouble());
    m_step->setEnabled(ranges.contains(QLatin1String("step_deg")));
    m_aiOff->setChecked(values.value(QLatin1String("ai_off_before_move")).toBool(true));
    m_aiOff->setEnabled(tiny2);
    m_before = formValues();
    m_filled = true;
}

QJsonObject CameraSetupDialog::formValues() const
{
    const QJsonObject ranges = m_data->config(m_cameraId).value(QLatin1String("ranges")).toObject();
    QJsonObject v;
    const QString name = m_name->text().trimmed();
    v.insert(QStringLiteral("name"), name.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(name));
    const auto limit = [&](const QDoubleSpinBox *box, const char *key) {
        if (ranges.contains(QLatin1String(key)))
            v.insert(QLatin1String(key), box->value() == box->minimum() ? QJsonValue(QJsonValue::Null)
                                                                         : number(box->value()));
    };
    limit(m_panMin, "pan_min");
    limit(m_panMax, "pan_max");
    limit(m_tiltMin, "tilt_min");
    limit(m_tiltMax, "tilt_max");
    v.insert(QStringLiteral("invert_pan"), m_invertPan->isChecked());
    v.insert(QStringLiteral("invert_tilt"), m_invertTilt->isChecked());
    if (ranges.contains(QLatin1String("speed")))
        v.insert(QStringLiteral("speed"), m_speed->value());
    if (ranges.contains(QLatin1String("step_deg")))
        v.insert(QStringLiteral("step_deg"), number(m_step->value()));
    if (m_aiOff->isEnabled())
        v.insert(QStringLiteral("ai_off_before_move"), m_aiOff->isChecked());
    return v;
}

void CameraSetupDialog::refresh()
{
    if (!m_filled)
        fill();
    for (QWidget *w : std::initializer_list<QWidget *>{m_name, m_invertPan, m_invertTilt})
        w->setEnabled(m_filled);

    // Gemerkte Werte nach jedem Ereignis controls; die Wahl bleibt beim selben Wert
    const QList<QTreeWidgetItem *> selected = m_remembered->selectedItems();
    const QString selectedKey = selected.isEmpty() ? QString() : selected.first()->data(0, Qt::UserRole).toString();
    {
        const QSignalBlocker block(m_remembered);
        m_remembered->clear();
        for (const dss::CameraRememberedRow &row : dss::cameraRememberedRows(m_data->controls(m_cameraId))) {
            auto *item = new QTreeWidgetItem({row.label, row.value, row.defaultValue});
            item->setData(0, Qt::UserRole, row.key);
            m_remembered->addTopLevelItem(item);
            if (row.key == selectedKey) {
                m_remembered->setCurrentItem(item);
                item->setSelected(true);
            }
        }
    }
    m_forget->setEnabled(!m_remembered->selectedItems().isEmpty());
    m_ok->setEnabled(m_filled && m_request == 0);
}

void CameraSetupDialog::accept()
{
    if (!m_filled || m_request != 0)
        return;
    const QJsonObject changes = dss::cameraConfigChanges(m_before, formValues());
    if (changes.isEmpty()) {
        QDialog::accept();
        return;
    }
    m_problem->clear();
    m_request = m_client->callCamera(dss::cameraConfigPutCall(m_cameraId, changes));
    if (m_request == 0)
        m_problem->setText(QStringLiteral("Keine Verbindung zum Kern, Änderung nicht geschickt"));
    refresh();
}
