#include "videoformat.h"

#include <QHash>

namespace dss {

std::optional<QString> browserUnsupportedVideoCodec(const QString &codec)
{
    // Quelle: Bericht P10 (siehe videoformat.h)
    static const QHash<QString, QString> unsupported = {
        {QStringLiteral("hevc"), QStringLiteral("HEVC")},
        {QStringLiteral("mpeg4"), QStringLiteral("MPEG-4 Part 2")},
        {QStringLiteral("prores"), QStringLiteral("ProRes")},
    };
    const auto it = unsupported.constFind(codec);
    if (it == unsupported.constEnd())
        return std::nullopt;
    return *it;
}

QString browserVideoWarning(const QString &name, const QString &codecDisplayName)
{
    return QStringLiteral("%1 (%2) wird in der Browserquelle nicht angezeigt. In H.264 oder VP9 "
                          "umwandeln.")
        .arg(name, codecDisplayName);
}

} // namespace dss
