#include "chatsession.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>

namespace dss::twitch {

ChatSession::ChatSession(events::EventBus *bus, const QString &stateFile, int saveDelayMs, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_stateFile(stateFile)
    , m_saveTimer(new QTimer(this))
{
    m_saveTimer->setSingleShot(true);
    m_saveTimer->setInterval(saveDelayMs);
    connect(m_saveTimer, &QTimer::timeout, this, &ChatSession::save);
    loadState();
    m_subscription = bus->subscribe(this,
                                    {QStringLiteral("twitch.chat_message"), QStringLiteral("twitch.stream_online"),
                                     QStringLiteral("twitch.stream_offline")},
                                    [this](const events::Event &event) { onEvent(event); });
}

ChatSession::~ChatSession()
{
    flush();
}

void ChatSession::flush()
{
    if (m_saveTimer->isActive()) {
        m_saveTimer->stop();
        save();
    }
}

void ChatSession::onEvent(const events::Event &event)
{
    if (event.type == QLatin1String("twitch.chat_message"))
        onChatMessage(event);
    else if (event.type == QLatin1String("twitch.stream_online"))
        onStreamOnline(event.data.value(QStringLiteral("started_at")).toString());
    else if (event.type == QLatin1String("twitch.stream_offline"))
        onStreamOffline();
}

void ChatSession::onStreamOnline(const QString &startedAt)
{
    if (!m_startedAt.isEmpty() && startedAt == m_startedAt)
        return; // derselbe Stream noch einmal gemeldet (zum Beispiel nach dem Wiederverbinden)
    m_startedAt = startedAt;
    m_chatters.clear();
    if (!m_storedStartedAt.isEmpty() && m_storedStartedAt == startedAt)
        m_chatters = m_storedChatters; // Neustart des Kerns im selben Stream
    m_storedStartedAt.clear();
    m_storedChatters.clear();
    m_saveTimer->start();
}

void ChatSession::onStreamOffline()
{
    m_startedAt.clear();
    m_chatters.clear();
    m_storedStartedAt.clear();
    m_storedChatters.clear();
    m_saveTimer->stop();
    if (!m_stateFile.isEmpty() && QFile::exists(m_stateFile) && !QFile::remove(m_stateFile))
        qWarning("Twitch: %s kann nicht gelöscht werden", qPrintable(m_stateFile));
}

void ChatSession::onChatMessage(const events::Event &event)
{
    const QString userId = event.data.value(QStringLiteral("user_id")).toString();
    if (userId.isEmpty() || m_chatters.contains(userId))
        return;
    if ((!m_channelId.isEmpty() && userId == m_channelId) || (!m_botId.isEmpty() && userId == m_botId))
        return; // eigener Kanal oder eigener Bot (em-7b)
    m_chatters.insert(userId);
    events::Event first;
    first.type = QStringLiteral("twitch.first_message");
    first.origin = event.origin;
    first.time = event.time;
    first.originId = event.originId;
    first.data = event.data;
    first.data.insert(QStringLiteral("first_ever"),
                      event.data.value(QStringLiteral("message_type")).toString() == QLatin1String("user_intro"));
    m_bus->publish(first);
    if (isLive())
        m_saveTimer->start();
}

void ChatSession::loadState()
{
    if (m_stateFile.isEmpty())
        return;
    QFile file(m_stateFile);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning("Twitch: %s nicht lesbar", qPrintable(m_stateFile));
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QString startedAt = root.value(QStringLiteral("started_at")).toString();
    if (startedAt.isEmpty()) {
        qWarning("Twitch: %s ohne started_at, nicht übernommen", qPrintable(m_stateFile));
        return;
    }
    m_storedStartedAt = startedAt;
    for (const QJsonValue &value : root.value(QStringLiteral("user_ids")).toArray()) {
        if (value.isString())
            m_storedChatters.insert(value.toString());
    }
}

void ChatSession::save()
{
    if (m_stateFile.isEmpty() || !isLive())
        return;
    QStringList ids(m_chatters.cbegin(), m_chatters.cend());
    std::sort(ids.begin(), ids.end());
    const QJsonObject root{{QStringLiteral("started_at"), m_startedAt},
                           {QStringLiteral("user_ids"), QJsonArray::fromStringList(ids)}};
    QDir().mkpath(QFileInfo(m_stateFile).absolutePath());
    QSaveFile file(m_stateFile);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(root).toJson(QJsonDocument::Compact)) < 0
        || !file.commit())
        qWarning("Twitch: %s kann nicht geschrieben werden (%s)", qPrintable(m_stateFile),
                 qPrintable(file.errorString()));
}

} // namespace dss::twitch
