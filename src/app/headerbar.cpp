#include "headerbar.h"

#include "symbols.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>

namespace {

QToolButton *moduleButton(const QString &text, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(text);
    button->setCheckable(true);
    button->setFocusPolicy(Qt::NoFocus);
    return button;
}

// Verbindungsknopf; Zustand über die Eigenschaft «state», Farben im Theme.
QToolButton *connectionButton(const QString &name, const QString &shortText,
                              const QString &symbolFile, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("connectionButton"));
    button->setProperty("state", QStringLiteral("disconnected"));
    button->setFixedSize(46, 40);
    button->setIconSize(QSize(28, 28));
    button->setFocusPolicy(Qt::NoFocus);
    button->setText(shortText);
    const QIcon icon = loadSymbol(symbolFile);
    if (!icon.isNull()) {
        button->setIcon(icon);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    button->setToolTip(QStringLiteral("%1: nicht verbunden").arg(name));
    button->setAccessibleName(QStringLiteral("%1: nicht verbunden").arg(name));
    return button;
}

// Anzeige mit Titel, Wert und Verlaufsfläche (noch ohne Verlauf).
QWidget *gauge(const QString &title, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *grid = new QGridLayout(box);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(1);
    auto *titleLabel = new QLabel(title, box);
    titleLabel->setObjectName(QStringLiteral("gaugeTitle"));
    auto *valueLabel = new QLabel(QStringLiteral("–"), box);
    valueLabel->setObjectName(QStringLiteral("gaugeValue"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    auto *graph = new QFrame(box);
    graph->setObjectName(QStringLiteral("gaugeGraph"));
    graph->setMinimumHeight(28);
    graph->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    grid->addWidget(titleLabel, 0, 0);
    grid->addWidget(valueLabel, 0, 1);
    grid->addWidget(graph, 1, 0, 1, 2);
    box->setMinimumWidth(140);
    box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    return box;
}

} // namespace

HeaderBar::HeaderBar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("headerBar"));

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(8, 6, 8, 6);
    row->setSpacing(8);

    auto *tabs = new QFrame(this);
    tabs->setObjectName(QStringLiteral("moduleTabs"));
    auto *tabRow = new QHBoxLayout(tabs);
    tabRow->setContentsMargins(2, 2, 2, 2);
    tabRow->setSpacing(2);
    m_modules = new QButtonGroup(this);
    m_modules->setExclusive(true);
    QToolButton *overlays = moduleButton(QStringLiteral("Overlays"), tabs);
    QToolButton *chatBot = moduleButton(QStringLiteral("Chat-Bot"), tabs);
    m_modules->addButton(overlays, Overlays);
    m_modules->addButton(chatBot, ChatBot);
    overlays->setChecked(true);
    tabRow->addWidget(overlays);
    tabRow->addWidget(chatBot);
    connect(m_modules, &QButtonGroup::idClicked, this, &HeaderBar::moduleSelected);

    row->addWidget(tabs, 0, Qt::AlignVCenter);
    row->addSpacing(70);
    row->addWidget(connectionButton(QStringLiteral("Twitch"), QStringLiteral("TW"),
                                    QStringLiteral("twitch.svg"), this));
    row->addWidget(connectionButton(QStringLiteral("YouTube"), QStringLiteral("YT"),
                                    QStringLiteral("youtube.svg"), this));
    row->addWidget(connectionButton(QStringLiteral("OBS"), QStringLiteral("OBS"),
                                    QStringLiteral("obs.svg"), this));
    row->addWidget(connectionButton(QStringLiteral("NDI"), QStringLiteral("NDI"),
                                    QStringLiteral("ndi.svg"), this));
    row->addSpacing(16);
    row->addWidget(gauge(QStringLiteral("CPU"), this), 1);
    row->addWidget(gauge(QStringLiteral("RAM"), this), 1);
    row->addWidget(gauge(QStringLiteral("LAN"), this), 1);
}
