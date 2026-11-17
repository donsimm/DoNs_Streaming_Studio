#pragma once

#include <QDoubleSpinBox>
#include <QList>
#include <QString>
#include <QStyledItemDelegate>

class QAbstractSpinBox;
class QGridLayout;
class QLabel;
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

// Zahl mit Komma und ohne Nullen am Ende, «8,5 s» statt «8,500 s» (Entscheid des Nutzers,
// 2026-11-16; Komma wie durationText und mediaDurationText). Die Nachkommastellen bleiben
// eingebbar. P14 e2-9b in «Aktionen» rechts, seit e3-6 auch für die Abstände der Zeitnachrichten.
// QDoubleSpinBox::textFromValue: https://doc.qt.io/qt-6/qdoublespinbox.html#textFromValue
class FieldSpinBox : public QDoubleSpinBox
{
public:
    explicit FieldSpinBox(QWidget *parent = nullptr);
    QString textFromValue(double value) const override;
};

// Bereich wie im Dock «Eigenschaften»: kleiner Titel (#propertySectionTitle), feine Linie
// (#propertySectionLine), darunter die Felder im Raster *grid. Knöpfe in titleButtons stehen
// in der Titelzeile am rechten Rand.
QWidget *section(const QString &title, QWidget *parent, QGridLayout **grid,
                 const QList<QToolButton *> &titleButtons = {});

// Beschriftung eines Felds im Raster von section(), mit dem Feld als Buddy (P14 e3-5, seit e3-6
// geteilt für die Formulare rechts in «Befehle» und «Zeitnachrichten»).
QLabel *fieldLabel(const QString &text, QWidget *buddy, QWidget *parent);

// Knopfleiste unter einer Liste (#dockButtons): left linksbündig, right nach einer Dehnung
// rechtsbündig. Ist stretchWidget gesetzt (ein Element aus left), nimmt es den freien Platz
// statt der Dehnung.
QWidget *buttonBar(const QList<QWidget *> &left, const QList<QWidget *> &right, QWidget *parent,
                   QWidget *stretchWidget = nullptr);

// Liste mit einer Knopfleiste (buttonBar) darunter.
QWidget *listWithButtons(const QList<QToolButton *> &buttons, QWidget *list, QWidget *parent);

// Prüfmeldung unter einem Abschnitt (#fieldProblem, stateError): Zeile mit reservierter Höhe, auch
// ohne Meldung. P14 e2-15 für die Mitte, seit e2-16 auch für die rechte Spalte.
QLabel *problemLine(QWidget *parent);

// Bereich wie ein Dock: Titel (#chatBotPanelTitle) wie QDockWidget::title, darunter der Inhalt
// (#chatBotPanelBody), fest ohne Lösen und Schliessen.
QWidget *dockPanel(const QString &title, QWidget *content, QWidget *parent);

// Textfarbe einer Zelle aus Qt::ForegroundRole (Theme-Wert, vom Fenster hereingereicht): Das
// Stylesheet setzt QTreeView::item { color }, und QStyleSheetStyle überschreibt damit die
// Textfarbe aus der Rolle (im Bild geprüft, tst_fensterchatbot). Darum zeichnet der Delegat
// Hintergrund, Rahmen und Kästchen wie die Zeile und den Text selbst in der Farbe der Rolle, mit
// der Schrift aus Qt::FontRole. Gewählte Zeile wie alle (Farbe auf @primary@ wäre ein neues Paar im
// Theme). P14 e2-12 für «Läufe der Aktionen», seit e2-15 auch für die Schritte.
class ForegroundDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};

// Spalte 0 eines Baums ohne Kopfzeile so breit wie der längste Eintrag, nichts gekürzt (Entscheid
// 16.11.): Mit dem Stylesheet ist die Breite aus QStyledItemDelegate::sizeHint bis 1 px kleiner als
// die Textfläche, die der Stil beim Zeichnen nimmt (im Handtest «Chat send…», tst_fensterchatbot).
// Darum misst sizeHint den Text in der Schrift der Zeile (auch fett) gegen SE_ItemViewItemText und
// legt den Rest dazu. Einzug und Kästchen rechnet QTreeView bzw. der Stil; ResizeToContents
// übernimmt die Breite bei neuen und umbenannten Einträgen.
class NameColumnDelegate : public ForegroundDelegate
{
public:
    using ForegroundDelegate::ForegroundDelegate;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
