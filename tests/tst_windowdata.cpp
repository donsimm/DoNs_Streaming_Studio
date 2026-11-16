// Prüft die Aufbereitung der Daten vom Kern für das Fenster ohne Qt Gui (P07, Teil F).

#include "eventtypes.h"
#include "windowdata.h"

#include <QJsonValue>
#include <QTest>
#include <QTimeZone>

using namespace dss;

class TestWindowData : public QObject
{
    Q_OBJECT

private slots:
    // P14 d: Bereich «Chat-Bot» und Markierung «live»
    void liveRows()
    {
        const QUuid shown = QUuid::createUuid();
        const QUuid changed = QUuid::createUuid();
        const QUuid other = QUuid::createUuid();
        LiveElements elements;
        elements.insert(shown, {true, std::nullopt, QString(), 5200});
        elements.insert(changed, {std::nullopt, QStringLiteral("Hallo"), QString(), -1});

        QCOMPARE(liveRow(elements, QUuid(), true).text, QString());
        QVERIFY(!liveRow(elements, QUuid(), true).live);
        LiveRow row = liveRow(elements, other, true);
        QCOMPARE(row.text, QStringLiteral("nicht live"));
        QVERIFY(!row.live && !row.canHide);
        row = liveRow(elements, shown, true);
        QCOMPARE(row.text, QStringLiteral("live sichtbar, noch 6 s"));
        QVERIFY(row.live && row.canHide);
        QVERIFY(!liveRow(elements, shown, false).canHide); // ohne Verbindung
        QCOMPARE(liveRow(elements, changed, true).text, QStringLiteral("live geändert"));
        elements[changed].visible = false;
        QCOMPARE(liveRow(elements, changed, true).text, QStringLiteral("live versteckt"));

        // Nächste Anzeige: bei 5200 ms nach 200 ms («noch 5 s»), ohne Dauer nie
        QCOMPARE(liveRefreshMs(elements), 200);
        elements[shown].remainingMs = 3000;
        QCOMPARE(liveRefreshMs(elements), 1000);
        elements[shown].remainingMs = 3010;
        QCOMPARE(liveRefreshMs(elements), 50);
        elements.remove(shown);
        QCOMPARE(liveRefreshMs(elements), -1);
        QCOMPARE(liveRefreshMs({}), -1);
    }

    void gaugeTexts()
    {
        QCOMPARE(cpuText(std::nullopt), QStringLiteral("–"));
        QCOMPARE(cpuText(0.6), QStringLiteral("1 %"));
        QCOMPARE(cpuText(4.104), QStringLiteral("4 %"));
        QCOMPARE(cpuText(100.0), QStringLiteral("100 %"));
        // Wie auf dem Bild: «0.8 / 7.8 GiB»
        QCOMPARE(ramText(qint64(838861), qint64(8178892)), QStringLiteral("0.8 / 7.8 GiB"));
        QCOMPARE(ramText(qint64(3713872), qint64(32809248)), QStringLiteral("3.5 / 31.3 GiB"));
        QCOMPARE(ramText(std::nullopt, qint64(1)), QStringLiteral("–"));
        QCOMPARE(lanText(std::nullopt), QStringLiteral("–"));
        QCOMPARE(lanText(LanStats{QStringLiteral("enp3s0"), 1.056, 0.4}),
                 QStringLiteral("↓ 1 ↑ 0 kbit/s"));
        QCOMPARE(lanText(LanStats{QStringLiteral("enp3s0"), 12345.6, 789.4}),
                 QStringLiteral("↓ 12346 ↑ 789 kbit/s"));
        QVERIFY(lanToolTip(LanStats{QStringLiteral("enp3s0"), 1, 2}).contains(QStringLiteral("enp3s0")));
    }

    void history()
    {
        ValueHistory h(3);
        QCOMPARE(h.maximum(), 0.0);
        h.add(1.0);
        h.add(std::nullopt);
        h.add(5.0);
        h.add(2.0);
        QCOMPARE(h.values().size(), 3);
        QVERIFY(!h.values().at(0)); // ältester Wert (1.0) ist herausgefallen
        QCOMPARE(*h.values().at(2), 2.0);
        QCOMPARE(h.maximum(), 5.0);
        QCOMPARE(lanScaleMaximum(h), 5.0);
        h.clear();
        QCOMPARE(lanScaleMaximum(h), 1.0);
        QCOMPARE(ValueHistory().capacity(), 60);
    }

    void sourceOrderLikeObs()
    {
        Overlay o;
        for (int i = 0; i < 3; ++i) {
            Source s;
            s.id = QUuid::createUuid();
            s.name = QStringLiteral("Q%1").arg(i);
            s.visible = i != 1;
            o.sources.append(s);
        }
        const QList<SourceRow> rows = sourceRows(o);
        QCOMPARE(rows.size(), 3);
        QCOMPARE(rows.at(0).name, QStringLiteral("Q2")); // vorderste oben
        QCOMPARE(rows.at(2).name, QStringLiteral("Q0"));
        QCOMPARE(rows.at(1).visible, false);
        QCOMPARE(findSource(o, o.sources.at(1).id), &o.sources.at(1));
        QCOMPARE(findSource(o, QUuid::createUuid()), nullptr);
        QCOMPARE(visibilityToolTip(false), QStringLiteral("ausgeblendet"));
    }

    void misc()
    {
        QCOMPARE(rotationDegrees(125), 12.5);
        QCOMPARE(rotationDegrees(3599), 359.9);
        QCOMPARE(coreStatusText(true), QStringLiteral("Kern-Dienst: verbunden"));
        QCOMPARE(coreStatusText(false), QStringLiteral("Kern-Dienst: nicht verbunden"));
        // P10-vor: laufende Übernahmen
        QCOMPARE(importStatusText({}), QString());
        QCOMPARE(importStatusText({QStringLiteral("a.png")}), QStringLiteral("Übernehme «a.png» …"));
        QCOMPARE(importStatusText({QStringLiteral("a.png"), QStringLiteral("b.mp4")}),
                 QStringLiteral("Übernehme 2 Dateien («a.png», «b.mp4») …"));
    }

    void settingsForm()
    {
        const CoreSettings current{8081, QStringLiteral("192.168.1.9")};
        const auto form = [](int port, const QString &address) {
            SettingsForm f;
            f.port = port;
            f.address = address;
            return f;
        };
        QVERIFY(!settingsUpdateFromForm(current, form(8081, QStringLiteral(" 192.168.1.9 ")), false));
        std::optional<SettingsUpdate> u = settingsUpdateFromForm(current, form(8090, QStringLiteral("192.168.1.9")), false);
        QVERIFY(u);
        QCOMPARE(u->port, std::optional<int>(8090));
        QVERIFY(!u->homeAddress);
        u = settingsUpdateFromForm(current, form(8081, QString()), false);
        QVERIFY(u && !u->port);
        QCOMPARE(u->homeAddress, std::optional<QString>(QString()));
        u = settingsUpdateFromForm(current, form(80, QStringLiteral("abc")), false);
        QVERIFY(u && u->port == 80 && u->homeAddress == QStringLiteral("abc")); // prüft der Kern
        QVERIFY(settingsDialogHint().contains(QStringLiteral("Nur Adressen dieses PCs")));
        // P10b, Punkt 85: leere Adresse ohne Neustart
        QVERIFY(settingsDialogHint().contains(QStringLiteral("innerhalb von 2 s")));
        QVERIFY(!settingsDialogProblem(false, std::nullopt).isEmpty());
        QVERIFY(settingsDialogProblem(true, std::nullopt).isEmpty());
        SettingsInfo info{current, 1024, 32767, QStringLiteral("/x/kern.ini"), QStringLiteral("kaputt")};
        QCOMPARE(settingsDialogProblem(true, info), QStringLiteral("kaputt"));
    }

    // P10c: Bereich «OBS» im Dialog
    void settingsFormObs()
    {
        CoreSettings current{8081, QStringLiteral("192.168.1.9")};
        SettingsForm f;
        f.port = 8081;
        f.address = QStringLiteral("192.168.1.9");
        QVERIFY(!settingsUpdateFromForm(current, f, false));
        // Einzeln, nur Geändertes; Adresse ohne Leerzeichen am Rand
        f.obsEnabled = true;
        f.obsHost = QStringLiteral(" 192.168.1.116 ");
        f.obsPort = 4456;
        std::optional<SettingsUpdate> u = settingsUpdateFromForm(current, f, false);
        QVERIFY(u && !u->port && !u->homeAddress && !u->obsPassword);
        QCOMPARE(u->obsEnabled, std::optional<bool>(true));
        QCOMPARE(u->obsHost, std::optional<QString>(QStringLiteral("192.168.1.116")));
        QCOMPARE(u->obsPort, std::optional<int>(4456));
        // Leer = behalten; «Passwort löschen» schickt leer; ein neues Passwort geht vor
        current.obsEnabled = true;
        current.obsHost = QStringLiteral("192.168.1.116");
        current.obsPort = 4456;
        QVERIFY(!settingsUpdateFromForm(current, f, false));
        f.deleteObsPassword = true;
        u = settingsUpdateFromForm(current, f, false);
        QVERIFY(u && u->obsPassword && u->obsPassword->isEmpty());
        f.obsPassword = QStringLiteral("testwert");
        u = settingsUpdateFromForm(current, f, false);
        QVERIFY(u);
        QCOMPARE(u->obsPassword, std::optional<QString>(QStringLiteral("testwert")));
        // Rot und unverändert: «OK» versucht es wieder, aber nicht, wenn OBS aus ist
        f.deleteObsPassword = false;
        f.obsPassword.clear();
        u = settingsUpdateFromForm(current, f, true);
        QVERIFY(u);
        QCOMPARE(u->obsEnabled, std::optional<bool>(true));
        QVERIFY(!u->obsHost && !u->obsPort && !u->obsPassword);
        current.obsEnabled = false;
        f.obsEnabled = false;
        QVERIFY(!settingsUpdateFromForm(current, f, true));

        SettingsInfo info;
        info.obsPasswordSet = true;
        QVERIFY(obsPasswordPlaceholder(info, false).contains(QStringLiteral("behalten")));
        QVERIFY(obsPasswordPlaceholder(info, true).contains(QStringLiteral("gelöscht")));
        info.obsPasswordSet = false;
        QVERIFY(obsPasswordPlaceholder(info, false).contains(QStringLiteral("kein Passwort")));
        // Ohne Einstellungen vom Kern ist es nicht bekannt
        QVERIFY(obsPasswordPlaceholder(std::nullopt, false).isEmpty());
    }

    // P13: Bereich «Twitch»; Client-ID und Secret sind Platzhalter, keine echten Werte.
    void settingsFormTwitch()
    {
        CoreSettings current{8081, QStringLiteral("192.168.1.9")};
        SettingsForm f;
        f.port = 8081;
        f.address = QStringLiteral("192.168.1.9");
        QVERIFY(!settingsUpdateFromForm(current, f, false));
        f.twitchEnabled = true;
        f.twitchClientId = QStringLiteral(" testclientid ");
        std::optional<SettingsUpdate> u = settingsUpdateFromForm(current, f, false);
        QVERIFY(u && !u->port && !u->obsEnabled && !u->twitchClientSecret);
        QCOMPARE(u->twitchEnabled, std::optional<bool>(true));
        QCOMPARE(u->twitchClientId, std::optional<QString>(QStringLiteral("testclientid")));
        current.twitchEnabled = true;
        current.twitchClientId = QStringLiteral("testclientid");
        QVERIFY(!settingsUpdateFromForm(current, f, false));
        // Leer = behalten; «Secret löschen» schickt leer; ein neues Secret geht vor
        f.deleteTwitchClientSecret = true;
        u = settingsUpdateFromForm(current, f, false);
        QVERIFY(u && u->twitchClientSecret && u->twitchClientSecret->isEmpty());
        QVERIFY(!u->twitchEnabled && !u->twitchClientId);
        f.twitchClientSecret = QStringLiteral("testsecret");
        u = settingsUpdateFromForm(current, f, false);
        QVERIFY(u);
        QCOMPARE(u->twitchClientSecret, std::optional<QString>(QStringLiteral("testsecret")));

        SettingsInfo info;
        info.twitchClientSecretSet = true;
        QVERIFY(twitchSecretPlaceholder(info, false).contains(QStringLiteral("behalten")));
        QVERIFY(twitchSecretPlaceholder(info, true).contains(QStringLiteral("gelöscht")));
        info.twitchClientSecretSet = false;
        QCOMPARE(twitchSecretPlaceholder(info, false), QStringLiteral("kein Secret gespeichert"));
        QVERIFY(twitchSecretPlaceholder(std::nullopt, false).isEmpty());
    }

    void twitchAccountRows()
    {
        // Ohne Status vom Kern: nichts bedienbar
        TwitchAccountView v = twitchAccountView(std::nullopt, QStringLiteral("channel"));
        QCOMPARE(v.text, QStringLiteral("–"));
        QVERIFY(!v.loginEnabled && !v.logoutEnabled && !v.loginPending);

        TwitchStatusInfo s;
        s.enabled = true;
        v = twitchAccountView(s, QStringLiteral("channel"));
        QCOMPARE(v.text, QStringLiteral("nicht angemeldet"));
        QVERIFY(v.loginEnabled && !v.logoutEnabled);
        // Kern ohne Twitch gebaut: «Anmelden …» grau
        s.enabled = false;
        QVERIFY(!twitchAccountView(s, QStringLiteral("channel")).loginEnabled);
        s.enabled = true;

        s.channel.state = QStringLiteral("ok");
        s.channel.login = QStringLiteral("testkanal");
        v = twitchAccountView(s, QStringLiteral("channel"));
        QCOMPARE(v.text, QStringLiteral("angemeldet als testkanal"));
        QVERIFY(!v.loginEnabled && v.logoutEnabled);
        s.channel.state = QStringLiteral("checking");
        QCOMPARE(twitchAccountView(s, QStringLiteral("channel")).text,
                 QStringLiteral("angemeldet als testkanal, wird geprüft …"));
        // Abgelaufen: neu anmelden geht, Meldung und fehlende Rechte darunter
        s.channel.state = QStringLiteral("expired");
        s.channel.message = QStringLiteral("Anmeldung abgelaufen");
        s.channel.missingScopes = {QStringLiteral("channel:bot"), QStringLiteral("user:read:chat")};
        v = twitchAccountView(s, QStringLiteral("channel"));
        QCOMPARE(v.text, QStringLiteral("angemeldet als testkanal\nAnmeldung abgelaufen\n"
                                        "Es fehlen Rechte: channel:bot, user:read:chat"));
        QVERIFY(v.loginEnabled && v.logoutEnabled);

        // Bot getrennt vom Kanal; Anmeldung unterwegs mit Code und Adresse
        const QDateTime expires(QDate(2026, 11, 15), QTime(12, 10), QTimeZone::UTC);
        s.bot.loginPending = TwitchLoginPending{QStringLiteral("ABCD-EFGH"),
                                                QStringLiteral("https://www.twitch.tv/activate"), expires};
        v = twitchAccountView(s, QStringLiteral("bot"));
        QVERIFY(v.loginPending);
        QCOMPARE(v.userCode, QStringLiteral("ABCD-EFGH"));
        QCOMPARE(v.verificationUri, QStringLiteral("https://www.twitch.tv/activate"));
        QCOMPARE(v.expiresAt, expires);
        QCOMPARE(v.text, QStringLiteral("nicht angemeldet"));
        QVERIFY(!twitchAccountView(s, QStringLiteral("channel")).loginPending);

        // Restzeit
        QCOMPARE(twitchCodeRemainingText(expires, expires.addSecs(-245)), QStringLiteral("Gültig noch 4:05"));
        QCOMPARE(twitchCodeRemainingText(expires, expires.addSecs(-1)), QStringLiteral("Gültig noch 0:01"));
        QCOMPARE(twitchCodeRemainingText(expires, expires), QStringLiteral("Code abgelaufen, neu anmelden"));
        QCOMPARE(twitchCodeRemainingText(QDateTime(), expires), QStringLiteral("Code abgelaufen, neu anmelden"));
        QVERIFY(twitchBotLoginHint().contains(QStringLiteral("privates Fenster")));
    }

    void twitchStatusTexts()
    {
        QVERIFY(twitchStatusText(std::nullopt).isEmpty());
        TwitchStatusInfo s;
        QCOMPARE(twitchStatusText(s), QStringLiteral("Kern ohne Anbindung an Twitch gebaut."));
        s.enabled = true;
        QVERIFY(twitchStatusText(s).isEmpty());
        s.message = QStringLiteral("Verbinde …");
        s.subscriptions = {{QStringLiteral("channel.follow"), true, QString()},
                           {QStringLiteral("channel.raid"), false, QStringLiteral("Rechte fehlen")},
                           {QStringLiteral("channel.cheer"), false, QString()}};
        s.badgeProblem = QStringLiteral("Abzeichen nicht aktiv: kein Client Secret");
        QCOMPARE(twitchStatusText(s), QStringLiteral("Verbinde …\nAbos: 1 von 3 aktiv; nicht aktiv: "
                                                     "channel.raid (Rechte fehlen), channel.cheer\n"
                                                     "Abzeichen nicht aktiv: kein Client Secret"));
    }

    void twitchBotAndHeader()
    {
        // Ohne Stand vom Kern
        QCOMPARE(twitchToolTip(ConnectionState::Off, std::nullopt), QStringLiteral("Twitch: nicht eingerichtet oder abgeschaltet"));
        ChatBotHeaderView h = chatBotHeaderView(ConnectionState::Off, std::nullopt);
        QCOMPARE(h.text, QStringLiteral("Twitch: –"));
        QCOMPARE(h.state, QStringLiteral("off"));
        TwitchStatusInfo s;
        QCOMPARE(chatBotHeaderView(ConnectionState::Off, s).text, QStringLiteral("Twitch: im Kern nicht gebaut"));
        QVERIFY(twitchBotProblem(s).isEmpty());

        // Kanal verbunden, Bot fehlt: gelb, Problem auch im Tooltip
        s.enabled = true;
        s.channel.state = QStringLiteral("ok");
        s.channel.login = QStringLiteral("testkanal");
        QCOMPARE(twitchBotProblem(s), QStringLiteral("Bot nicht angemeldet, der Kanal sendet ersatzweise"));
        QCOMPARE(twitchToolTip(ConnectionState::Connected, s),
                 QStringLiteral("Twitch: verbunden\nBot nicht angemeldet, der Kanal sendet ersatzweise"));
        h = chatBotHeaderView(ConnectionState::Connected, s);
        QCOMPARE(h.text, QStringLiteral("Twitch: verbunden · liest als testkanal · "
                                        "Bot nicht angemeldet, der Kanal sendet ersatzweise"));
        QCOMPARE(h.state, QStringLiteral("warning"));

        // Bot angemeldet: grün, Tooltip ohne zweite Zeile
        s.bot.state = QStringLiteral("ok");
        s.bot.login = QStringLiteral("testbot");
        QVERIFY(twitchBotProblem(s).isEmpty());
        QCOMPARE(twitchToolTip(ConnectionState::Connected, s), QStringLiteral("Twitch: verbunden"));
        h = chatBotHeaderView(ConnectionState::Connected, s);
        QCOMPARE(h.text, QStringLiteral("Twitch: verbunden · liest als testkanal · sendet als testbot"));
        QCOMPARE(h.state, QStringLiteral("ok"));

        // Bot abgelaufen, Kanal mit Fehler: rot, Meldung des Kerns im Tooltip
        s.bot.state = QStringLiteral("expired");
        s.bot.message = QStringLiteral("Anmeldung abgelaufen");
        s.channel.state = QStringLiteral("error");
        s.message = QStringLiteral("Netz nicht erreichbar");
        QCOMPARE(twitchBotProblem(s), QStringLiteral("Bot: Anmeldung abgelaufen"));
        QCOMPARE(twitchToolTip(ConnectionState::Error, s), QStringLiteral("Netz nicht erreichbar\nBot: Anmeldung abgelaufen"));
        h = chatBotHeaderView(ConnectionState::Error, s);
        QCOMPARE(h.text, QStringLiteral("Twitch: eingerichtet, aber nicht verbunden · Kanal nicht angemeldet · "
                                        "Bot: Anmeldung abgelaufen"));
        QCOMPARE(h.state, QStringLiteral("error"));
        QCOMPARE(h.toolTip, twitchStatusText(s));
    }

    void chatBotEvents()
    {
        // Bestellung: nur gültige Filter, ohne Chat-Nachrichten
        const QStringList filters = chatBotEventFilters();
        QVERIFY(!filters.isEmpty());
        for (const QString &f : filters)
            QVERIFY2(events::isValidFilter(f), qPrintable(f));
        QVERIFY(!filters.contains(QStringLiteral("twitch.chat_message")));

        const QDateTime time(QDate(2026, 11, 15), QTime(19, 7, 31), QTimeZone::UTC);
        const QString localTime = time.toLocalTime().toString(QStringLiteral("HH:mm:ss"));
        const auto event = [&](const char *type, const QJsonObject &data) {
            events::Event e;
            e.type = QString::fromLatin1(type);
            e.time = time;
            e.data = data;
            return e;
        };
        const QJsonValue null(QJsonValue::Null);
        QCOMPARE(eventRow(event("twitch.stream_online", {})),
                 (EventRow{localTime, QStringLiteral("Stream online"), QStringLiteral("—"), QString()}));
        QCOMPARE(eventRow(event("twitch.follow", {{"user_name", "Zuschauer42"}, {"user_login", "zuschauer42"}})),
                 (EventRow{localTime, QStringLiteral("Follow"), QStringLiteral("Zuschauer42"), QString()}));
        QCOMPARE(eventRow(event("twitch.resubscribe", {{"user_name", "Zuschauer42"}, {"tier", 1000}, {"months", 3}})).details,
                 QStringLiteral("Stufe 1, 3. Monat"));
        QCOMPARE(eventRow(event("twitch.subscribe", {{"user_name", "Zuschauer42"}, {"tier", 2000}})).details,
                 QStringLiteral("Stufe 2"));
        const EventRow gift = eventRow(event("twitch.gift_bomb", {{"user_name", null}, {"is_anonymous", true},
                                                                  {"total", 2}, {"tier", 1000}}));
        QCOMPARE(gift.kind, QStringLiteral("Geschenk-Sub"));
        QCOMPARE(gift.name, QStringLiteral("anonym"));
        QCOMPARE(gift.details, QStringLiteral("2 × Stufe 1"));
        QCOMPARE(eventRow(event("twitch.cheer", {{"user_name", "Zuschauer42"}, {"bits", 100}})).details,
                 QStringLiteral("100 Bits"));
        const EventRow raid = eventRow(event("twitch.raid", {{"from_user_name", "Zuschauer7"}, {"viewers", 23}}));
        QCOMPARE(raid.name, QStringLiteral("Zuschauer7"));
        QCOMPARE(raid.details, QStringLiteral("23 Zuschauer"));
        QCOMPARE(eventRow(event("twitch.first_message", {{"user_name", "Zuschauer42"}})).kind,
                 QStringLiteral("Erste Nachricht"));
        QCOMPARE(eventRow(event("obs.scene_changed", {{"scene_name", "Live"}})).kind, QStringLiteral("obs.scene_changed"));
        QCOMPARE(eventRow(events::Event()).time, QStringLiteral("–"));

        // Höchstens 200 Zeilen, älteste fallen vorne weg
        QList<events::Event> history;
        for (int i = 0; i < 250; ++i)
            history.append(event("twitch.raid", {{"from_user_name", QStringLiteral("Zuschauer%1").arg(i)}, {"viewers", i}}));
        EventRows rows;
        rows.reset(history.mid(0, 3));
        QCOMPARE(rows.rows().size(), 3);
        rows.reset(history);
        QCOMPARE(rows.rows().size(), EventRows::kMaxRows);
        QCOMPARE(rows.rows().first().name, QStringLiteral("Zuschauer50"));
        QCOMPARE(rows.rows().last().name, QStringLiteral("Zuschauer249"));
        QCOMPARE(rows.append(history.first()), 1);
        QCOMPARE(rows.rows().size(), EventRows::kMaxRows);
        QCOMPARE(rows.rows().first().name, QStringLiteral("Zuschauer51"));
        QCOMPARE(rows.rows().last().name, QStringLiteral("Zuschauer0"));
        rows.reset({});
        QCOMPARE(rows.append(history.first()), 0);
        QCOMPARE(rows.rows().size(), 1);
        QCOMPARE(eventsDroppedText(5), QStringLiteral("5 Ereignisse nicht angezeigt: Das Fenster kam nicht nach."));
    }

    void moduleDocks()
    {
        ModuleDocks docks;
        // Im Reiter «Overlays» nichts ändern
        QVERIFY(docks.switchTo(true, {true, false, true}).isEmpty());
        QVERIFY(!docks.docksHidden());
        // Zum «Chat-Bot»: alle aus, zweimal wechseln ändert nichts mehr
        QCOMPARE(docks.switchTo(false, {true, false, true}), (QList<bool>{false, false, false}));
        QVERIFY(docks.docksHidden());
        QVERIFY(docks.switchTo(false, {false, false, false}).isEmpty());
        // Zurück: genau die vorher offenen
        QCOMPARE(docks.switchTo(true, {false, false, false}), (QList<bool>{true, false, true}));
        QVERIFY(!docks.docksHidden());
        QVERIFY(docks.switchTo(true, {true, false, true}).isEmpty());
    }

    void obsToolTipTexts()
    {
        ObsStatusInfo status;
        // Ohne Stand und aus: wie die anderen Knöpfe
        QCOMPARE(obsToolTip(ConnectionState::Off, std::nullopt), QStringLiteral("OBS: nicht eingerichtet oder abgeschaltet"));
        status.message = QStringLiteral("Nicht eingerichtet oder abgeschaltet");
        QCOMPARE(obsToolTip(ConnectionState::Off, status), QStringLiteral("OBS: nicht eingerichtet oder abgeschaltet"));
        // Problem: die Meldung des Kerns
        status.message = QStringLiteral("OBS hat das Passwort abgelehnt. Im Dialog «Einstellungen» neu eingeben.");
        QCOMPARE(obsToolTip(ConnectionState::Error, status), status.message);
        status.message.clear();
        QCOMPARE(obsToolTip(ConnectionState::Connecting, status), QStringLiteral("OBS: Verbindungsversuch läuft"));
        // Verbunden: mit Einzelheiten, soweit bekannt
        QCOMPARE(obsToolTip(ConnectionState::Connected, status), QStringLiteral("OBS: verbunden"));
        status.obsVersion = QStringLiteral("33.0.1");
        QCOMPARE(obsToolTip(ConnectionState::Connected, status), QStringLiteral("OBS: verbunden (OBS 33.0.1)"));
        status.currentScene = {QStringLiteral("a"), QStringLiteral("Live")};
        QCOMPARE(obsToolTip(ConnectionState::Connected, status), QStringLiteral("OBS: verbunden (OBS 33.0.1, Szene «Live»)"));
    }

    // P10c, E-29: Fenster «Quelle in OBS anlegen»
    void obsSourceDialogStates()
    {
        const QString name = QStringLiteral("Quelle");
        ObsSourceDialogState state = obsSourceDialogState(true, ConnectionState::Connected, 2, name, false);
        QVERIFY(state.problem.isEmpty() && state.canEdit && state.canSubmit);
        // Leerer Name, keine Szene, laufende Anfrage: nicht bestätigen
        QVERIFY(!obsSourceDialogState(true, ConnectionState::Connected, 2, QStringLiteral("  "), false).canSubmit);
        QVERIFY(!obsSourceDialogState(true, ConnectionState::Connected, 0, name, false).canSubmit);
        state = obsSourceDialogState(true, ConnectionState::Connected, 2, name, true);
        QVERIFY(state.problem.isEmpty() && !state.canEdit && !state.canSubmit);
        // Ohne OBS, ohne Kern: je eine eigene Meldung, alles gesperrt
        state = obsSourceDialogState(true, ConnectionState::Error, 0, name, false);
        QCOMPARE(state.problem, QStringLiteral("OBS ist nicht verbunden."));
        QVERIFY(!state.canEdit && !state.canSubmit);
        state = obsSourceDialogState(false, ConnectionState::Off, 0, name, false);
        QVERIFY(state.problem.contains(QStringLiteral("Kern")));
        QVERIFY(!state.canEdit && !state.canSubmit);
    }

    // P10c, E-29: Zeile «OBS» im Dock «Steuerung»
    void obsSourceRowTexts()
    {
        const QUuid id = QUuid::createUuid();
        const ObsSourceInfo none{id, ObsSourceState::None, QString()};
        const ObsSourceInfo present{id, ObsSourceState::Present, QStringLiteral("Quelle")};
        const ObsSourceInfo missing{id, ObsSourceState::Missing, QStringLiteral("Quelle")};
        const QString create = QStringLiteral("In OBS anlegen …");
        const QString add = QStringLiteral("In weitere Szene einfügen …");

        ObsSourceRow row = obsSourceRow(false, ConnectionState::Connected, none);
        QCOMPARE(row.text, QStringLiteral("–"));
        QVERIFY(!row.canAct && !row.problem);
        QCOMPARE(row.buttonText, create);

        row = obsSourceRow(true, ConnectionState::Connected, none);
        QCOMPARE(row.text, QStringLiteral("Noch keine Quelle in OBS"));
        QVERIFY(row.canAct && !row.problem && !row.addsToScene);
        QCOMPARE(row.buttonText, create);

        // F6: Quelle da, weitere Szene
        row = obsSourceRow(true, ConnectionState::Connected, present);
        QCOMPARE(row.text, QStringLiteral("Quelle «Quelle» in OBS"));
        QVERIFY(row.canAct && !row.problem && row.addsToScene);
        QCOMPARE(row.buttonText, add);

        // F7: fehlt, wieder anlegen
        row = obsSourceRow(true, ConnectionState::Connected, missing);
        QCOMPARE(row.text, QStringLiteral("Quelle in OBS fehlt"));
        QVERIFY(row.canAct && row.problem && !row.addsToScene);
        QCOMPARE(row.buttonText, create);

        // Getrennt: letzter bekannter Stand, Knopf gesperrt
        row = obsSourceRow(true, ConnectionState::Error, present);
        QVERIFY(row.text.contains(QStringLiteral("gerade nicht verbunden")));
        QVERIFY(!row.canAct && row.problem);
        QCOMPARE(row.buttonText, add);
        row = obsSourceRow(true, ConnectionState::Error, none);
        QCOMPARE(row.text, connectionStateText(Connection::Obs, ConnectionState::Error));
        QVERIFY(!row.canAct && row.problem);
        row = obsSourceRow(true, ConnectionState::Connecting, none);
        QCOMPARE(row.text, connectionStateText(Connection::Obs, ConnectionState::Connecting));
        QVERIFY(!row.canAct && !row.problem);
        row = obsSourceRow(true, ConnectionState::Off, none);
        QCOMPARE(row.text, QStringLiteral("OBS: nicht eingerichtet oder abgeschaltet"));
        QVERIFY(!row.canAct && !row.problem);
        // Tooltip nennt den Grund für den gesperrten Knopf
        QVERIFY(row.buttonToolTip.contains(QStringLiteral("verbundenem OBS")));
    }

    // P10, Teil C
    void browserSourceTexts()
    {
        const QUuid a = QUuid::createUuid();
        const QUuid b = QUuid::createUuid();
        BrowserSourceField f = browserSourceField(QUuid(), std::nullopt);
        QVERIFY(f.text.isEmpty() && !f.canCopy);
        QCOMPARE(f.placeholder, QStringLiteral("Kein Overlay gewählt"));
        f = browserSourceField(a, BrowserSourceInfo{b, QStringLiteral("http://x/"), QString()});
        QVERIFY(f.text.isEmpty() && !f.canCopy); // Antwort zu einem anderen Overlay
        f = browserSourceField(a, BrowserSourceInfo{a, QStringLiteral("http://192.168.1.9:8081/o/a/t/"), QString()});
        QCOMPARE(f.text, QStringLiteral("http://192.168.1.9:8081/o/a/t/"));
        QVERIFY(f.canCopy);
        f = browserSourceField(a, BrowserSourceInfo{a, QString(), QStringLiteral("Keine Adresse")});
        QVERIFY(f.text.isEmpty() && !f.canCopy);
        QCOMPARE(f.placeholder, QStringLiteral("Keine Adresse"));

        QCOMPARE(outputStateText(std::nullopt, 3), QStringLiteral("Ausgabe: –"));
        QCOMPARE(outputStateText(true, std::nullopt), QStringLiteral("Ausgabe: ein"));
        QCOMPARE(outputStateText(true, 0), QStringLiteral("Ausgabe: ein, keine Browserquelle verbunden"));
        QCOMPARE(outputStateText(true, 1), QStringLiteral("Ausgabe: ein, 1 Browserquelle verbunden"));
        QCOMPARE(outputStateText(false, 2), QStringLiteral("Ausgabe: aus (Browserquelle leer), 2 Browserquellen verbunden"));
        QVERIFY(outputToolTip(true).startsWith(QStringLiteral("Ausgabe ein")));
        QVERIFY(outputToolTip(false).startsWith(QStringLiteral("Ausgabe aus")));

        ServerStatusInfo s;
        s.enabled = true;
        QCOMPARE(serverMessages(true, s).text, QString());
        QCOMPARE(serverMessages(true, std::nullopt).shortText, QString());
        s.problems = {QStringLiteral("Port 8081 ist belegt, vermutlich von DoNs Streaming Studio im Einsatz (Prozess 12). "
                                     "Die Browserquelle ist nicht erreichbar. Anderen Port wählen.")};
        s.invalidUse = QStringLiteral("Eine Browserquelle (192.168.1.116) benutzt eine ungültige Adresse (/o/a/…).");
        QCOMPARE(serverMessages(false, s).text, QString()); // ohne Verbindung nichts
        const ServerMessages m = serverMessages(true, s);
        QCOMPARE(m.text, s.problems.first() + QLatin1Char('\n') + s.invalidUse);
        QCOMPARE(m.shortText, QStringLiteral("Browserquelle: Port 8081 ist belegt, vermutlich von DoNs Streaming Studio im "
                                             "Einsatz (Pro… (+1)"));
        s.problems = {QStringLiteral("Die Adresse im Heimnetz 192.168.1.9 ist auf diesem PC gerade nicht vorhanden "
                                     "(Netzwerkkabel?). Die Browserquelle ist nur auf diesem PC erreichbar (127.0.0.1) "
                                     "und wird es dort, sobald die Adresse da ist.")};
        s.invalidUse.clear();
        // Punkte in der Adresse beenden den Satz nicht; höchstens 90 Zeichen
        QCOMPARE(serverMessages(true, s).shortText,
                 QStringLiteral("Browserquelle: Die Adresse im Heimnetz 192.168.1.9 ist auf diesem PC gerade nicht vorhand…"));
        QCOMPARE(serverMessages(true, s).shortText.size(), 90);
        s = ServerStatusInfo();
        QVERIFY(serverMessages(true, s).text.contains(QStringLiteral("ohne Overlay-Server")));
    }

    // P12b b5 (Entscheid 6 vom 2026-11-12): Zeile «Ton Videos» im Dock «Steuerung»
    void audioRouteRow()
    {
        const QString video = QString::fromLatin1(kAudioPurposeVideo);
        // Ohne Verbindung: nur «Kein Ausgang», ausgegraut, «–»
        AudioRouteRow row = dss::audioRouteRow(std::nullopt, std::nullopt, video);
        QCOMPARE(row.choices, (QList<AudioOutputChoice>{{QString(), QStringLiteral("Kein Ausgang")}}));
        QCOMPARE(row.current, 0);
        QCOMPARE(row.text, QStringLiteral("–"));
        QVERIFY(!row.canEdit);
        QVERIFY(!row.problem);

        // Neuer Kern: kein Ausgang gewählt, Versatz 0, Geräte nach «Kein Ausgang»
        SettingsInfo settings;
        AudioOutputsInfo outputs;
        outputs.enabled = true;
        outputs.devices = {{QStringLiteral("alsa_output.hdmi"), QStringLiteral("HDMI"), true},
                           {QStringLiteral("alsa_output.iec958"), QString(), false}};
        outputs.routes.insert(video, {QStringLiteral("no_target"), QString()});
        row = dss::audioRouteRow(settings, outputs, video);
        QCOMPARE(row.choices, (QList<AudioOutputChoice>{{QString(), QStringLiteral("Kein Ausgang")},
                                                       {QStringLiteral("alsa_output.hdmi"), QStringLiteral("HDMI (Standard)")},
                                                       {QStringLiteral("alsa_output.iec958"), QStringLiteral("alsa_output.iec958")}}));
        QCOMPARE(row.current, 0); // nie der Standardausgang vorgewählt
        QCOMPARE(row.offsetMs, 0);
        QCOMPARE(row.offsetMinMs, -500);
        QCOMPARE(row.offsetMaxMs, 500);
        QCOMPARE(row.text, QStringLiteral("Kein Ausgang gewählt"));
        QVERIFY(row.canEdit);
        QVERIFY(!row.problem);

        // Gewählt und bereit
        settings.settings.audioRoutes[video] = {QStringLiteral("alsa_output.iec958"), -120};
        outputs.routes[video] = {QStringLiteral("playing"), QString()};
        row = dss::audioRouteRow(settings, outputs, video);
        QCOMPARE(row.current, 2);
        QCOMPARE(row.offsetMs, -120);
        QCOMPARE(row.text, QStringLiteral("Ausgabe läuft"));
        QVERIFY(!row.problem);

        // Gewählter Ausgang fehlt: bleibt gewählt (eigener Eintrag), Hinweis rot wie die Zeile «OBS»
        settings.settings.audioRoutes[video].output = QStringLiteral("alsa_output.rodecaster");
        outputs.routes[video] = {QStringLiteral("missing"), QString()};
        row = dss::audioRouteRow(settings, outputs, video);
        QCOMPARE(row.choices.size(), 4);
        QCOMPARE(row.current, 3);
        QCOMPARE(row.choices.last().name, QStringLiteral("alsa_output.rodecaster"));
        QCOMPARE(row.choices.last().label, QStringLiteral("alsa_output.rodecaster (fehlt)"));
        QCOMPARE(row.text, QStringLiteral("Ausgang fehlt"));
        QVERIFY(row.problem);

        // Fehler mit Grund im Tooltip; PipeWire fehlt
        outputs.routes[video] = {QStringLiteral("error"), QStringLiteral("Verbindung zu einem anderen Gerät")};
        row = dss::audioRouteRow(settings, outputs, video);
        QCOMPARE(row.text, QStringLiteral("Fehler am Ausgang"));
        QCOMPARE(row.toolTip, QStringLiteral("Fehler am Ausgang: Verbindung zu einem anderen Gerät"));
        QVERIFY(row.problem);
        outputs.routes[video] = {QStringLiteral("no_pipewire"), QString()};
        QVERIFY(dss::audioRouteRow(settings, outputs, video).problem);
        outputs.routes[video] = {QStringLiteral("connecting"), QString()};
        QCOMPARE(dss::audioRouteRow(settings, outputs, video).text, QStringLiteral("Verbindungsversuch läuft"));

        // Kern ohne Ton gebaut: ausgegraut, kein Problem
        outputs = AudioOutputsInfo();
        row = dss::audioRouteRow(settings, outputs, video);
        QCOMPARE(row.text, QStringLiteral("Kern ohne Ton gebaut"));
        QVERIFY(!row.canEdit);
        QVERIFY(!row.problem);
    }
};

QTEST_GUILESS_MAIN(TestWindowData)
#include "tst_windowdata.moc"
