#include "address.h"

namespace dss {

QString addressFromName(const QString &name)
{
    // NFC zuerst, damit auch zerlegt eingegebene Umlaute (u + U+0308) als «ü» erkannt werden.
    QString text = name.normalized(QString::NormalizationForm_C).toLower();
    text.replace(QChar(0x00E4), QLatin1String("ae")); // ä
    text.replace(QChar(0x00F6), QLatin1String("oe")); // ö
    text.replace(QChar(0x00FC), QLatin1String("ue")); // ü
    // Buchstaben ohne Unicode-Zerlegung (Entscheid nach P04). Grossbuchstaben sind durch
    // toLower() schon klein; «ẞ» (U+1E9E) wird dabei zu «ß».
    text.replace(QChar(0x00DF), QLatin1String("ss")); // ß
    text.replace(QChar(0x00E6), QLatin1String("ae")); // æ
    text.replace(QChar(0x00F8), QLatin1String("oe")); // ø
    text.replace(QChar(0x0142), QLatin1String("l"));  // ł

    // Übrige Akzente: zerlegen (NFD) und die Akzentzeichen (Kategorie Mn) weglassen.
    // https://doc.qt.io/qt-6.8/qstring.html#normalized
    text = text.normalized(QString::NormalizationForm_D);

    QString result;
    result.reserve(text.size());
    bool lastWasDash = true; // unterdrückt «-» am Anfang
    for (const QChar c : std::as_const(text)) {
        if (c.category() == QChar::Mark_NonSpacing)
            continue;
        if ((c >= QLatin1Char('a') && c <= QLatin1Char('z'))
            || (c >= QLatin1Char('0') && c <= QLatin1Char('9'))) {
            result.append(c);
            lastWasDash = false;
        } else if (!lastWasDash) {
            result.append(QLatin1Char('-'));
            lastWasDash = true;
        }
    }
    if (result.endsWith(QLatin1Char('-')))
        result.chop(1);
    if (result.isEmpty())
        return QStringLiteral("overlay");
    return result;
}

QString uniqueAddress(const QString &base, const QSet<QString> &taken)
{
    if (!taken.contains(base))
        return base;
    for (int n = 2;; ++n) {
        const QString candidate = base + QLatin1Char('-') + QString::number(n);
        if (!taken.contains(candidate))
            return candidate;
    }
}

} // namespace dss
