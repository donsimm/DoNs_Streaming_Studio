#include "panels.h"

#include "symbols.h"

#include <QAbstractSpinBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

QToolButton *symbolButton(const QString &symbolFile, const QString &fallback,
                          const QString &toolTip, QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setText(fallback);
    const QIcon icon = loadSymbol(symbolFile, QSize(14, 14));
    if (!icon.isNull())
        button->setIcon(icon);
    button->setIconSize(QSize(14, 14));
    button->setToolTip(toolTip);
    button->setAccessibleName(toolTip);
    button->setEnabled(false);
    return button;
}

void keepValueVisible(QAbstractSpinBox *spin)
{
    spin->setSizePolicy(QSizePolicy::Minimum, spin->sizePolicy().verticalPolicy());
}

// P09b, E-26: Titel und Linie wie der Rahmen des Kopfbereichs (Farbe border); P10a, Teil A 2:
// Knöpfe in der Titelzeile.
QWidget *section(const QString &title, QWidget *parent, QGridLayout **grid,
                 const QList<QToolButton *> &titleButtons)
{
    auto *w = new QWidget(parent);
    auto *column = new QVBoxLayout(w);
    column->setContentsMargins(8, 6, 8, 8);
    column->setSpacing(4);
    auto *titleRow = new QHBoxLayout;
    titleRow->setContentsMargins(0, 0, 0, 0);
    titleRow->setSpacing(4);
    auto *label = new QLabel(title, w);
    label->setObjectName(QStringLiteral("propertySectionTitle"));
    titleRow->addWidget(label);
    titleRow->addStretch(1);
    for (QToolButton *b : titleButtons) {
        b->setParent(w);
        titleRow->addWidget(b);
    }
    column->addLayout(titleRow);
    auto *line = new QFrame(w);
    line->setObjectName(QStringLiteral("propertySectionLine"));
    line->setFixedHeight(1);
    column->addWidget(line);
    column->addSpacing(2);
    *grid = new QGridLayout;
    (*grid)->setContentsMargins(0, 0, 0, 0);
    (*grid)->setHorizontalSpacing(8);
    (*grid)->setVerticalSpacing(6);
    column->addLayout(*grid);
    return w;
}

QWidget *buttonBar(const QList<QWidget *> &left, const QList<QWidget *> &right, QWidget *parent,
                   QWidget *stretchWidget)
{
    auto *bar = new QWidget(parent);
    bar->setObjectName(QStringLiteral("dockButtons"));
    bar->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(6, 4, 6, 4);
    row->setSpacing(6);
    for (QWidget *w : left) {
        w->setParent(bar);
        row->addWidget(w, w == stretchWidget ? 1 : 0);
    }
    if (!stretchWidget)
        row->addStretch(1);
    for (QWidget *w : right) {
        w->setParent(bar);
        row->addWidget(w);
    }
    return bar;
}

QWidget *listWithButtons(const QList<QToolButton *> &buttons, QWidget *list, QWidget *parent)
{
    auto *box = new QWidget(parent);
    auto *column = new QVBoxLayout(box);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    list->setParent(box);
    column->addWidget(list, 1);
    QList<QWidget *> left;
    for (QToolButton *b : buttons)
        left.append(b);
    column->addWidget(buttonBar(left, {}, box));
    return box;
}

QWidget *dockPanel(const QString &title, QWidget *content, QWidget *parent)
{
    auto *panel = new QWidget(parent);
    auto *column = new QVBoxLayout(panel);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    auto *titleLabel = new QLabel(title, panel);
    titleLabel->setObjectName(QStringLiteral("chatBotPanelTitle"));
    column->addWidget(titleLabel);
    auto *body = new QFrame(panel);
    body->setObjectName(QStringLiteral("chatBotPanelBody"));
    auto *bodyColumn = new QVBoxLayout(body);
    bodyColumn->setContentsMargins(2, 2, 2, 2);
    content->setParent(body);
    bodyColumn->addWidget(content);
    column->addWidget(body, 1);
    return panel;
}
