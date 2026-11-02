#include "tokenstore.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>

namespace dss {

static constexpr QFileDevice::Permissions kOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
// Nur die Unix-Rechte (Owner/Group/Other); die User-Bits beschreiben den aktuellen Benutzer.
static constexpr QFileDevice::Permissions kUnixBits =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
    | QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
    | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;

TokenStore::TokenStore(const QString &filePath)
    : m_filePath(filePath)
{
}

QString TokenStore::generateToken()
{
    // QRandomGenerator::system() nutzt die Zufallsquelle des Betriebssystems
    // (unter Linux /dev/urandom bzw. getrandom()), laut Qt kryptografisch sicher.
    // https://doc.qt.io/qt-6.8/qrandomgenerator.html#system
    quint32 words[kTokenBytes / sizeof(quint32)];
    QRandomGenerator::system()->fillRange(words);
    const QByteArray bytes(reinterpret_cast<const char *>(words), sizeof(words));
    return QString::fromLatin1(
        bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

bool TokenStore::load(QString *error, QString *warning)
{
    m_tokens.clear();
    warning->clear();
    QFile file(m_filePath);
    if (!file.exists())
        return true;
    if ((file.permissions() & kUnixBits) != kOwnerOnly) {
        if (!file.setPermissions(kOwnerOnly)) {
            *error = QStringLiteral("tokens.json: Dateirechte lassen sich nicht auf 0600 setzen (%1)")
                         .arg(file.errorString());
            return false;
        }
        *warning = QStringLiteral("tokens.json: Dateirechte waren nicht 0600 und wurden auf 0600 gesetzt");
    }
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("tokens.json: kann nicht gelesen werden (%1)").arg(file.errorString());
        return false;
    }
    // Meldungen nennen nie den Inhalt eines Tokens.
    const QString broken = QStringLiteral("tokens.json: ungültiger Inhalt (%1); die Datei wird nicht verändert");
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = broken.arg(QStringLiteral("kein gültiges JSON-Objekt"));
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.keys() != QStringList{QStringLiteral("format_version"), QStringLiteral("tokens")}) {
        *error = broken.arg(QStringLiteral("erwartet genau die Felder format_version und tokens"));
        return false;
    }
    const QJsonValue version = root.value(QStringLiteral("format_version"));
    if (!version.isDouble() || version.toDouble() != kTokenFormatVersion) {
        *error = broken.arg(QStringLiteral("format_version muss %1 sein").arg(kTokenFormatVersion));
        return false;
    }
    if (!root.value(QStringLiteral("tokens")).isObject()) {
        *error = broken.arg(QStringLiteral("tokens muss ein Objekt sein"));
        return false;
    }
    static const QRegularExpression tokenPattern(QStringLiteral("^[A-Za-z0-9_-]{22,}$")); // ≥ 128 Bit
    const QJsonObject tokens = root.value(QStringLiteral("tokens")).toObject();
    QHash<QUuid, QString> loaded;
    for (auto it = tokens.begin(); it != tokens.end(); ++it) {
        const QUuid id = QUuid::fromString(it.key());
        if (id.isNull() || id.toString(QUuid::WithoutBraces) != it.key()) {
            *error = broken.arg(QStringLiteral("ungültige Kennung «%1»").arg(it.key()));
            return false;
        }
        if (!it.value().isString() || !tokenPattern.match(it.value().toString()).hasMatch()) {
            *error = broken.arg(QStringLiteral("Token für %1 ungültig").arg(it.key()));
            return false;
        }
        loaded.insert(id, it.value().toString());
    }
    m_tokens = loaded;
    return true;
}

bool TokenStore::save(const QHash<QUuid, QString> &tokens, QString *error)
{
    QJsonObject map;
    for (auto it = tokens.begin(); it != tokens.end(); ++it)
        map.insert(it.key().toString(QUuid::WithoutBraces), it.value());
    QJsonObject root;
    root.insert(QStringLiteral("format_version"), kTokenFormatVersion);
    root.insert(QStringLiteral("tokens"), map);

    // Atomar über QSaveFile. Die Rechte 0600 werden auf der temporären Datei gesetzt, bevor
    // ein Token hineingeschrieben wird; commit() benennt sie dann um.
    // https://doc.qt.io/qt-6.8/qsavefile.html, https://doc.qt.io/qt-6.8/qfiledevice.html#setPermissions
    QSaveFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QStringLiteral("tokens.json kann nicht geschrieben werden (%1)").arg(file.errorString());
        return false;
    }
    if (!file.setPermissions(kOwnerOnly)) {
        file.cancelWriting();
        *error = QStringLiteral("tokens.json: Dateirechte 0600 lassen sich nicht setzen (%1)")
                     .arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        *error = QStringLiteral("tokens.json kann nicht gespeichert werden (%1)").arg(file.errorString());
        return false;
    }
    return true;
}

bool TokenStore::createToken(const QUuid &overlayId, QString *error)
{
    QHash<QUuid, QString> changed = m_tokens;
    changed.insert(overlayId, generateToken());
    if (!save(changed, error))
        return false;
    m_tokens = changed;
    return true;
}

bool TokenStore::removeToken(const QUuid &overlayId, QString *error)
{
    if (!m_tokens.contains(overlayId))
        return true;
    QHash<QUuid, QString> changed = m_tokens;
    changed.remove(overlayId);
    if (!save(changed, error))
        return false;
    m_tokens = changed;
    return true;
}

} // namespace dss
