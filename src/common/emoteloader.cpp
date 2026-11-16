#include "emoteloader.h"

#include "version.h"

#include <QNetworkAccessManager>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <utility>

namespace dss {

namespace {

// Grund, aus dem der Loader eine Anfrage selbst abgebrochen hat (Eigenschaft der Antwort)
constexpr char kAbortReason[] = "dssEmoteAbort";

const QList<EmoteProvider> kListProviders = {EmoteProvider::Bttv, EmoteProvider::SevenTv, EmoteProvider::Ffz};

int defaultPort(const QUrl &url)
{
    return url.scheme() == QLatin1String("https") ? 443 : 80;
}

QString basePath(const QUrl &base)
{
    QString path = base.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

QString listLabel(EmoteProvider provider, bool channel)
{
    return emoteProviderName(provider) + (channel ? QStringLiteral(" Kanal") : QStringLiteral(" global"));
}

} // namespace

EmoteEndpoints EmoteEndpoints::standard()
{
    // Plan P14 Emotes, «Nur die vier Anbieter» (Recherche 8)
    EmoteEndpoints endpoints;
    for (const char *host : {"api.betterttv.net", "cdn.betterttv.net", "7tv.io", "cdn.7tv.app",
                             "api.frankerfacez.com", "cdn.frankerfacez.com", "static-cdn.jtvnw.net"}) {
        const QString name = QString::fromLatin1(host);
        endpoints.list.append({name, QUrl(QStringLiteral("https://") + name)});
    }
    return endpoints;
}

QUrl EmoteEndpoints::map(const QUrl &real) const
{
    for (const EmoteEndpoint &endpoint : list) {
        if (real.host().compare(endpoint.host, Qt::CaseInsensitive) != 0)
            continue;
        QUrl url = endpoint.base;
        url.setPath(basePath(endpoint.base) + real.path());
        url.setQuery(real.query());
        return url;
    }
    return {};
}

bool EmoteEndpoints::allows(const QUrl &url) const
{
    if (!url.isValid() || !url.userInfo().isEmpty())
        return false;
    const QString path = url.adjusted(QUrl::NormalizePathSegments).path();
    for (const EmoteEndpoint &endpoint : list) {
        const QUrl &base = endpoint.base;
        if (url.scheme().compare(base.scheme(), Qt::CaseInsensitive) != 0
            || url.host().compare(base.host(), Qt::CaseInsensitive) != 0
            || url.port(defaultPort(url)) != base.port(defaultPort(base)))
            continue;
        const QString prefix = basePath(base);
        if (prefix.isEmpty() || path == prefix || path.startsWith(prefix + QLatin1Char('/')))
            return true;
    }
    return false;
}

EmoteLoader::EmoteLoader(const QString &cacheDir, EmoteEndpoints endpoints, QObject *parent)
    : QObject(parent), m_endpoints(std::move(endpoints)), m_network(new QNetworkAccessManager(this))
{
    // Nur der HTTP-Zwischenspeicher, keine eigene Bildsammlung (Plan «Wer lädt», Frage 4).
    // https://doc.qt.io/qt-6.8/qnetworkdiskcache.html
    auto *cache = new QNetworkDiskCache(m_network);
    cache->setCacheDirectory(cacheDir);
    cache->setMaximumCacheSize(kEmoteCacheMaxBytes);
    m_network->setCache(cache);
}

EmoteLoader::~EmoteLoader()
{
    abortLists();
    for (const QPointer<QNetworkReply> &reply : std::as_const(m_imageReplies)) {
        if (reply) {
            disconnect(reply, nullptr, this, nullptr);
            reply->abort();
        }
    }
}

QNetworkReply *EmoteLoader::get(const QUrl &real, int cacheControl, qint64 maxBytes)
{
    const QUrl url = m_endpoints.map(real);
    if (url.isEmpty() || !m_endpoints.allows(url))
        return nullptr;
    QNetworkRequest request(url);
    // Weiterleitungen nur an erlaubte Server, geprüft in redirected()
    // https://doc.qt.io/qt-6.8/qnetworkrequest.html#RedirectPolicy-enum
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, cacheControl);
    request.setTransferTimeout(kEmoteRequestTimeoutMs);
    // Fest, ohne Rechnernamen und ohne Twitch-Token (Plan «Nur die vier Anbieter»)
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DoNs-Streaming-Studio/") + version());
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::redirected, reply, [this, reply](const QUrl &target) {
        if (m_endpoints.allows(reply->url().resolved(target))) {
            emit reply->redirectAllowed();
            return;
        }
        reply->setProperty(kAbortReason, QStringLiteral("Weiterleitung an einen fremden Server abgewiesen"));
        reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 received, qint64 total) {
        if (received > maxBytes || total > maxBytes) {
            reply->setProperty(kAbortReason, QStringLiteral("grösser als %1 MiB, nicht gelesen").arg(maxBytes / (1024 * 1024)));
            reply->abort();
        }
    });
    return reply;
}

void EmoteLoader::abortLists()
{
    const QList<QPointer<QNetworkReply>> replies = std::exchange(m_listReplies, {});
    for (const QPointer<QNetworkReply> &reply : replies) {
        if (!reply)
            continue;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    m_pendingLists = 0;
}

void EmoteLoader::load(const std::optional<QString> &channelId, bool webp)
{
    if (isLoading() && channelId == m_channelId)
        return;
    abortLists();
    m_channelId = channelId;
    m_webp = webp;
    m_results.clear();
    // Ohne WebP lassen sich BTTV und 7TV nicht anzeigen: keine nutzlosen Abrufe (Plan «Ohne WebP»)
    m_channelValid = channelId && !emoteListUrl(EmoteProvider::Ffz, channelId).isEmpty();
    QList<ListJob> jobs;
    for (EmoteProvider provider : kListProviders) {
        if (!webp && provider != EmoteProvider::Ffz)
            continue;
        jobs.append({provider, false, emoteListUrl(provider, std::nullopt)});
        if (m_channelValid)
            jobs.append({provider, true, emoteListUrl(provider, channelId)});
    }
    m_pendingLists = int(jobs.size());
    for (const ListJob &job : std::as_const(jobs))
        startList(job);
}

void EmoteLoader::startList(ListJob job)
{
    // Listen zuerst aus dem Netz, ohne Antwort aus dem Zwischenspeicher (Plan «Ohne Netz»)
    // https://doc.qt.io/qt-6.8/qnetworkrequest.html#CacheLoadControl-enum
    QNetworkReply *reply = get(job.url, job.fromCacheTry ? QNetworkRequest::AlwaysCache : QNetworkRequest::PreferNetwork,
                               kEmoteListMaxBytes);
    if (!reply) {
        qWarning("Emotes: %s, Server nicht erlaubt", qPrintable(listLabel(job.provider, job.channel)));
        m_results.append({job.provider, job.channel, {}, false, job.fromCacheTry, false});
        if (--m_pendingLists == 0)
            finishLoad();
        return;
    }
    m_listReplies.append(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, job] { listFinished(reply, job); });
}

void EmoteLoader::listFinished(QNetworkReply *reply, const ListJob &job)
{
    m_listReplies.removeAll(reply);
    reply->deleteLater();
    const QString label = listLabel(job.provider, job.channel);
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString reason = reply->property(kAbortReason).toString();
    ListResult result{job.provider, job.channel, {}, false, job.fromCacheTry, false};
    // Keine Antwort, Serverfehler oder abgebrochene Übertragung: der Server ist nicht erreichbar
    const bool netFailed = reason.isEmpty()
                           && (status == 0 || status >= 500 || (status == 200 && reply->error() != QNetworkReply::NoError));
    if (netFailed && !job.fromCacheTry) {
        // errorString() enthält die Adresse; ins Protokoll kommen nur Status und Fehlernummer
        qWarning("Emotes: %s nicht erreichbar (Status %d, Fehler %d), versuche den Zwischenspeicher",
                 qPrintable(label), status, int(reply->error()));
        ListJob retry = job;
        retry.fromCacheTry = true;
        startList(retry);
        return;
    }
    if (!reason.isEmpty()) {
        qWarning("Emotes: %s: %s", qPrintable(label), qPrintable(reason));
    } else if (netFailed) {
        qWarning("Emotes: %s auch nicht im Zwischenspeicher", qPrintable(label));
    } else {
        const EmoteListResult parsed = parseEmoteList(job.provider, job.channel, status, reply->readAll());
        if (!parsed.warning.isEmpty())
            qWarning("Emotes: %s", qPrintable(parsed.warning));
        if (parsed.ok()) {
            result.ok = true;
            result.fromCache = job.fromCacheTry;
            result.emotes = parsed.emotes;
        } else {
            qWarning("Emotes: %s", qPrintable(parsed.error));
        }
    }
    m_results.append(result);
    if (--m_pendingLists == 0)
        finishLoad();
}

void EmoteLoader::finishLoad()
{
    EmoteIndex index;
    EmoteLoadState state;
    state.webp = m_webp;
    state.channel = m_channelValid;
    bool allNetFailed = true;
    bool anyCached = false;
    for (EmoteProvider provider : kListProviders) {
        int count = 0;
        bool anyOk = false;
        bool anyFailed = false;
        bool any = false;
        for (const ListResult &result : std::as_const(m_results)) {
            if (result.provider != provider)
                continue;
            any = true;
            if (result.ok) {
                index.setList(provider, result.channel, result.emotes);
                count += int(result.emotes.size());
                anyOk = true;
            }
            // Ohne Antwort zählt der Anbieter als nicht erreichbar, auch mit Liste vom letzten Mal
            if (!result.ok || result.netFailed)
                anyFailed = true;
            allNetFailed = allNetFailed && result.netFailed;
            anyCached = anyCached || (result.ok && result.fromCache);
        }
        if (!any)
            continue;
        if (anyOk)
            state.counts.append({provider, count});
        if (anyFailed)
            state.unreachable.append(provider);
    }
    if (allNetFailed)
        state.network = anyCached ? EmoteLoadState::Network::OfflineCached : EmoteLoadState::Network::OfflineEmpty;
    if (!m_webp && !std::exchange(m_webpWarned, true))
        qWarning("Emotes: WebP fehlt (Paket qt6-image-formats-plugins), BTTV und 7TV nicht geladen");
    qInfo("%s", qPrintable(emoteStatusText(state)));
    m_results.clear();
    emit indexChanged(index);
    emit stateChanged(state);
}

QString EmoteLoader::image(const Emote &emote, bool animate)
{
    const QUrl real = emoteImageUrl(emote, animate, m_webp);
    if (real.isEmpty())
        return {};
    const QString key = real.toString();
    if (m_imageReplies.value(key))
        return key;
    // Bilder zuerst aus dem Zwischenspeicher (Plan «Ohne Netz»)
    QNetworkReply *reply = get(real, QNetworkRequest::PreferCache, kEmoteImageMaxBytes);
    if (!reply)
        return {};
    m_imageReplies.insert(key, reply);
    const QString host = real.host();
    connect(reply, &QNetworkReply::finished, this, [this, reply, key, host] {
        m_imageReplies.remove(key);
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error = reply->property(kAbortReason).toString();
        QByteArray bytes;
        if (error.isEmpty() && status != 200)
            error = QStringLiteral("Antwort %1").arg(status);
        else if (error.isEmpty() && reply->error() != QNetworkReply::NoError)
            error = QStringLiteral("Netzwerkfehler %1").arg(int(reply->error()));
        if (error.isEmpty()) {
            bytes = reply->readAll();
            if (bytes.size() > kEmoteImageMaxBytes) {
                bytes.clear();
                error = QStringLiteral("grösser als %1 MiB, nicht gelesen").arg(kEmoteImageMaxBytes / (1024 * 1024));
            }
        }
        // Protokoll ohne Adresse, nur der Server
        if (!error.isEmpty())
            qWarning("Emotes: Bild von %s nicht geladen: %s", qPrintable(host), qPrintable(error));
        emit imageLoaded(key, bytes);
    });
    return key;
}

} // namespace dss
