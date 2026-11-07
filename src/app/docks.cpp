#include "docks.h"

#include "dockviews.h"
#include "overlay.h"
#include "symbols.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
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

// Liste mit einer Knopfleiste darunter.
QWidget *listWithButtons(const QList<QToolButton *> &buttons, QWidget *list, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    list->setParent(box);
    column->addWidget(list, 1);
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

// Ganze Pixel der Overlay-Auflösung (CLAUDE.md Regel 11). Ohne keyboardTracking meldet das Feld
// einen Wert erst nach Enter, beim Verlassen oder mit den Pfeilen, nicht bei jeder Ziffer.
// https://doc.qt.io/qt-6.8/qabstractspinbox.html#keyboardTracking-prop
QSpinBox *pixelBox(int value, int minimum, QWidget *parent)
{
    auto *spin = new QSpinBox(parent);
    keepValueVisible(spin);
    spin->setRange(minimum, 100000);
    spin->setValue(value);
    spin->setSuffix(QStringLiteral(" px"));
    spin->setKeyboardTracking(false);
    return spin;
}

// Feste Namen für den Test des Fensters (tst_fensterbearbeiten).
void nameWidgets(std::initializer_list<std::pair<QObject *, const char *>> widgets)
{
    for (const auto &[w, name] : widgets)
        w->setObjectName(QString::fromLatin1(name));
}

// Bereich im Dock «Eigenschaften» (P09b, E-26): kleiner Titel, darunter eine feine Linie wie
// der Rahmen des Kopfbereichs (Farbe border), darunter die Felder im Raster *grid. Knöpfe in
// titleButtons stehen in der Titelzeile am rechten Rand (P10a, Teil A 2).
QWidget *section(const QString &title, QWidget *parent, QGridLayout **grid,
                 const QList<QToolButton *> &titleButtons = {})
{
    auto *w = new QWidget(parent);
    auto *column = new QVBoxLayout(w);
    column->setContentsMargins(8, 6, 8, 8);
    column->setSpacing(4);
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(4);
    auto *label = new QLabel(title, w);
    label->setObjectName(QStringLiteral("propertySectionTitle"));
    titleRow->addWidget(label);
    titleRow->addStretch(1);
    for (QToolButton *b : titleButtons) {
        b->setParent(w);
        titleRow->addWidget(b);
    }
    column->addLayout(titleRow);
    auto *line = new QFrame(w);
    line->setObjectName(QStringLiteral("propertySectionLine"));
    line->setFixedHeight(1);
    column->addWidget(line);
    column->addSpacing(2);
    *grid = new QGridLayout;
    (*grid)->setContentsMargins(0, 0, 0, 0);
    (*grid)->setHorizontalSpacing(8);
    (*grid)->setVerticalSpacing(6);
    column->addLayout(*grid);
    return w;
}

} // namespace

OverlaysDock createOverlaysDock(QWidget *parent)
{
    OverlaysDock result;
    result.add = symbolButton(QStringLiteral("hinzufuegen.svg"), QStringLiteral("+"),
                              QStringLiteral("Overlay hinzufügen"), nullptr);
    result.remove = symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"),
                                 QStringLiteral("Overlay löschen"), nullptr);
    result.copy = symbolButton(QStringLiteral("kopieren.svg"), QStringLiteral("K"),
                               QStringLiteral("Overlay kopieren"), nullptr);
    result.tree = new OverlayTree;
    result.tree->setAccessibleName(QStringLiteral("Overlays"));
    nameWidgets({{result.tree, "overlayTree"}, {result.add, "overlayAdd"},
                 {result.remove, "overlayRemove"}, {result.copy, "overlayCopy"}});
    QWidget *content = listWithButtons({result.add, result.remove, result.copy}, result.tree, nullptr);
    result.dock = dock(QStringLiteral("Overlays"), QStringLiteral("overlaysDock"), content, parent);
    return result;
}

SourcesDock createSourcesDock(QWidget *parent)
{
    SourcesDock result;
    result.add = symbolButton(QStringLiteral("hinzufuegen.svg"), QStringLiteral("+"),
                              QStringLiteral("Quelle hinzufügen"), nullptr);
    auto *menu = new QMenu(result.add);
    result.addImage = menu->addAction(QStringLiteral("Bild …"));
    result.addVideo = menu->addAction(QStringLiteral("Video …"));
    result.addText = menu->addAction(QStringLiteral("Text"));
    result.add->setMenu(menu);
    result.add->setPopupMode(QToolButton::InstantPopup);
    result.remove = symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"),
                                 QStringLiteral("Quelle entfernen"), nullptr);
    result.forward = symbolButton(QStringLiteral("nach-oben.svg"), QStringLiteral("↑"),
                                  QStringLiteral("Quelle nach vorne"), nullptr);
    result.backward = symbolButton(QStringLiteral("nach-unten.svg"), QStringLiteral("↓"),
                                   QStringLiteral("Quelle nach hinten"), nullptr);
    result.list = new SourceList;
    result.list->setAccessibleName(QStringLiteral("Quellen"));
    nameWidgets({{result.list, "sourceList"}, {result.add, "sourceAdd"},
                 {result.remove, "sourceRemove"}, {result.forward, "sourceForward"},
                 {result.backward, "sourceBackward"}});
    QWidget *content = listWithButtons({result.add, result.remove, result.forward, result.backward},
                                       result.list, nullptr);
    result.dock = dock(QStringLiteral("Quellen"), QStringLiteral("sourcesDock"), content, parent);
    return result;
}

MediaDock createMediaDock(QWidget *parent)
{
    MediaDock result;
    result.add = symbolButton(QStringLiteral("hinzufuegen.svg"), QStringLiteral("+"),
                              QStringLiteral("Dateien hinzufügen …"), nullptr);
    result.addFolder = symbolButton(QStringLiteral("ordner-neu.svg"), QStringLiteral("O"),
                                    QStringLiteral("Ordner anlegen …"), nullptr);
    result.remove = symbolButton(QStringLiteral("entfernen.svg"), QStringLiteral("–"),
                                 QStringLiteral("Datei oder Ordner löschen"), nullptr);
    result.tree = new MediaTree;
    result.tree->setAccessibleName(QStringLiteral("Medien"));
    // Zeile zur Auswahl: immer da (keine Änderung der Anordnung beim Wechsel der Auswahl), eine
    // Zeile, abgeschnitten; der ganze Text steht im Hinweis beim Darüberfahren.
    auto *list = new QWidget;
    auto *column = new QVBoxLayout(list);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(2);
    column->addWidget(result.tree, 1);
    result.info = new QLabel(list);
    result.info->setObjectName(QStringLiteral("mediaInfo"));
    result.info->setContentsMargins(6, 0, 6, 0);
    result.info->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    result.info->setTextFormat(Qt::PlainText);
    column->addWidget(result.info);
    nameWidgets({{result.tree, "mediaTree"}, {result.add, "mediaAdd"},
                 {result.addFolder, "mediaAddFolder"}, {result.remove, "mediaRemove"}});
    QWidget *content = listWithButtons({result.add, result.addFolder, result.remove}, list, nullptr);
    result.dock = dock(QStringLiteral("Medien"), QStringLiteral("mediaDock"), content, parent);
    return result;
}

PropertiesDock createPropertiesDock(QWidget *parent)
{
    PropertiesDock r;
    auto *content = new QWidget;
    auto *column = new QVBoxLayout(content);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    // Felder in einem Bereich mit Bildlaufleiste: Bei einer Textquelle sind es mehr, als in
    // die Höhe des Docks passen.
    auto *scroll = new QScrollArea(content);
    scroll->setObjectName(QStringLiteral("propertiesScroll"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *form = new QWidget(scroll);
    form->setObjectName(QStringLiteral("propertiesForm"));
    auto *formColumn = new QVBoxLayout(form);
    formColumn->setContentsMargins(0, 0, 0, 0);
    formColumn->setSpacing(0);

    // Oben der Name (P09b, E-26), darunter der Hinweis (keine Quelle, nur Anzeige).
    auto *nameBox = new QWidget(form);
    auto *nameGrid = new QGridLayout(nameBox);
    nameGrid->setContentsMargins(8, 8, 8, 0);
    nameGrid->setHorizontalSpacing(8);
    nameGrid->setVerticalSpacing(4);
    r.name = new QLineEdit(nameBox);
    r.hint = new QLabel(QStringLiteral("Keine Quelle gewählt"), nameBox);
    r.hint->setObjectName(QStringLiteral("hintLabel"));
    nameGrid->addWidget(new QLabel(QStringLiteral("Name"), nameBox), 0, 0);
    nameGrid->addWidget(r.name, 0, 1);
    nameGrid->addWidget(r.hint, 1, 1);
    nameGrid->setColumnStretch(1, 1);
    formColumn->addWidget(nameBox);

    // Bereich «Transformation»: Lage, Grösse, Drehung, Spiegeln (für alle Arten). In der
    // Titelzeile rechts Kopieren, Einfügen, Zurücksetzen (P10a, Teil A 2); was sie tun und wann
    // sie gehen, verbindet MainWindow mit den Befehlen «Transformieren» (E-25).
    r.copyTransform = symbolButton(QStringLiteral("kopieren.svg"), QStringLiteral("K"),
                                   QStringLiteral("Transformation kopieren"), nullptr);
    r.pasteTransform = symbolButton(QStringLiteral("einfuegen.svg"), QStringLiteral("E"),
                                    QStringLiteral("Transformation einfügen"), nullptr);
    r.resetTransform = symbolButton(QStringLiteral("zuruecksetzen.svg"), QStringLiteral("Z"),
                                    QStringLiteral("Transformation zurücksetzen"), nullptr);
    nameWidgets({{r.copyTransform, "transformCopyButton"},
                 {r.pasteTransform, "transformPasteButton"},
                 {r.resetTransform, "transformResetButton"}});
    QGridLayout *grid = nullptr;
    QWidget *transform = section(QStringLiteral("Transformation"), form, &grid,
                                 {r.copyTransform, r.pasteTransform, r.resetTransform});
    transform->setObjectName(QStringLiteral("propertyTransform"));
    r.x = pixelBox(0, -100000, transform);
    r.y = pixelBox(0, -100000, transform);
    r.width = pixelBox(1, 1, transform);
    r.height = pixelBox(1, 1, transform);
    // Drehung 0.0 bis 359.9 Grad, gespeichert in Zehntelgrad; das Feld läuft rundum.
    r.rotation = new QDoubleSpinBox(transform);
    r.rotation->setDecimals(1);
    r.rotation->setRange(0.0, 359.9);
    r.rotation->setWrapping(true);
    r.rotation->setSuffix(QStringLiteral(" °"));
    r.rotation->setKeyboardTracking(false);
    keepValueVisible(r.rotation);
    r.flipHorizontal = new QCheckBox(QStringLiteral("Waagrecht gespiegelt"), transform);
    r.flipVertical = new QCheckBox(QStringLiteral("Senkrecht gespiegelt"), transform);
    grid->addWidget(new QLabel(QStringLiteral("X"), transform), 0, 0);
    grid->addWidget(r.x, 0, 1);
    grid->addWidget(new QLabel(QStringLiteral("Y"), transform), 0, 2);
    grid->addWidget(r.y, 0, 3);
    grid->addWidget(new QLabel(QStringLiteral("Breite"), transform), 1, 0);
    grid->addWidget(r.width, 1, 1);
    grid->addWidget(new QLabel(QStringLiteral("Höhe"), transform), 1, 2);
    grid->addWidget(r.height, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("Drehung"), transform), 2, 0);
    grid->addWidget(r.rotation, 2, 1);
    grid->addWidget(r.flipHorizontal, 3, 0, 1, 2);
    grid->addWidget(r.flipVertical, 3, 2, 1, 2);
    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);
    formColumn->addWidget(transform);

    // Bereich je Art der Quelle («Bild», «Video», «Text»); die Seite wechselt mit der gewählten
    // Quelle (kein Umbau des Layouts, CLAUDE.md Regel 9).
    r.kindPages = new QStackedWidget(form);
    r.kindPages->addWidget(new QWidget(r.kindPages)); // kEmptyPage

    QGridLayout *g = nullptr;
    QWidget *imagePage = section(QStringLiteral("Bild"), r.kindPages, &g);
    r.replaceImage = new QPushButton(QStringLiteral("Datei ersetzen …"), imagePage);
    g->addWidget(r.replaceImage, 0, 0);
    g->setColumnStretch(1, 1);
    g->setRowStretch(1, 1);
    r.kindPages->addWidget(imagePage);

    QWidget *videoPage = section(QStringLiteral("Video"), r.kindPages, &g);
    r.loop = new QCheckBox(QStringLiteral("Wiederholen"), videoPage);
    g->addWidget(r.loop, 0, 0);
    r.holdAtEnd = new QCheckBox(QStringLiteral("Am Ende anhalten"), videoPage);
    r.holdAtEnd->setToolTip(QStringLiteral("Letztes Bild bleibt stehen. Ohne beides wird die Quelle "
                                           "am Ende leer."));
    g->addWidget(r.holdAtEnd, 1, 0);
    g->setColumnStretch(1, 1);
    g->setRowStretch(2, 1);
    r.kindPages->addWidget(videoPage);

    QWidget *textPage = section(QStringLiteral("Text"), r.kindPages, &g);
    r.content = new QPlainTextEdit(textPage);
    r.content->setAccessibleName(QStringLiteral("Inhalt"));
    r.content->setTabChangesFocus(true);
    r.content->setFixedHeight(r.content->fontMetrics().lineSpacing() * 3 + 16);
    r.family = new QComboBox(textPage);
    r.family->setAccessibleName(QStringLiteral("Schrift"));
    r.family->setPlaceholderText(QStringLiteral("Schrift unbekannt"));
    r.family->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    r.family->setMinimumContentsLength(10);
    r.weight = new QComboBox(textPage);
    r.weight->setAccessibleName(QStringLiteral("Schnitt"));
    r.weight->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    r.weight->setMinimumContentsLength(8);
    r.italic = new QCheckBox(QStringLiteral("Kursiv"), textPage);
    r.fontSize = pixelBox(48, 1, textPage);
    r.color = new QToolButton(textPage);
    r.color->setObjectName(QStringLiteral("colorButton"));
    r.color->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    r.color->setAccessibleName(QStringLiteral("Farbe"));
    r.color->setToolTip(QStringLiteral("Farbe mit Deckkraft wählen"));
    r.align = new QComboBox(textPage);
    r.align->setAccessibleName(QStringLiteral("Ausrichtung"));
    r.align->addItem(QStringLiteral("Links"), int(dss::TextAlign::Left));
    r.align->addItem(QStringLiteral("Mitte"), int(dss::TextAlign::Center));
    r.align->addItem(QStringLiteral("Rechts"), int(dss::TextAlign::Right));
    g->addWidget(new QLabel(QStringLiteral("Inhalt"), textPage), 0, 0, Qt::AlignTop);
    g->addWidget(r.content, 0, 1, 1, 3);
    g->addWidget(new QLabel(QStringLiteral("Schrift"), textPage), 1, 0);
    g->addWidget(r.family, 1, 1, 1, 3);
    g->addWidget(new QLabel(QStringLiteral("Schnitt"), textPage), 2, 0);
    g->addWidget(r.weight, 2, 1);
    g->addWidget(r.italic, 2, 2, 1, 2);
    g->addWidget(new QLabel(QStringLiteral("Grösse"), textPage), 3, 0);
    g->addWidget(r.fontSize, 3, 1);
    g->addWidget(new QLabel(QStringLiteral("Farbe"), textPage), 4, 0);
    g->addWidget(r.color, 4, 1, 1, 3, Qt::AlignLeft);
    g->addWidget(new QLabel(QStringLiteral("Ausrichtung"), textPage), 5, 0);
    g->addWidget(r.align, 5, 1);
    g->setColumnStretch(1, 1);
    g->setColumnStretch(3, 1);
    g->setRowStretch(6, 1);
    r.kindPages->addWidget(textPage);
    formColumn->addWidget(r.kindPages);
    formColumn->addStretch(1);
    scroll->setWidget(form);
    column->addWidget(scroll, 1);

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
    for (const auto &[file, toolTip] : align) {
        r.alignButtons.append(symbolButton(file, toolTip.left(1), toolTip, bar));
        row->addWidget(r.alignButtons.last());
    }
    row->addStretch(1);
    column->addWidget(bar);
    r.dock = dock(QStringLiteral("Eigenschaften"), QStringLiteral("propertiesDock"), content, parent);
    r.scroll = scroll;
    nameWidgets({{r.name, "propertyName"}, {r.x, "propertyX"}, {r.y, "propertyY"},
                 {r.width, "propertyWidth"}, {r.height, "propertyHeight"},
                 {r.rotation, "propertyRotation"}, {r.kindPages, "propertyPages"},
                 {r.flipHorizontal, "propertyFlipHorizontal"},
                 {r.flipVertical, "propertyFlipVertical"},
                 {r.replaceImage, "imageReplace"}, {r.loop, "videoLoop"},
                 {r.holdAtEnd, "videoHoldAtEnd"},
                 {r.content, "textContent"}, {r.family, "textFamily"}, {r.weight, "textWeight"},
                 {r.italic, "textItalic"}, {r.fontSize, "textSize"}, {r.align, "textAlign"}});
    for (QWidget *w : std::initializer_list<QWidget *>{
             r.name, r.x, r.y, r.width, r.height, r.rotation, r.flipHorizontal, r.flipVertical,
             r.replaceImage, r.loop, r.holdAtEnd,
             r.content,
             r.family, r.weight, r.italic, r.fontSize, r.color, r.align})
        w->setEnabled(false);
    return r;
}

ControlDock createControlDock(QWidget *parent)
{
    ControlDock result;
    auto *content = new QWidget;
    auto *grid = new QGridLayout(content);
    grid->setContentsMargins(8, 8, 8, 8);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);

    auto *address = new QLineEdit(content);
    address->setReadOnly(true);
    auto *copy = new QPushButton(QStringLiteral("Kopieren"), content);
    auto *newToken = new QPushButton(QStringLiteral("Neuer Token"), content);
    newToken->setToolTip(QStringLiteral("Neue Adresse der Browserquelle; die alte gilt danach nicht mehr"));
    auto *message = new QLabel(content);
    message->setWordWrap(true);
    message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    // P10c, E-29: Zeile «OBS», Text und Knopf setzt MainWindow::refreshObsSource.
    // Platz für zwei Zeilen von Anfang an: Das Setzen des Textes ändert die Anordnung nicht (Regel 9).
    // Längeres steht im Tooltip.
    auto *obsState = new QLabel(QStringLiteral("–"), content);
    obsState->setWordWrap(true);
    obsState->setFixedHeight(obsState->fontMetrics().lineSpacing() * 2);
    obsState->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto *obsButton = new QPushButton(QStringLiteral("In OBS anlegen …"), content);
    obsButton->setEnabled(false);
    nameWidgets({{obsState, "obsSourceState"}, {obsButton, "obsSourceButton"}});
    result.obsSourceState = obsState;
    result.obsSourceButton = obsButton;
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
    for (QWidget *w : std::initializer_list<QWidget *>{address, copy, newToken, output, outputState,
                                                       preview, previewSmall, snap, gridCheck,
                                                       gridSize, clearGuides})
        w->setEnabled(false);
    nameWidgets({{address, "browserSource"}, {copy, "browserSourceCopy"}, {newToken, "browserSourceNewToken"},
                 {outputState, "outputState"}, {message, "serverMessage"}});
    result.browserSource = address;
    result.copyAddress = copy;
    result.newToken = newToken;
    result.outputState = outputState;
    result.serverMessage = message;

    grid->addWidget(new QLabel(QStringLiteral("Browserquelle"), content), 0, 0);
    grid->addWidget(address, 0, 1, 1, 2);
    auto *addressButtons = new QHBoxLayout;
    addressButtons->setSpacing(8);
    addressButtons->addWidget(copy);
    addressButtons->addWidget(newToken);
    grid->addLayout(addressButtons, 0, 3);
    grid->addWidget(message, 1, 1, 1, 3);
    grid->addWidget(new QLabel(QStringLiteral("OBS"), content), 2, 0);
    // Der Knopf belegt zwei Spalten: Sein längerer Text macht das Dock nicht breiter.
    grid->addWidget(obsState, 2, 1);
    grid->addWidget(obsButton, 2, 2, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("Ausgabe"), content), 3, 0);
    grid->addWidget(output, 3, 1);
    grid->addWidget(outputState, 3, 2, 1, 2);
    grid->addWidget(new QLabel(QStringLiteral("Vorschau"), content), 4, 0);
    grid->addWidget(preview, 4, 1);
    grid->addWidget(previewSmall, 4, 2, 1, 2);
    auto *snapRow = new QHBoxLayout;
    snapRow->setSpacing(8);
    snapRow->addWidget(snap);
    snapRow->addStretch(1);
    snapRow->addWidget(gridCheck);
    grid->addLayout(snapRow, 5, 0, 1, 2);
    grid->addWidget(gridSize, 5, 2);
    grid->addWidget(clearGuides, 5, 3);
    // Auflösung des gewählten Overlays (P09, Entscheid 5), ganze Pixel wie beim Anlegen.
    result.overlayWidth = pixelBox(1920, dss::kOverlayMinSize, content);
    result.overlayWidth->setMaximum(dss::kOverlayMaxWidth);
    result.overlayWidth->setToolTip(QStringLiteral("Breite des Overlays"));
    result.overlayWidth->setAccessibleName(QStringLiteral("Breite des Overlays"));
    result.overlayHeight = pixelBox(1080, dss::kOverlayMinSize, content);
    result.overlayHeight->setMaximum(dss::kOverlayMaxHeight);
    result.overlayHeight->setToolTip(QStringLiteral("Höhe des Overlays"));
    result.overlayHeight->setAccessibleName(QStringLiteral("Höhe des Overlays"));
    result.overlayWidth->setEnabled(false);
    result.overlayHeight->setEnabled(false);
    nameWidgets({{result.overlayWidth, "overlayWidth"}, {result.overlayHeight, "overlayHeight"}});
    grid->addWidget(new QLabel(QStringLiteral("Auflösung"), content), 6, 0);
    grid->addWidget(result.overlayWidth, 6, 1);
    grid->addWidget(result.overlayHeight, 6, 2);

    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(2, 1);
    grid->setRowStretch(7, 1);
    result.dock = dock(QStringLiteral("Steuerung"), QStringLiteral("controlDock"), content, parent);
    return result;
}
