#include "headerbar.h"

#include "gaugewidget.h"
#include "symbols.h"
#include "theme.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QStyle>
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
QToolButton *connectionButton(const QString &shortText, const QString &symbolFile, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("connectionButton"));
    button->setFixedSize(46, 40);
    button->setIconSize(QSize(28, 28));
    button->setFocusPolicy(Qt::NoFocus);
    button->setText(shortText);
    const QIcon icon = loadSymbol(symbolFile, button->iconSize());
    if (!icon.isNull()) {
        button->setIcon(icon);
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    return button;
}

// Anzeige mit Titel, Wert und Verlaufsfläche.
QWidget *gauge(const QString &title, const QColor &line, QLabel **value, GaugeGraph **graphOut,
               QWidget *parent)
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
    auto *graph = new GaugeGraph(line, box);
    graph->setMinimumHeight(28);
    graph->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    grid->addWidget(titleLabel, 0, 0);
    grid->addWidget(valueLabel, 0, 1);
    grid->addWidget(graph, 1, 0, 1, 2);
    box->setMinimumWidth(140);
    box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    *value = valueLabel;
    *graphOut = graph;
    return box;
}

} // namespace

HeaderBar::HeaderBar(const Theme &theme, QWidget *parent)
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
    const QList<std::tuple<dss::Connection, QString, QString>> connections = {
        {dss::Connection::Twitch, QStringLiteral("TW"), QStringLiteral("twitch.svg")},
        {dss::Connection::YouTube, QStringLiteral("YT"), QStringLiteral("youtube.svg")},
        {dss::Connection::Obs, QStringLiteral("OBS"), QStringLiteral("obs.svg")},
        {dss::Connection::Ndi, QStringLiteral("NDI"), QStringLiteral("ndi.svg")},
    };
    for (const auto &[connection, shortText, symbolFile] : connections) {
        QToolButton *button = connectionButton(shortText, symbolFile, this);
        m_connections.insert(connection, button);
        row->addWidget(button);
        // Ohne Kern: alle grau.
        setConnectionState(connection, dss::ConnectionState::Off);
    }
    row->addSpacing(16);
    const QList<std::pair<QString, QColor>> gauges = {{QStringLiteral("CPU"), theme.graphCpu},
                                                      {QStringLiteral("RAM"), theme.graphRam},
                                                      {QStringLiteral("LAN"), theme.graphLan}};
    for (const auto &[title, color] : gauges) {
        QLabel *value = nullptr;
        GaugeGraph *graph = nullptr;
        row->addWidget(gauge(title, color, &value, &graph, this), 1);
        m_gaugeValues.append(value);
        m_gaugeGraphs.append(graph);
    }
}

void HeaderBar::setGauge(Gauge gauge, const QString &text, const QString &toolTip,
                         const QList<std::optional<double>> &history, int capacity,
                         double maximum)
{
    m_gaugeValues.at(gauge)->setText(text);
    m_gaugeValues.at(gauge)->setToolTip(toolTip);
    m_gaugeGraphs.at(gauge)->setToolTip(toolTip);
    m_gaugeGraphs.at(gauge)->setHistory(history, capacity, maximum);
}

void HeaderBar::setConnectionState(dss::Connection connection, dss::ConnectionState state)
{
    QToolButton *button = m_connections.value(connection);
    if (!button)
        return;
    const QString text = dss::connectionStateText(connection, state);
    button->setProperty("state", dss::connectionStateKey(state));
    button->setToolTip(text);
    button->setAccessibleName(text);
    // Das Stylesheet wertet [state="…"] nur beim Polieren aus.
    // https://doc.qt.io/qt-6.8/stylesheet-syntax.html#selector-types (Hinweis zu Eigenschaften)
    button->style()->unpolish(button);
    button->style()->polish(button);
}
