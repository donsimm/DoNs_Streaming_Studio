// Quellen:
//   QDateTime ISO 8601 (Qt::ISODateWithMs): https://doc.qt.io/qt-6.8/qt.html#DateFormat-enum
//   QSaveFile (Schreiben über eine Zwischendatei): https://doc.qt.io/qt-6.8/qsavefile.html
//   QDir::entryInfoList: https://doc.qt.io/qt-6.8/qdir.html#entryInfoList
//   Struktur der Daten aus Streamer.bot: docs/archiv/recherchen/2026-11-15-z0-streamerbot-struktur.txt

#include "viewerimport.h"

#include "actionjson.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <cmath>

namespace dss::actions {

namespace {

constexpr double kMaxExactInteger = 9007199254740992.0;

// Felder, über die ein Dokument einen Zuschauer nennt. userId zuerst; die übrigen sind Logins.
const QStringList kLoginFields = {QStringLiteral("userLogin"), QStringLiteral("login"), QStringLiteral("user"),
                                  QStringLiteral("userName")};

QString collectionKey(const QString &file, const QString &collection)
{
    return file + QLatin1Char('/') + collection;
}

bool isAuthName(const QString &name)
{
    return name.startsWith(QLatin1String("auth"), Qt::CaseInsensitive);
}

// Meldungen nennen nur den Namen der Datei, nie den Inhalt
bool readJsonFile(const QString &path, const QString &label, QJsonDocument *doc, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1: nicht lesbar").arg(label);
        return false;
    }
    QByteArray bytes = file.readAll();
    // Streamer.bot schreibt quotes.dat mit UTF-8-Kennung (BOM), der Export kopiert sie unverändert
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    QJsonParseError parseError;
    *doc = QJsonDocument::fromJson(bytes, &parseError);
    if (doc->isNull()) {
        *error = QStringLiteral("%1: kein gültiges JSON (%2 bei Byte %3)")
                     .arg(label, parseError.errorString())
                     .arg(parseError.offset);
        return false;
    }
    return true;
}

// Einfacher Wert für eine Variable: Text (nicht selbst ein JSON-Objekt oder -Array) oder eine
// endliche Zahl, die als double genau bleibt. Wahr/falsch, null und Verschachteltes nicht.
std::optional<QJsonValue> simpleValue(const QJsonValue &value)
{
    if (value.isString()) {
        const QString trimmed = value.toString().trimmed();
        if (trimmed.startsWith(QLatin1Char('{')) || trimmed.startsWith(QLatin1Char('['))) {
            const QJsonDocument inner = QJsonDocument::fromJson(trimmed.toUtf8());
            if (inner.isObject() || inner.isArray())
                return std::nullopt;
        }
        return value;
    }
    if (value.isDouble()) {
        const double number = value.toDouble();
        if (std::isfinite(number) && std::fabs(number) <= kMaxExactInteger)
            return QJsonValue(number);
    }
    return std::nullopt;
}

QDateTime parseTime(const QJsonValue &value, const QDateTime &fallback)
{
    const QDateTime time = QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    return time.isValid() ? time.toUTC() : fallback;
}

// Ohne Feld «platform» gilt Twitch (Annahme, im Bericht z1a)
bool isTwitch(const QJsonObject &doc)
{
    const QJsonValue platform = doc.value(QStringLiteral("platform"));
    return platform.isUndefined() || platform.toString().compare(QLatin1String("twitch"), Qt::CaseInsensitive) == 0;
}

bool namesUser(const QJsonObject &doc)
{
    if (doc.contains(QStringLiteral("userId")))
        return true;
    for (const QString &field : kLoginFields) {
        if (doc.contains(field))
            return true;
    }
    return false;
}

enum class Merge { Added, Replaced, Unchanged, Kept, Overwritten };

void count(ImportCount *counter, Merge merge)
{
    switch (merge) {
    case Merge::Added: ++counter->added; break;
    case Merge::Replaced: ++counter->replaced; break;
    case Merge::Unchanged: ++counter->unchanged; break;
    case Merge::Kept: ++counter->kept; break;
    case Merge::Overwritten: ++counter->overwritten; break;
    }
}

// Ein Wert aus Streamer.bot trifft auf den Stand im Studio (Plan 9.4)
Merge mergeValue(std::optional<VariableValue> *target, const VariableValue &imported, bool overwrite)
{
    if (!target->has_value()) {
        *target = imported;
        return Merge::Added;
    }
    if ((*target)->value == imported.value)
        return Merge::Unchanged;
    if ((*target)->origin == VariableOrigin::Studio) {
        if (!overwrite)
            return Merge::Kept;
        *target = imported;
        return Merge::Overwritten;
    }
    *target = imported;
    return Merge::Replaced;
}

Merge mergeIntoMap(QMap<QString, VariableValue> *map, const QString &name, const VariableValue &imported,
                   bool overwrite)
{
    std::optional<VariableValue> slot;
    if (map->contains(name))
        slot = map->value(name);
    const Merge merge = mergeValue(&slot, imported, overwrite);
    map->insert(name, *slot);
    return merge;
}

class Planner
{
public:
    Planner(const StreamerbotExport &source, const ImportState &current, const ImportOptions &options)
        : m_source(source), m_current(current), m_options(options)
    {
    }

    ImportResult run()
    {
        m_report.skipped = m_source.skipped;
        for (auto it = m_current.viewers.viewers.cbegin(); it != m_current.viewers.viewers.cend(); ++it)
            m_loginToId.insert(it->login.toLower(), it.key());
        readUsersFile();
        readTwitchViewers();
        for (const ExportCollection &c : m_source.collections)
            readCollection(c);
        readQuotesFile();
        for (auto it = m_source.otherJson.cbegin(); it != m_source.otherJson.cend(); ++it)
            m_archiveFields[it.key()] = it.value();
        return merge();
    }

private:
    void toArchive(const QString &file, const QString &collection, const QJsonValue &doc)
    {
        m_archiveDocs[file][collection].append(doc);
        ++m_report.archived[collectionKey(file, collection)];
    }

    void wholeToArchive(const ExportCollection &c)
    {
        QJsonArray &docs = m_archiveDocs[c.file][c.collection];
        for (const QJsonValue &doc : c.documents)
            docs.append(doc);
        m_report.archived[collectionKey(c.file, c.collection)] += int(c.documents.size());
    }

    // Twitch-ID aus userId, sonst über den Login (Zuschauer aus users.dat und twitch_data.db)
    QString resolveUser(const QJsonObject &doc) const
    {
        const QString userId = doc.value(QStringLiteral("userId")).toString().trimmed();
        if (!userId.isEmpty())
            return userId;
        for (const QString &field : kLoginFields) {
            const QString login = doc.value(field).toString().trimmed().toLower();
            if (!login.isEmpty())
                return m_loginToId.value(login);
        }
        return {};
    }

    void noteImportedViewer(const QString &id, const Viewer &viewer)
    {
        m_viewers.insert(id, viewer);
        m_loginToId.insert(viewer.login, id);
    }

    // users.dat: Zuschauer (users) und Benutzergruppen (groups)
    void readUsersFile()
    {
        if (!m_source.users)
            return;
        const QString file = QStringLiteral("users.dat");
        QJsonObject rest = *m_source.users;
        const QJsonValue users = rest.take(QStringLiteral("users"));
        const QJsonValue groups = rest.take(QStringLiteral("groups"));
        if (!rest.isEmpty())
            m_archiveFields[file] = rest;
        if (!users.isUndefined() && !users.isArray())
            m_archiveFields[file].insert(QStringLiteral("users"), users);
        if (!groups.isUndefined() && !groups.isArray())
            m_archiveFields[file].insert(QStringLiteral("groups"), groups);

        QHash<QString, QJsonObject> docs; // Twitch-ID → Eintrag, für doppelte
        for (const QJsonValue &value : users.toArray()) {
            const QJsonObject doc = value.toObject();
            const QString type = doc.value(QStringLiteral("type")).toString();
            ++m_report.userTypes[type.isEmpty() ? QStringLiteral("(leer)") : type];
            const QString id = doc.value(QStringLiteral("id")).toString().trimmed();
            const QString login = doc.value(QStringLiteral("name")).toString().trimmed().toLower();
            if (!value.isObject() || type.compare(QLatin1String("twitch"), Qt::CaseInsensitive) != 0 || id.isEmpty()
                || login.isEmpty()) {
                toArchive(file, QStringLiteral("users"), value);
                continue;
            }
            if (docs.contains(id)) {
                ++m_report.duplicates;
                const QJsonObject &other = docs[id];
                const QDateTime mine = parseTime(doc.value(QStringLiteral("lastActive")), QDateTime());
                const QDateTime theirs = parseTime(other.value(QStringLiteral("lastActive")), QDateTime());
                if (!(mine > theirs)) {
                    toArchive(file, QStringLiteral("users"), doc);
                    continue;
                }
                toArchive(file, QStringLiteral("users"), other);
            }
            docs.insert(id, doc);
            QJsonObject extra = doc;
            extra.remove(QStringLiteral("id"));
            extra.remove(QStringLiteral("name"));
            const QString display = extra.take(QStringLiteral("display")).toString();
            noteImportedViewer(id, Viewer{login, display, extra});
        }

        for (const QJsonValue &value : groups.toArray()) {
            const QJsonObject doc = value.toObject();
            const QUuid id = QUuid::fromString(doc.value(QStringLiteral("id")).toString());
            const QString name = doc.value(QStringLiteral("name")).toString().trimmed();
            const QJsonValue userIds = doc.value(QStringLiteral("userIds"));
            bool valid = value.isObject() && !id.isNull() && !name.isEmpty() && userIds.isArray();
            UserGroup group{id, name, {}, {}};
            for (const QJsonValue &user : userIds.toArray()) {
                if (!user.isString() || user.toString().trimmed().isEmpty())
                    valid = false;
                else
                    group.users.append(UserRef{user.toString().trimmed(), {}});
            }
            if (!valid) {
                toArchive(file, QStringLiteral("groups"), value);
                continue;
            }
            group.extra = doc;
            group.extra.remove(QStringLiteral("id"));
            group.extra.remove(QStringLiteral("name"));
            group.extra.remove(QStringLiteral("userIds"));
            m_groups.append(group);
        }
    }

    // twitch_data.db: Login und Anzeigename aus Sammlungen mit userId und userLogin (zum Beispiel
    // subscriptionHistory, giftBombHistory). Die Sammlungen selbst gehen unverändert ins Archiv.
    void readTwitchViewers()
    {
        static const QList<std::array<QString, 3>> kFields = {
            {QStringLiteral("userId"), QStringLiteral("userLogin"), QStringLiteral("userName")},
            {QStringLiteral("gifterUserId"), QStringLiteral("gifterUserLogin"), QStringLiteral("gifterUserName")}};
        for (const ExportCollection &c : m_source.collections) {
            if (c.file != QLatin1String("twitch_data.db"))
                continue;
            for (const QJsonValue &value : c.documents) {
                const QJsonObject doc = value.toObject();
                for (const auto &[idField, loginField, nameField] : kFields) {
                    const QString id = doc.value(idField).toString().trimmed();
                    const QString login = doc.value(loginField).toString().trimmed().toLower();
                    if (id.isEmpty() || login.isEmpty() || m_viewers.contains(id))
                        continue;
                    noteImportedViewer(id, Viewer{login, doc.value(nameField).toString(), {}});
                }
            }
        }
    }

    void readCollection(const ExportCollection &c)
    {
        if (c.documents.isEmpty()) {
            m_report.emptyCollections.append(collectionKey(c.file, c.collection));
            return;
        }
        if (c.file == QLatin1String("globals.db") && c.collection == QLatin1String("globals"))
            readGlobals(c);
        else if (c.file == QLatin1String("globals.db") && c.collection == QLatin1String("user_globals"))
            readUserGlobals(c);
        else if (c.file == QLatin1String("commands.db") && c.collection == QLatin1String("commandUsage"))
            readCommandUsage(c);
        else if (c.file == QLatin1String("firstwords.db") || c.file == QLatin1String("pronouns.db"))
            readPrefixed(c);
        else
            wholeToArchive(c);
    }

    // globals.db/globals: dauerhafte globale Variablen; Name in _id
    void readGlobals(const ExportCollection &c)
    {
        for (const QJsonValue &value : c.documents) {
            const QJsonObject doc = value.toObject();
            const QString name = doc.value(QStringLiteral("_id")).toString();
            const std::optional<QJsonValue> simple = simpleValue(doc.value(QStringLiteral("value")));
            if (name.trimmed().isEmpty() || !simple) {
                toArchive(c.file, c.collection, value);
                continue;
            }
            m_globals.insert(name, VariableValue{*simple, VariableOrigin::Streamerbot,
                                                 parseTime(doc.value(QStringLiteral("lastWrite")), m_options.now)});
        }
    }

    // globals.db/user_globals: dauerhafte Variablen je Zuschauer
    void readUserGlobals(const ExportCollection &c)
    {
        QHash<QString, QJsonObject> docs; // ID + Name → Dokument, für doppelte
        for (const QJsonValue &value : c.documents) {
            const QJsonObject doc = value.toObject();
            if (!isTwitch(doc)) {
                toArchive(c.file, c.collection, value);
                continue;
            }
            const QString userId = resolveUser(doc);
            if (userId.isEmpty()) {
                ++m_report.withoutId;
                toArchive(c.file, c.collection, value);
                continue;
            }
            const QString name = doc.value(QStringLiteral("name")).toString();
            const std::optional<QJsonValue> simple = simpleValue(doc.value(QStringLiteral("value")));
            if (name.trimmed().isEmpty() || !simple) {
                toArchive(c.file, c.collection, value);
                continue;
            }
            const VariableValue imported{*simple, VariableOrigin::Streamerbot,
                                         parseTime(doc.value(QStringLiteral("lastWrite")), m_options.now)};
            VariableMap &vars = m_users[userId];
            const QString docKey = userId + QLatin1Char('\n') + name;
            if (vars.contains(name)) {
                ++m_report.duplicates;
                if (!(imported.changedAt > vars.value(name).changedAt)) {
                    toArchive(c.file, c.collection, value);
                    continue;
                }
                toArchive(c.file, c.collection, docs.value(docKey));
            }
            docs.insert(docKey, doc);
            vars.insert(name, imported);
        }
    }

    // commands.db/commandUsage: je Benutzung ein Dokument. Zähler über alle und je Twitch-Zuschauer
    // nach command_counters; die Liste selbst bleibt unverändert im Archiv (Zeitpunkte).
    void readCommandUsage(const ExportCollection &c)
    {
        struct Tally
        {
            qint64 count = 0;
            QDateTime last;
        };
        QMap<QUuid, Tally> counters;
        QMap<QUuid, QMap<QString, Tally>> userCounters;
        auto note = [](Tally *tally, const QDateTime &at) {
            ++tally->count;
            if (!tally->last.isValid() || at > tally->last)
                tally->last = at;
        };
        for (const QJsonValue &value : c.documents) {
            const QJsonObject doc = value.toObject();
            const QUuid id = QUuid::fromString(doc.value(QStringLiteral("commandId")).toString());
            if (id.isNull())
                continue;
            const QDateTime at = parseTime(doc.value(QStringLiteral("usedAt")), m_options.now);
            note(&counters[id], at);
            if (!isTwitch(doc) || !namesUser(doc))
                continue;
            const QString userId = resolveUser(doc);
            if (userId.isEmpty())
                ++m_report.withoutId;
            else
                note(&userCounters[id][userId], at);
        }
        for (auto it = counters.cbegin(); it != counters.cend(); ++it) {
            CommandCounter &counter = m_counters[it.key()];
            counter.counter = VariableValue{QJsonValue(double(it->count)), VariableOrigin::Streamerbot, it->last};
            const QMap<QString, Tally> users = userCounters.value(it.key());
            for (auto user = users.cbegin(); user != users.cend(); ++user)
                counter.userCounters.insert(user.key(), VariableValue{QJsonValue(double(user->count)),
                                                                      VariableOrigin::Streamerbot, user->last});
        }
        wholeToArchive(c);
    }

    // firstwords.db, pronouns.db: einfache Werte je Twitch-Zuschauer als Variablen mit Vorsilbe der
    // Datei (firstwords.<Feld>), alles andere ins Archiv (Plan 9.3)
    void readPrefixed(const ExportCollection &c)
    {
        static const QStringList kSkip = {QStringLiteral("_id"), QStringLiteral("userId"), QStringLiteral("platform"),
                                          QStringLiteral("userLogin"), QStringLiteral("login"), QStringLiteral("user"),
                                          QStringLiteral("userName")};
        const QString prefix = c.file.chopped(3) + QLatin1Char('.'); // ohne «.db»
        for (const QJsonValue &value : c.documents) {
            const QJsonObject doc = value.toObject();
            if (!namesUser(doc) || !isTwitch(doc)) {
                toArchive(c.file, c.collection, value);
                continue;
            }
            const QString userId = resolveUser(doc);
            if (userId.isEmpty()) {
                ++m_report.withoutId;
                toArchive(c.file, c.collection, value);
                continue;
            }
            bool allSimple = true;
            VariableMap &vars = m_users[userId];
            for (auto it = doc.begin(); it != doc.end(); ++it) {
                if (kSkip.contains(it.key()))
                    continue;
                const std::optional<QJsonValue> simple = simpleValue(it.value());
                if (!simple) {
                    allSimple = false;
                    continue;
                }
                const QString name = prefix + it.key();
                if (vars.contains(name))
                    ++m_report.duplicates;
                vars.insert(name, VariableValue{*simple, VariableOrigin::Streamerbot, m_options.now});
            }
            if (!allSimple)
                toArchive(c.file, c.collection, value);
        }
    }

    void readQuotesFile()
    {
        if (!m_source.quotes)
            return;
        const QString file = QStringLiteral("quotes.dat");
        QJsonObject rest = *m_source.quotes;
        const QJsonValue quotes = rest.take(QStringLiteral("quotes"));
        if (!quotes.isUndefined() && !quotes.isArray())
            rest.insert(QStringLiteral("quotes"), quotes);
        if (!rest.isEmpty())
            m_archiveFields[file] = rest;
        for (const QJsonValue &value : quotes.toArray()) {
            if (value.isObject())
                m_quotes.append(value.toObject());
            else
                toArchive(file, QStringLiteral("quotes"), value);
        }
    }

    ImportResult merge()
    {
        ImportResult result;
        result.state = m_current;
        ImportState &state = result.state;
        const bool overwrite = m_options.overwrite;

        for (auto it = m_globals.cbegin(); it != m_globals.cend(); ++it) {
            m_report.globalVariableNames.append(it.key());
            noteMerge(&m_report.globalVariables, it.key(),
                      mergeIntoMap(&state.variables.global, it.key(), *it, overwrite));
        }
        for (auto user = m_users.cbegin(); user != m_users.cend(); ++user) {
            if (user->isEmpty())
                continue;
            VariableMap &target = state.variables.users[user.key()];
            for (auto it = user->cbegin(); it != user->cend(); ++it) {
                ++m_report.userVariableNames[it.key()];
                noteMerge(&m_report.userVariables, it.key(), mergeIntoMap(&target, it.key(), *it, overwrite));
            }
        }
        for (auto it = m_counters.cbegin(); it != m_counters.cend(); ++it) {
            CommandCounter &target = state.variables.commandCounters[it.key()];
            if (it->counter)
                count(&m_report.commandCounters, mergeValue(&target.counter, *it->counter, overwrite));
            for (auto user = it->userCounters.cbegin(); user != it->userCounters.cend(); ++user)
                count(&m_report.commandCounters, mergeIntoMap(&target.userCounters, user.key(), *user, overwrite));
        }

        // Zuschauer: Login und Anzeigename aus dem Studio (Chat, neuer) bleiben; weitere Felder aus
        // Streamer.bot kommen dazu oder ersetzen gleichnamige
        for (auto it = m_viewers.cbegin(); it != m_viewers.cend(); ++it) {
            auto existing = state.viewers.viewers.find(it.key());
            if (existing == state.viewers.viewers.end()) {
                state.viewers.viewers.insert(it.key(), *it);
                ++m_report.viewers.added;
                continue;
            }
            Viewer merged = *existing;
            if (merged.displayName.isEmpty())
                merged.displayName = it->displayName;
            for (auto field = it->extra.begin(); field != it->extra.end(); ++field)
                merged.extra.insert(field.key(), field.value());
            if (merged == *existing) {
                ++m_report.viewers.unchanged;
            } else {
                *existing = merged;
                ++m_report.viewers.replaced;
            }
        }

        // Zitate: gleiche «id» ersetzt, sonst angehängt
        for (const QJsonObject &quote : m_quotes) {
            QList<QJsonObject> &target = state.quotes.quotes;
            const QJsonValue id = quote.value(QStringLiteral("id"));
            qsizetype found = -1;
            for (qsizetype i = 0; i < target.size(); ++i) {
                if (id.isUndefined() ? target[i] == quote : target[i].value(QStringLiteral("id")) == id) {
                    found = i;
                    break;
                }
            }
            if (found < 0) {
                target.append(quote);
                ++m_report.quotes.added;
            } else if (target[found] == quote) {
                ++m_report.quotes.unchanged;
            } else {
                target[found] = quote;
                ++m_report.quotes.replaced;
            }
        }

        // Benutzergruppen: gleiche ID ersetzt, sonst angehängt
        if (!state.actions) {
            m_report.groupsSkipped = !m_groups.isEmpty();
        } else {
            for (const UserGroup &group : m_groups) {
                QList<UserGroup> &target = state.actions->groups;
                auto existing = std::find_if(target.begin(), target.end(),
                                             [&](const UserGroup &g) { return g.id == group.id; });
                if (existing == target.end()) {
                    target.append(group);
                    ++m_report.groups.added;
                } else if (*existing == group) {
                    ++m_report.groups.unchanged;
                } else {
                    *existing = group;
                    ++m_report.groups.replaced;
                }
            }
        }

        // Archiv: je Datei und Sammlung ersetzt; Dateien und Sammlungen nur im Studio bleiben
        for (auto file = m_archiveFields.cbegin(); file != m_archiveFields.cend(); ++file) {
            QJsonObject &target = state.archive.files[file.key()];
            for (auto it = file->begin(); it != file->end(); ++it)
                target.insert(it.key(), it.value());
        }
        for (auto file = m_archiveDocs.cbegin(); file != m_archiveDocs.cend(); ++file) {
            QJsonObject &target = state.archive.files[file.key()];
            for (auto it = file->cbegin(); it != file->cend(); ++it)
                target.insert(it.key(), *it);
        }

        result.report = m_report;
        return result;
    }

    void noteMerge(ImportCount *counter, const QString &name, Merge merge)
    {
        count(counter, merge);
        if (merge == Merge::Kept)
            ++m_report.keptVariableNames[name];
        else if (merge == Merge::Overwritten)
            ++m_report.overwrittenVariableNames[name];
    }

    const StreamerbotExport &m_source;
    const ImportState &m_current;
    const ImportOptions &m_options;
    ImportReport m_report;
    QHash<QString, QString> m_loginToId; // Login (Kleinbuchstaben) → Twitch-ID

    VariableMap m_globals;
    QMap<QString, VariableMap> m_users;
    QMap<QUuid, CommandCounter> m_counters;
    QMap<QString, Viewer> m_viewers;
    QList<QJsonObject> m_quotes;
    QList<UserGroup> m_groups;
    QMap<QString, QMap<QString, QJsonArray>> m_archiveDocs; // Datei → Sammlung → Dokumente
    QMap<QString, QJsonObject> m_archiveFields;             // Datei → übrige Felder (JSON-Texte)
};

template <typename T>
bool loadFile(const QDir &dir, const QString &name, bool (*parse)(const QByteArray &, T *, QString *), T *out,
              bool *present, QString *error)
{
    QFile file(dir.filePath(name));
    *present = file.exists();
    if (!*present)
        return true;
    if (!file.open(QIODevice::ReadOnly)) {
        *error = QStringLiteral("%1: nicht lesbar").arg(name);
        return false;
    }
    QString parseError;
    if (!parse(file.readAll(), out, &parseError)) {
        *error = QStringLiteral("%1: %2").arg(name, parseError);
        return false;
    }
    return true;
}

QString countLine(const QString &label, const ImportCount &c)
{
    QString line = QStringLiteral("%1: %2 neu, %3 ersetzt, %4 gleich").arg(label).arg(c.added).arg(c.replaced).arg(c.unchanged);
    if (c.kept > 0)
        line += QStringLiteral(", %1 im Studio geändert (bleiben)").arg(c.kept);
    if (c.overwritten > 0)
        line += QStringLiteral(", %1 im Studio geändert (überschrieben)").arg(c.overwritten);
    return line;
}

QString nameCounts(const QMap<QString, int> &names, const QString &unit)
{
    QStringList parts;
    for (auto it = names.cbegin(); it != names.cend(); ++it)
        parts.append(QStringLiteral("%1 (%2 %3)").arg(it.key()).arg(it.value()).arg(unit));
    return parts.join(QStringLiteral(", "));
}

} // namespace

bool readStreamerbotExport(const QString &dir, StreamerbotExport *out, QString *error)
{
    const QDir root(dir);
    if (!root.exists()) {
        *error = QStringLiteral("Ordner des Exports fehlt");
        return false;
    }
    StreamerbotExport result;
    const QFileInfoList entries = root.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.fileName();
        if (isAuthName(name)) {
            result.skipped.append(name); // nie gelesen (Plan 9.1)
            continue;
        }
        if (entry.isDir()) {
            const QString file = name + QStringLiteral(".db");
            const QDir sub(entry.absoluteFilePath());
            const QFileInfoList parts = sub.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &part : parts) {
                const QString label = name + QLatin1Char('/') + part.fileName();
                if (!part.isFile() || part.suffix() != QLatin1String("json")) {
                    result.skipped.append(label);
                    continue;
                }
                QJsonDocument doc;
                if (!readJsonFile(part.absoluteFilePath(), label, &doc, error))
                    return false;
                if (!doc.isArray()) {
                    *error = QStringLiteral("%1: muss ein Array der Dokumente sein").arg(label);
                    return false;
                }
                result.collections.append({file, part.fileName().chopped(5), doc.array()});
            }
        } else if (entry.suffix() == QLatin1String("json")) {
            QJsonDocument doc;
            if (!readJsonFile(entry.absoluteFilePath(), name, &doc, error))
                return false;
            if (!doc.isObject()) {
                *error = QStringLiteral("%1: muss ein JSON-Objekt sein").arg(name);
                return false;
            }
            const QString base = name.chopped(5);
            if (base == QLatin1String("quotes"))
                result.quotes = doc.object();
            else if (base == QLatin1String("users"))
                result.users = doc.object();
            else
                result.otherJson.insert(base + QStringLiteral(".dat"), doc.object());
        } else {
            result.skipped.append(name);
        }
    }
    *out = result;
    return true;
}

bool loadImportState(const QString &dataDir, ImportState *state, QString *error)
{
    const QDir dir(dataDir);
    ImportState result;
    bool present = false;
    if (!loadFile(dir, QString::fromLatin1(kVariablesFileName), &parseVariables, &result.variables, &present, error)
        || !loadFile(dir, QString::fromLatin1(kViewersFileName), &parseViewers, &result.viewers, &present, error)
        || !loadFile(dir, QString::fromLatin1(kQuotesFileName), &parseQuotes, &result.quotes, &present, error)
        || !loadFile(dir, QString::fromLatin1(kArchiveFileName), &parseArchive, &result.archive, &present, error))
        return false;
    ActionsConfig actions;
    if (!loadFile(dir, QString::fromLatin1(kActionsFileName), &parseActions, &actions, &present, error))
        return false;
    if (present)
        result.actions = actions;
    *state = result;
    return true;
}

ImportResult planViewerImport(const StreamerbotExport &source, const ImportState &current,
                              const ImportOptions &options)
{
    return Planner(source, current, options).run();
}

QString importReportText(const ImportReport &r, bool final)
{
    QStringList lines;
    lines.append(final ? QStringLiteral("Import aus Streamer.bot: endgültig übernommen.")
                       : QStringLiteral("Import aus Streamer.bot: Probelauf, nichts geschrieben."));
    lines.append(countLine(QStringLiteral("Variablen für alle"), r.globalVariables));
    if (!r.globalVariableNames.isEmpty())
        lines.append(QStringLiteral("  Namen: ") + r.globalVariableNames.join(QStringLiteral(", ")));
    lines.append(countLine(QStringLiteral("Variablen je Zuschauer"), r.userVariables));
    if (!r.userVariableNames.isEmpty())
        lines.append(QStringLiteral("  Namen: ") + nameCounts(r.userVariableNames, QStringLiteral("Zuschauer")));
    lines.append(countLine(QStringLiteral("Zähler der Befehle"), r.commandCounters));
    lines.append(countLine(QStringLiteral("Zuschauer"), r.viewers));
    lines.append(countLine(QStringLiteral("Zitate"), r.quotes));
    if (r.groupsSkipped)
        lines.append(QStringLiteral("Benutzergruppen: nicht übernommen, aktionen.json fehlt"));
    else
        lines.append(countLine(QStringLiteral("Benutzergruppen"), r.groups));
    if (!r.keptVariableNames.isEmpty())
        lines.append(QStringLiteral("Im Studio geändert, bleiben (ersetzen mit --ueberschreiben): ")
                     + nameCounts(r.keptVariableNames, QStringLiteral("Werte")));
    if (!r.overwrittenVariableNames.isEmpty())
        lines.append(QStringLiteral("Im Studio geändert, überschrieben: ")
                     + nameCounts(r.overwrittenVariableNames, QStringLiteral("Werte")));
    lines.append(QStringLiteral("Ohne Twitch-ID (ins Archiv): %1").arg(r.withoutId));
    lines.append(QStringLiteral("Doppelte Einträge (der neuere gilt): %1").arg(r.duplicates));
    if (!r.userTypes.isEmpty())
        lines.append(QStringLiteral("users.dat nach Art: ") + nameCounts(r.userTypes, QStringLiteral("Einträge")));
    for (auto it = r.archived.cbegin(); it != r.archived.cend(); ++it)
        lines.append(QStringLiteral("Ins Archiv: %1: %2 Dokumente").arg(it.key()).arg(it.value()));
    lines.append(QStringLiteral("Leere Sammlungen: %1").arg(r.emptyCollections.size()));
    if (!r.skipped.isEmpty())
        lines.append(QStringLiteral("Nicht gelesen: ") + r.skipped.join(QStringLiteral(", ")));
    return lines.join(QLatin1Char('\n'));
}

QString importBackupDirName(const QDateTime &now)
{
    return QStringLiteral("sicherung-") + now.toUTC().toString(QStringLiteral("yyyyMMdd-HHmmss"));
}

bool writeViewerImport(const QString &dataDir, const QString &backupDir, const ImportState &before,
                       const ImportState &after, QStringList *written, QString *error)
{
    struct Output
    {
        QString name;
        QByteArray bytes;
    };
    QList<Output> outputs;
    if (after.variables != before.variables)
        outputs.append({QString::fromLatin1(kVariablesFileName), serializeVariables(after.variables)});
    if (after.viewers != before.viewers)
        outputs.append({QString::fromLatin1(kViewersFileName), serializeViewers(after.viewers)});
    if (after.quotes != before.quotes)
        outputs.append({QString::fromLatin1(kQuotesFileName), serializeQuotes(after.quotes)});
    if (after.archive != before.archive)
        outputs.append({QString::fromLatin1(kArchiveFileName), serializeArchive(after.archive)});
    if (after.actions && after.actions != before.actions)
        outputs.append({QString::fromLatin1(kActionsFileName), serializeActions(*after.actions)});
    written->clear();
    if (outputs.isEmpty())
        return true;

    // Sicherung zuerst; misslingt sie, wird nichts geschrieben
    if (QFileInfo::exists(backupDir)) {
        *error = QStringLiteral("Ordner der Sicherung besteht schon");
        return false;
    }
    const QDir data(dataDir);
    if (!QDir().mkpath(backupDir) || !QDir().mkpath(dataDir)) {
        *error = QStringLiteral("Ordner der Sicherung oder Datenordner nicht anlegbar");
        return false;
    }
    const QDir backup(backupDir);
    for (const Output &output : outputs) {
        const QString source = data.filePath(output.name);
        if (QFile::exists(source) && !QFile::copy(source, backup.filePath(output.name))) {
            *error = QStringLiteral("%1: Sicherung misslungen").arg(output.name);
            return false;
        }
    }
    for (const Output &output : outputs) {
        QSaveFile file(data.filePath(output.name));
        if (!file.open(QIODevice::WriteOnly) || file.write(output.bytes) != output.bytes.size() || !file.commit()) {
            *error = QStringLiteral("%1: Schreiben misslungen").arg(output.name);
            return false;
        }
        written->append(output.name);
    }
    return true;
}

} // namespace dss::actions
