#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

// Farben des Fensters an einer Stelle (Auftrag P05; später auch das helle Design, E-16).
// Kein anderer Teil des Fensters schreibt eine Farbe als Wert hin.

struct Theme
{
    QColor window;        // Menüleiste, Eingabefelder, Hintergrund zwischen den Bereichen
    QColor panel;         // Kopfbereich, Docks, Statuszeile
    QColor border;        // Rahmen und Trennlinien
    QColor segment;       // Hintergrund der Modul-Reiter
    QColor text;          // normale Schrift
    QColor textMuted;     // Beschriftungen, Lineal
    QColor textDisabled;  // ausgegraute Elemente
    QColor accent;        // gewählter Modul-Reiter, Häkchen
    QColor selection;     // gewählter Listeneintrag
    QColor good;          // verbunden (Rahmen, Schrift)
    QColor goodBack;      // verbunden (Hintergrund)
    QColor bad;           // nicht verbunden (Rahmen, Schrift)
    QColor badBack;       // nicht verbunden (Hintergrund)
    QColor graph;         // Verlaufsfläche der Anzeigen CPU, RAM, LAN
    QColor canvasOutside; // dunkler Rand ausserhalb der Leinwand
    QColor checkerLight;  // Schachbrettmuster für Transparenz
    QColor checkerDark;
    QColor ruler;         // Hintergrund der Lineale
    QColor rulerText;     // Striche und Zahlen der Lineale
    QColor positionBack;  // Hintergrund der Anzeige «X: … Y: …»
};

// Dunkles Design, Werte abgelesen aus docs/bilder/oberflaeche.png.
Theme darkTheme();

// Palette für den Stil «Fusion», für alles, was das Stylesheet nicht abdeckt (zum Beispiel
// Dialoge). https://doc.qt.io/qt-6.8/qpalette.html
QPalette palette(const Theme &theme);

// Stylesheet für die ganze Anwendung aus den Farben des Themes.
// https://doc.qt.io/qt-6.8/stylesheet-reference.html
QString styleSheet(const Theme &theme);
