#include "actionspage.h"

#include "panels.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSplitter>
#include <QVBoxLayout>

ActionsPage::ActionsPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("actionsPage"));
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setObjectName(QStringLiteral("actionsSplitter"));
    m_splitter->setChildrenCollapsible(false);
    row->addWidget(m_splitter);

    const QString titles[3] = {QStringLiteral("Aktionen"), QStringLiteral("Aktion"), QStringLiteral("Schritt")};
    for (int i = 0; i < 3; ++i) {
        m_areas[i] = new QWidget;
        auto *column = new QVBoxLayout(m_areas[i]);
        column->setContentsMargins(0, 0, 0, 0);
        QWidget *panel = dockPanel(titles[i], m_areas[i], m_splitter);
        m_titles[i] = panel->findChild<QLabel *>(QStringLiteral("chatBotPanelTitle"), Qt::FindDirectChildrenOnly);
        m_splitter->addWidget(panel);
    }
    // Die Mitte bekommt den zusätzlichen Platz, links und rechts wachsen weniger (wie Docks neben
    // der Leinwand).
    m_splitter->setStretchFactor(List, 1);
    m_splitter->setStretchFactor(Action, 2);
    m_splitter->setStretchFactor(Detail, 1);
}

void ActionsPage::setAreaTitle(Area which, const QString &title)
{
    m_titles[which]->setText(title);
}

QString ActionsPage::areaTitle(Area which) const
{
    return m_titles[which]->text();
}
