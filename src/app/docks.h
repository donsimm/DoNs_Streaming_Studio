#pragma once

#include <QList>

class OverlayTree;
class QAction;
class QCheckBox;
class QComboBox;
class QDockWidget;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QScrollArea;
class QSpinBox;
class QStackedWidget;
class QToolButton;
class QWidget;
class SourceList;
class MediaTree;

// Die vier Docks unten, wie auf docs/bilder/oberflaeche.png. Jedes Dock hat einen festen
// objectName, damit QMainWindow::saveState() die Anordnung speichern kann. Seit P07 zeigen
// «Overlays», «Quellen» und «Eigenschaften» Daten vom Kern, seit P08 lassen sie sich bearbeiten
// (Schalter DSS_APP_EDITING). Hier wird nur aufgebaut; was die Knöpfe tun, verbindet MainWindow.
// Alle Knöpfe und Felder beginnen ausgegraut.

struct OverlaysDock
{
    QDockWidget *dock = nullptr;
    OverlayTree *tree = nullptr;
    QToolButton *add = nullptr;
    QToolButton *remove = nullptr;
    QToolButton *copy = nullptr;
};

struct SourcesDock
{
    QDockWidget *dock = nullptr;
    SourceList *list = nullptr;
    QToolButton *add = nullptr; // Menü: Bild, Video, Text
    QAction *addImage = nullptr;
    QAction *addVideo = nullptr;
    QAction *addText = nullptr;
    QToolButton *remove = nullptr;
    QToolButton *forward = nullptr;
    QToolButton *backward = nullptr;
};

struct PropertiesDock
{
    QDockWidget *dock = nullptr;
    QScrollArea *scroll = nullptr;
    // Für alle Arten: oben der Name, dann der Bereich «Transformation» (P09b, E-26)
    QLineEdit *name = nullptr;
    QSpinBox *x = nullptr;
    QSpinBox *y = nullptr;
    QSpinBox *width = nullptr;
    QSpinBox *height = nullptr;
    QDoubleSpinBox *rotation = nullptr;
    QCheckBox *flipHorizontal = nullptr; // P09b
    QCheckBox *flipVertical = nullptr;
    // P10a: Knöpfe in der Titelzeile des Bereichs «Transformation»
    QToolButton *copyTransform = nullptr;
    QToolButton *pasteTransform = nullptr;
    QToolButton *resetTransform = nullptr;
    QLabel *hint = nullptr;
    // Seite je Art mit eigenem Bereich («Bild», «Video», «Text»): kEmptyPage, kImagePage,
    // kVideoPage, kTextPage
    QStackedWidget *kindPages = nullptr;
    // Bild
    QPushButton *replaceImage = nullptr;
    // Video
    QCheckBox *loop = nullptr;
    QCheckBox *holdAtEnd = nullptr; // P09: am Ende anhalten, ausgegraut mit «Wiederholen»
    // Text
    QPlainTextEdit *content = nullptr;
    QComboBox *family = nullptr;
    QComboBox *weight = nullptr;
    QCheckBox *italic = nullptr;
    QSpinBox *fontSize = nullptr;
    QToolButton *color = nullptr;
    QComboBox *align = nullptr; // Daten: dss::TextAlign als int
    // Ausrichten auf der Leinwand: bleibt in P08 ausgegraut.
    QList<QToolButton *> alignButtons;

    static constexpr int kEmptyPage = 0;
    static constexpr int kImagePage = 1;
    static constexpr int kVideoPage = 2;
    static constexpr int kTextPage = 3;
};

// Dock «Medien» (P10b, E-28, Schalter DSS_APP_MEDIA_DOCK): alle Dateien im Datenordner mit Name,
// Typ, Grösse, in Ordnern. Unter der Liste eine Zeile zur Auswahl («verwendet in …»), darunter die
// Knöpfe Hinzufügen, Ordner anlegen, Löschen (bei einer laufenden Übernahme: Abbrechen).
struct MediaDock
{
    QDockWidget *dock = nullptr;
    MediaTree *tree = nullptr;
    QLabel *info = nullptr;
    QToolButton *add = nullptr;
    QToolButton *addFolder = nullptr;
    QToolButton *remove = nullptr;
};

OverlaysDock createOverlaysDock(QWidget *parent);
MediaDock createMediaDock(QWidget *parent);
SourcesDock createSourcesDock(QWidget *parent);
PropertiesDock createPropertiesDock(QWidget *parent);
// Dock «Steuerung». Seit P09 lässt sich die Auflösung des gewählten Overlays ändern
// (Entscheid 5); die übrigen Elemente bleiben ausgegraut bis P10.
struct ControlDock
{
    QDockWidget *dock = nullptr;
    // P10, Teil C: Adresse der Browserquelle des gewählten Overlays (mit Token)
    QLineEdit *browserSource = nullptr;
    QPushButton *copyAddress = nullptr;
    QPushButton *newToken = nullptr;
    QLabel *outputState = nullptr;
    // Meldungen des Overlay-Servers (Port belegt, Adresse fehlt, ungültige Adresse benutzt);
    // immer da, leer ohne Meldung (keine Änderung der Anordnung).
    QLabel *serverMessage = nullptr;
    QSpinBox *overlayWidth = nullptr;
    QSpinBox *overlayHeight = nullptr;
};

ControlDock createControlDock(QWidget *parent);
