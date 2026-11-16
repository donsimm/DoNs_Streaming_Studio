#pragma once

#include <QList>
#include <QString>

class QAbstractSpinBox;
class QGridLayout;
class QToolButton;
class QWidget;

// Gemeinsame Bausteine der Docks und der Bereiche wie ein Dock (P14 e2-10, aus docks.cpp und
// ChatBotPage::addLivePanel herausgelöst). Farben und Schriften nur über die Objektnamen im
// Stylesheet (theme.cpp).

// Ausgegrauter Symbolknopf (14 px); ohne Symboldatei steht der Ersatztext da.
QToolButton *symbolButton(const QString &symbolFile, const QString &fallback,
                          const QString &toolTip, QWidget *parent);

// Zahlenfeld nie schmaler als sein sizeHint (längster Wert mit Einheit), damit der Wert
// neben den Pfeilknöpfen ganz sichtbar bleibt (P06). Ändert nur die Mindestbreite.
void keepValueVisible(QAbstractSpinBox *spin);

// Bereich wie im Dock «Eigenschaften»: kleiner Titel (#propertySectionTitle), feine Linie
// (#propertySectionLine), darunter die Felder im Raster *grid. Knöpfe in titleButtons stehen
// in der Titelzeile am rechten Rand.
QWidget *section(const QString &title, QWidget *parent, QGridLayout **grid,
                 const QList<QToolButton *> &titleButtons = {});

// Knopfleiste unter einer Liste (#dockButtons): left linksbündig, right nach einer Dehnung
// rechtsbündig.
QWidget *buttonBar(const QList<QWidget *> &left, const QList<QWidget *> &right, QWidget *parent);

// Liste mit einer Knopfleiste (buttonBar) darunter.
QWidget *listWithButtons(const QList<QToolButton *> &buttons, QWidget *list, QWidget *parent);

// Bereich wie ein Dock: Titel (#chatBotPanelTitle) wie QDockWidget::title, darunter der Inhalt
// (#chatBotPanelBody), fest ohne Lösen und Schliessen.
QWidget *dockPanel(const QString &title, QWidget *content, QWidget *parent);
