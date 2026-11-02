#pragma once

#include <QColor>
#include <QPalette>
#include <QString>

// Farben des Fensters an einer Stelle (Auftrag P05; später auch das helle Design, E-16).
// Kein anderer Teil des Fensters schreibt eine Farbe als Wert hin.
//
// Seit P06 nach dem Design «Yami» (Variante «Default») von OBS Studio:
// https://github.com/obsproject/obs-studio, Datei frontend/data/themes/Yami.obt,
// Commit 65c320b46e1af5c7a555161aea6182cdf7a8c524. Lizenz von OBS Studio: GNU GPL Version 2
// (Datei COPYING im Repo von OBS Studio). In den Kommentaren steht die Variable aus Yami.obt.

struct Theme
{
    // Schrift: Yami.obt nennt 'Open Sans' zuerst; --font_base = 1pt × obsFontScale, Standard
    // von obsFontScale ist 10 (OBS Studio, frontend/OBSApp.cpp, config_set_default_int(…,
    // "Appearance", "FontScale", 10)); --font_small = max(7pt, 0.8pt × 10) = 8pt.
    QString fontFamily;     // Open Sans
    int fontSize = 10;      // --font_base in Punkt
    int fontSizeSmall = 8;  // --font_small in Punkt (Titel der Docks)

    // Grundfarben
    QColor window;          // --bg_window (--grey7): Fenster, Menüleiste, Statuszeile
    QColor base;            // --bg_base (--grey6): Kopfbereich, Docks, Listen
    QColor text;            // --text (--white1)
    QColor textMuted;       // --text_muted (--white5): Lineale, Hinweise, Titel der Anzeigen
    QColor textDisabled;    // --text_disabled (= --text_muted)
    QColor textInactive;    // --text_inactive: Text in einem nicht aktiven Fenster
    QColor primary;         // --primary (--blue3): Auswahl, gewählter Reiter
    QColor primaryLight;    // --primary_light (--blue2)
    QColor primaryLighter;  // --primary_lighter (--blue1)
    QColor link;            // --palette_link (--blue2)
    QColor border;          // --border_color (--grey4): Rahmen und Trennlinien
    QColor light;           // --palette_light (--grey2)

    // Bedienelemente
    QColor input;           // --input_bg (--grey4), auch --button_bg und --button_border
    QColor inputHover;      // --input_bg_hover, --input_bg_focus (--grey7)
    QColor inputBorderHover;// --input_border_hover (--grey1)
    QColor buttonHover;     // --button_bg_hover (--grey3)
    QColor buttonDown;      // --button_bg_down (--grey7)
    QColor buttonDisabled;  // --button_bg_disabled (--grey6)
    QColor focusBorder;     // Rahmen eines Listeneintrags mit Fokus (--white3)
    QColor dockTitle;       // Hintergrund der Dock-Titel (--grey5)
    QColor scrollHandle;    // --scrollbar_handle (--grey4)
    QColor scrollHover;     // --scrollbar_hover (--grey3)
    QColor scrollDown;      // --scrollbar_down, --scrollbar_border (--grey2)

    // Zustände der Verbindungsknöpfe (docs/01-festlegungen.md, «Verbindungsanzeige im Kopfbereich»)
    QColor stateConnected;  // grün, --green3
    QColor stateConnecting; // orange, --yellow2
    QColor stateError;      // rot, --red3
    QColor stateOff;        // grau, --grey1

    // Schrift «Kern-Dienst: …» in der Statuszeile
    QColor coreConnected;   // --green1
    QColor coreDisconnected;// --red1

    // Leinwand
    QColor preview;         // --bg_preview (--grey8): Umgebung der Leinwand und Lineale
    QColor rulerText;       // Striche und Zahlen der Lineale (--text_muted)
    QColor checkerLight;    // Schachbrettmuster für Transparenz (Werte aus P05)
    QColor checkerDark;
    QColor positionBack;    // Hintergrund der Anzeige «X: … Y: …» (--bg_window)
    QColor graph;           // Rahmen der Verlaufsfläche der Anzeigen CPU, RAM, LAN (--grey5)
};

// Dunkles Design nach «Yami» (OBS Studio).
Theme darkTheme();

// Palette für den Stil «Fusion», für alles, was das Stylesheet nicht abdeckt (zum Beispiel
// Dialoge). Belegung wie die Variablen --palette_… in Yami.obt.
// https://doc.qt.io/qt-6.8/qpalette.html
QPalette palette(const Theme &theme);

// Stylesheet für die ganze Anwendung aus den Farben des Themes. symbolDir ist der Ordner
// mit den Bildern für Auswahlliste, Kontrollkästchen, Dock-Knöpfe und Zahlenfelder.
// https://doc.qt.io/qt-6.8/stylesheet-reference.html
QString styleSheet(const Theme &theme, const QString &symbolDir);
