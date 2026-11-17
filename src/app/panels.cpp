#include "panels.h"

#include "symbols.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QFontMetricsF>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QStyle>
#include <QtMath>
#include <QToolButton>
#include <QVBoxLayout>

void ForegroundDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    const QVariant foreground = index.data(Qt::ForegroundRole);
    if (!foreground.canConvert<QBrush>() || option.state.testFlag(QStyle::State_Selected)) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    const QWidget *widget = opt.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    // Rand des Textes wie QCommonStyle (viewItemDrawText): PM_FocusFrameHMargin + 1
    const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, widget) + 1;
    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget)
                               .adjusted(margin, 0, -margin, 0);
    const QString text = opt.text;
    opt.text.clear();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
    painter->save();
    painter->setFont(opt.font);
    painter->setPen(foreground.value<QBrush>().color());
    painter->drawText(textRect, int(opt.displayAlignment),
                      opt.fontMetrics.elidedText(text, opt.textElideMode, textRect.width()));
    painter->restore();
}

QSize NameColumnDelegate::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    QSize size = ForegroundDelegate::sizeHint(option, index);
    if (index.column() != 0)
        return size;
    QStyleOptionViewItem opt(option);
    initStyleOption(&opt, index);
    opt.rect = QRect(QPoint(0, 0), size);
    const QWidget *widget = opt.widget;
    QStyle *style = widget ? widget->style() : QApplication::style();
    // Rand wie beim Zeichnen (viewItemDrawText): PM_FocusFrameHMargin + 1 je Seite
    const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, widget) + 1;
    const int needed = qCeil(QFontMetricsF(opt.font).horizontalAdvance(opt.text)) + 2 * margin;
    const int available = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, widget).width();
    size.rwidth() += qMax(0, needed - available);
    return size;
}

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

FieldSpinBox::FieldSpinBox(QWidget *parent) : QDoubleSpinBox(parent)
{
    QLocale comma(QLocale::German, QLocale::Germany);
    comma.setNumberOptions(QLocale::OmitGroupSeparator);
    setLocale(comma);
}

QString FieldSpinBox::textFromValue(double value) const
{
    QString text = locale().toString(value, 'f', decimals());
    const QString point = locale().decimalPoint();
    if (decimals() > 0 && text.contains(point)) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(point))
            text.chop(point.size());
    }
    return text;
}

// P09b, E-26: Titel und Linie wie der Rahmen des Kopfbereichs (Farbe border); P10a, Teil A 2:
// Knöpfe in der Titelzeile.
QWidget *section(const QString &title, QWidget *parent, QGridLayout **grid,
                 const QList<QToolButton *> &titleButtons, QLabel **titleText)
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
    if (titleText) {
        *titleText = new QLabel(w);
        (*titleText)->setObjectName(QStringLiteral("propertySectionText"));
        (*titleText)->setTextFormat(Qt::PlainText);
        titleRow->addWidget(*titleText);
    }
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

QLabel *fieldLabel(const QString &text, QWidget *buddy, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setBuddy(buddy);
    return l;
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

QLabel *problemLine(QWidget *parent)
{
    // Prüfmeldung am Feld (Entscheid des Nutzers, 2026-11-16): eine Zeile, immer da, damit eine
    // Meldung die Anordnung nicht verschiebt; der ganze Text steht im Hinweis beim Darüberfahren.
    auto *label = new QLabel(QStringLiteral("—"), parent);
    label->setObjectName(QStringLiteral("fieldProblem"));
    label->setTextFormat(Qt::PlainText);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    label->ensurePolished();
    label->setMinimumHeight(label->sizeHint().height());
    label->clear();
    return label;
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
