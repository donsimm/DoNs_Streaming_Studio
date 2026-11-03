#include "theme.h"

#include <QRegularExpression>

// Werte aus Yami.obt (OBS Studio, Commit 65c320b46e1af5c7a555161aea6182cdf7a8c524, GPL Version 2),
// Abschnitt @OBSThemeVars. Die Farbtabelle dort:
//   --grey1 #5B6273  --grey2 #4E5566  --grey3 #464B59  --grey4 #3C404D
//   --grey5 #323540  --grey6 #272A33  --grey7 #1D1F26  --grey8 #13141A
//   --blue1 #718CDC  --blue2 #476BD7  --blue3 #284CB8
//   --white1 #FFFFFF --white3 #D2D2D2 --white5 #969696
//   --purple1 #997FDC
//   --green1 #59D966 --green3 #25A231 --yellow2 #E5AF24 --red1 #E85E75 --red3 #C01C37
Theme darkTheme()
{
    Theme t;
    t.fontFamily = QStringLiteral("Open Sans");
    t.fontSize = 10;
    t.fontSizeSmall = 8;
    t.window = QColor(0x1d, 0x1f, 0x26);
    t.base = QColor(0x27, 0x2a, 0x33);
    t.text = QColor(0xff, 0xff, 0xff);
    t.textMuted = QColor(0x96, 0x96, 0x96);
    t.textDisabled = t.textMuted;
    t.textInactive = QColor(255, 254, 255);
    t.primary = QColor(0x28, 0x4c, 0xb8);
    t.primaryLight = QColor(0x47, 0x6b, 0xd7);
    t.primaryLighter = QColor(0x71, 0x8c, 0xdc);
    t.link = t.primaryLight;
    t.border = QColor(0x3c, 0x40, 0x4d);
    t.light = QColor(0x4e, 0x55, 0x66);

    t.input = QColor(0x3c, 0x40, 0x4d);
    t.inputHover = QColor(0x1d, 0x1f, 0x26);
    t.inputBorderHover = QColor(0x5b, 0x62, 0x73);
    t.buttonHover = QColor(0x46, 0x4b, 0x59);
    t.buttonDown = QColor(0x1d, 0x1f, 0x26);
    t.buttonDisabled = QColor(0x27, 0x2a, 0x33);
    t.focusBorder = QColor(0xd2, 0xd2, 0xd2);
    t.dockTitle = QColor(0x32, 0x35, 0x40);
    t.scrollHandle = QColor(0x3c, 0x40, 0x4d);
    t.scrollHover = QColor(0x46, 0x4b, 0x59);
    t.scrollDown = QColor(0x4e, 0x55, 0x66);

    t.stateConnected = QColor(0x25, 0xa2, 0x31);
    t.stateConnecting = QColor(0xe5, 0xaf, 0x24);
    t.stateError = QColor(0xc0, 0x1c, 0x37);
    t.stateOff = QColor(0x5b, 0x62, 0x73);

    t.coreConnected = QColor(0x59, 0xd9, 0x66);
    t.coreDisconnected = QColor(0xe8, 0x5e, 0x75);

    t.preview = QColor(0x13, 0x14, 0x1a);
    t.rulerText = t.textMuted;
    t.checkerLight = QColor(0x38, 0x3c, 0x48);
    t.checkerDark = QColor(0x2b, 0x2e, 0x38);
    t.positionBack = t.window;
    t.graph = QColor(0x32, 0x35, 0x40);
    t.graphCpu = QColor(0x47, 0x6b, 0xd7);
    t.graphRam = QColor(0x99, 0x7f, 0xdc);
    t.graphLan = QColor(0x59, 0xd9, 0x66);
    return t;
}

QPalette palette(const Theme &t)
{
    // Wie --palette_… in Yami.obt.
    QPalette p;
    p.setColor(QPalette::Window, t.window);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.base);
    p.setColor(QPalette::AlternateBase, t.base);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.input);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::Highlight, t.primary);
    p.setColor(QPalette::HighlightedText, t.text);
    p.setColor(QPalette::Link, t.link);
    p.setColor(QPalette::LinkVisited, t.link);
    p.setColor(QPalette::ToolTipBase, t.base);
    p.setColor(QPalette::ToolTipText, t.text);
    p.setColor(QPalette::PlaceholderText, t.textMuted);
    p.setColor(QPalette::Light, t.light);
    p.setColor(QPalette::Mid, t.window);
    p.setColor(QPalette::Dark, t.base);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, t.textDisabled);
    p.setColor(QPalette::Inactive, QPalette::Text, t.textInactive);
    return p;
}

QString styleSheet(const Theme &t, const std::function<QString(const QString &)> &symbolPath)
{
    // Aus Yami.obt übernommen und mit aufgelösten Werten geschrieben. OBS rechnet die
    // Variablen beim Laden aus; hier gelten die Standardwerte obsFontScale = 10 und
    // obsPadding = 4 (Dichte 1). Damit (Yami.obt, @OBSThemeVars):
    //   --padding_base 2px, --padding_large 4px, --padding_xlarge 7px, --padding_small 1px,
    //   --padding_wide 16px, --padding_menu 12px, --padding_menubar 7px,
    //   --spacing_base 1.6px (hier 2px, Qt nimmt ganze Pixel), --spacing_large 4px,
    //   --spacing_small 1px, --input_padding 4px, --input_text_padding 8px,
    //   --input_height 22px, --icon_base 16px, --icon_small 14px,
    //   --border_radius 4px, --border_radius_small 2px, --scrollbar_size 12px.
    // Namen von OBS-eigenen Klassen sind durch unsere ersetzt: OBSDock → QDockWidget,
    // OBSDock QToolBar → #dockButtons, OBSBasicStatusBar → QStatusBar.
    // Platzhalter @name@ werden durch die Farben ersetzt, @symbols@/<Datei> durch den Pfad aus symbolPath.
    QString css = QStringLiteral(R"(
QWidget { alternate-background-color: @base@; color: @text@;
          selection-background-color: @primary@; selection-color: @text@;
          font-size: @fontSize@pt; font-family: '@fontFamily@', Helvetica, Arial, sans-serif; }
QWidget:disabled { color: @textDisabled@; }

QDialog, QMainWindow, QMenuBar, QMenu { background-color: @window@; }
QMainWindow::separator { background: transparent; width: 4px; height: 4px; margin: 0px; }
QMainWindow::separator:hover { border: 1px solid @text@; margin: 1px; }

QLabel, QCheckBox { background: transparent; }
QComboBox, QCheckBox, QPushButton, QSpinBox, QDoubleSpinBox { margin-top: 2px; margin-bottom: 2px; }
QListView QWidget, QListWidget QWidget { margin-top: 0; margin-bottom: 0; }
* [frameShape="1"], * [frameShape="2"], * [frameShape="3"], * [frameShape="4"],
* [frameShape="5"], * [frameShape="6"] { border: 1px solid @base@; }
QCheckBox, QMenuBar { spacing: 1px; }
QAbstractItemView { background-color: @base@; }
QToolTip { background-color: @base@; color: @text@; border: none; }

QMenu::separator { background: @border@; height: 1px; margin: 2px 4px; }
QMenu::item:disabled { color: @textDisabled@; background: transparent; }
QMenuBar::item { background-color: transparent; padding: 4px 7px; }
QMenuBar::item:selected { background: @primary@; }
QMenuBar::item:disabled { color: @textDisabled@; }

QListWidget { border-radius: 4px; }
QListWidget::item { color: @text@; }
QListView, QListWidget, QMenu { padding: 2px; outline: none; }
QListWidget::item { padding: 4px 4px; }
QMenu::item { padding: 4px 12px; padding-right: 20px; }
QMenu::item, QListView::item, QListWidget::item { border-radius: 4px; color: @text@;
                                                  border: 1px solid transparent; }
QMenu { border: 1px solid @border@; padding: 2px; }
QListView::item, QListWidget::item { min-height: 12px; }
QMenu::item:selected, QListView::item:selected, QListWidget::item:selected {
    background-color: @primary@; border-color: @primaryLight@; }
QMenu::item:hover, QListView::item:hover, QListWidget::item:hover {
    background: @input@; border: 1px solid @buttonHover@; }
QListView::item:focus, QListWidget::item:focus, QListView::item:selected:focus,
QListWidget::item:selected:focus { border: 1px solid @focusBorder@; }
QMenu::item:selected:hover, QListView::item:selected:hover, QListWidget::item:selected:hover {
    background: @primaryLight@; border: 1px solid @primaryLighter@; color: @text@; }
QListView::item:disabled, QListWidget::item:disabled { background: transparent;
                                                       color: @textDisabled@; }

QDockWidget > QWidget { background: @base@; border-bottom-left-radius: 4px;
                        border-bottom-right-radius: 4px; border: 1px solid @border@;
                        border-top: none; }
#dockButtons { padding: 1px; margin: 0px; min-height: 30px; max-height: 30px;
               border: none; border-top: 1px solid @border@; border-radius: 0px; }
#dockButtons QToolButton { background: transparent; border: 1px solid transparent; margin: 1px;
                           margin-top: 2px; padding: 4px; max-width: 14px; max-height: 14px; }
#dockButtons QToolButton:hover { border-color: @inputBorderHover@; }
#dockButtons QToolButton:disabled { background: @window@; }
QDockWidget { font-size: @fontSizeSmall@pt; font-weight: bold; color: @text@;
              titlebar-close-icon: url("@symbols@/dock-schliessen.svg");
              titlebar-normal-icon: url("@symbols@/dock-loesen.svg"); }
QDockWidget::title { text-align: left; background-color: @dockTitle@; border: 1px solid @border@;
                     padding: 2px 4px; border-top-left-radius: 4px; border-top-right-radius: 4px; }
QDockWidget::close-button, QDockWidget::float-button { border: none; border-radius: 4px;
    background: transparent; margin-right: 1px; min-width: 16px; min-height: 16px;
    icon-size: 16px; padding: 2px; }
QDockWidget::close-button:hover, QDockWidget::float-button:hover { background: @buttonHover@; }
QDockWidget::close-button:pressed, QDockWidget::float-button:pressed {
    background: @buttonDown@; padding: 1px -1px -1px 1px; }
QAbstractScrollArea { border-radius: 4px; }

QStatusBar { margin-top: 4px; border-top: 1px solid @border@; background: @base@; }
QStatusBar::item { border: none; }

QScrollBar { background-color: @base@; margin: 0px; border-radius: 4px; border: 1px solid @base@; }
::corner { background-color: @window@; border: none; }
QScrollBar:vertical { width: 12px; }
QScrollBar:horizontal { height: 12px; }
QScrollBar::add-line, QScrollBar::sub-line { border: none; background: none; height: 0px; width: 0px; }
QScrollBar::up-arrow, QScrollBar::down-arrow, QScrollBar::left-arrow, QScrollBar::right-arrow,
QScrollBar::add-page, QScrollBar::sub-page { border: none; background: none; color: none; }
QScrollBar::handle { background-color: @scrollHandle@; margin: 2px; border-radius: 2px;
                     border: 1px solid @scrollHandle@; }
QScrollBar::handle:hover { background-color: @scrollHover@; border-color: @scrollDown@; }
QScrollBar::handle:pressed { background-color: @scrollDown@; border-color: @scrollDown@; }
QScrollBar::handle:vertical { min-height: 32px; }
QScrollBar::handle:horizontal { min-width: 32px; }
QScrollBar::handle:disabled { background: transparent; border-color: transparent; }
QDockWidget QScrollBar { border-radius: 0; }
QDockWidget QScrollBar:horizontal { border-top: 1px solid @border@; }
QDockWidget QScrollBar:vertical { border-left: 1px solid @border@; }

QComboBox { background-color: @input@; border: 1px solid @input@; border-radius: 4px;
            padding: 4px 8px; height: 22px; }
QComboBox QAbstractItemView { padding: 2px 4px; border: 1px solid @border@; outline: none; }
QComboBox QAbstractItemView::item { padding: 4px 7px; }
QComboBox QAbstractItemView::item:selected, QComboBox QAbstractItemView::item:hover {
    background-color: @primary@; }
QComboBox:hover, QComboBox:focus { border-color: @inputBorderHover@; }
QComboBox:on { border-bottom-left-radius: 0px; border-bottom-right-radius: 0px;
               background-color: @inputHover@; border-color: @primary@; }
QComboBox::drop-down { border: none; border-left: 1px solid @base@; width: 22px; }
QComboBox::down-arrow { image: url("@symbols@/auswahl-pfeil.svg"); }

QLineEdit { background-color: @input@; border-radius: 4px; padding: 4px 8px;
            border: 1px solid @input@; height: 22px; }
QLineEdit:hover { background-color: @inputHover@; border-color: @inputBorderHover@; }
QLineEdit:focus { background-color: @inputHover@; border-color: @primary@; }
QLineEdit:read-only, QLineEdit:read-only:hover, QLineEdit:read-only:focus {
    background-color: transparent; border-color: @input@; }

/* Abweichung von Yami: ohne min-width (--spinbox_min_width 55px). Qt setzt damit eine feste
   Mindestbreite, die den sizeHint des Feldes übersteuert; bei wenig Platz wurde der Wert neben
   den Pfeilknöpfen abgeschnitten (P06, «20 px» in «Steuerung» bei 1280x800). */
QSpinBox, QDoubleSpinBox { background-color: @input@; border: 1px solid @input@;
    border-radius: 4px; padding: 4px 8px; padding-right: 2px; height: 22px; max-height: 22px; }
QSpinBox:hover, QDoubleSpinBox:hover { background-color: @inputHover@;
                                       border-color: @inputBorderHover@; }
QSpinBox:focus, QDoubleSpinBox:focus { background-color: @inputHover@; border-color: @primary@; }
/* Pfeilknöpfe der Zahlenfelder (Punkt 30): Breite --input_height 22px, Höhe
   --spinbox_button_height = --input_height_half − 1px = 15px − 1px = 14px. */
QSpinBox::up-button, QDoubleSpinBox::up-button { subcontrol-origin: padding;
    subcontrol-position: top right; width: 22px; height: 14px; border-left: 1px solid @base@;
    border-bottom: 1px solid transparent; border-radius: 0px; border-top-right-radius: 2px; }
QSpinBox::down-button, QDoubleSpinBox::down-button { subcontrol-origin: padding;
    subcontrol-position: bottom right; width: 22px; height: 14px; border-left: 1px solid @base@;
    border-top: 1px solid @base@; border-radius: 0px; border-bottom-right-radius: 2px; }
QSpinBox::up-button:hover, QSpinBox::down-button:hover, QDoubleSpinBox::up-button:hover,
QDoubleSpinBox::down-button:hover { background-color: @buttonHover@; }
QSpinBox::up-button:pressed, QSpinBox::down-button:pressed, QDoubleSpinBox::up-button:pressed,
QDoubleSpinBox::down-button:pressed { background-color: @buttonDown@; }
QSpinBox::up-button:disabled, QSpinBox::up-button:off, QSpinBox::down-button:disabled,
QSpinBox::down-button:off, QDoubleSpinBox::up-button:disabled, QDoubleSpinBox::up-button:off,
QDoubleSpinBox::down-button:disabled, QDoubleSpinBox::down-button:off {
    background-color: @buttonDisabled@; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow { image: url("@symbols@/zahlenfeld-hoch.svg");
                                               margin: 2px; }
QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: url("@symbols@/zahlenfeld-runter.svg");
                                                   padding: 2px; }

QPushButton { background-color: @input@; color: @text@; border: 1px solid @input@;
    border-radius: 4px; height: 22px; max-height: 22px; margin-top: 2px; margin-bottom: 2px;
    padding: 4px 16px; icon-size: 16px; outline: none; }
QPushButton:hover { background-color: @buttonHover@; }
QPushButton:hover, QPushButton:focus { border-color: @inputBorderHover@; }
QPushButton:checked { background-color: @primary@; border-color: @primaryLight@; }
QPushButton:checked:hover, QPushButton:checked:focus { border-color: @primaryLighter@; }
QPushButton:pressed, QPushButton:pressed:hover { background-color: @buttonDown@;
                                                 border-color: @input@; }
QPushButton:disabled { background-color: @buttonDisabled@; border-color: @input@; }

QToolButton { background-color: @input@; padding: 2px 2px; margin: 0px 0px;
              border: 1px solid @input@; border-radius: 4px; icon-size: 16px; }
QToolButton:hover, QToolButton:focus { border-color: @input@; background-color: @buttonHover@; }
QToolButton:checked { background-color: @primary@; border-color: @primaryLight@; }
QToolButton:checked:hover, QToolButton:checked:focus { border-color: @primaryLighter@; }
QToolButton:pressed, QToolButton:pressed:hover { background-color: @buttonDown@;
                                                 border-color: @input@; }
QToolButton:disabled { background-color: @buttonDisabled@; border-color: transparent; }

QCheckBox::indicator { width: 16px; height: 16px; margin-right: 4px; }
QCheckBox::indicator:unchecked { image: url("@symbols@/kaestchen-aus.svg"); }
QCheckBox::indicator:unchecked:hover { image: url("@symbols@/kaestchen-aus-fokus.svg"); }
QCheckBox::indicator:checked { image: url("@symbols@/kaestchen-an.svg"); }
QCheckBox::indicator:checked:hover { image: url("@symbols@/kaestchen-an-fokus.svg"); }
QCheckBox::indicator:checked:disabled { image: url("@symbols@/kaestchen-an-ausgegraut.svg"); }
QCheckBox::indicator:unchecked:disabled { image: url("@symbols@/kaestchen-aus-ausgegraut.svg"); }

/* Eigene Bereiche (in OBS nicht vorhanden), mit den Werten von Yami gestaltet. */
#headerBar { background: @base@; border: 1px solid @border@; border-radius: 4px; }
#moduleTabs { background: transparent; border: none; }
/* Modul-Reiter wie QTabBar::tab in Yami.obt (--tab_bg = --button_bg_disabled). */
#moduleTabs QToolButton { background: @buttonDisabled@; color: @textMuted@;
    border: 1px solid @buttonDisabled@; padding: 4px 16px; min-width: 50px; border-radius: 4px; }
#moduleTabs QToolButton:hover { background: @buttonHover@; border-color: @inputBorderHover@;
                                color: @text@; }
#moduleTabs QToolButton:checked { background: @primary@; color: @text@;
                                  border: 1px solid @primaryLight@; }

QToolButton#connectionButton { border: 2px solid @stateOff@; border-radius: 4px;
                               background: @input@; color: @text@; font-weight: bold; }
QToolButton#connectionButton:hover { background: @buttonHover@; }
/* Zustand im Rahmen (Werte von «state»: src/common/connectionstate.cpp). */
QToolButton#connectionButton[state="off"] { border-color: @stateOff@; }
QToolButton#connectionButton[state="error"] { border-color: @stateError@; }
QToolButton#connectionButton[state="connecting"] { border-color: @stateConnecting@; }
QToolButton#connectionButton[state="connected"] { border-color: @stateConnected@; }

#gaugeTitle { color: @textMuted@; }
#gaugeValue { color: @text@; font-weight: bold; }
#gaugeGraph { background: @base@; border: 1px solid @graph@; }

#chatBotPage { background: @preview@; }
#chatBotPage QLabel { color: @textMuted@; }

/* P08: Baum der Overlays wie die Listen, mehrzeiliges Textfeld wie QLineEdit. */
/* P09, Punkt 48: Den Einzug links vom gewählten Eintrag füllt OverlayTree::drawBranches
   (dockviews.cpp); show-decoration-selected allein reicht mit Stylesheet nicht. */
QTreeView { border-radius: 4px; padding: 2px; outline: none; show-decoration-selected: 0; }
QTreeView::item { border-radius: 4px; border: 1px solid transparent; padding: 3px 2px; color: @text@; }
QTreeView::item:hover { background: @input@; border: 1px solid @buttonHover@; }
QTreeView::item:selected { background-color: @primary@; border-color: @primaryLight@; }
QTreeView::item:selected:hover { background: @primaryLight@; border: 1px solid @primaryLighter@; }
QPlainTextEdit { background-color: @input@; border-radius: 4px; padding: 2px 4px;
                 border: 1px solid @input@; }
QPlainTextEdit:hover { background-color: @inputHover@; border-color: @inputBorderHover@; }
QPlainTextEdit:focus { background-color: @inputHover@; border-color: @primary@; }
#propertiesScroll, #propertiesForm { background: transparent; border: none; }
#dockButtons QToolButton::menu-indicator { image: none; width: 0px; }

#hintLabel { color: @textMuted@; }
#coreStatus { font-weight: bold; padding: 0 8px; }
#coreStatus[state="disconnected"] { color: @coreDisconnected@; }
#coreStatus[state="connected"] { color: @coreConnected@; }
)");
    const QList<std::pair<const char *, QColor>> colors = {
        {"window", t.window},
        {"base", t.base},
        {"text", t.text},
        {"textMuted", t.textMuted},
        {"textDisabled", t.textDisabled},
        {"primary", t.primary},
        {"primaryLight", t.primaryLight},
        {"primaryLighter", t.primaryLighter},
        {"border", t.border},
        {"input", t.input},
        {"inputHover", t.inputHover},
        {"inputBorderHover", t.inputBorderHover},
        {"buttonHover", t.buttonHover},
        {"buttonDown", t.buttonDown},
        {"buttonDisabled", t.buttonDisabled},
        {"focusBorder", t.focusBorder},
        {"dockTitle", t.dockTitle},
        {"scrollHandle", t.scrollHandle},
        {"scrollHover", t.scrollHover},
        {"scrollDown", t.scrollDown},
        {"stateConnected", t.stateConnected},
        {"stateConnecting", t.stateConnecting},
        {"stateError", t.stateError},
        {"stateOff", t.stateOff},
        {"coreConnected", t.coreConnected},
        {"coreDisconnected", t.coreDisconnected},
        {"preview", t.preview},
        {"graph", t.graph},
    };
    for (const auto &[name, color] : colors)
        css.replace(QLatin1Char('@') + QLatin1String(name) + QLatin1Char('@'), color.name());
    static const QRegularExpression symbolPattern(QStringLiteral("@symbols@/([A-Za-z0-9._-]+)"));
    QRegularExpressionMatch m;
    while ((m = symbolPattern.match(css)).hasMatch())
        css.replace(m.capturedStart(), m.capturedLength(), symbolPath(m.captured(1)));
    css.replace(QStringLiteral("@fontFamily@"), t.fontFamily);
    css.replace(QStringLiteral("@fontSize@"), QString::number(t.fontSize));
    css.replace(QStringLiteral("@fontSizeSmall@"), QString::number(t.fontSizeSmall));
    return css;
}
