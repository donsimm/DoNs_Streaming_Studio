#include "overlaylivestate.h"

#include "mediaimport.h"
#include "overlaystore.h"
#include "protocol.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <memory>

namespace dss {

namespace {

QString imageSuffix(const QByteArray &data)
{
    // Kennung am Anfang der Datei: PNG https://www.w3.org/TR/png-3/#5PNG-file-signature,
    // JPEG (SOI und erster Marker) https://www.w3.org/Graphics/JPEG/itu-t81.pdf, Anhang B.
    if (data.startsWith("\x89PNG\r\n\x1a\n"))
        return QStringLiteral("png");
    if (data.startsWith("\xff\xd8\xff"))
        return QStringLiteral("jpg");
    return QString();
}

QString sourceName(const Overlay &overlay, const QUuid &id)
{
    const int i = sourceIndex(overlay, id);
    return i < 0 ? id.toString(QUuid::WithoutBraces) : overlay.sources.at(i).name;
}

} // namespace

OverlayLiveState::OverlayLiveState(const OverlayStore *store, const Options &options, QObject *parent)
    : QObject(parent)
    , m_store(store)
    , m_options(options)
    , m_substitute(options.substitute)
    , m_expiry(new QTimer(this))
{
    m_clock.start();
    m_expiry->setSingleShot(true);
    connect(m_expiry, &QTimer::timeout, this, &OverlayLiveState::expire);
}

OverlayLiveState::~OverlayLiveState() = default;

void OverlayLiveState::setSubstitute(const Substitute &substitute)
{
    m_substitute = substitute ? substitute : m_options.substitute;
}

int OverlayLiveState::clearImages()
{
    QDir dir(m_options.imageDir);
    int removed = 0;
    for (const QFileInfo &f : dir.entryInfoList(QDir::Files | QDir::Hidden | QDir::System)) {
        if (QFile::remove(f.absoluteFilePath()))
            ++removed;
    }
    m_urlCache.clear();
    m_imageOrder.clear();
    return removed;
}

void OverlayLiveState::set(const LiveSetRequest &request, const Done &done)
{
    const Overlay *overlay = m_store->overlay(request.overlayId);
    if (!overlay) {
        done({false, QString::fromLatin1(err::kUnknownOverlay),
              QStringLiteral("Kein Overlay mit der Kennung %1").arg(request.overlayId.toString(QUuid::WithoutBraces)),
              QString()});
        return;
    }
    const QString invalid = validateLiveSet(request, *overlay);
    if (!invalid.isEmpty()) {
        done({false, QString::fromLatin1(err::kRejected), invalid, QString()});
        return;
    }
    // Bilder: Datei der Mediathek sofort, Adressen erst nach dem Laden.
    QHash<QUuid, QString> images;
    QHash<QUuid, QString> urls;
    QStringList warnings;
    for (const LiveElementRequest &e : request.elements) {
        const QString image = (request.resolved ? e.image : m_substitute(e.image, request.arguments)).trimmed();
        if (image.isEmpty())
            continue;
        if (isLiveImageUrl(image)) {
            urls.insert(e.source, image);
            continue;
        }
        const QString suffix = QFileInfo(image).suffix().toLower();
        if (!validateMediaReference(image).isEmpty() || !allowedMediaSuffixes(MediaUse::Image).contains(suffix)
            || !QFileInfo::exists(m_store->mediaFilePath(image))) {
            warnings.append(QStringLiteral("Bild «%1» für «%2» ist kein Bild der Mediathek; das Element zeigt sein "
                                           "eigenes Bild.").arg(image.left(80), sourceName(*overlay, e.source)));
            continue;
        }
        images.insert(e.source, image);
    }
    if (urls.isEmpty()) {
        apply(request, images, warnings, done);
        return;
    }
    struct Pending
    {
        int open = 0;
        QHash<QUuid, QString> images;
        QStringList warnings;
    };
    auto pending = std::make_shared<Pending>();
    pending->open = int(urls.size());
    pending->images = images;
    pending->warnings = warnings;
    const QPointer<OverlayLiveState> self(this);
    for (auto it = urls.cbegin(); it != urls.cend(); ++it) {
        const QUuid source = it.key();
        const QString name = sourceName(*overlay, source);
        loadImage(it.value(), [self, pending, request, done, source, name](const QString &reference, const QString &error) {
            if (reference.isEmpty())
                pending->warnings.append(QStringLiteral("Profilbild für «%1» nicht geladen (%2); das Element zeigt "
                                                        "sein eigenes Bild.").arg(name, error));
            else
                pending->images.insert(source, reference);
            if (--pending->open > 0 || !self)
                return;
            self->apply(request, pending->images, pending->warnings, done);
        });
    }
}

void OverlayLiveState::apply(const LiveSetRequest &request, const QHash<QUuid, QString> &images,
                             QStringList warnings, const Done &done)
{
    // Beim Laden der Bilder kann sich das Overlay geändert haben: neu nachsehen.
    const Overlay *overlay = m_store->overlay(request.overlayId);
    if (!overlay) {
        done({false, QString::fromLatin1(err::kUnknownOverlay),
              QStringLiteral("Das Overlay wurde inzwischen gelöscht."), QString()});
        return;
    }
    QMap<QUuid, Entry> &state = m_state[request.overlayId];
    const qint64 now = m_clock.elapsed();
    int applied = 0;
    for (const LiveElementRequest &e : request.elements) {
        const int index = sourceIndex(*overlay, e.source);
        if (index < 0) {
            warnings.append(QStringLiteral("Element %1 wurde inzwischen gelöscht.")
                                .arg(e.source.toString(QUuid::WithoutBraces)));
            continue;
        }
        const Source &s = overlay->sources.at(index);
        Entry entry;
        const auto old = state.constFind(e.source);
        const bool visibleNow = old != state.constEnd() && old->element.visible ? *old->element.visible : s.visible;
        switch (request.mode) {
        case LiveMode::Show:
            entry.element.visible = true;
            break;
        case LiveMode::Hide:
            entry.element.visible = false;
            break;
        case LiveMode::Toggle:
            entry.element.visible = !visibleNow;
            break;
        }
        if (s.kind == SourceKind::Text) {
            // Platzhalter im eigenen Text des Elements (Plan 4.4) oder Text aus dem Aufruf
            const QString original = e.text ? *e.text : s.text.content;
            QString text = e.text && request.resolved ? original : m_substitute(original, request.arguments);
            if (text.size() > kLiveMaxTextChars) {
                text.truncate(kLiveMaxTextChars);
                warnings.append(QStringLiteral("Text für «%1» auf %2 Zeichen gekürzt.").arg(s.name).arg(kLiveMaxTextChars));
            }
            if (e.text || text != s.text.content)
                entry.element.text = text;
        }
        if (s.kind == SourceKind::Image)
            entry.element.image = images.value(e.source);
        if (request.durationMs > 0)
            entry.deadlineMs = now + request.durationMs;
        state.insert(e.source, entry);
        ++applied;
    }
    if (state.isEmpty())
        m_state.remove(request.overlayId);
    if (applied > 0) {
        qInfo("Laufzustand: %s %d Element(e) von «%s»%s", qPrintable(liveModeName(request.mode)), applied,
              qPrintable(overlay->name),
              request.durationMs > 0 ? qPrintable(QStringLiteral(" für %1 ms").arg(request.durationMs)) : "");
        scheduleExpiry();
        emit changed(request.overlayId);
    }
    done({true, QString(), QString(), warnings.join(QLatin1Char(' '))});
}

QString OverlayLiveState::reset(const LiveResetRequest &request)
{
    const Overlay *overlay = m_store->overlay(request.overlayId);
    if (!overlay)
        return QStringLiteral("Kein Overlay mit der Kennung %1").arg(request.overlayId.toString(QUuid::WithoutBraces));
    auto it = m_state.find(request.overlayId);
    if (it == m_state.end())
        return QString();
    qsizetype removed = 0;
    if (request.sources.isEmpty()) {
        removed = it->size();
        m_state.erase(it);
    } else {
        for (const QUuid &id : request.sources)
            removed += it->remove(id);
        if (it->isEmpty())
            m_state.erase(it);
    }
    if (removed > 0) {
        qInfo("Laufzustand: %lld Element(e) von «%s» zurückgesetzt", static_cast<long long>(removed),
              qPrintable(overlay->name));
        scheduleExpiry();
        emit changed(request.overlayId);
    }
    return QString();
}

void OverlayLiveState::overlaysChanged()
{
    QList<QUuid> changedIds;
    for (auto it = m_state.begin(); it != m_state.end();) {
        const Overlay *overlay = m_store->overlay(it.key());
        bool changedOne = false;
        if (overlay) {
            for (auto e = it->begin(); e != it->end();) {
                if (sourceIndex(*overlay, e.key()) < 0) {
                    e = it->erase(e);
                    changedOne = true;
                } else {
                    ++e;
                }
            }
        }
        if (!overlay || changedOne)
            changedIds.append(it.key());
        if (!overlay || it->isEmpty())
            it = m_state.erase(it);
        else
            ++it;
    }
    for (const QUuid &id : std::as_const(changedIds))
        emit changed(id);
}

LiveElements OverlayLiveState::elements(const QUuid &overlayId) const
{
    LiveElements result;
    const auto it = m_state.constFind(overlayId);
    if (it == m_state.constEnd())
        return result;
    const qint64 now = m_clock.elapsed();
    for (auto e = it->cbegin(); e != it->cend(); ++e) {
        LiveElement element = e->element;
        element.remainingMs = e->deadlineMs < 0 ? -1 : std::max<qint64>(0, e->deadlineMs - now);
        result.insert(e.key(), element);
    }
    return result;
}

QList<OverlayLiveInfo> OverlayLiveState::all() const
{
    QList<OverlayLiveInfo> list;
    for (auto it = m_state.cbegin(); it != m_state.cend(); ++it)
        list.append({it.key(), elements(it.key())});
    std::sort(list.begin(), list.end(), [](const OverlayLiveInfo &a, const OverlayLiveInfo &b) {
        return a.overlayId.toString() < b.overlayId.toString();
    });
    return list;
}

bool OverlayLiveState::usesImage(const QUuid &overlayId, const QString &reference) const
{
    const auto it = m_state.constFind(overlayId);
    if (it == m_state.constEnd() || reference.isEmpty())
        return false;
    return std::any_of(it->cbegin(), it->cend(), [&reference](const Entry &e) { return e.element.image == reference; });
}

QString OverlayLiveState::checkImageUrl(const QString &url) const
{
    const QUrl u(url, QUrl::StrictMode);
    const QString scheme = u.scheme().toLower();
    if (!u.isValid() || !(scheme == QLatin1String("https") || (m_options.allowHttp && scheme == QLatin1String("http"))))
        return QStringLiteral("nur HTTPS-Adressen");
    if (!u.userInfo().isEmpty() || (!m_options.allowHttp && u.port() != -1)
        || !m_options.imageHosts.contains(u.host().toLower()))
        return QStringLiteral("nur vom Bildserver von Twitch");
    return QString();
}

void OverlayLiveState::loadImage(const QString &url, const Loaded &loaded)
{
    const QString problem = checkImageUrl(url);
    if (!problem.isEmpty()) {
        loaded(QString(), problem);
        return;
    }
    const QString cached = m_urlCache.value(url);
    if (!cached.isEmpty() && QFileInfo::exists(m_options.imageDir + QLatin1Char('/') + QFileInfo(cached).fileName())) {
        m_imageOrder.removeAll(cached);
        m_imageOrder.append(cached);
        loaded(cached, QString());
        return;
    }
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request{QUrl(url)};
    // Keine Weiterleitung (sonst ginge es an einen anderen Server), Grenze für die ganze Übertragung.
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(m_options.timeoutMs);
    QNetworkReply *reply = m_network->get(request);
    const qint64 max = m_options.maxImageBytes;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply, max](qint64 received, qint64 total) {
        if (received > max || total > max)
            reply->abort();
    });
    const QString host = QUrl(url).host();
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, host, loaded, max] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QString error;
        QByteArray data;
        // errorString() enthält die Adresse; ins Protokoll kommen nur Status und Fehlernummer
        if (reply->error() == QNetworkReply::OperationCanceledError)
            error = QStringLiteral("grösser als %1 MiB oder abgebrochen").arg(max / (1024 * 1024));
        else if (status != 0 && status != 200)
            error = QStringLiteral("Antwort %1").arg(status);
        else if (reply->error() == QNetworkReply::TimeoutError)
            error = QStringLiteral("Zeitüberschreitung");
        else if (reply->error() != QNetworkReply::NoError)
            error = QStringLiteral("Netzwerkfehler %1").arg(int(reply->error()));
        else
            data = reply->readAll();
        QString reference;
        if (error.isEmpty() && data.size() > max)
            error = QStringLiteral("grösser als %1 MiB").arg(max / (1024 * 1024));
        if (error.isEmpty())
            reference = storeImage(data, &error);
        if (!reference.isEmpty()) {
            m_urlCache.insert(url, reference);
            // Protokoll ohne Adresse (sie gehört zu einem Zuschauer), nur der Server
            qInfo("Laufzustand: Profilbild von %s geladen (%lld Byte)", qPrintable(host),
                  static_cast<long long>(data.size()));
        } else {
            qWarning("Laufzustand: Profilbild von %s nicht geladen: %s", qPrintable(host), qPrintable(error));
        }
        loaded(reference, error);
    });
}

QString OverlayLiveState::storeImage(const QByteArray &data, QString *error)
{
    const QString suffix = imageSuffix(data);
    if (suffix.isEmpty()) {
        *error = QStringLiteral("weder PNG noch JPEG");
        return QString();
    }
    const QString name = QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex())
                         + QLatin1Char('.') + suffix;
    const QString reference = QString::fromLatin1(kLiveImageDir) + QLatin1Char('/') + name;
    const QString path = m_options.imageDir + QLatin1Char('/') + name;
    if (!QFileInfo::exists(path)) {
        if (!QDir().mkpath(m_options.imageDir)) {
            *error = QStringLiteral("Ordner %1 nicht anlegbar").arg(m_options.imageDir);
            return QString();
        }
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
            *error = QStringLiteral("nicht gespeichert: %1").arg(file.errorString());
            return QString();
        }
    }
    m_imageOrder.removeAll(reference);
    m_imageOrder.append(reference);
    pruneImages();
    return reference;
}

void OverlayLiveState::pruneImages()
{
    // Älteste zuerst löschen, aber keines, das ein Laufzustand gerade zeigt.
    for (qsizetype i = 0; m_imageOrder.size() > m_options.maxImages && i < m_imageOrder.size();) {
        const QString reference = m_imageOrder.at(i);
        const bool used = std::any_of(m_state.cbegin(), m_state.cend(), [&reference](const QMap<QUuid, Entry> &s) {
            return std::any_of(s.cbegin(), s.cend(), [&reference](const Entry &e) { return e.element.image == reference; });
        });
        if (used) {
            ++i;
            continue;
        }
        QFile::remove(m_options.imageDir + QLatin1Char('/') + QFileInfo(reference).fileName());
        m_imageOrder.removeAt(i);
        for (auto it = m_urlCache.begin(); it != m_urlCache.end();) {
            if (it.value() == reference)
                it = m_urlCache.erase(it);
            else
                ++it;
        }
    }
}

void OverlayLiveState::scheduleExpiry()
{
    qint64 next = -1;
    for (const QMap<QUuid, Entry> &s : std::as_const(m_state)) {
        for (const Entry &e : s) {
            if (e.deadlineMs >= 0 && (next < 0 || e.deadlineMs < next))
                next = e.deadlineMs;
        }
    }
    if (next < 0) {
        m_expiry->stop();
        return;
    }
    m_expiry->start(int(std::max<qint64>(0, next - m_clock.elapsed())));
}

void OverlayLiveState::expire()
{
    const qint64 now = m_clock.elapsed();
    QList<QUuid> changedIds;
    for (auto it = m_state.begin(); it != m_state.end();) {
        qsizetype before = it->size();
        for (auto e = it->begin(); e != it->end();) {
            if (e->deadlineMs >= 0 && e->deadlineMs <= now)
                e = it->erase(e);
            else
                ++e;
        }
        if (it->size() != before)
            changedIds.append(it.key());
        if (it->isEmpty())
            it = m_state.erase(it);
        else
            ++it;
    }
    scheduleExpiry();
    for (const QUuid &id : std::as_const(changedIds))
        emit changed(id);
}

} // namespace dss
