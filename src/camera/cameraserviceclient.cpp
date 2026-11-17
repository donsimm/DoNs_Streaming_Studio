#include "cameraserviceclient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QTimer>

namespace dss::camera {

namespace {

const QString kOffMessage = QStringLiteral("Nicht eingerichtet oder abgeschaltet");

// Antwort des Dienstes: Status und JSON-Objekt; status 0, wenn keine HTTP-Antwort kam.
Reply readReply(QNetworkReply *reply)
{
    Reply result;
    const QVariant code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!code.isValid()) {
        result.error = reply->error() == QNetworkReply::OperationCanceledError
            ? QStringLiteral("Keine Antwort des Kamera-Dienstes innerhalb der Zeitgrenze.")
            : QStringLiteral("Kamera-Dienst nicht erreichbar (%1).").arg(reply->errorString());
        return result;
    }
    result.status = code.toInt();
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(reply->readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        result.error = QStringLiteral("Antwort des Kamera-Dienstes ist kein JSON-Objekt.");
    else
        result.body = document.object();
    return result;
}

// Presets wie in GET status: nur id und name.
QJsonArray presetList(const QJsonValue &presets)
{
    QJsonArray list;
    for (const QJsonValue &item : presets.toArray()) {
        const QJsonObject preset = item.toObject();
        list.append(QJsonObject{{QStringLiteral("id"), preset.value(QStringLiteral("id"))},
                                {QStringLiteral("name"), preset.value(QStringLiteral("name"))}});
    }
    return list;
}

} // namespace

CameraServiceClient::CameraServiceClient(QObject *parent)
    : CameraServiceClient(Timing(), parent)
{
}

CameraServiceClient::CameraServiceClient(const Timing &timing, QObject *parent)
    : QObject(parent)
    , m_timing(timing)
    , m_network(new QNetworkAccessManager(this))
    , m_reconnectTimer(new QTimer(this))
    , m_attemptTimer(new QTimer(this))
    , m_idleTimer(new QTimer(this))
    , m_message(kOffMessage)
{
    // Heimnetz: nie über einen Proxy aus der Umgebung.
    m_network->setProxy(QNetworkProxy::NoProxy);
    m_reconnectTimer->setSingleShot(true);
    m_reconnectTimer->setInterval(m_timing.reconnect);
    connect(m_reconnectTimer, &QTimer::timeout, this, &CameraServiceClient::startAttempt);
    m_attemptTimer->setSingleShot(true);
    m_attemptTimer->setInterval(m_timing.connectTimeout);
    connect(m_attemptTimer, &QTimer::timeout, this, [this] {
        failAttempt(QStringLiteral("Kamera-Dienst unter %1 antwortet nicht. Neuer Versuch läuft.").arg(endpoint()));
    });
    m_idleTimer->setSingleShot(true);
    m_idleTimer->setInterval(m_timing.streamIdle);
    connect(m_idleTimer, &QTimer::timeout, this, [this] {
        qWarning("Kamera-Dienst: %d ms keine Daten im Ereignisstrom, Verbindung gilt als tot", m_timing.streamIdle);
        failAttempt(QStringLiteral("Verbindung zum Kamera-Dienst unter %1 unterbrochen. Neuer Versuch läuft.")
                        .arg(endpoint()));
    });
}

CameraServiceClient::~CameraServiceClient()
{
    dropReplies();
    // Offene Anfragen enden ohne Aufruf ihres handlers: Der Besitzer ist schon weg.
    for (QNetworkReply *reply : std::as_const(m_requests)) {
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_requests.clear();
}

QString CameraServiceClient::endpoint() const
{
    return QStringLiteral("%1:%2").arg(m_config.host).arg(m_config.port);
}

QUrl CameraServiceClient::url(const QString &path) const
{
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_config.host);
    url.setPort(m_config.port);
    url.setPath(QStringLiteral("/api/v1/") + path);
    return url;
}

bool CameraServiceClient::isValidPath(const QString &path)
{
    if (path.isEmpty() || path.startsWith(QLatin1Char('/')) || path.endsWith(QLatin1Char('/')))
        return false;
    for (const QChar c : path) {
        if (c.isSpace() || c == QLatin1Char('?') || c == QLatin1Char('#') || c == QLatin1Char('\\'))
            return false;
    }
    const QStringList parts = path.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
    }
    return true;
}

void CameraServiceClient::setStatus(State state, const QString &message)
{
    if (state == m_state && message == m_message)
        return;
    m_state = state;
    m_message = message;
    qInfo("Kamera-Dienst: %s", message.isEmpty() ? "verbunden" : qPrintable(message));
    emit stateChanged(state);
}

void CameraServiceClient::configure(const Config &config)
{
    if (config == m_config)
        return;
    m_config = config;
    m_reconnectTimer->stop();
    dropReplies();
    if (!m_status.isEmpty()) {
        m_status = {}; // Andere Gegenstelle: alter Stand gilt nicht mehr.
        emit statusChanged();
    }
    if (!m_config.enabled) {
        setStatus(State::Off, kOffMessage);
        return;
    }
    if (m_config.host.isEmpty()) {
        setStatus(State::Error, QStringLiteral("Keine Adresse für den Kamera-Dienst eingetragen."));
        return;
    }
    setStatus(State::Connecting, QStringLiteral("Verbinde mit dem Kamera-Dienst unter %1 …").arg(endpoint()));
    startAttempt();
}

void CameraServiceClient::dropReplies()
{
    m_attemptTimer->stop();
    m_idleTimer->stop();
    m_streamOpen = false;
    for (QNetworkReply **slot : {&m_statusReply, &m_streamReply}) {
        if (QNetworkReply *reply = *slot) {
            *slot = nullptr;
            reply->disconnect(this);
            reply->abort();
            reply->deleteLater();
        }
    }
    m_parser.reset();
}

void CameraServiceClient::failAttempt(const QString &message)
{
    dropReplies();
    setStatus(State::Error, message);
    m_reconnectTimer->start();
}

void CameraServiceClient::startAttempt()
{
    dropReplies();
    QNetworkRequest request(url(QStringLiteral("status")));
    request.setTransferTimeout(m_timing.connectTimeout);
    m_statusReply = m_network->get(request);
    QNetworkReply *reply = m_statusReply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] { onStatusReply(reply); });
}

void CameraServiceClient::onStatusReply(QNetworkReply *reply)
{
    if (reply != m_statusReply)
        return;
    m_statusReply = nullptr;
    reply->deleteLater();
    const Reply result = readReply(reply);
    if (result.status == 0) {
        // Zeitgrenze von setTransferTimeout: OperationCanceledError.
        failAttempt((reply->error() == QNetworkReply::OperationCanceledError
                         ? QStringLiteral("Kamera-Dienst unter %1 antwortet nicht. Neuer Versuch läuft.")
                         : QStringLiteral("Kamera-Dienst unter %1 nicht erreichbar. Neuer Versuch läuft."))
                        .arg(endpoint()));
        return;
    }
    if (result.status != 200 || result.body.value(QStringLiteral("ok")) != QJsonValue(true)) {
        const QString code = result.body.value(QStringLiteral("code")).toString();
        failAttempt(QStringLiteral("Kamera-Dienst unter %1 lehnt ab (%2 %3). Neuer Versuch läuft.")
                        .arg(endpoint())
                        .arg(result.status)
                        .arg(code.isEmpty() ? result.error : code));
        return;
    }
    const int apiVersion = result.body.value(QStringLiteral("api_version")).toInt();
    if (apiVersion != kApiVersion) {
        failAttempt(QStringLiteral("Kamera-Dienst unter %1 hat Schnittstelle %2, erwartet ist %3.")
                        .arg(endpoint())
                        .arg(apiVersion)
                        .arg(kApiVersion));
        return;
    }
    QJsonObject status = result.body;
    status.remove(QStringLiteral("ok"));
    if (status != m_status) {
        m_status = status;
        emit statusChanged();
    }
    openStream();
}

void CameraServiceClient::openStream()
{
    QNetworkRequest request(url(QStringLiteral("events")));
    request.setRawHeader("Accept", "text/event-stream");
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    // Keine Zeitgrenze für die ganze Übertragung: Der Strom bleibt offen. Den Kopf wacht
    // m_attemptTimer ab, danach m_idleTimer.
    request.setTransferTimeout(0);
    m_parser.reset();
    m_streamReply = m_network->get(request);
    QNetworkReply *reply = m_streamReply;
    connect(reply, &QNetworkReply::metaDataChanged, this, &CameraServiceClient::onStreamHeaders);
    connect(reply, &QNetworkReply::readyRead, this, &CameraServiceClient::onStreamData);
    connect(reply, &QNetworkReply::finished, this, &CameraServiceClient::onStreamFinished);
    m_attemptTimer->start();
}

void CameraServiceClient::onStreamHeaders()
{
    if (!m_streamReply || m_streamOpen)
        return;
    const int code = m_streamReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QString type = m_streamReply->header(QNetworkRequest::ContentTypeHeader).toString();
    if (code != 200 || !type.startsWith(QStringLiteral("text/event-stream"))) {
        failAttempt(QStringLiteral("Kamera-Dienst unter %1 liefert keine Ereignisse (%2). Neuer Versuch läuft.")
                        .arg(endpoint())
                        .arg(code));
        return;
    }
    m_streamOpen = true;
    m_attemptTimer->stop();
    m_idleTimer->start();
    setStatus(State::Connected, QString());
}

void CameraServiceClient::onStreamData()
{
    if (!m_streamReply)
        return;
    if (!m_streamOpen)
        onStreamHeaders();
    if (!m_streamReply || !m_streamOpen)
        return;
    m_idleTimer->start();
    bool ok = true;
    // QPointer: ein handler von eventReceived darf configure() aufrufen.
    const QPointer<QNetworkReply> reply = m_streamReply;
    const QList<SseEvent> events = m_parser.feed(reply->readAll(), &ok);
    for (const SseEvent &event : events) {
        if (m_streamReply != reply)
            return;
        handleEvent(event);
    }
    if (!ok && m_streamReply == reply) {
        qWarning("Kamera-Dienst: Ereignis grösser als %lld Bytes", static_cast<long long>(SseParser::kMaxBytes));
        failAttempt(QStringLiteral("Kamera-Dienst unter %1 sendet unlesbare Ereignisse. Neuer Versuch läuft.")
                        .arg(endpoint()));
    }
}

void CameraServiceClient::onStreamFinished()
{
    if (!m_streamReply)
        return;
    if (m_streamOpen) {
        failAttempt(QStringLiteral("Verbindung zum Kamera-Dienst unter %1 unterbrochen. Neuer Versuch läuft.")
                        .arg(endpoint()));
        return;
    }
    // Vor dem Kopf beendet: wie ein Fehlschlag von GET status.
    failAttempt(QStringLiteral("Kamera-Dienst unter %1 nicht erreichbar. Neuer Versuch läuft.").arg(endpoint()));
}

void CameraServiceClient::handleEvent(const SseEvent &event)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(event.data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        qWarning("Kamera-Dienst: Ereignis «%s» ohne JSON-Objekt übergangen", qPrintable(event.name));
        return;
    }
    const QJsonObject data = document.object();
    if (event.name == QLatin1String("status")) {
        setStatusPart(QStringLiteral("cameras"), data.value(QStringLiteral("cameras")));
    } else if (event.name == QLatin1String("lights")) {
        setStatusPart(QStringLiteral("lights"), data.value(QStringLiteral("lights")));
    } else if (event.name == QLatin1String("presets")) {
        const QJsonObject before = m_status;
        m_status.insert(QStringLiteral("camera_presets"), presetList(data.value(QStringLiteral("camera_presets"))));
        m_status.insert(QStringLiteral("light_presets"), presetList(data.value(QStringLiteral("light_presets"))));
        if (m_status != before)
            emit statusChanged();
    }
    emit eventReceived(event.name, data);
}

void CameraServiceClient::setStatusPart(const QString &key, const QJsonValue &value)
{
    if (!value.isArray() || m_status.value(key) == value)
        return;
    m_status.insert(key, value);
    emit statusChanged();
}

void CameraServiceClient::request(const QByteArray &method, const QString &path, const QJsonValue &body,
                                  ReplyHandler handler)
{
    const auto failLater = [this, handler](const QString &message) {
        QTimer::singleShot(0, this, [handler, message] {
            Reply reply;
            reply.error = message;
            handler(reply);
        });
    };
    if (m_state == State::Off) {
        failLater(QStringLiteral("Kamera-Dienst ist abgeschaltet."));
        return;
    }
    if (m_config.host.isEmpty()) {
        failLater(QStringLiteral("Keine Adresse für den Kamera-Dienst eingetragen."));
        return;
    }
    if (!isValidPath(path)) {
        failLater(QStringLiteral("Ungültiger Pfad «%1».").arg(path));
        return;
    }
    if (method != "GET" && method != "PUT" && method != "POST" && method != "DELETE") {
        failLater(QStringLiteral("Ungültige Methode «%1».").arg(QString::fromLatin1(method)));
        return;
    }
    QNetworkRequest request(url(path));
    request.setTransferTimeout(m_timing.requestTimeout);
    QByteArray data;
    if (!body.isUndefined() && !body.isNull()) {
        data = body.isArray() ? QJsonDocument(body.toArray()).toJson(QJsonDocument::Compact)
                              : QJsonDocument(body.toObject()).toJson(QJsonDocument::Compact);
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    }
    QNetworkReply *reply = data.isEmpty() ? m_network->sendCustomRequest(request, method)
                                          : m_network->sendCustomRequest(request, method, data);
    m_requests.insert(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, handler] {
        m_requests.remove(reply);
        reply->deleteLater();
        handler(readReply(reply));
    });
}

} // namespace dss::camera
