#include "docks.h"

#include "symbols.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QDockWidget *dock(const QString &title, const QString &objectName, QWidget *content,
                  QWidget *parent)
{
    auto *d = new QDockWidget(title, parent);
    d->setObjectName(objectName);
    d->setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable
                   | QDockWidget::DockWidgetFloatable);
    d->setWidget(content);
    return d;
}

// Ausgegrauter Symbolknopf; ohne Symboldatei steht der Ersatztext da.
QToolButton *symbolButton(const QString &symbolFile, const QString &fallback,
                          const QString &toolTip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(fallback);
    const QIcon icon = loadSymbol(symbolFile, QSize(14, 14));
    if (!icon.isNull())
        button->setIcon(icon);
    button->setIconSize(QSize(14, 14));
    button->setToolTip(toolTip);
    button->setAccessibleName(toolTip);
    button->setEnabled(false);
    return button;
}

// Leere Liste mit einer Knopfleiste darunter.
QWidget *listWithButtons(const QList<QToolButton *> &buttons, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(new QListWidget(box), 1);
    auto *bar = new QWidget(box);
    bar->setObjectName(QStringLiteral("dockButtons"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(6, 4, 6, 4);
    row->setSpacing(6);
    for (QToolButton *b : buttons) {
        b->setParent(bar);
        row->addWidget(b);
    }
    row->addStretch(1);
    column->addWidget(bar);
    return box;
}

// Zahlenfeld nie schmaler als sein sizeHint (längster Wert mit Einheit), damit der Wert
// neben den Pfeilknöpfen ganz sichtbar bleibt (P06). Ändert nur die Mindestbreite.
void keepValueVisible(QAbstractSpinBox *spin)
{
    spin->setSizePolicy(QSizePolicy::Minimum, spin->sizePolicy().verticalPolicy());
}

QSpinBox *pixelBox(int value, int minimum, QWidget *parent)
{
    auto *spin = new QSpinBox(parent);
    keepValueVisible(spin);
    spin->setRange(minimum, 100000);
    spin->setValue(value);
    spin->setSuffix(QStringLiteral(" px"));
    return spin;
}

} // namespace

QDockWidget *createOverlaysDock(QWidget *parent)
{
    QWidget *content = listWithButtons(
        {symbolButton(QStringLiteral("hinzufuegen.svg"), QStringLiteral("+"),
                      QStringLiteral("Overlay hinzufügen"), nullptr),
         symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"),
                      QStringLiteral("Overlay löschen"), nullptr),
         symbolButton(QStringLiteral("kopieren.svg"), QStringLiteral("K"),
                      QStringLiteral("Overlay kopieren"), nullptr)},
        nullptr);
    return dock(QStringLiteral("Overlays"), QStringLiteral("overlaysDock"), content, parent);
}

QDockWidget *createSourcesDock(QWidget *parent)
{
    QWidget *content = listWithButtons(
        {symbolButton(QStringLiteral("hinzufuegen.svg"), QStringLiteral("+"),
                      QStringLiteral("Quelle hinzufügen"), nullptr),
         symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"),
                      QStringLiteral("Quelle entfernen"), nullptr),
         symbolButton(QStringLiteral("nach-oben.svg"), QStringLiteral("↑"),
                      QStringLiteral("Quelle nach vorne"), nullptr),
         symbolButton(QStringLiteral("nach-unten.svg"), QStringLiteral("↓"),
                      QStringLiteral("Quelle nach hinten"), nullptr)},
        nullptr);
    return dock(QStringLiteral("Quellen"), QStringLiteral("sourcesDock"), content, parent);
}

QDockWidget *createPropertiesDock(QWidget *parent)
{
    auto *content = new QWidget;
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto *form = new QWidget(content);
    auto *grid = new QGridLayout(form);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    auto *name = new QLineEdit(form);
    auto *x = pixelBox(0, -100000, form);
    auto *y = pixelBox(0, -100000, form);
    auto *width = pixelBox(1, 1, form);
    auto *height = pixelBox(1, 1, form);
    auto *rotation = new QDoubleSpinBox(form);
    rotation->setDecimals(1);
    rotation->setRange(0.0, 359.9);
    rotation->setSuffix(QStringLiteral(" °"));
    keepValueVisible(rotation);
    for (QWidget *w : std::initializer_list<QWidget *>{name, x, y, width, height, rotation})
        w->setEnabled(false);
    grid->addWidget(new QLabel(QStringLiteral("Name"), form), 0, 0);
    grid->addWidget(name, 0, 1, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("X"), form), 1, 0);
    grid->addWidget(x, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("Y"), form), 1, 2);
    grid->addWidget(y, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("Breite"), form), 2, 0);
    grid->addWidget(width, 2, 1);
    grid->addWidget(new QLabel(QStringLiteral("Höhe"), form), 2, 2);
    grid->addWidget(height, 2, 3);
    grid->addWidget(new QLabel(QStringLiteral("Drehung"), form), 3, 0);
    grid->addWidget(rotation, 3, 1);
    auto *hint = new QLabel(QStringLiteral("Keine Quelle gewählt"), form);
    hint->setObjectName(QStringLiteral("hintLabel"));
    grid->addWidget(hint, 3, 2, 1, 2);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    grid->setRowStretch(4, 1);
    column->addWidget(form, 1);

    auto *bar = new QWidget(content);
    bar->setObjectName(QStringLiteral("dockButtons"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(6, 4, 6, 4);
    row->setSpacing(6);
    const QList<std::pair<QString, QString>> align = {
        {QStringLiteral("ausrichten-links.svg"), QStringLiteral("Links ausrichten")},
        {QStringLiteral("ausrichten-mitte-waagrecht.svg"), QStringLiteral("Waagrecht zentrieren")},
        {QStringLiteral("ausrichten-rechts.svg"), QStringLiteral("Rechts ausrichten")},
        {QStringLiteral("ausrichten-oben.svg"), QStringLiteral("Oben ausrichten")},
        {QStringLiteral("ausrichten-mitte-senkrecht.svg"), QStringLiteral("Senkrecht zentrieren")},
        {QStringLiteral("ausrichten-unten.svg"), QStringLiteral("Unten ausrichten")},
    };
    for (const auto &[file, toolTip] : align)
        row->addWidget(symbolButton(file, toolTip.left(1), toolTip, bar));
    row->addStretch(1);
    column->addWidget(bar);
    return dock(QStringLiteral("Eigenschaften"), QStringLiteral("propertiesDock"), content, parent);
}

QDockWidget *createControlDock(QWidget *parent)
{
    auto *content = new QWidget;
    auto *grid = new QGridLayout(content);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    auto *address = new QLineEdit(content);
    address->setReadOnly(true);
    auto *copy = new QPushButton(QStringLiteral("Kopieren"), content);
    auto *output = new QComboBox(content);
    output->addItem(QStringLiteral("Browserquelle"));
    auto *outputState = new QLabel(QStringLiteral("Ausgabe: –"), content);
    auto *preview = new QSpinBox(content);
    preview->setRange(1, 60);
    preview->setValue(30);
    preview->setSuffix(QStringLiteral(" Bilder/s"));
    keepValueVisible(preview);
    auto *previewSmall = new QCheckBox(QStringLiteral("Vorschau verkleinern"), content);
    auto *snap = new QCheckBox(QStringLiteral("Einrasten"), content);
    snap->setChecked(true);
    auto *gridCheck = new QCheckBox(QStringLiteral("Raster"), content);
    auto *gridSize = new QSpinBox(content);
    gridSize->setRange(1, 1000);
    gridSize->setValue(20);
    gridSize->setSuffix(QStringLiteral(" px"));
    keepValueVisible(gridSize);
    auto *clearGuides = new QPushButton(QStringLiteral("Hilfslinien löschen"), content);
    for (QWidget *w : std::initializer_list<QWidget *>{address, copy, output, outputState, preview,
                                                       previewSmall, snap, gridCheck, gridSize,
                                                       clearGuides})
        w->setEnabled(false);

    grid->addWidget(new QLabel(QStringLiteral("Browserquelle"), content), 0, 0);
    grid->addWidget(address, 0, 1, 1, 2);
    grid->addWidget(copy, 0, 3);
    grid->addWidget(new QLabel(QStringLiteral("Ausgabe"), content), 1, 0);
    grid->addWidget(output, 1, 1);
    grid->addWidget(outputState, 1, 2, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("Vorschau"), content), 2, 0);
    grid->addWidget(preview, 2, 1);
    grid->addWidget(previewSmall, 2, 2, 1, 2);
    auto *snapRow = new QHBoxLayout;
    snapRow->setSpacing(8);
    snapRow->addWidget(snap);
    snapRow->addStretch(1);
    snapRow->addWidget(gridCheck);
    grid->addLayout(snapRow, 3, 0, 1, 2);
    grid->addWidget(gridSize, 3, 2);
    grid->addWidget(clearGuides, 3, 3);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setRowStretch(4, 1);
    return dock(QStringLiteral("Steuerung"), QStringLiteral("controlDock"), content, parent);
}
