#include "theme.h"

Theme darkTheme()
{
    Theme t;
    t.window = QColor(0x1d, 0x1f, 0x26);
    t.panel = QColor(0x27, 0x2a, 0x33);
    t.border = QColor(0x3c, 0x40, 0x4d);
    t.segment = QColor(0x0b, 0x0b, 0x0c);
    t.text = QColor(0xe6, 0xe6, 0xe6);
    t.textMuted = QColor(0xb4, 0xb8, 0xc0);
    t.textDisabled = QColor(0x6b, 0x6e, 0x78);
    t.accent = QColor(0x4c, 0x8d, 0xff);
    t.selection = QColor(0x28, 0x4c, 0xb8);
    t.good = QColor(0x5f, 0xd3, 0x8d);
    t.goodBack = QColor(0x31, 0x48, 0x43);
    t.bad = QColor(0xff, 0x7b, 0x72);
    t.badBack = QColor(0x4d, 0x39, 0x3e);
    t.graph = QColor(0x31, 0x35, 0x40);
    t.canvasOutside = QColor(0x13, 0x14, 0x1a);
    t.checkerLight = QColor(0x38, 0x3c, 0x48);
    t.checkerDark = QColor(0x2b, 0x2e, 0x38);
    t.ruler = QColor(0x3c, 0x40, 0x4d);
    t.rulerText = QColor(0xb4, 0xb8, 0xc0);
    t.positionBack = QColor(0x23, 0x25, 0x2e);
    return t;
}

QPalette palette(const Theme &t)
{
    QPalette p;
    p.setColor(QPalette::Window, t.panel);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.window);
    p.setColor(QPalette::AlternateBase, t.panel);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.window);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::Highlight, t.selection);
    p.setColor(QPalette::HighlightedText, t.text);
    p.setColor(QPalette::ToolTipBase, t.panel);
    p.setColor(QPalette::ToolTipText, t.text);
    p.setColor(QPalette::PlaceholderText, t.textMuted);
    p.setColor(QPalette::Light, t.border);
    p.setColor(QPalette::Mid, t.border);
    p.setColor(QPalette::Dark, t.window);
    for (QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, t.textDisabled);
    return p;
}

QString styleSheet(const Theme &t)
{
    // Platzhalter @name@ werden durch die Farben ersetzt, damit das Stylesheet lesbar bleibt.
    QString css = QStringLiteral(R"(
QMainWindow, QMenuBar, QMenu { background: @window@; color: @text@; }
QMenuBar::item { background: transparent; padding: 4px 6px; }
QMenuBar::item:selected, QMenu::item:selected { background: @selection@; }
QMenuBar::item:disabled, QMenu::item:disabled { color: @textDisabled@; }
QMenu { border: 1px solid @border@; }
QWidget { color: @text@; }
QToolTip { background: @panel@; color: @text@; border: 1px solid @border@; }

#headerBar { background: @panel@; border: 1px solid @border@; border-radius: 4px; }
#moduleTabs { background: @segment@; border-radius: 3px; }
#moduleTabs QToolButton { background: transparent; color: @text@; border: none;
                          padding: 4px 14px; border-radius: 2px; }
#moduleTabs QToolButton:checked { background: @accent@; color: @text@; }

QToolButton#connectionButton { border: 2px solid @border@; border-radius: 6px;
                               background: @border@; color: @text@; font-weight: bold; }
QToolButton#connectionButton[state="disconnected"] { border-color: @bad@; background: @badBack@; }
QToolButton#connectionButton[state="connected"] { border-color: @good@; background: @goodBack@; }

#gaugeTitle { color: @textMuted@; }
#gaugeValue { color: @text@; font-weight: bold; }
#gaugeGraph { background: @panel@; border: 1px solid @graph@; }

#chatBotPage { background: @canvasOutside@; }
#chatBotPage QLabel { color: @textMuted@; }

QDockWidget { color: @text@; font-weight: bold; }
QDockWidget::title { background: @panel@; padding: 5px 6px; border-bottom: 1px solid @border@; }
QDockWidget > QWidget { background: @panel@; font-weight: normal; border: 1px solid @border@; }
QMainWindow::separator { background: @window@; width: 4px; height: 4px; }

QListWidget { background: @panel@; border: none; outline: none; }
QListWidget::item:selected { background: @selection@; }
#dockButtons { border-top: 1px solid @border@; }
#dockButtons QToolButton { background: transparent; border: none; padding: 3px; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { background: @window@; color: @text@;
    border: 1px solid @border@; border-radius: 2px; padding: 2px 4px; }
QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled,
QLabel:disabled, QCheckBox:disabled { color: @textDisabled@; }
QPushButton { background: @window@; color: @text@; border: 1px solid @border@;
              border-radius: 2px; padding: 3px 10px; }
QPushButton:disabled { color: @textDisabled@; }
QCheckBox::indicator { width: 14px; height: 14px; border: 1px solid @textMuted@;
                       border-radius: 2px; background: @window@; }
QCheckBox::indicator:checked { background: @accent@; border-color: @accent@; }
QCheckBox::indicator:disabled { border-color: @textDisabled@; }
QCheckBox::indicator:checked:disabled { background: @textDisabled@; }
#hintLabel { color: @textMuted@; }

QStatusBar { background: @panel@; color: @textMuted@; border-top: 1px solid @border@; }
QStatusBar::item { border: none; }
#coreStatus { font-weight: bold; padding: 0 8px; }
#coreStatus[state="disconnected"] { color: @bad@; }
#coreStatus[state="connected"] { color: @good@; }
)");
    const QList<std::pair<const char *, QColor>> colors = {
        {"window", t.window},       {"panel", t.panel},
        {"border", t.border},       {"segment", t.segment},
        {"text", t.text},           {"textMuted", t.textMuted},
        {"textDisabled", t.textDisabled}, {"accent", t.accent},
        {"selection", t.selection}, {"good", t.good},
        {"goodBack", t.goodBack},   {"bad", t.bad},
        {"badBack", t.badBack},     {"graph", t.graph},
        {"canvasOutside", t.canvasOutside},
    };
    for (const auto &[name, color] : colors)
        css.replace(QLatin1Char('@') + QLatin1String(name) + QLatin1Char('@'), color.name());
    return css;
}
