#include "windowdata.h"

#include <algorithm>
#include <cmath>

namespace dss {

QString cpuText(const std::optional<double> &percent)
{
    if (!percent)
        return QStringLiteral("–");
    return QStringLiteral("%1 %").arg(std::lround(*percent));
}

QString ramText(const std::optional<qint64> &usedKiB, const std::optional<qint64> &totalKiB)
{
    if (!usedKiB || !totalKiB)
        return QStringLiteral("–");
    // GiB = KiB / 1024², eine Nachkommastelle wie auf dem Bild («0.8 / 7.8 GiB»).
    const double gib = 1024.0 * 1024.0;
    return QStringLiteral("%1 / %2 GiB")
        .arg(double(*usedKiB) / gib, 0, 'f', 1)
        .arg(double(*totalKiB) / gib, 0, 'f', 1);
}

QString lanText(const std::optional<LanStats> &lan)
{
    if (!lan)
        return QStringLiteral("–");
    return QStringLiteral("↓ %1 ↑ %2 kbit/s")
        .arg(std::llround(lan->receivedKbitPerSecond))
        .arg(std::llround(lan->sentKbitPerSecond));
}

QString lanToolTip(const std::optional<LanStats> &lan)
{
    if (!lan)
        return QStringLiteral("LAN: keine Standardroute oder noch kein Wert");
    return QStringLiteral("LAN über %1 (Standardroute): empfangen %2 kbit/s, gesendet %3 kbit/s")
        .arg(lan->interface)
        .arg(lan->receivedKbitPerSecond, 0, 'f', 1)
        .arg(lan->sentKbitPerSecond, 0, 'f', 1);
}

ValueHistory::ValueHistory(int capacity)
    : m_capacity(std::max(1, capacity))
{
}

void ValueHistory::add(const std::optional<double> &value)
{
    m_values.append(value);
    while (m_values.size() > m_capacity)
        m_values.removeFirst();
}

double ValueHistory::maximum() const
{
    double m = 0.0;
    for (const std::optional<double> &v : m_values) {
        if (v)
            m = std::max(m, *v);
    }
    return m;
}

double lanScaleMaximum(const ValueHistory &history)
{
    return std::max(1.0, history.maximum());
}

QList<SourceRow> sourceRows(const Overlay &overlay)
{
    QList<SourceRow> rows;
    for (auto it = overlay.sources.crbegin(); it != overlay.sources.crend(); ++it)
        rows.append({it->id, it->name, it->visible});
    return rows;
}

QString visibilityToolTip(bool visible)
{
    return visible ? QStringLiteral("eingeblendet") : QStringLiteral("ausgeblendet");
}

LiveRow liveRow(const LiveElements &elements, const QUuid &sourceId, bool connected)
{
    LiveRow row;
    if (sourceId.isNull())
        return row;
    const auto it = elements.constFind(sourceId);
    if (it == elements.constEnd()) {
        row.text = liveStateText(std::nullopt, -1);
        return row;
    }
    row.text = liveStateText(*it, it->remainingMs);
    row.live = true;
    row.canHide = connected;
    return row;
}

int liveRefreshMs(const LiveElements &elements)
{
    qint64 next = -1;
    for (const LiveElement &e : elements) {
        if (e.remainingMs < 0)
            continue;
        // Anzeige rundet auf: Wechsel bei jeder vollen Sekunde der Restdauer
        const qint64 toSecond = e.remainingMs % 1000 == 0 ? 1000 : e.remainingMs % 1000;
        next = next < 0 ? toSecond : std::min(next, toSecond);
    }
    return next < 0 ? -1 : int(std::max<qint64>(50, next));
}

const Source *findSource(const Overlay &overlay, const QUuid &sourceId)
{
    const int index = sourceIndex(overlay, sourceId);
    return index < 0 ? nullptr : &overlay.sources.at(index);
}

double rotationDegrees(int tenthDegrees)
{
    return tenthDegrees / 10.0;
}

QString coreStatusText(bool connected)
{
    return connected ? QStringLiteral("Kern-Dienst: verbunden")
                     : QStringLiteral("Kern-Dienst: nicht verbunden");
}

QString importStatusText(const QStringList &fileNames)
{
    if (fileNames.isEmpty())
        return {};
    QStringList quoted;
    for (const QString &name : fileNames)
        quoted.append(QStringLiteral("«%1»").arg(name));
    if (fileNames.size() == 1)
        return QStringLiteral("Übernehme %1 …").arg(quoted.first());
    return QStringLiteral("Übernehme %1 Dateien (%2) …")
        .arg(fileNames.size()).arg(quoted.join(QStringLiteral(", ")));
}

std::optional<SettingsUpdate> settingsUpdateFromForm(const CoreSettings &current, const SettingsForm &form,
                                                     bool obsInError)
{
    SettingsUpdate update;
    const QString address = form.address.trimmed();
    const QString obsHost = form.obsHost.trimmed();
    if (form.port != current.port)
        update.port = form.port;
    if (address != current.homeAddress)
        update.homeAddress = address;
    if (form.obsEnabled != current.obsEnabled)
        update.obsEnabled = form.obsEnabled;
    if (obsHost != current.obsHost)
        update.obsHost = obsHost;
    if (form.obsPort != current.obsPort)
        update.obsPort = form.obsPort;
    if (!form.obsPassword.isEmpty())
        update.obsPassword = form.obsPassword;
    else if (form.deleteObsPassword)
        update.obsPassword = QString();
    const QString twitchClientId = form.twitchClientId.trimmed();
    if (form.twitchEnabled != current.twitchEnabled)
        update.twitchEnabled = form.twitchEnabled;
    if (twitchClientId != current.twitchClientId)
        update.twitchClientId = twitchClientId;
    if (!form.twitchClientSecret.isEmpty())
        update.twitchClientSecret = form.twitchClientSecret;
    else if (form.deleteTwitchClientSecret)
        update.twitchClientSecret = QString();
    if (!update.port && !update.homeAddress && !update.obsEnabled && !update.obsHost && !update.obsPort
        && !update.obsPassword && !update.twitchEnabled && !update.twitchClientId && !update.twitchClientSecret) {
        if (!form.obsEnabled || !obsInError)
            return std::nullopt;
        update.obsEnabled = form.obsEnabled;
    }
    return update;
}

QString settingsDialogHint()
{
    return QStringLiteral("Port und Adresse gelten sofort: Der Overlay-Server startet mit den neuen "
                          "Werten neu. Quellen, die das Studio in OBS angelegt hat, führt es nach; "
                          "andere Browserquellen brauchen in OBS die neue Adresse. Nur Adressen dieses "
                          "PCs. Leere Adresse: Der Kern trägt innerhalb von 2 s die Adresse der "
                          "Netzwerkkarte mit der Standardroute ein, sobald es eine gibt.");
}

QString obsPasswordPlaceholder(const std::optional<SettingsInfo> &info, bool deleting)
{
    if (!info)
        return {};
    if (deleting)
        return QStringLiteral("wird mit «OK» gelöscht");
    return info->obsPasswordSet ? QStringLiteral("gespeichert; leer lassen = behalten")
                                : QStringLiteral("kein Passwort gespeichert");
}

QString twitchSecretPlaceholder(const std::optional<SettingsInfo> &info, bool deleting)
{
    if (!info)
        return {};
    if (deleting)
        return QStringLiteral("wird mit «OK» gelöscht");
    return info->twitchClientSecretSet ? QStringLiteral("gespeichert; leer lassen = behalten")
                                       : QStringLiteral("kein Secret gespeichert");
}

TwitchAccountView twitchAccountView(const std::optional<TwitchStatusInfo> &status, const QString &account)
{
    TwitchAccountView v;
    if (!status) {
        v.text = QStringLiteral("–");
        return v;
    }
    const TwitchAccountInfo &a = account == QLatin1String("bot") ? status->bot : status->channel;
    if (a.loginPending) {
        v.loginPending = true;
        v.userCode = a.loginPending->userCode;
        v.verificationUri = a.loginPending->verificationUri;
        v.expiresAt = a.loginPending->expiresAt;
    }
    QStringList lines;
    if (a.login.isEmpty())
        lines.append(QStringLiteral("nicht angemeldet"));
    else if (a.state == QLatin1String("checking"))
        lines.append(QStringLiteral("angemeldet als %1, wird geprüft …").arg(a.login));
    else
        lines.append(QStringLiteral("angemeldet als %1").arg(a.login));
    if (!a.message.isEmpty())
        lines.append(a.message);
    if (!a.missingScopes.isEmpty())
        lines.append(QStringLiteral("Es fehlen Rechte: %1").arg(a.missingScopes.join(QStringLiteral(", "))));
    v.text = lines.join(QLatin1Char('\n'));
    // Neu anmelden geht auch nach abgelaufener oder ungültiger Anmeldung; nur eine gültige (oder
    // gerade geprüfte) Anmeldung braucht zuerst «Abmelden».
    const bool valid = !a.login.isEmpty() && (a.state == QLatin1String("ok") || a.state == QLatin1String("checking"));
    v.loginEnabled = status->enabled && !valid;
    v.logoutEnabled = !a.login.isEmpty() || a.state != QLatin1String("off");
    return v;
}

QString twitchCodeRemainingText(const QDateTime &expiresAt, const QDateTime &now)
{
    const qint64 seconds = now.secsTo(expiresAt);
    if (!expiresAt.isValid() || seconds <= 0)
        return QStringLiteral("Code abgelaufen, neu anmelden");
    return QStringLiteral("Gültig noch %1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString twitchStatusText(const std::optional<TwitchStatusInfo> &status)
{
    if (!status)
        return {};
    if (!status->enabled)
        return QStringLiteral("Kern ohne Anbindung an Twitch gebaut.");
    QStringList lines;
    if (!status->message.isEmpty())
        lines.append(status->message);
    if (!status->subscriptions.isEmpty()) {
        int active = 0;
        QStringList problems;
        for (const TwitchSubscriptionInfo &s : status->subscriptions) {
            if (s.ok)
                ++active;
            else
                problems.append(s.problem.isEmpty() ? s.type : QStringLiteral("%1 (%2)").arg(s.type, s.problem));
        }
        QString line = QStringLiteral("Abos: %1 von %2 aktiv").arg(active).arg(status->subscriptions.size());
        if (!problems.isEmpty())
            line += QStringLiteral("; nicht aktiv: ") + problems.join(QStringLiteral(", "));
        lines.append(line);
    }
    if (!status->badgeProblem.isEmpty())
        lines.append(status->badgeProblem);
    return lines.join(QLatin1Char('\n'));
}

QString twitchBotLoginHint()
{
    return QStringLiteral("Im Browser zuerst vom Kanal-Konto abmelden oder ein privates Fenster nutzen.");
}

namespace {

bool twitchAccountValid(const TwitchAccountInfo &a)
{
    return !a.login.isEmpty() && (a.state == QLatin1String("ok") || a.state == QLatin1String("checking"));
}

} // namespace

QString twitchBotProblem(const std::optional<TwitchStatusInfo> &status)
{
    if (!status || !status->enabled || twitchAccountValid(status->bot))
        return {};
    QString problem;
    if (status->bot.login.isEmpty())
        problem = QStringLiteral("Bot nicht angemeldet");
    else if (!status->bot.message.isEmpty())
        problem = QStringLiteral("Bot: %1").arg(status->bot.message);
    else
        problem = QStringLiteral("Bot: Anmeldung ungültig");
    if (twitchAccountValid(status->channel))
        problem += QStringLiteral(", der Kanal sendet ersatzweise");
    return problem;
}

QString twitchToolTip(ConnectionState state, const std::optional<TwitchStatusInfo> &status)
{
    const QString plain = connectionStateText(Connection::Twitch, state);
    if (!status || !status->enabled)
        return plain;
    QStringList lines;
    lines.append(state == ConnectionState::Connected || status->message.isEmpty() ? plain : status->message);
    const QString bot = twitchBotProblem(status);
    if (!bot.isEmpty())
        lines.append(bot);
    return lines.join(QLatin1Char('\n'));
}

namespace {

// Kopfzeile nur aus dem Stand von Twitch (P13)
ChatBotHeaderView twitchHeaderView(ConnectionState channelState, const std::optional<TwitchStatusInfo> &status)
{
    ChatBotHeaderView v;
    v.toolTip = twitchStatusText(status);
    if (!status) {
        v.text = QStringLiteral("Twitch: –");
        return v;
    }
    if (!status->enabled) {
        v.text = QStringLiteral("Twitch: im Kern nicht gebaut");
        return v;
    }
    QStringList parts;
    parts.append(connectionStateText(Connection::Twitch, channelState));
    if (twitchAccountValid(status->channel))
        parts.append(QStringLiteral("liest als %1").arg(status->channel.login));
    else
        parts.append(QStringLiteral("Kanal nicht angemeldet"));
    const QString botProblem = twitchBotProblem(status);
    if (botProblem.isEmpty())
        parts.append(QStringLiteral("sendet als %1").arg(status->bot.login));
    else
        parts.append(botProblem);
    v.text = parts.join(QStringLiteral(" · "));
    if (channelState == ConnectionState::Error)
        v.state = QStringLiteral("error");
    else if (channelState == ConnectionState::Connected)
        v.state = botProblem.isEmpty() ? QStringLiteral("ok") : QStringLiteral("warning");
    return v;
}

} // namespace

ChatBotHeaderView chatBotHeaderView(ConnectionState channelState, const std::optional<TwitchStatusInfo> &status,
                                    const std::optional<ActionsStatus> &actions)
{
    ChatBotHeaderView v = twitchHeaderView(channelState, status);
    if (!actions)
        return v;
    const QString &state = actions->state;
    if (state == QLatin1String("unavailable") || state == QLatin1String("file_error")) {
        // Der Kern nimmt keine Änderung an: Schalter aus und ausgegraut
        v.text = actions->message;
        v.state = QStringLiteral("error");
        return v;
    }
    // Stumm: actions_status sagt nicht, ob der Hauptschalter an ist; ausgegraut, weil die Version
    // ohnehin nicht sendet ([OFFEN] im Bericht e2-4)
    v.switchEnabled = state != QLatin1String("muted");
    v.switchChecked = state == QLatin1String("on");
    if (channelState == ConnectionState::Error)
        return v;
    if (state == QLatin1String("off")) {
        v.text = QStringLiteral("aus");
        v.state = QStringLiteral("off");
        return v;
    }
    if (state == QLatin1String("muted")) {
        v.text = QStringLiteral("stumm: Version im Einsatz läuft");
        v.state = QStringLiteral("warning");
        return v;
    }
    // on: ohne verbundenen Kanal bleibt der Text von Twitch (verbinde, nicht eingerichtet)
    if (channelState != ConnectionState::Connected || !status || !twitchAccountValid(status->channel))
        return v;
    if (!twitchBotProblem(status).isEmpty()) {
        v.text = QStringLiteral("Bot nicht angemeldet: sendet als Kanal");
        v.state = QStringLiteral("warning");
        return v;
    }
    v.text = QStringLiteral("läuft · liest als %1 · sendet als %2").arg(status->channel.login, status->bot.login);
    v.state = QStringLiteral("ok");
    return v;
}

ActionsEdit actionsSetEnabledEdit(bool enabled)
{
    return {QString::fromLatin1(msg::kActionsSetEnabled), QJsonObject{{QStringLiteral("enabled"), enabled}}};
}

QStringList chatBotEventFilters()
{
    // twitch.gift_sub nicht: Jedes Geschenk kommt schon als twitch.gift_bomb (mit Anzahl), sonst
    // stünde ein Geschenk doppelt in der Liste.
    return {QStringLiteral("twitch.stream_online"), QStringLiteral("twitch.stream_offline"),
            QStringLiteral("twitch.first_message"), QStringLiteral("twitch.follow"),
            QStringLiteral("twitch.subscribe"),     QStringLiteral("twitch.resubscribe"),
            QStringLiteral("twitch.gift_bomb"),     QStringLiteral("twitch.cheer"),
            QStringLiteral("twitch.raid")};
}

namespace {

// Anzeigename eines Zuschauers aus data (prefix «user» oder «from_user»); anonym oder leer: Ersatz
QString viewerName(const QJsonObject &data, const QString &prefix)
{
    QString name = data.value(prefix + QStringLiteral("_name")).toString();
    if (name.isEmpty())
        name = data.value(prefix + QStringLiteral("_login")).toString();
    if (!name.isEmpty())
        return name;
    return data.value(QStringLiteral("is_anonymous")).toBool() ? QStringLiteral("anonym") : QStringLiteral("—");
}

// tier 1000, 2000, 3000 (Twitch) → «Stufe 1» …
QString tierText(const QJsonObject &data)
{
    const int tier = data.value(QStringLiteral("tier")).toInt();
    return tier >= 1000 ? QStringLiteral("Stufe %1").arg(tier / 1000) : QString();
}

} // namespace

EventRow eventRow(const events::Event &event)
{
    EventRow row;
    row.time = event.time.isValid() ? event.time.toLocalTime().toString(QStringLiteral("HH:mm:ss"))
                                    : QStringLiteral("–");
    const QJsonObject &d = event.data;
    const QString user = QStringLiteral("user");
    const QString &t = event.type;
    if (t == QLatin1String("twitch.stream_online")) {
        row.kind = QStringLiteral("Stream online");
        row.name = QStringLiteral("—");
    } else if (t == QLatin1String("twitch.stream_offline")) {
        row.kind = QStringLiteral("Stream offline");
        row.name = QStringLiteral("—");
    } else if (t == QLatin1String("twitch.first_message")) {
        row.kind = QStringLiteral("Erste Nachricht");
        row.name = viewerName(d, user);
    } else if (t == QLatin1String("twitch.follow")) {
        row.kind = QStringLiteral("Follow");
        row.name = viewerName(d, user);
    } else if (t == QLatin1String("twitch.subscribe")) {
        row.kind = QStringLiteral("Sub");
        row.name = viewerName(d, user);
        row.details = tierText(d);
    } else if (t == QLatin1String("twitch.resubscribe")) {
        row.kind = QStringLiteral("Sub");
        row.name = viewerName(d, user);
        row.details = QStringLiteral("%1, %2. Monat").arg(tierText(d)).arg(d.value(QStringLiteral("months")).toInt());
    } else if (t == QLatin1String("twitch.gift_bomb")) {
        row.kind = QStringLiteral("Geschenk-Sub");
        row.name = viewerName(d, user);
        row.details = QStringLiteral("%1 × %2").arg(d.value(QStringLiteral("total")).toInt()).arg(tierText(d));
    } else if (t == QLatin1String("twitch.gift_sub")) {
        row.kind = QStringLiteral("Geschenk-Sub");
        row.name = viewerName(d, QStringLiteral("recipient_user"));
        row.details = QStringLiteral("erhalten, %1").arg(tierText(d));
    } else if (t == QLatin1String("twitch.cheer")) {
        row.kind = QStringLiteral("Cheer");
        row.name = viewerName(d, user);
        row.details = QStringLiteral("%1 Bits").arg(d.value(QStringLiteral("bits")).toInt());
    } else if (t == QLatin1String("twitch.raid")) {
        row.kind = QStringLiteral("Raid");
        row.name = viewerName(d, QStringLiteral("from_user"));
        row.details = QStringLiteral("%1 Zuschauer").arg(d.value(QStringLiteral("viewers")).toInt());
    } else {
        // Typen ohne eigene Zeile (später bestellt): Typ als Art
        row.kind = t;
        row.name = d.contains(QStringLiteral("user_name")) ? viewerName(d, user) : QStringLiteral("—");
    }
    return row;
}

void EventRows::reset(const QList<events::Event> &events)
{
    m_rows.clear();
    for (qsizetype i = std::max<qsizetype>(0, events.size() - kMaxRows); i < events.size(); ++i)
        m_rows.append(eventRow(events.at(i)));
}

int EventRows::append(const events::Event &event)
{
    m_rows.append(eventRow(event));
    if (m_rows.size() <= kMaxRows)
        return 0;
    m_rows.removeFirst();
    return 1;
}

QString eventsDroppedText(qint64 count)
{
    return QStringLiteral("%1 Ereignisse nicht angezeigt: Das Fenster kam nicht nach.").arg(count);
}

QList<bool> ModuleDocks::switchTo(bool editorModule, const QList<bool> &visible)
{
    if (!editorModule) {
        if (m_hidden)
            return {};
        m_hidden = true;
        m_remembered = visible;
        return QList<bool>(visible.size(), false);
    }
    if (!m_hidden)
        return {};
    m_hidden = false;
    QList<bool> restored = m_remembered;
    m_remembered.clear();
    return restored.size() == visible.size() ? restored : QList<bool>();
}

QString obsToolTip(ConnectionState state, const std::optional<ObsStatusInfo> &status)
{
    const QString plain = connectionStateText(Connection::Obs, state);
    if (!status || state == ConnectionState::Off)
        return plain;
    if (state != ConnectionState::Connected)
        return status->message.isEmpty() ? plain : status->message;
    QStringList details;
    if (!status->obsVersion.isEmpty())
        details.append(QStringLiteral("OBS %1").arg(status->obsVersion));
    if (!status->currentScene.name.isEmpty())
        details.append(QStringLiteral("Szene «%1»").arg(status->currentScene.name));
    return details.isEmpty() ? plain : QStringLiteral("%1 (%2)").arg(plain, details.join(QStringLiteral(", ")));
}

ObsSourceRow obsSourceRow(bool overlaySelected, ConnectionState obsState, const ObsSourceInfo &source)
{
    ObsSourceRow row;
    const bool connected = obsState == ConnectionState::Connected;
    row.addsToScene = overlaySelected && source.state == ObsSourceState::Present;
    row.canAct = overlaySelected && connected;
    row.buttonText = row.addsToScene ? QStringLiteral("In weitere Szene einfügen …") : QStringLiteral("In OBS anlegen …");
    if (!row.canAct)
        row.buttonToolTip = QStringLiteral("Geht nur mit gewähltem Overlay und verbundenem OBS");
    else if (row.addsToScene)
        row.buttonToolTip = QStringLiteral("Fügt dieselbe Quelle in eine weitere Szene ein");
    else
        row.buttonToolTip = QStringLiteral("Legt für dieses Overlay eine Browserquelle in OBS an");
    if (!overlaySelected) {
        row.text = QStringLiteral("–");
    } else if (source.state == ObsSourceState::Missing) {
        row.text = QStringLiteral("Quelle in OBS fehlt");
        row.problem = true;
    } else if (source.state == ObsSourceState::Present) {
        row.text = QStringLiteral("Quelle «%1» in OBS").arg(source.inputName);
        if (!connected && obsState != ConnectionState::Off)
            row.text += QStringLiteral(", OBS gerade nicht verbunden");
        row.problem = obsState == ConnectionState::Error;
    } else if (connected) {
        row.text = QStringLiteral("Noch keine Quelle in OBS");
    } else {
        // Dieselben Worte für die Zustände wie die Verbindungsknöpfe
        row.text = connectionStateText(Connection::Obs, obsState);
        row.problem = obsState == ConnectionState::Error;
    }
    return row;
}

AudioRouteRow audioRouteRow(const std::optional<SettingsInfo> &settings,
                            const std::optional<AudioOutputsInfo> &outputs, const QString &purpose)
{
    AudioRouteRow row;
    row.choices.append({QString(), QStringLiteral("Kein Ausgang")});
    if (settings) {
        const AudioRoute route = settings->settings.audioRoutes.value(purpose);
        row.offsetMs = route.offsetMs;
        row.offsetMinMs = settings->audioOffsetMinMs;
        row.offsetMaxMs = settings->audioOffsetMaxMs;
    }
    if (outputs) {
        for (const AudioDeviceInfo &d : outputs->devices) {
            QString label = d.description.isEmpty() ? d.name : d.description;
            if (d.isDefault)
                label += QStringLiteral(" (Standard)");
            row.choices.append({d.name, label});
        }
    }
    const QString selected = settings ? settings->settings.audioRoutes.value(purpose).output : QString();
    if (!selected.isEmpty()) {
        const auto it = std::find_if(row.choices.cbegin(), row.choices.cend(),
                                     [&selected](const AudioOutputChoice &c) { return c.name == selected; });
        if (it == row.choices.cend()) {
            // Gewählter Ausgang nicht in der Liste: bleibt sichtbar gewählt, kein Ausweichen
            row.choices.append({selected, QStringLiteral("%1 (fehlt)").arg(selected)});
            row.current = int(row.choices.size()) - 1;
        } else {
            row.current = int(it - row.choices.cbegin());
        }
    }
    if (!settings || !outputs)
        return row;
    if (!outputs->enabled) {
        row.text = QStringLiteral("Kern ohne Ton gebaut");
        row.toolTip = row.text;
        return row;
    }
    row.canEdit = true;
    const AudioRouteStatus status = outputs->routes.value(purpose);
    if (status.state == QLatin1String("no_pipewire")) {
        row.text = QStringLiteral("PipeWire nicht erreichbar");
        row.problem = true;
    } else if (status.state == QLatin1String("no_target")) {
        row.text = QStringLiteral("Kein Ausgang gewählt");
    } else if (status.state == QLatin1String("missing")) {
        row.text = QStringLiteral("Ausgang fehlt");
        row.problem = true;
    } else if (status.state == QLatin1String("connecting")) {
        // Dieselben Worte wie die Verbindungsknöpfe (connectionStateText, Ausgabe wie NDI).
        row.text = QStringLiteral("Verbindungsversuch läuft");
    } else if (status.state == QLatin1String("playing")) {
        row.text = QStringLiteral("Ausgabe läuft");
    } else if (status.state == QLatin1String("error")) {
        row.text = QStringLiteral("Fehler am Ausgang");
        row.problem = true;
    }
    row.toolTip = status.detail.isEmpty() ? row.text : QStringLiteral("%1: %2").arg(row.text, status.detail);
    return row;
}

ObsSourceDialogState obsSourceDialogState(bool coreConnected, ConnectionState obsState, int sceneCount,
                                          const QString &name, bool pending)
{
    ObsSourceDialogState state;
    const bool connected = coreConnected && obsState == ConnectionState::Connected;
    if (!coreConnected)
        state.problem = QStringLiteral("Keine Verbindung zum Kern, OBS nicht verfügbar.");
    else if (!connected)
        state.problem = QStringLiteral("OBS ist nicht verbunden.");
    state.canEdit = connected && !pending;
    state.canSubmit = state.canEdit && sceneCount > 0 && !name.trimmed().isEmpty();
    return state;
}

QString settingsDialogProblem(bool connected, const std::optional<SettingsInfo> &info)
{
    if (!connected)
        return QStringLiteral("Keine Verbindung zum Kern, Einstellungen nicht verfügbar.");
    if (info && !info->error.isEmpty())
        return info->error;
    return {};
}

BrowserSourceField browserSourceField(const QUuid &selectedOverlay,
                                      const std::optional<BrowserSourceInfo> &info)
{
    if (selectedOverlay.isNull())
        return {QString(), QStringLiteral("Kein Overlay gewählt"), false};
    if (!info || info->overlayId != selectedOverlay)
        return {QString(), QStringLiteral("Adresse wird vom Kern geholt …"), false};
    if (!info->url.isEmpty())
        return {info->url, QString(), true};
    return {QString(), info->problem, false};
}

QString outputStateText(const std::optional<bool> &output, const std::optional<int> &pages)
{
    if (!output)
        return QStringLiteral("Ausgabe: –");
    QString text = *output ? QStringLiteral("Ausgabe: ein")
                           : QStringLiteral("Ausgabe: aus (Browserquelle leer)");
    if (pages) {
        if (*pages <= 0)
            text += QStringLiteral(", keine Browserquelle verbunden");
        else if (*pages == 1)
            text += QStringLiteral(", 1 Browserquelle verbunden");
        else
            text += QStringLiteral(", %1 Browserquellen verbunden").arg(*pages);
    }
    return text;
}

QString outputToolTip(bool output)
{
    return output ? QStringLiteral("Ausgabe ein: Die Browserquelle zeigt das Overlay. Klicken schaltet aus.")
                  : QStringLiteral("Ausgabe aus: Die Browserquelle ist leer. Klicken schaltet ein.");
}

ServerMessages serverMessages(bool connected, const std::optional<ServerStatusInfo> &info)
{
    if (!connected || !info)
        return {};
    QStringList lines;
    if (!info->enabled)
        lines.append(QStringLiteral("Der Kern ist ohne Overlay-Server gebaut (Schalter "
                                    "DSS_CORE_OVERLAY_SERVER); es gibt keine Browserquelle."));
    lines.append(info->problems);
    if (!info->invalidUse.isEmpty())
        lines.append(info->invalidUse);
    if (lines.isEmpty())
        return {};
    // Erster Satz: bis zum ersten «. » (Adressen wie 192.168.1.9 enthalten Punkte ohne Leerzeichen).
    QString first = lines.first();
    const qsizetype end = first.indexOf(QLatin1String(". "));
    if (end >= 0)
        first = first.left(end + 1);
    constexpr int kMaxShort = 90;
    QString shortText = QStringLiteral("Browserquelle: ") + first;
    if (shortText.size() > kMaxShort)
        shortText = shortText.left(kMaxShort - 1) + QStringLiteral("…");
    if (lines.size() > 1)
        shortText += QStringLiteral(" (+%1)").arg(lines.size() - 1);
    return {lines.join(QLatin1Char('\n')), shortText};
}

} // namespace dss
