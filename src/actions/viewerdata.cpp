#include "viewerdata.h"

#include "actionjson.h"
#include "variables.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <cmath>

namespace dss::actions {

namespace {

// Liest das JSON-Objekt, prüft format_version und genau die Felder keys (dazu format_version).
bool readRoot(const QByteArray &bytes, int expectedVersion, const QStringList &keys, QJsonObject *root,
              QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (!doc.isObject()) {
        *error = doc.isNull() ? parseError.errorString() : QStringLiteral("muss ein JSON-Objekt sein");
        return false;
    }
    *root = doc.object();
    const QJsonValue version = root->value(QStringLiteral("format_version"));
    if (!version.isDouble() || std::trunc(version.toDouble()) != version.toDouble()) {
        *error = QStringLiteral("Feld «format_version»: fehlt oder ist keine ganze Zahl");
        return false;
    }
    if (version.toInt() != expectedVersion) {
        *error = QStringLiteral("format_version %1 ist unbekannt, erwartet %2.")
                     .arg(version.toDouble()).arg(expectedVersion);
        return false;
    }
    for (auto it = root->begin(); it != root->end(); ++it) {
        if (it.key() != QLatin1String("format_version") && !keys.contains(it.key())) {
            *error = QStringLiteral("Feld «%1»: unbekanntes Feld").arg(it.key());
            return false;
        }
    }
    for (const QString &key : keys) {
        if (!root->contains(key)) {
            *error = QStringLiteral("Feld «%1»: fehlt").arg(key);
            return false;
        }
    }
    return true;
}

QByteArray write(const QJsonObject &root)
{
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

} // namespace

QByteArray serializeViewers(const ViewersData &data)
{
    QJsonObject viewers;
    for (auto it = data.viewers.cbegin(); it != data.viewers.cend(); ++it) {
        QJsonObject o = it->extra;
        o.insert(QStringLiteral("login"), it->login);
        o.insert(QStringLiteral("display_name"), it->displayName);
        viewers.insert(it.key(), o);
    }
    return write({{QStringLiteral("format_version"), kViewersFormatVersion}, {QStringLiteral("viewers"), viewers}});
}

bool parseViewers(const QByteArray &bytes, ViewersData *data, QString *error)
{
    QJsonObject root;
    if (!readRoot(bytes, kViewersFormatVersion, {QStringLiteral("viewers")}, &root, error))
        return false;
    if (!root.value(QStringLiteral("viewers")).isObject()) {
        *error = QStringLiteral("Feld «viewers»: muss ein Objekt sein");
        return false;
    }
    ViewersData result;
    const QJsonObject viewers = root.value(QStringLiteral("viewers")).toObject();
    for (auto it = viewers.begin(); it != viewers.end(); ++it) {
        const QString path = QStringLiteral("viewers.") + it.key();
        if (it.key().trimmed().isEmpty()) {
            *error = QStringLiteral("Feld «viewers»: leere Twitch-ID");
            return false;
        }
        if (!it.value().isObject()) {
            *error = QStringLiteral("Feld «%1»: muss ein Objekt sein").arg(path);
            return false;
        }
        QJsonObject o = it.value().toObject();
        const QJsonValue login = o.take(QStringLiteral("login"));
        const QJsonValue displayName = o.take(QStringLiteral("display_name"));
        if (!login.isString() || login.toString().trimmed().isEmpty()) {
            *error = QStringLiteral("Feld «%1.login»: muss Text sein, nicht leer").arg(path);
            return false;
        }
        if (!displayName.isString()) {
            *error = QStringLiteral("Feld «%1.display_name»: muss Text sein").arg(path);
            return false;
        }
        result.viewers.insert(it.key(), Viewer{login.toString(), displayName.toString(), o});
    }
    *data = result;
    return true;
}

bool noteViewer(ViewersData *data, const QString &userId, const QString &login, const QString &displayName)
{
    if (userId.isEmpty() || login.isEmpty())
        return false;
    Viewer &v = data->viewers[userId];
    if (v.login == login && v.displayName == displayName)
        return false;
    v.login = login;
    v.displayName = displayName;
    return true;
}

QByteArray serializeQuotes(const QuotesData &data)
{
    QJsonArray quotes;
    for (const QJsonObject &q : data.quotes)
        quotes.append(q);
    return write({{QStringLiteral("format_version"), kQuotesFormatVersion}, {QStringLiteral("quotes"), quotes}});
}

bool parseQuotes(const QByteArray &bytes, QuotesData *data, QString *error)
{
    QJsonObject root;
    if (!readRoot(bytes, kQuotesFormatVersion, {QStringLiteral("quotes")}, &root, error))
        return false;
    if (!root.value(QStringLiteral("quotes")).isArray()) {
        *error = QStringLiteral("Feld «quotes»: muss eine Liste sein");
        return false;
    }
    QuotesData result;
    const QJsonArray quotes = root.value(QStringLiteral("quotes")).toArray();
    for (qsizetype i = 0; i < quotes.size(); ++i) {
        if (!quotes.at(i).isObject()) {
            *error = QStringLiteral("Feld «quotes[%1]»: muss ein Objekt sein").arg(i);
            return false;
        }
        result.quotes.append(quotes.at(i).toObject());
    }
    *data = result;
    return true;
}

QByteArray serializeArchive(const ArchiveData &data)
{
    QJsonObject files;
    for (auto it = data.files.cbegin(); it != data.files.cend(); ++it)
        files.insert(it.key(), it.value());
    return write({{QStringLiteral("format_version"), kArchiveFormatVersion}, {QStringLiteral("files"), files}});
}

bool parseArchive(const QByteArray &bytes, ArchiveData *data, QString *error)
{
    QJsonObject root;
    if (!readRoot(bytes, kArchiveFormatVersion, {QStringLiteral("files")}, &root, error))
        return false;
    if (!root.value(QStringLiteral("files")).isObject()) {
        *error = QStringLiteral("Feld «files»: muss ein Objekt sein");
        return false;
    }
    ArchiveData result;
    const QJsonObject files = root.value(QStringLiteral("files")).toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        if (!it.value().isObject()) {
            *error = QStringLiteral("Feld «files.%1»: muss ein Objekt sein (Sammlung → Inhalt)").arg(it.key());
            return false;
        }
        result.files.insert(it.key(), it.value().toObject());
    }
    *data = result;
    return true;
}

QList<DataFileFormat> actionDataFiles()
{
    return {
        {QString::fromLatin1(kActionsFileName), kActionsFormatVersion,
         [](const QByteArray &b, QString *e) { ActionsConfig c; return parseActions(b, &c, e); }},
        {QString::fromLatin1(kVariablesFileName), kVariablesFormatVersion,
         [](const QByteArray &b, QString *e) { VariablesData d; return parseVariables(b, &d, e); }},
        {QString::fromLatin1(kViewersFileName), kViewersFormatVersion,
         [](const QByteArray &b, QString *e) { ViewersData d; return parseViewers(b, &d, e); }},
        {QString::fromLatin1(kQuotesFileName), kQuotesFormatVersion,
         [](const QByteArray &b, QString *e) { QuotesData d; return parseQuotes(b, &d, e); }},
        {QString::fromLatin1(kArchiveFileName), kArchiveFormatVersion,
         [](const QByteArray &b, QString *e) { ArchiveData d; return parseArchive(b, &d, e); }},
    };
}

} // namespace dss::actions
