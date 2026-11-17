// P14 Emotes em-7: Emotes im Bereich «Chat» (Plan P14 Emotes, «Wer lädt: das Fenster», em-7).
// Das Fenster verbindet nur: dss::EmoteLoader lädt Listen und Bilder, dss::ChatRows erkennt die
// Emotes, dss::EmoteImages dekodiert, der Delegat in chatbotpage.cpp zeichnet. Kein Knopf, kein
// Eintrag in fenster.ini, kein Takt zum Neuladen (Entscheid des Nutzers 2026-11-16, Fragen 1 und 3).
// Ausnahme ohne Netz und ohne gespeicherte Listen: still neu laden nach 1, 5 und 15 Min.; die
// Meldung wartet, bis die Statuszeile frei ist (Entscheide des Nutzers 2026-11-16, Nacharbeit em-7).

#include "mainwindow.h"

#include "chatbotpage.h"
#include "coreclient.h"
#include "emoteloader.h"

#include <QStatusBar>
#include <QTimer>

#include <utility>

void MainWindow::enableEmotes(const dss::EmoteEndpoints &endpoints, const QString &cacheDir)
{
    if (m_emoteLoader || !m_client)
        return;
    m_emoteLoader = new dss::EmoteLoader(cacheDir, endpoints, this);
    m_chatBot->setEmoteImages(&m_emoteImages, &m_gifImages);
    // Neue Listen: Abschnitte aller Zeilen neu, Liste neu aufbauen, Bilder der Zeilen bestellen
    connect(m_emoteLoader, &dss::EmoteLoader::indexChanged, this, [this](const dss::EmoteIndex &index) {
        m_chatRows.setEmotes(index);
        m_chatBot->resetChat(m_chatRows.rows());
        requestEmoteImages(m_chatRows.rows());
    });
    // Text aus der Logik (emoteStatusText). Das Protokoll schreibt der Loader selbst: WebP fehlt
    // einmal, nicht erreichbare Anbieter je Liste, beides ohne Kanal-ID (emoteloader.cpp).
    connect(m_emoteLoader, &dss::EmoteLoader::stateChanged, this, &MainWindow::onEmoteState);
    // Wartende Meldung, sobald die andere Meldung abgelaufen ist (QStatusBar meldet dann "")
    // https://doc.qt.io/qt-6.8/qstatusbar.html#messageChanged
    connect(statusBar(), &QStatusBar::messageChanged, this, [this](const QString &message) {
        if (message.isEmpty() && !m_emoteStatusPending.isEmpty())
            showEmoteStatus(std::exchange(m_emoteStatusPending, QString()));
    });
    // Wiederholung ohne Netz: dieselbe Kanal-ID wie beim letzten Laden
    m_emoteRetryTimer = new QTimer(this);
    m_emoteRetryTimer->setSingleShot(true);
    connect(m_emoteRetryTimer, &QTimer::timeout, this, [this] {
        if (!m_emoteChannel)
            return;
        m_emoteRetrying = true;
        m_emoteLoader->load(m_emoteChannel->isEmpty() ? std::nullopt : std::optional<QString>(*m_emoteChannel),
                            dss::EmoteImages::webpAvailable());
    });
    // Leere Bytes oder nicht dekodierbar: das Wort bleibt Text (Zeile im Protokoll kommt von Loader
    // bzw. EmoteImages). Sonst nur neu zeichnen, keine neuen Zeilen.
    connect(m_emoteLoader, &dss::EmoteLoader::imageLoaded, this, [this](const QString &key, const QByteArray &bytes) {
        if (!bytes.isEmpty() && m_emoteImages.add(key, bytes))
            m_chatBot->emoteImagesChanged();
    });
    // GIFs (em-9) mit eigener Höhe und eigenen Grenzen; ohne Bild bleibt der Text des Fragments
    connect(m_emoteLoader, &dss::EmoteLoader::gifLoaded, this, [this](const QString &key, const QByteArray &bytes) {
        if (!bytes.isEmpty() && m_gifImages.add(key, bytes, dss::kGifImageLimits))
            m_chatBot->emoteImagesChanged();
    });
    connect(m_client, &dss::CoreClient::twitchStatusChanged, this, &MainWindow::loadEmotes);
    loadEmotes();
}

void MainWindow::loadEmotes()
{
    const std::optional<dss::TwitchStatusInfo> status = m_client->twitchStatus();
    if (!m_emoteLoader || !status)
        return;
    // Beim ersten twitch_status (ohne Kanal-ID nur die globalen Listen), danach nur bei einer
    // anderen Kanal-ID; ein Zustand ohne Kanal-ID lädt nicht nochmals.
    const QString channel = status->channel.userId;
    if (m_emoteChannel && (channel.isEmpty() || channel == *m_emoteChannel))
        return;
    m_emoteChannel = channel;
    // Neue Kanal-ID beendet eine laufende Reihe von Wiederholungen; bei Bedarf beginnt eine neue
    m_emoteRetryTimer->stop();
    m_emoteRetryNext = 0;
    m_emoteRetrying = false;
    m_emoteLoader->load(channel.isEmpty() ? std::nullopt : std::optional<QString>(channel),
                        dss::EmoteImages::webpAvailable());
}

void MainWindow::onEmoteState(const dss::EmoteLoadState &state)
{
    const bool retry = std::exchange(m_emoteRetrying, false);
    if (state.network == dss::EmoteLoadState::Network::OfflineEmpty) {
        // Nächster Versuch, nach dem letzten Abstand keiner mehr. Eine Wiederholung ohne Erfolg
        // bleibt still, die Meldung dazu steht schon (keine Meldung je Versuch).
        if (m_emoteRetryNext < m_emoteRetryDelays.size())
            m_emoteRetryTimer->start(m_emoteRetryDelays.at(m_emoteRetryNext++));
        if (retry)
            return;
    } else {
        // Erfolgreich geladen (auch Listen vom letzten Mal): Reihe beendet
        m_emoteRetryTimer->stop();
        m_emoteRetryNext = 0;
    }
    showEmoteStatus(dss::emoteStatusText(state));
}

void MainWindow::showEmoteStatus(const QString &text)
{
    if (text.isEmpty())
        return;
    // Die Meldung kommt ungefragt: Steht eine andere Meldung (innerhalb ihrer 10 s), wartet sie,
    // bis die Zeile frei ist; eine spätere ersetzt die wartende. Die eigene ersetzt sie sofort.
    const QString current = statusBar()->currentMessage();
    if (!current.isEmpty() && current != m_emoteStatusShown) {
        m_emoteStatusPending = text;
        return;
    }
    m_emoteStatusPending.clear();
    m_emoteStatusShown = text;
    showStatus(text);
}

void MainWindow::showChatChange(const dss::RowsAppend &chat)
{
    // Echo der eigenen Nachricht (em-3): die geänderte Zeile an ihrer Stelle, sonst die neue Zeile
    const QList<dss::ChatRow> &rows = m_chatRows.rows();
    const dss::ChatRow *row = nullptr;
    if (chat.changed >= 0 && chat.changed < rows.size())
        row = &rows.at(chat.changed);
    else if (chat.appended && !rows.isEmpty())
        row = &rows.last();
    m_chatBot->appendChat(chat, row ? *row : dss::ChatRow());
    if (row)
        requestEmoteImages({*row});
}

void MainWindow::requestEmoteImages(const QList<dss::ChatRow> &rows)
{
    if (!m_emoteLoader)
        return;
    for (const dss::ChatRow &row : rows) {
        for (const dss::ChatSegment &segment : row.segments) {
            if (!segment.emote)
                continue;
            // Derselbe Schlüssel wie im Delegaten, weil der Loader mit webpAvailable() lädt
            const QString key = chatEmoteImageKey(*segment.emote);
            if (key.isEmpty() || m_emoteRequested.contains(key) || m_emoteImages.contains(key))
                continue;
            m_emoteRequested.insert(key);
            m_emoteLoader->image(*segment.emote, true);
        }
        // GIFs (em-9): Schlüssel ist die unveränderte Adresse wie im Delegaten
        for (const dss::ChatGif &gif : dss::chatGifs(row.fragments)) {
            if (m_emoteRequested.contains(gif.url) || m_gifImages.contains(gif.url))
                continue;
            m_emoteRequested.insert(gif.url);
            m_emoteLoader->gif(gif.url);
        }
    }
}
