#include "headerbar.h"

#include "gaugewidget.h"
#include "symbols.h"
#include "theme.h"

#include "windowsize.h"

#include <QButtonGroup>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QStyle>
#include <QToolButton>

#include <algorithm>

namespace {

// Reihe des Kopfbereichs (P09b, Entscheid 2): links Modul-Reiter und Verbindungsknöpfe in ihrer
// eigenen Breite, rechts die drei Anzeigen in der Breite aus dss::headerGaugesWidth (600 px,
// höchstens ein Drittel der Fensterbreite). Der Platz dazwischen bleibt leer. Die Breite rechnet
// dss_common; ein eigenes Layout setzt sie bei jeder Änderung der Grösse, damit kein
// Ereignis-Behandler das Layout ändert (CLAUDE.md Regel 9).
// QLayout ableiten: https://doc.qt.io/qt-6.8/layout.html#how-to-write-a-custom-layout-manager
class HeaderRowLayout : public QLayout
{
public:
    explicit HeaderRowLayout(QWidget *parent) : QLayout(parent) {}
    ~HeaderRowLayout() override
    {
        while (QLayoutItem *item = takeAt(0))
            delete item;
    }

    // Erstes Element: linker Teil, danach die Anzeigen von links nach rechts.
    void addItem(QLayoutItem *item) override { m_items.append(item); }
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override
    {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    QSize sizeHint() const override { return size(false); }
    QSize minimumSize() const override { return size(true); }

    void setGeometry(const QRect &rect) override
    {
        QLayout::setGeometry(rect);
        if (m_items.isEmpty())
            return;
        const QRect area = contentsRect();
        // Fensterbreite, nicht die Breite des Kopfbereichs (Entscheid 2).
        const QWidget *window = parentWidget() ? parentWidget()->window() : nullptr;
        const int total = dss::headerGaugesWidth(window ? window->width() : rect.width());
        int x = area.right() + 1 - total;
        for (int i = 1; i < m_items.size(); ++i) {
            const int w = dss::headerGaugeWidth(total, i - 1);
            m_items.at(i)->setGeometry(QRect(x, area.top(), w, area.height()));
            x += w;
        }
        const int leftWidth = std::min(m_items.first()->sizeHint().width(),
                                       std::max(0, area.width() - total));
        m_items.first()->setGeometry(QRect(area.left(), area.top(), leftWidth, area.height()));
    }

private:
    QSize size(bool minimum) const
    {
        int height = 0;
        for (const QLayoutItem *item : m_items)
            height = std::max(height, (minimum ? item->minimumSize() : item->sizeHint()).height());
        const int left = m_items.isEmpty() ? 0 : m_items.first()->sizeHint().width();
        const QMargins m = contentsMargins();
        return QSize(left + m.left() + m.right(), height + m.top() + m.bottom());
    }

    QList<QLayoutItem *> m_items;
};

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
    // Breite setzt HeaderRowLayout; der Rand links trennt die Anzeigen voneinander.
    box->setContentsMargins(8, 0, 0, 0);
    *value = valueLabel;
    *graphOut = graph;
    return box;
}

} // namespace

HeaderBar::HeaderBar(const Theme &theme, QWidget *parent)
    : QFrame(parent)
{
    setObjectName(QStringLiteral("headerBar"));

    auto *row = new HeaderRowLayout(this);
    row->setContentsMargins(8, 6, 8, 6);
    auto *left = new QWidget(this);
    left->setObjectName(QStringLiteral("headerLeft"));
    auto *leftRow = new QHBoxLayout(left);
    leftRow->setContentsMargins(0, 0, 0, 0);
    leftRow->setSpacing(8);
    row->addWidget(left);

    auto *tabs = new QFrame(left);
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

    leftRow->addWidget(tabs, 0, Qt::AlignVCenter);
    leftRow->addSpacing(70);
    const QList<std::tuple<dss::Connection, QString, QString>> connections = {
        {dss::Connection::Twitch, QStringLiteral("TW"), QStringLiteral("twitch.svg")},
        {dss::Connection::YouTube, QStringLiteral("YT"), QStringLiteral("youtube.svg")},
        {dss::Connection::Obs, QStringLiteral("OBS"), QStringLiteral("obs.svg")},
        {dss::Connection::Ndi, QStringLiteral("NDI"), QStringLiteral("ndi.svg")},
    };
    for (const auto &[connection, shortText, symbolFile] : connections) {
        QToolButton *button = connectionButton(shortText, symbolFile, left);
        m_connections.insert(connection, button);
        leftRow->addWidget(button);
        // Ohne Kern: alle grau.
        setConnectionState(connection, dss::ConnectionState::Off);
    }
    // Anzeigen rechts in fester Breite (Entscheid 2 in P09b), der Platz davor bleibt leer.
    const QList<std::tuple<QString, QColor, const char *>> gauges = {
        {QStringLiteral("CPU"), theme.graphCpu, "gaugeCpu"},
        {QStringLiteral("RAM"), theme.graphRam, "gaugeRam"},
        {QStringLiteral("LAN"), theme.graphLan, "gaugeLan"}};
    for (const auto &[title, color, name] : gauges) {
        QLabel *value = nullptr;
        GaugeGraph *graph = nullptr;
        QWidget *box = gauge(title, color, &value, &graph, this);
        box->setObjectName(QString::fromLatin1(name));
        row->addWidget(box);
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
