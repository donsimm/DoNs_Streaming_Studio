#include "windowsize.h"

#include <QRegularExpression>

#include <algorithm>

namespace dss {

bool parseWindowSize(const QString &text, QSize *size)
{
    static const QRegularExpression pattern(QStringLiteral("^([0-9]{1,5})x([0-9]{1,5})$"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch())
        return false;
    const int width = match.captured(1).toInt();
    const int height = match.captured(2).toInt();
    if (width < 1 || width > 10000 || height < 1 || height > 10000)
        return false;
    *size = QSize(width, height);
    return true;
}

int headerGaugesWidth(int windowWidth)
{
    if (windowWidth <= 0)
        return 0;
    return std::min(kHeaderGaugesWidth, windowWidth / 3);
}

int headerGaugeWidth(int total, int index)
{
    if (total <= 0 || index < 0 || index >= kHeaderGaugeCount)
        return 0;
    return total / kHeaderGaugeCount + (index < total % kHeaderGaugeCount ? 1 : 0);
}

} // namespace dss
