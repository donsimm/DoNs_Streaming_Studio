#include "emotes.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

#include <algorithm>

namespace dss {

namespace {

// Zero-Width bei BTTV: Die API kennzeichnet es nicht; feste Liste nur für globale Emotes wie
// Chatterino (https://raw.githubusercontent.com/Chatterino/chatterino2/master/src/providers/bttv/BttvEmotes.cpp)
const QSet<QString> &bttvZeroWidthNames()
{
    static const QSet<QString> names{
        QStringLiteral("SoSnowy"),   QStringLiteral("IceCold"),   QStringLiteral("SantaHat"),
        QStringLiteral("TopHat"),    QStringLiteral("ReinDeer"),  QStringLiteral("CandyCane"),
        QStringLiteral("cvMask"),    QStringLiteral("cvHazmat"),
    };
    return names;
}

// IDs gehen unverändert in den Pfad der Bild-Adresse: nur Buchstaben, Ziffern, «_» und «-»
// (BTTV hexadezimal, 7TV ULID, FFZ Zahl, Twitch z. B. «emotesv2_…»).
bool isSafeId(const QString &id)
{
    if (id.isEmpty() || id.size() > 128)
        return false;
    for (const QChar c : id) {
        const char16_t u = c.unicode();
        const bool ok = (u >= u'a' && u <= u'z') || (u >= u'A' && u <= u'Z') || (u >= u'0' && u <= u'9')
            || u == u'_' || u == u'-';
        if (!ok)
            return false;
    }
    return true;
}

bool isDigits(const QString &text)
{
    if (text.isEmpty() || text.size() > 20)
        return false;
    for (const QChar c : text) {
        if (c < u'0' || c > u'9')
            return false;
    }
    return true;
}

// Ganze Zahl > 0, sonst 0
int positiveInt(const QJsonValue &value)
{
    if (!value.isDouble())
        return 0;
    const double d = value.toDouble();
    if (d < 1 || d > 100000 || d != static_cast<int>(d))
        return 0;
    return static_cast<int>(d);
}

QString listLabel(EmoteProvider provider, bool channel)
{
    return channel ? QStringLiteral("Liste des Kanals von %1").arg(emoteProviderName(provider))
                   : QStringLiteral("Globale Liste von %1").arg(emoteProviderName(provider));
}

// --- BTTV (Recherche 2) ---

void readBttvEmotes(const QJsonArray &array, bool global, QList<Emote> *out)
{
    for (const QJsonValue &value : array) {
        const QJsonObject o = value.toObject();
        const QString id = o.value(QStringLiteral("id")).toString();
        const QString code = o.value(QStringLiteral("code")).toString();
        if (!isSafeId(id) || code.isEmpty())
            continue;
        if (o.value(QStringLiteral("modifier")).toBool())
            continue;
        Emote e;
        e.provider = EmoteProvider::Bttv;
        e.id = id;
        e.name = code;
        e.animated = o.value(QStringLiteral("animated")).toBool();
        e.zeroWidth = global && bttvZeroWidthNames().contains(code);
        e.width = positiveInt(o.value(QStringLiteral("width")));
        e.height = positiveInt(o.value(QStringLiteral("height")));
        out->append(e);
    }
}

bool parseBttv(const QJsonDocument &doc, bool channel, QList<Emote> *out)
{
    if (!channel) {
        if (!doc.isArray())
            return false;
        readBttvEmotes(doc.array(), true, out);
        return true;
    }
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    const QJsonValue own = root.value(QStringLiteral("channelEmotes"));
    const QJsonValue shared = root.value(QStringLiteral("sharedEmotes"));
    if (!own.isArray() && !shared.isArray())
        return false;
    readBttvEmotes(own.toArray(), false, out);
    readBttvEmotes(shared.toArray(), false, out);
    return true;
}

// --- 7TV, REST v3 (Recherche 3). v3 könnte durch v4 ersetzt werden: der Zugriff auf die Felder
// steht nur hier. Quelle der Felder: direkte Abrufe vom 2026-11-16 (tests/daten/emotes/) und
// https://raw.githubusercontent.com/Chatterino/chatterino2/master/src/providers/seventv/SeventvEmotes.cpp

// Ein Emote-Set (`emotes[]`); null oder fehlend heisst leeres Set.
bool parseSevenTvV3Set(const QJsonObject &set, QList<Emote> *out)
{
    const QJsonValue emotes = set.value(QStringLiteral("emotes"));
    if (!emotes.isArray() && !emotes.isNull() && !emotes.isUndefined())
        return false;
    for (const QJsonValue &value : emotes.toArray()) {
        const QJsonObject active = value.toObject();
        const QJsonObject data = active.value(QStringLiteral("data")).toObject();
        const QString id = data.value(QStringLiteral("id")).toString();
        const QString name = active.value(QStringLiteral("name")).toString(); // Name im Set
        if (!isSafeId(id) || name.isEmpty())
            continue;
        // Nur WebP (wie Chatterino); Grösse aus der kleinsten Stufe
        int width = 0;
        int height = 0;
        bool hasWebp = false;
        const QJsonArray files = data.value(QStringLiteral("host")).toObject().value(QStringLiteral("files")).toArray();
        for (const QJsonValue &f : files) {
            const QJsonObject file = f.toObject();
            if (file.value(QStringLiteral("format")).toString() != QLatin1String("WEBP"))
                continue;
            const int h = positiveInt(file.value(QStringLiteral("height")));
            if (!hasWebp || (h > 0 && (height == 0 || h < height))) {
                width = positiveInt(file.value(QStringLiteral("width")));
                height = h;
            }
            hasWebp = true;
        }
        if (!hasWebp)
            continue;
        Emote e;
        e.provider = EmoteProvider::SevenTv;
        e.id = id;
        e.name = name;
        e.animated = data.value(QStringLiteral("animated")).toBool();
        // ActiveEmoteFlag ZeroWidth = 1 << 0 (flags im Set, nicht data.flags)
        e.zeroWidth = (active.value(QStringLiteral("flags")).toInteger() & 1) != 0;
        e.width = width;
        e.height = height;
        out->append(e);
    }
    return true;
}

bool parseSevenTv(const QJsonDocument &doc, bool channel, QList<Emote> *out)
{
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    if (!channel)
        return root.contains(QStringLiteral("emotes")) && parseSevenTvV3Set(root, out);
    // Benutzer: emote_set null heisst kein aktives Set
    const QJsonValue set = root.value(QStringLiteral("emote_set"));
    if (set.isNull())
        return true;
    if (!set.isObject())
        return false;
    return parseSevenTvV3Set(set.toObject(), out);
}

// --- FFZ, API v1 (Recherche 4) ---

QString ffzId(const QJsonValue &value)
{
    if (value.isString())
        return value.toString();
    if (value.isDouble() && value.toDouble() >= 0 && value.toDouble() == static_cast<qint64>(value.toDouble()))
        return QString::number(static_cast<qint64>(value.toDouble()));
    return QString();
}

void readFfzSet(const QJsonObject &set, QList<Emote> *out)
{
    for (const QJsonValue &value : set.value(QStringLiteral("emoticons")).toArray()) {
        const QJsonObject o = value.toObject();
        const QString id = ffzId(o.value(QStringLiteral("id")));
        const QString name = o.value(QStringLiteral("name")).toString();
        if (!isSafeId(id) || name.isEmpty())
            continue;
        if (o.value(QStringLiteral("modifier")).toBool() || o.value(QStringLiteral("hidden")).toBool())
            continue;
        Emote e;
        e.provider = EmoteProvider::Ffz;
        e.id = id;
        e.name = name;
        // `animated` gibt es nur bei animierten Emotes, mit denselben Stufen wie `urls`
        const QJsonValue animated = o.value(QStringLiteral("animated"));
        e.animated = animated.isObject() && !animated.toObject().isEmpty();
        e.width = positiveInt(o.value(QStringLiteral("width")));
        e.height = positiveInt(o.value(QStringLiteral("height")));
        out->append(e);
    }
}

bool parseFfz(const QJsonDocument &doc, bool channel, QList<Emote> *out)
{
    if (!doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    const QJsonValue setsValue = root.value(QStringLiteral("sets"));
    if (!setsValue.isObject())
        return false;
    const QJsonObject sets = setsValue.toObject();
    QStringList setIds;
    if (!channel) {
        // Nur die Sets aus default_sets (wie Chatterino, FfzEmotes.cpp)
        const QJsonValue defaults = root.value(QStringLiteral("default_sets"));
        if (!defaults.isArray())
            return false;
        for (const QJsonValue &v : defaults.toArray()) {
            const QString id = ffzId(v);
            if (!id.isEmpty())
                setIds.append(id);
        }
    } else {
        if (!root.value(QStringLiteral("room")).isObject())
            return false;
        // Set des Raums; fehlt es, alle Sets der Antwort
        const QString roomSet = ffzId(root.value(QStringLiteral("room")).toObject().value(QStringLiteral("set")));
        if (!roomSet.isEmpty() && sets.contains(roomSet))
            setIds.append(roomSet);
        else
            setIds = sets.keys();
    }
    for (const QString &id : std::as_const(setIds))
        readFfzSet(sets.value(id).toObject(), out);
    return true;
}

} // namespace

QString emoteProviderName(EmoteProvider provider)
{
    switch (provider) {
    case EmoteProvider::Twitch:
        return QStringLiteral("Twitch");
    case EmoteProvider::Bttv:
        return QStringLiteral("BTTV");
    case EmoteProvider::SevenTv:
        return QStringLiteral("7TV");
    case EmoteProvider::Ffz:
        return QStringLiteral("FFZ");
    }
    return QString();
}

QUrl emoteListUrl(EmoteProvider provider, std::optional<QString> channelId)
{
    // Adressen: Recherche 2–4 (direkte Abrufe vom 2026-11-16)
    if (channelId && !isDigits(*channelId))
        return QUrl();
    switch (provider) {
    case EmoteProvider::Twitch:
        return QUrl();
    case EmoteProvider::Bttv:
        return channelId ? QUrl(QStringLiteral("https://api.betterttv.net/3/cached/users/twitch/%1").arg(*channelId))
                         : QUrl(QStringLiteral("https://api.betterttv.net/3/cached/emotes/global"));
    case EmoteProvider::SevenTv:
        return channelId ? QUrl(QStringLiteral("https://7tv.io/v3/users/twitch/%1").arg(*channelId))
                         : QUrl(QStringLiteral("https://7tv.io/v3/emote-sets/global"));
    case EmoteProvider::Ffz:
        return channelId ? QUrl(QStringLiteral("https://api.frankerfacez.com/v1/room/id/%1").arg(*channelId))
                         : QUrl(QStringLiteral("https://api.frankerfacez.com/v1/set/global"));
    }
    return QUrl();
}

EmoteListResult parseEmoteList(EmoteProvider provider, bool channel, int httpStatus, const QByteArray &body)
{
    EmoteListResult result;
    const QString label = listLabel(provider, channel);
    if (provider == EmoteProvider::Twitch) {
        result.error = QStringLiteral("Twitch hat keine Emote-Liste");
        return result;
    }
    // 7TV («user not found») und FFZ («No such room») antworten mit 404, wenn der Kanal dort
    // nicht eingerichtet ist (Abrufe em-0, 2026-11-16).
    if (channel && httpStatus == 404)
        return result;
    if (httpStatus != 200) {
        result.error = QStringLiteral("%1: Antwort mit HTTP-Status %2").arg(label).arg(httpStatus);
        return result;
    }
    if (body.size() > kEmoteListMaxBytes) {
        result.error = QStringLiteral("%1: grösser als 8 MiB, nicht gelesen").arg(label);
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        result.error = QStringLiteral("%1: kein gültiges JSON").arg(label);
        return result;
    }
    bool ok = false;
    switch (provider) {
    case EmoteProvider::Twitch:
        break;
    case EmoteProvider::Bttv:
        ok = parseBttv(doc, channel, &result.emotes);
        break;
    case EmoteProvider::SevenTv:
        ok = parseSevenTv(doc, channel, &result.emotes);
        break;
    case EmoteProvider::Ffz:
        ok = parseFfz(doc, channel, &result.emotes);
        break;
    }
    if (!ok) {
        result.emotes.clear();
        result.error = QStringLiteral("%1: unerwartetes Format").arg(label);
        return result;
    }
    if (result.emotes.size() > kEmoteListMaxEmotes) {
        result.warning = QStringLiteral("%1: mehr als %2 Emotes, %3 ausgelassen")
                             .arg(label)
                             .arg(kEmoteListMaxEmotes)
                             .arg(result.emotes.size() - kEmoteListMaxEmotes);
        result.emotes.resize(kEmoteListMaxEmotes);
    }
    return result;
}

QUrl emoteImageUrl(const Emote &emote, bool animate, bool webp)
{
    // Adressen: Recherche 1–4; kleinste Stufe
    if (!isSafeId(emote.id))
        return QUrl();
    const bool animated = emote.animated && animate;
    switch (emote.provider) {
    case EmoteProvider::Twitch:
        // Vorlage aus https://dev.twitch.tv/docs/api/reference/chat (Feld `template`)
        return QUrl(QStringLiteral("https://static-cdn.jtvnw.net/emoticons/v2/%1/%2/dark/1.0")
                        .arg(emote.id, animated ? QStringLiteral("animated") : QStringLiteral("static")));
    case EmoteProvider::Bttv:
        if (!webp)
            return QUrl();
        // Statische Variante unter /static/ (https://raw.githubusercontent.com/night/betterttv/master/src/utils/cdn.js)
        return QUrl(emote.animated && !animate
                        ? QStringLiteral("https://cdn.betterttv.net/emote/%1/static/1x.webp").arg(emote.id)
                        : QStringLiteral("https://cdn.betterttv.net/emote/%1/1x.webp").arg(emote.id));
    case EmoteProvider::SevenTv:
        if (!webp)
            return QUrl();
        return QUrl(emote.animated && !animate
                        ? QStringLiteral("https://cdn.7tv.app/emote/%1/1x_static.webp").arg(emote.id)
                        : QStringLiteral("https://cdn.7tv.app/emote/%1/1x.webp").arg(emote.id));
    case EmoteProvider::Ffz:
        // Format der animierten Bilder nicht bestätigt (Recherche 4): ohne WebP nur statisch
        return QUrl(animated && webp ? QStringLiteral("https://cdn.frankerfacez.com/emote/%1/animated/1").arg(emote.id)
                                     : QStringLiteral("https://cdn.frankerfacez.com/emote/%1/1").arg(emote.id));
    }
    return QUrl();
}

int EmoteIndex::listIndex(EmoteProvider provider, bool channel)
{
    // Vorrang nach Recherche 5: Kanal vor global, darin FFZ, BTTV, 7TV
    int order = 0;
    switch (provider) {
    case EmoteProvider::Twitch:
        return -1;
    case EmoteProvider::Ffz:
        order = 0;
        break;
    case EmoteProvider::Bttv:
        order = 1;
        break;
    case EmoteProvider::SevenTv:
        order = 2;
        break;
    }
    return channel ? order : 3 + order;
}

void EmoteIndex::setList(EmoteProvider provider, bool channel, const QList<Emote> &emotes)
{
    const int list = listIndex(provider, channel);
    if (list < 0)
        return;
    m_lists[list] = emotes;
    m_byName.clear();
    for (const QList<Emote> &l : m_lists) {
        for (const Emote &emote : l) {
            if (!m_byName.contains(emote.name))
                m_byName.insert(emote.name, emote);
        }
    }
}

const Emote *EmoteIndex::find(const QString &name) const
{
    const auto it = m_byName.constFind(name);
    return it == m_byName.cend() ? nullptr : &it.value();
}

int EmoteIndex::count(EmoteProvider provider) const
{
    const int channel = listIndex(provider, true);
    if (channel < 0)
        return 0;
    return int(m_lists[channel].size() + m_lists[listIndex(provider, false)].size());
}

QList<ChatFragment> chatFragments(const QJsonValue &fragments)
{
    QList<ChatFragment> out;
    const auto string = [](const QJsonObject &o, const char *key) { return o.value(QLatin1String(key)).toString(); };
    for (const QJsonValue &value : fragments.toArray()) {
        if (!value.isObject())
            continue;
        const QJsonObject o = value.toObject();
        ChatFragment fragment{string(o, "type"), string(o, "text"), string(o, "emote_id"), {},
                              string(o, "gif_id"), string(o, "gif_url")};
        for (const QJsonValue &format : o.value(QStringLiteral("emote_format")).toArray())
            fragment.formats.append(format.toString());
        out.append(fragment);
    }
    return out;
}

QList<ChatGif> chatGifs(const QList<ChatFragment> &fragments)
{
    QList<ChatGif> out;
    for (const ChatFragment &fragment : fragments) {
        if (fragment.isGif())
            out.append({fragment.gifUrl, fragment.text});
    }
    return out;
}

namespace {

void appendText(QList<ChatSegment> *segments, const QString &text)
{
    if (text.isEmpty())
        return;
    if (!segments->isEmpty() && !segments->last().emote)
        segments->last().text += text;
    else
        segments->append({text, std::nullopt});
}

bool onlySpaces(const QString &text)
{
    return std::all_of(text.cbegin(), text.cend(), [](QChar c) { return c == u' '; });
}

// Steht vor dem nächsten Abschnitt ein Emote (Leerzeichen dazwischen zählen nicht)?
bool afterEmote(const QList<ChatSegment> &segments)
{
    for (auto it = segments.crbegin(); it != segments.crend(); ++it) {
        if (it->emote)
            return true;
        if (!onlySpaces(it->text))
            return false;
    }
    return false;
}

void appendEmote(QList<ChatSegment> *segments, Emote emote)
{
    if (emote.zeroWidth && !afterEmote(*segments))
        emote.zeroWidth = false;
    segments->append({emote.name, emote});
}

} // namespace

QList<ChatSegment> chatSegments(const QList<ChatFragment> &fragments, const EmoteIndex &index)
{
    QList<ChatSegment> segments;
    for (const ChatFragment &fragment : fragments) {
        if (fragment.type == QLatin1String("emote") && !fragment.emoteId.isEmpty() && !fragment.text.isEmpty()) {
            Emote emote;
            emote.provider = EmoteProvider::Twitch;
            emote.id = fragment.emoteId;
            emote.name = fragment.text;
            emote.animated = fragment.formats.contains(QStringLiteral("animated"));
            appendEmote(&segments, emote);
            continue;
        }
        if (fragment.isGif())
            continue;
        if (fragment.type != QLatin1String("text")) {
            appendText(&segments, fragment.text);
            continue;
        }
        // Wörter und Leerzeichen abwechselnd; nur Wörter werden gesucht
        const QString &text = fragment.text;
        qsizetype start = 0;
        while (start < text.size()) {
            qsizetype end = start;
            if (text.at(start) == u' ') {
                while (end < text.size() && text.at(end) == u' ')
                    ++end;
                appendText(&segments, text.mid(start, end - start));
            } else {
                while (end < text.size() && text.at(end) != u' ')
                    ++end;
                const QString word = text.mid(start, end - start);
                if (const Emote *emote = index.find(word))
                    appendEmote(&segments, *emote);
                else
                    appendText(&segments, word);
            }
            start = end;
        }
    }
    return segments;
}

QString emoteStatusText(const EmoteLoadState &state)
{
    // Wortlaut: Checkliste P14 Emotes, Abschnitt 4 «Neue Texte»
    const auto names = [](const QList<EmoteProvider> &providers) {
        QStringList list;
        for (EmoteProvider p : providers)
            list.append(emoteProviderName(p));
        const QString last = list.isEmpty() ? QString() : list.takeLast();
        return list.isEmpty() ? last : list.join(QStringLiteral(", ")) + QStringLiteral(" und ") + last;
    };
    // Teile ohne «Emotes»; das Wort steht nur einmal vorne (Befund der Prüfung em-7):
    // «Emotes geladen: …» oder «Emotes: <erster Teil>», danach die weiteren Teile.
    QStringList parts;
    bool loaded = false;
    switch (state.network) {
    case EmoteLoadState::Network::OfflineCached:
        parts.append(QStringLiteral("Ohne Netz, Listen vom letzten Mal"));
        break;
    case EmoteLoadState::Network::OfflineEmpty:
        parts.append(QStringLiteral("Ohne Netz und ohne gespeicherte Listen"));
        break;
    case EmoteLoadState::Network::Online: {
        QStringList counts;
        for (const auto &[provider, count] : state.counts)
            counts.append(QStringLiteral("%1 %2").arg(emoteProviderName(provider)).arg(count));
        if (!counts.isEmpty()) {
            parts.append(QStringLiteral("geladen: ") + counts.join(QStringLiteral(", ")));
            loaded = true;
        }
        if (!state.unreachable.isEmpty())
            parts.append(QStringLiteral("%1 nicht erreichbar, die Wörter bleiben Text").arg(names(state.unreachable)));
        break;
    }
    }
    if (!state.webp)
        parts.append(QStringLiteral("WebP fehlt (Paket qt6-image-formats-plugins), BTTV und 7TV bleiben Text"));
    if (!state.channel)
        parts.append(QStringLiteral("kein Kanal angemeldet"));
    if (parts.isEmpty())
        return {};
    return (loaded ? QStringLiteral("Emotes ") : QStringLiteral("Emotes: ")) + parts.join(QStringLiteral(" · "));
}

QString emoteToolTip(const Emote &emote)
{
    return QStringLiteral("%1 (%2)").arg(emote.name, emoteProviderName(emote.provider));
}

} // namespace dss
