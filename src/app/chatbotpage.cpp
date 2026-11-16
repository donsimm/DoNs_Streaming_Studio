#include "chatbotpage.h"

#include "actionspage.h"
#include "commandspage.h"
#include "emoteimages.h"
#include "symbols.h"
#include "timerspage.h"
#include "headerbar.h"
#include "listdetailpage.h"
#include "panels.h"
#include "variablespage.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHash>
#include <QHeaderView>
#include <QHelpEvent>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

QTreeWidgetItem *eventItem(const dss::EventRow &row)
{
    return new QTreeWidgetItem(QStringList{row.time, row.kind, row.name, row.details});
}

// Daten einer Zeile «Läufe der Aktionen» für Auswahl und Knöpfe
constexpr int kRunIdRole = Qt::UserRole;
constexpr int kActionIdRole = Qt::UserRole + 1;
constexpr int kCancellableRole = Qt::UserRole + 2;

// Mitlaufen nur, wenn die Liste schon unten stand (sonst liest der Nutzer gerade weiter oben).
bool atBottom(QTreeWidget *list)
{
    const QScrollBar *bar = list->verticalScrollBar();
    return bar->value() == bar->maximum();
}

// Änderung nach dss::RowsAppend: vorne removedFront Zeilen weg, dann item (falls da) anhängen;
// nach unten rollen nur, wenn die Liste schon unten stand.
void appendItem(QTreeWidget *list, int removedFront, QTreeWidgetItem *item)
{
    const bool follow = atBottom(list);
    for (int i = 0; i < removedFront && list->topLevelItemCount() > 0; ++i)
        delete list->takeTopLevelItem(0);
    if (item) {
        list->addTopLevelItem(item);
        if (follow)
            list->scrollToBottom();
    }
}

// Daten einer Zeile «Chat» für den Delegaten
constexpr int kChatEventRole = Qt::UserRole;
constexpr int kChatBadgesRole = Qt::UserRole + 1;
constexpr int kChatNameRole = Qt::UserRole + 2;
constexpr int kChatColorRole = Qt::UserRole + 3;
constexpr int kChatTextRole = Qt::UserRole + 4;
constexpr int kChatSegmentsRole = Qt::UserRole + 5; // QList<dss::ChatSegment> (P14 Emotes em-6)
constexpr int kChatPlatformRole = Qt::UserRole + 6; // dss::ChatPlatform als int
constexpr int kChatGifsRole = Qt::UserRole + 7;     // QList<dss::ChatGif> (P14 Emotes em-9)
constexpr int kChatFragmentsRole = Qt::UserRole + 8; // bool: Zeile hat Fragmente (em-9)

// Takt der Animation (Plan P14 Emotes, «Annahmen»: 30 ms mit gemeinsamer Uhr)
constexpr int kEmoteTickMs = 30;
// Abstand zwischen Emote und Text bzw. Symbol und Abzeichen: 4 px wie zwischen den Abzeichen
constexpr int kChatGap = 4;

// Höhe des Kastens für YouTube im Verhältnis zum Symbol selbst: Die Datei youtube-farbig.svg
// enthält den Schutzraum (viewBox 602,187 × 515,868, das Symbol selbst von y 119,168 bis
// 396,570); so wirkt es so gross wie das Twitch-Symbol, ohne die Datei zu ändern.
constexpr double kYouTubeBoxFactor = 515.868 / (396.570 - 119.168);

void fillChatItem(QTreeWidgetItem *item, const dss::ChatRow &row)
{
    // Angezeigter Text auch als Text der Zelle (Suche, Tooltip, Tests); gezeichnet vom Delegaten
    const QString message = row.name.isEmpty() ? row.text : row.name + QStringLiteral(": ") + row.text;
    item->setText(0, row.time);
    item->setText(1, message);
    for (int column = 0; column < 2; ++column)
        item->setData(column, kChatEventRole, row.isEvent);
    item->setData(1, kChatBadgesRole, row.badges);
    item->setData(1, kChatNameRole, row.name);
    item->setData(1, kChatColorRole, row.color);
    item->setData(1, kChatTextRole, row.text);
    item->setData(1, kChatSegmentsRole, QVariant::fromValue(row.segments));
    item->setData(1, kChatPlatformRole, int(row.platform));
    item->setData(1, kChatGifsRole, QVariant::fromValue(dss::chatGifs(row.fragments)));
    item->setData(1, kChatFragmentsRole, !row.fragments.isEmpty());
}

// Höhe der GIF-Zeile unter dem Kopf (mit Abstand), 0 ohne GIF; Daten in Spalte 1
int gifLineHeight(const QModelIndex &index)
{
    return index.siblingAtColumn(1).data(kChatGifsRole).value<QList<dss::ChatGif>>().isEmpty()
               ? 0
               : dss::kGifHeight + kChatGap;
}

// Rahmen der Zelle ohne die GIF-Zeile: darin stehen Zeit und Kopf wie in einer Zeile ohne GIF
QStyleOptionViewItem headOption(const QStyleOptionViewItem &option, int gifLine)
{
    QStyleOptionViewItem head(option);
    head.rect.setHeight(option.rect.height() - gifLine);
    return head;
}

QTreeWidgetItem *chatItem(const dss::ChatRow &row)
{
    auto *item = new QTreeWidgetItem;
    fillChatItem(item, row);
    return item;
}

// Ein Stück der gezeichneten Nachricht: Text oder ein Emote; mit Zero-Width-Emotes darüber
// mehrere Bilder, mittig übereinander, Breite die grösste.
struct ChatPiece
{
    QString text;
    QList<dss::Emote> emotes;
    QList<const QImage *> images; // gilt nur während paint bzw. helpEvent (EmoteImages::frame)
    int x = 0;
    int width = 0;

    bool isEmote() const { return !images.isEmpty(); }
};

// Zeilen «Chat»: graue Ereigniszeilen (chatEventRow), Abzeichen in badgeBot/badgeChannel/badgeMod,
// Name in der Chatfarbe der Zeile. Wie ForegroundDelegate (panels.h) selbst gezeichnet, weil das Stylesheet
// Text- und Hintergrundfarbe aus den Rollen überdeckt. Farben nur aus dem Theme und der Chatfarbe.
// P14 Emotes em-6: vor einer Nachricht das farbige Symbol der Plattform, danach die Abschnitte
// (Text und Emotes). Ein Emote ohne Bild bleibt sein Name als Text. Jede Zeile hält den Platz
// des Symbols frei, auch ohne Symbol (Entscheid des Nutzers 2026-11-16): Namen und Texte der
// Ereigniszeilen stehen bündig in einer Spalte.
// P14 Emotes em-9: GIFs in einer eigenen Zeile unter dem Kopf, bündig mit dem Namen (wie
// Chatterino, Recherche 2026-11-16-twitch-gif, Abschnitt 6); ohne Bild der Text des Fragments.
class ChatRowDelegate : public QStyledItemDelegate
{
public:
    // clock, images und gifs gehören der Seite; *images = nullptr heisst nur Text.
    ChatRowDelegate(const Theme &theme, const QElapsedTimer *clock, const dss::EmoteImages *const *images,
                    const dss::EmoteImages *const *gifs, QObject *parent)
        : QStyledItemDelegate(parent), m_theme(theme), m_clock(clock), m_images(images), m_gifs(gifs)
    {
    }

    // Jede Zeile ohne GIF gleich hoch: mindestens ein Emote plus die Polsterung des Stylesheets
    // (#chatBotChat::item), die der Stil schon um die Textzeile legt. Mit GIF kommt die GIF-Zeile
    // dazu, auch solange das Bild fehlt (die Liste springt nicht, wenn es kommt).
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const int padding = size.height() - opt.fontMetrics.height();
        size.setHeight(qMax(size.height(), dss::kEmoteHeight + padding) + gifLineHeight(index));
        return size;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const bool event = index.data(kChatEventRole).toBool();
        if (event && !option.state.testFlag(QStyle::State_Selected))
            painter->fillRect(option.rect, m_theme.chatEventRow);
        const int gifLine = gifLineHeight(index);
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        if (index.column() == 0) {
            if (gifLine == 0) {
                QStyledItemDelegate::paint(painter, option, index);
                return;
            }
            // Zeit auf der Höhe des Kopfs, nicht mittig in der hohen Zeile: Hintergrund über die
            // ganze Zeile, danach die Zeit vom Stil im Rahmen ohne GIF-Zeile. Ohne Hover und
            // Auswahl zeichnet das Stylesheet dort keinen zweiten Hintergrund (QTreeView::item
            // ohne background in theme.cpp), die Textfarbe ist in allen Zuständen @text@.
            QStyleOptionViewItem head = headOption(opt, gifLine);
            opt.text.clear();
            style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
            head.state &= ~(QStyle::State_Selected | QStyle::State_MouseOver | QStyle::State_HasFocus);
            style->drawControl(QStyle::CE_ItemViewItem, &head, painter, widget);
            return;
        }
        const Layout layout = layoutRow(opt, index);
        opt.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);

        painter->save();
        painter->setClipRect(layout.gifRect.isNull() ? layout.textRect : layout.textRect | layout.gifRect);
        painter->setRenderHint(QPainter::Antialiasing);
        const QRect &textRect = layout.textRect;
        // Symbol unverändert (nicht eingefärbt, Seitenverhältnis der Datei), mittig in der Zeile
        if (!layout.symbol.isNull())
            painter->drawImage(QPoint(textRect.left(), textRect.top() + (textRect.height() - layout.symbol.height()) / 2),
                               layout.symbol);
        // Abzeichen: Kästchen mit Rundung 4 px wie die Listen, Innenabstand 4 px, Abstand 4 px
        painter->setFont(layout.badgeFont);
        for (const auto &[box, badge] : layout.badges) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(badgeColor(badge));
            painter->drawRoundedRect(box, 4, 4);
            painter->setPen(m_theme.text);
            painter->drawText(box, Qt::AlignCenter, badge);
        }
        // Name fett in der Chatfarbe (leer = Text), danach die Nachricht
        const QColor nameColor(index.data(kChatColorRole).toString());
        painter->setPen(nameColor.isValid() ? nameColor : m_theme.text);
        painter->drawText(QRect(layout.nameX, textRect.top(), textRect.right() - layout.nameX + 1, textRect.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, layout.name);
        painter->setFont(opt.font);
        painter->setPen(m_theme.text);
        for (const ChatPiece &piece : layout.pieces) {
            if (!piece.isEmote()) {
                painter->drawText(QRect(piece.x, textRect.top(), textRect.right() - piece.x + 1, textRect.height()),
                                  Qt::AlignLeft | Qt::AlignVCenter, piece.text);
                continue;
            }
            for (const QImage *image : piece.images)
                painter->drawImage(QPoint(piece.x + (piece.width - image->width()) / 2,
                                          textRect.top() + (textRect.height() - image->height()) / 2),
                                   *image);
        }
        if (layout.ellipsisX >= 0)
            painter->drawText(QRect(layout.ellipsisX, textRect.top(), textRect.right() - layout.ellipsisX + 1,
                                    textRect.height()),
                              Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("…"));
        // GIF-Zeile: Bild oben links, ohne Bild der Text auf der Höhe einer Textzeile
        for (const GifPiece &gif : layout.gifs) {
            if (gif.image)
                painter->drawImage(gif.rect.topLeft(), *gif.image);
            else
                painter->drawText(gif.rect, Qt::AlignLeft | Qt::AlignVCenter, gif.text);
        }
        painter->restore();
    }

    // Tooltip über einem Emote: «<Name> (<Anbieter>)» aus dss::emoteToolTip, bei Zero-Width eine
    // Zeile je Emote; über einem GIF der Text des Fragments; sonst wie bisher der Text der Zelle.
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view, const QStyleOptionViewItem &option,
                   const QModelIndex &index) override
    {
        if (event->type() == QEvent::ToolTip && index.column() == 1 && !index.data(kChatEventRole).toBool()) {
            QStyleOptionViewItem opt(option);
            initStyleOption(&opt, index);
            const Layout layout = layoutRow(opt, index);
            for (const GifPiece &gif : layout.gifs) {
                if (!gif.rect.contains(event->pos()) || gif.tip.isEmpty())
                    continue;
                QToolTip::showText(event->globalPos(), gif.tip, view);
                return true;
            }
            if (event->pos().y() > layout.textRect.bottom())
                return QStyledItemDelegate::helpEvent(event, view, option, index);
            for (const ChatPiece &piece : layout.pieces) {
                if (!piece.isEmote() || event->pos().x() < piece.x || event->pos().x() >= piece.x + piece.width)
                    continue;
                QStringList tips;
                for (const dss::Emote &emote : piece.emotes)
                    tips.append(dss::emoteToolTip(emote));
                QToolTip::showText(event->globalPos(), tips.join(QLatin1Char('\n')), view);
                return true;
            }
        }
        return QStyledItemDelegate::helpEvent(event, view, option, index);
    }

private:
    // Ein GIF der GIF-Zeile: mit Bild dessen Lage, ohne Bild der Text (gekürzt) an rect
    struct GifPiece
    {
        QRect rect;
        const QImage *image = nullptr; // gilt nur während paint bzw. helpEvent
        QString text;
        QString tip; // Text des Fragments
    };
    struct Layout
    {
        QRect textRect; // Kopf der Nachricht
        QRect gifRect;  // GIF-Zeile, leer ohne GIF
        QList<GifPiece> gifs;
        QImage symbol;
        QFont badgeFont;
        QList<QPair<QRect, QString>> badges;
        int nameX = 0;
        QString name;
        QList<ChatPiece> pieces;
        int ellipsisX = -1; // «…» an Stelle eines Emotes, das nicht mehr ganz passt
    };

    // Lage aller Teile einer Nachricht; gemeinsam für paint und helpEvent.
    Layout layoutRow(const QStyleOptionViewItem &opt, const QModelIndex &index) const
    {
        Layout layout;
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        // Rand des Textes wie QCommonStyle (viewItemDrawText): PM_FocusFrameHMargin + 1. Mit GIF
        // gilt für den Kopf der Rahmen ohne GIF-Zeile (headOption).
        const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, widget) + 1;
        const int gifLine = gifLineHeight(index);
        const QStyleOptionViewItem head = headOption(opt, gifLine);
        layout.textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &head, widget)
                              .adjusted(margin, 0, -margin, 0);
        const QRect &textRect = layout.textRect;
        const QFontMetrics &fm = opt.fontMetrics;
        const int height = fm.height();
        const int top = textRect.top() + (textRect.height() - height) / 2;
        int x = textRect.left();

        // Graue Ereigniszeile: ohne Symbol, Abzeichen und Namen, nur der Text der Zelle
        const bool event = index.data(kChatEventRole).toBool();
        if (!event)
            layout.symbol = platformSymbol(dss::ChatPlatform(index.data(kChatPlatformRole).toInt()), height);
        x += symbolColumn(height);

        layout.badgeFont = opt.font;
        layout.badgeFont.setBold(true);
        layout.nameX = x;
        QList<dss::ChatSegment> segments;
        if (event) {
            segments.append(dss::ChatSegment{index.data(Qt::DisplayRole).toString(), std::nullopt});
        } else {
            const QFontMetrics badgeMetrics(layout.badgeFont);
            for (const QString &badge : index.data(kChatBadgesRole).toStringList()) {
                const QRect box(x, top, badgeMetrics.horizontalAdvance(badge) + 8, height);
                layout.badges.append({box, badge});
                x = box.right() + 1 + kChatGap;
            }
            layout.nameX = x;
            layout.name = index.data(kChatNameRole).toString() + QStringLiteral(": ");
            x += badgeMetrics.horizontalAdvance(layout.name);
            // Mit Fragmenten gilt die Liste der Abschnitte, auch leer (Nachricht nur aus GIFs:
            // Kopf nur «Name:»); der Zellentext nur ohne Fragmente
            segments = index.data(kChatSegmentsRole).value<QList<dss::ChatSegment>>();
            if (segments.isEmpty() && !index.data(kChatFragmentsRole).toBool())
                segments.append(dss::ChatSegment{index.data(kChatTextRole).toString(), std::nullopt});
        }
        QList<ChatPiece> pieces = chatPieces(segments);

        // Eine Zeile wie bisher: Text mit «…» gekürzt; ein Emote, das nicht ganz passt, fällt weg
        // und an seiner Stelle steht «…».
        const int right = textRect.right() + 1;
        for (int i = 0; i < pieces.size(); ++i) {
            ChatPiece &piece = pieces[i];
            if (i > 0 && (piece.isEmote() || pieces.at(i - 1).isEmote()))
                x += kChatGap;
            piece.width = piece.isEmote() ? 0 : fm.horizontalAdvance(piece.text);
            for (const QImage *image : std::as_const(piece.images))
                piece.width = qMax(piece.width, image->width());
            piece.x = x;
            if (x + piece.width > right) {
                if (piece.isEmote()) {
                    layout.ellipsisX = x;
                    pieces.resize(i);
                } else {
                    piece.text = fm.elidedText(piece.text, Qt::ElideRight, right - x);
                    piece.width = fm.horizontalAdvance(piece.text);
                    pieces.resize(i + 1);
                }
                break;
            }
            x += piece.width;
        }
        layout.pieces = pieces;
        if (gifLine > 0 && !event)
            layoutGifs(&layout, index, fm, right);
        return layout;
    }

    // GIF-Zeile unter dem Kopf. Decided (Nutzer, 2026-11-16): Alle GIFs beginnen in derselben
    // Spalte, dem Beginn von Abzeichen und Namen (nach der Spalte des Symbols), auch wenn die
    // Zeile Abzeichen hat; GIFs nebeneinander mit kChatGap. Was nicht mehr passt, wird gekürzt: ein Bild
    // abgeschnitten (Clip), Text mit «…».
    void layoutGifs(Layout *layout, const QModelIndex &index, const QFontMetrics &fm, int right) const
    {
        const QRect &textRect = layout->textRect;
        const int left = textRect.left() + symbolColumn(fm.height());
        layout->gifRect = QRect(left, textRect.bottom() + 1 + kChatGap, right - left, dss::kGifHeight);
        const qint64 elapsed = m_clock->elapsed();
        int x = left;
        for (const dss::ChatGif &gif : index.data(kChatGifsRole).value<QList<dss::ChatGif>>()) {
            if (x >= right)
                break;
            GifPiece piece;
            piece.tip = gif.text;
            piece.image = *m_gifs ? (*m_gifs)->frame(gif.url, elapsed) : nullptr;
            if (piece.image) {
                piece.rect = QRect(QPoint(x, layout->gifRect.top()), piece.image->size());
            } else {
                piece.text = fm.elidedText(gif.text, Qt::ElideRight, right - x);
                // Tooltip-Bereich nur so hoch wie die Textzeile; die GIF-Zeile bleibt reserviert
                piece.rect = QRect(x, layout->gifRect.top(), fm.horizontalAdvance(piece.text), textRect.height());
            }
            x = piece.rect.right() + 1 + kChatGap;
            layout->gifs.append(piece);
        }
    }

    // Abschnitte → Stücke. Emote mit Bild: eigenes Stück; ohne Bild der Name als Text. Ein
    // Zero-Width-Emote liegt über dem letzten Emote, wenn dazwischen nur Leerzeichen stehen (die
    // fallen dann weg). Neben einem Emote ersetzt der feste Abstand kChatGap ein Leerzeichen.
    QList<ChatPiece> chatPieces(const QList<dss::ChatSegment> &segments) const
    {
        QList<ChatPiece> pieces;
        const qint64 elapsed = m_clock->elapsed();
        int lastEmote = -1;
        for (const dss::ChatSegment &segment : segments) {
            const QImage *image = segment.emote && *m_images
                                      ? (*m_images)->frame(chatEmoteImageKey(*segment.emote), elapsed)
                                      : nullptr;
            if (!image) {
                if (!pieces.isEmpty() && !pieces.last().isEmote())
                    pieces.last().text += segment.text;
                else
                    pieces.append(ChatPiece{segment.text, {}, {}});
                if (!segment.text.trimmed().isEmpty())
                    lastEmote = -1;
                continue;
            }
            if (segment.emote->zeroWidth && lastEmote >= 0) {
                pieces.resize(lastEmote + 1);
                pieces[lastEmote].emotes.append(*segment.emote);
                pieces[lastEmote].images.append(image);
                continue;
            }
            pieces.append(ChatPiece{{}, {*segment.emote}, {image}});
            lastEmote = int(pieces.size()) - 1;
        }
        for (int i = 0; i < pieces.size(); ++i) {
            ChatPiece &piece = pieces[i];
            if (piece.isEmote())
                continue;
            if (i > 0 && pieces.at(i - 1).isEmote() && piece.text.startsWith(QLatin1Char(' ')))
                piece.text.remove(0, 1);
            if (i + 1 < pieces.size() && pieces.at(i + 1).isEmote() && piece.text.endsWith(QLatin1Char(' ')))
                piece.text.chop(1);
        }
        pieces.removeIf([](const ChatPiece &piece) { return !piece.isEmote() && piece.text.isEmpty(); });
        return pieces;
    }

    // Farbiges Symbol der Plattform (Nachtrag Plattform-Symbol im Plan): Datei über symbolPath
    // (zuerst der Ordner des Nutzers), unverändert gezeichnet, Seitenverhältnis der Datei, so hoch
    // wie die Textzeile; YouTube im grösseren Kasten (kYouTubeBoxFactor). Leer ohne Plattform.
    QImage platformSymbol(dss::ChatPlatform platform, int height) const
    {
        if (platform == dss::ChatPlatform::None)
            return {};
        const double factor = platform == dss::ChatPlatform::YouTube ? kYouTubeBoxFactor : 1.0;
        const int boxHeight = qRound(height * factor);
        Symbol &cached = m_symbols[int(platform)];
        if (cached.height == boxHeight)
            return cached.image;
        cached.height = boxHeight;
        cached.image = QImage();
        const QString path = symbolPath(symbolFile(platform));
        QImageReader reader(path);
        const QSize size = reader.size();
        if (size.isValid()) {
            reader.setScaledSize(size.scaled(QSize(boxHeight * 4, boxHeight), Qt::KeepAspectRatio));
            cached.image = reader.read();
        }
        if (cached.image.isNull())
            qWarning("Symbol nicht lesbar: %s", qPrintable(path));
        return cached.image;
    }

    static QString symbolFile(dss::ChatPlatform platform)
    {
        switch (platform) {
        case dss::ChatPlatform::None:
            break;
        case dss::ChatPlatform::Twitch:
            return QStringLiteral("twitch-farbig.svg");
        case dss::ChatPlatform::YouTube:
            return QStringLiteral("youtube-farbig.svg");
        }
        return {};
    }

    // Breite der Symbolspalte, die jede Zeile freihält: das breiteste Symbol der Plattformen,
    // deren Datei es gibt (youtube-farbig.svg erst mit der Anbindung), plus Abstand. Neu nur bei
    // anderer Höhe, nicht bei jedem Zeichnen.
    int symbolColumn(int height) const
    {
        if (m_symbolColumn.height != height) {
            int width = 0;
            for (const dss::ChatPlatform platform : {dss::ChatPlatform::Twitch, dss::ChatPlatform::YouTube}) {
                if (QFileInfo::exists(symbolPath(symbolFile(platform))))
                    width = qMax(width, platformSymbol(platform, height).width());
            }
            m_symbolColumn = {height, width > 0 ? width + kChatGap : 0};
        }
        return m_symbolColumn.width;
    }

    QColor badgeColor(const QString &badge) const
    {
        // Texte der Abzeichen aus dss::chatRow (chatbotlive.cpp)
        if (badge == QLatin1String("Bot"))
            return m_theme.badgeBot;
        if (badge == QLatin1String("Mod"))
            return m_theme.badgeMod;
        return m_theme.badgeChannel;
    }

    struct Symbol
    {
        int height = -1;
        QImage image;
    };
    struct SymbolColumn
    {
        int height = -1;
        int width = 0;
    };

    Theme m_theme;
    const QElapsedTimer *m_clock;
    const dss::EmoteImages *const *m_images;
    const dss::EmoteImages *const *m_gifs;
    mutable QHash<int, Symbol> m_symbols; // je Plattform, neu nur bei anderer Höhe
    mutable SymbolColumn m_symbolColumn;
};

} // namespace

QString chatEmoteImageKey(const dss::Emote &emote)
{
    return dss::emoteImageUrl(emote, true, dss::EmoteImages::webpAvailable()).toString();
}

ChatBotPage::ChatBotPage(const Theme &theme, QWidget *parent) : QWidget(parent), m_theme(theme)
{
    setObjectName(QStringLiteral("chatBotPage"));
    setAttribute(Qt::WA_StyledBackground);
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(6);

    // Kopfzeile mit fester Höhe (Regel 9): Ein neuer Zustand ändert nur den Text, nie die Anordnung.
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("chatBotHeader"));
    header->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *headerRow = new QHBoxLayout(header);
    headerRow->setContentsMargins(0, 0, 0, 0);
    headerRow->setSpacing(6);
    auto *tabs = new QFrame(header);
    tabs->setObjectName(QStringLiteral("chatBotTabs"));
    m_tabRow = new QHBoxLayout(tabs);
    m_tabRow->setContentsMargins(2, 2, 2, 2);
    m_tabRow->setSpacing(2);
    m_tabs = new QButtonGroup(this);
    m_tabs->setExclusive(true);
    headerRow->addWidget(tabs, 0, Qt::AlignVCenter);
    headerRow->addStretch(1);
    // P14 e2-11: Stand und Freigabe nur aus setHeader; ein Klick meldet nur den Wunsch, den Stand
    // setzt erst die Antwort des Kerns (actions_status).
    m_enabled = new QCheckBox(QStringLiteral("Chat-Bot aktiv"), header);
    m_enabled->setObjectName(QStringLiteral("chatBotEnabled"));
    m_enabled->setEnabled(false);
    connect(m_enabled, &QCheckBox::clicked, this, &ChatBotPage::enabledClicked);
    headerRow->addWidget(m_enabled, 0, Qt::AlignVCenter);
    // Eine Zeile ohne Umbruch; ein langer Text wird abgeschnitten, Einzelheiten im Tooltip.
    m_state = new QLabel(header);
    m_state->setObjectName(QStringLiteral("chatBotState"));
    m_state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_state->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    headerRow->addWidget(m_state, 1);
    column->addWidget(header);

    m_pages = new QStackedWidget(this);
    column->addWidget(m_pages, 1);
    connect(m_tabs, &QButtonGroup::idClicked, m_pages, &QStackedWidget::setCurrentIndex);
    connect(m_pages, &QStackedWidget::currentChanged, this, &ChatBotPage::subTabChanged);

    // P14 e2-12: Bereiche mit verschiebbaren Teilern (QSplitter::handle im Theme wie der Teiler
    // der Docks), keiner lässt sich ganz zuschieben (wie ActionsPage).
    m_liveSplitter = new QSplitter(Qt::Horizontal, m_pages);
    m_liveSplitter->setObjectName(QStringLiteral("chatBotLiveSplitter"));
    m_liveSplitter->setChildrenCollapsible(false);
    addSubTab(QStringLiteral("Live"), m_liveSplitter);
    m_tabs->button(Live)->setChecked(true);

    m_events = new QTreeWidget;
    m_events->setObjectName(QStringLiteral("chatBotEvents"));
    m_events->setAccessibleName(QStringLiteral("Ereignisse"));
    m_events->setColumnCount(4);
    m_events->setHeaderLabels({QStringLiteral("Zeit"), QStringLiteral("Art"), QStringLiteral("Name"),
                               QStringLiteral("Details")});
    m_events->setRootIsDecorated(false);
    m_events->setUniformRowHeights(true);
    m_events->setSelectionMode(QAbstractItemView::SingleSelection);
    m_events->setFocusPolicy(Qt::NoFocus);
    // Feste Spaltenbreiten aus der Schrift (keine Anpassung an den Inhalt bei jedem Ereignis).
    const int digit = m_events->fontMetrics().horizontalAdvance(QLatin1Char('0'));
    m_events->header()->setStretchLastSection(true);
    m_events->setColumnWidth(0, digit * 10);
    m_events->setColumnWidth(1, digit * 16);
    m_events->setColumnWidth(2, digit * 16);
    // P14 e2-13: Leiste mit «Chat-Nachrichten ausblenden», Standard wie dss::EventRows
    m_hideChat = new QCheckBox(QStringLiteral("Chat-Nachrichten ausblenden"));
    m_hideChat->setObjectName(QStringLiteral("chatBotHideChat"));
    m_hideChat->setAccessibleName(QStringLiteral("Chat-Nachrichten ausblenden"));
    m_hideChat->setChecked(dss::EventRows().hideChat());
    connect(m_hideChat, &QCheckBox::toggled, this, &ChatBotPage::hideChatToggled);
    auto *eventsBox = new QWidget;
    auto *eventsColumn = new QVBoxLayout(eventsBox);
    eventsColumn->setContentsMargins(0, 0, 0, 0);
    eventsColumn->setSpacing(0);
    eventsColumn->addWidget(m_events, 1);
    eventsColumn->addWidget(buttonBar({m_hideChat}, {}, eventsBox));
    addLivePanel(QStringLiteral("Ereignisse"), eventsBox, 1);

    // Bereich «Läufe der Aktionen»: Tabelle wie «Ereignisse», darunter die Knopfleiste
    m_runs = new QTreeWidget;
    m_runs->setObjectName(QStringLiteral("chatBotRuns"));
    m_runs->setAccessibleName(QStringLiteral("Läufe der Aktionen"));
    m_runs->setColumnCount(4);
    m_runs->setHeaderLabels({QStringLiteral("Zeit"), QStringLiteral("Aktion"), QStringLiteral("Auslöser"),
                             QStringLiteral("Ergebnis")});
    m_runs->setRootIsDecorated(false);
    m_runs->setUniformRowHeights(true);
    m_runs->setSelectionMode(QAbstractItemView::SingleSelection);
    m_runs->setFocusPolicy(Qt::NoFocus);
    // Breiten je Spalte: Zeit hh:mm:ss, ein Aktionsname, der längste Auslöser («Aktion ausführen»)
    m_runs->header()->setStretchLastSection(true);
    m_runs->setColumnWidth(0, digit * 10);
    m_runs->setColumnWidth(1, digit * 20);
    m_runs->setColumnWidth(2, digit * 18);
    m_runs->setItemDelegateForColumn(3, new ForegroundDelegate(m_runs));
    connect(m_runs, &QTreeWidget::itemSelectionChanged, this, &ChatBotPage::refreshRunButtons);

    m_openAction = new QPushButton(QStringLiteral("Im Editor öffnen"));
    m_openAction->setObjectName(QStringLiteral("chatBotRunOpenAction"));
    m_cancelRun = new QPushButton(QStringLiteral("Lauf abbrechen"));
    m_cancelRun->setObjectName(QStringLiteral("chatBotRunCancel"));
    m_onlyErrors = new QCheckBox(QStringLiteral("Nur Fehler"));
    m_onlyErrors->setObjectName(QStringLiteral("chatBotRunsOnlyErrors"));
    connect(m_openAction, &QPushButton::clicked, this, [this] {
        if (const QTreeWidgetItem *item = m_runs->currentItem(); item && item->isSelected())
            emit openActionClicked(item->data(0, kActionIdRole).toUuid());
    });
    connect(m_cancelRun, &QPushButton::clicked, this, [this] {
        if (const QTreeWidgetItem *item = m_runs->currentItem(); item && item->isSelected())
            emit cancelRunClicked(item->data(0, kRunIdRole).toUuid());
    });
    connect(m_onlyErrors, &QCheckBox::toggled, this, &ChatBotPage::onlyErrorsToggled);

    auto *runsBox = new QWidget;
    auto *runsColumn = new QVBoxLayout(runsBox);
    runsColumn->setContentsMargins(0, 0, 0, 0);
    runsColumn->setSpacing(0);
    runsColumn->addWidget(m_runs, 1);
    runsColumn->addWidget(buttonBar({m_openAction, m_cancelRun}, {m_onlyErrors}, runsBox));
    addLivePanel(QStringLiteral("Läufe der Aktionen"), runsBox, 1);
    refreshRunButtons();

    // P14 e2-13: Bereich «Chat» links: Liste wie «Ereignisse» ohne Spaltenkopf (Zeit, Nachricht),
    // darunter die Eingabezeile als Leiste. Texte, Zähler und Freigabe nur aus setChatSend.
    m_chat = new QTreeWidget;
    m_chat->setObjectName(QStringLiteral("chatBotChat"));
    m_chat->setAccessibleName(QStringLiteral("Chat"));
    m_chat->setColumnCount(2);
    m_chat->setHeaderHidden(true);
    m_chat->setRootIsDecorated(false);
    // P14 Emotes em-9: Zeilen mit GIF sind um die GIF-Zeile höher (Plan em-9: eigene Zeile unter
    // dem Kopf), darum keine gleichen Zeilenhöhen; höchstens dss::ChatRows::kMaxRows Zeilen
    m_chat->setUniformRowHeights(false);
    m_chat->setSelectionMode(QAbstractItemView::SingleSelection);
    m_chat->setFocusPolicy(Qt::NoFocus);
    m_chat->header()->setStretchLastSection(true);
    // P14 Emotes em-6: Schrift der Liste aus dem Stylesheet (#chatBotChat, Theme fontSizeChat);
    // die Breite der Zeitspalte setzt eventFilter, sobald die Schrift gilt
    m_emoteClock.start();
    m_chat->setItemDelegate(new ChatRowDelegate(m_theme, &m_emoteClock, &m_emoteImages, &m_gifImages, m_chat));
    // Gemeinsamer Takt: nur neu zeichnen, die Bilder wählt der Delegat nach der Uhr
    m_emoteTimer = new QTimer(this);
    m_emoteTimer->setObjectName(QStringLiteral("chatBotEmoteTimer"));
    m_emoteTimer->setInterval(kEmoteTickMs);
    connect(m_emoteTimer, &QTimer::timeout, m_chat->viewport(), qOverload<>(&QWidget::update));
    m_chat->installEventFilter(this);

    m_chatAccount = new QComboBox;
    m_chatAccount->setObjectName(QStringLiteral("chatBotChatAccount"));
    m_chatAccount->setAccessibleName(QStringLiteral("Konto für den Chat"));
    m_chatText = new QLineEdit;
    m_chatText->setObjectName(QStringLiteral("chatBotChatText"));
    m_chatText->setAccessibleName(QStringLiteral("Nachricht an den Chat"));
    m_chatText->setPlaceholderText(QStringLiteral("Nachricht an den Chat …"));
    // Breite für «500 / 500»: Der Wechsel des Zählers ändert die Anordnung nicht (Regel 9).
    m_chatCounter = new QLabel;
    m_chatCounter->setObjectName(QStringLiteral("chatBotChatCounter"));
    m_chatCounter->setAccessibleName(QStringLiteral("Zeichen der Nachricht"));
    m_chatCounter->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_chatCounter->setText(QStringLiteral("%1 / %1").arg(dss::kChatSendMaxLength));
    m_chatCounter->setMinimumWidth(m_chatCounter->sizeHint().width());
    m_chatSend = new QPushButton(QStringLiteral("Senden"));
    m_chatSend->setObjectName(QStringLiteral("chatBotChatSend"));
    m_chatSend->setEnabled(false);
    connect(m_chatText, &QLineEdit::textChanged, this, &ChatBotPage::chatInputChanged);
    connect(m_chatAccount, &QComboBox::currentIndexChanged, this, &ChatBotPage::chatInputChanged);
    connect(m_chatText, &QLineEdit::returnPressed, this, &ChatBotPage::sendChat);
    connect(m_chatSend, &QPushButton::clicked, this, &ChatBotPage::sendChat);

    auto *chatBox = new QWidget;
    auto *chatColumn = new QVBoxLayout(chatBox);
    chatColumn->setContentsMargins(0, 0, 0, 0);
    chatColumn->setSpacing(0);
    chatColumn->addWidget(m_chat, 1);
    // Das Eingabefeld nimmt den freien Platz statt der Dehnung von buttonBar.
    chatColumn->addWidget(buttonBar({m_chatAccount, m_chatText}, {m_chatCounter, m_chatSend}, chatBox, m_chatText));
    addLivePanel(QStringLiteral("Chat"), chatBox, 1, 0);

    m_actions = new ActionsPage(m_theme, m_pages);
    addSubTab(QStringLiteral("Aktionen"), m_actions);

    // P14 e3-4: «Befehle» und «Zeitnachrichten» als Tabelle links, Gewähltes rechts. Spalten aus
    // CommandRow bzw. TimerRow (actionview.h), Breiten in Ziffern der Schrift. P14 e3-5: Aufbau und
    // Formular von «Befehle» in commandspage.h.
    m_commands = new ListDetailPage(m_theme, commandsPageSetup(), m_pages);
    m_commandForm = new CommandForm;
    m_commands->setDetailForm(m_commandForm);
    m_commands->setDetailBar(m_commandForm->testBar());
    addSubTab(QStringLiteral("Befehle"), m_commands);

    // P14 e3-6: Aufbau und Formular von «Zeitnachrichten» in timerspage.h
    m_timers = new ListDetailPage(m_theme, timersPageSetup(), m_pages);
    m_timerForm = new TimerForm;
    m_timers->setDetailForm(m_timerForm);
    addSubTab(QStringLiteral("Zeitnachrichten"), m_timers);

    // «Variablen»: «Für alle» und «Je Zuschauer» nebeneinander mit Teiler wie «Live»
    // (Entscheid des Nutzers 2026-11-16). P14 e3-7: Aufbau in variablespage.h.
    m_variables = new VariablesPage(m_pages);
    addSubTab(QStringLiteral("Variablen"), m_variables);
}

int ChatBotPage::addSubTab(const QString &title, QWidget *page)
{
    const int id = m_pages->addWidget(page);
    QToolButton *button = moduleButton(title, m_tabRow->parentWidget());
    button->setObjectName(QStringLiteral("chatBotTab%1").arg(id));
    m_tabs->addButton(button, id);
    m_tabRow->addWidget(button);
    return id;
}

QWidget *ChatBotPage::addLivePanel(const QString &title, QWidget *content, int stretch, int position)
{
    QWidget *panel = dockPanel(title, content, nullptr);
    m_liveSplitter->insertWidget(position, panel);
    m_liveSplitter->setStretchFactor(m_liveSplitter->indexOf(panel), stretch);
    return panel;
}

void ChatBotPage::setCurrentSubTab(SubTab tab)
{
    m_tabs->button(tab)->setChecked(true);
    m_pages->setCurrentIndex(tab);
}

void ChatBotPage::setHeader(const dss::ChatBotHeaderView &view)
{
    m_state->setText(view.text);
    m_state->setToolTip(view.toolTip);
    m_state->setProperty("state", view.state);
    // Das Stylesheet wertet [state="…"] nur beim Polieren aus (wie HeaderBar::setConnectionState).
    m_state->style()->unpolish(m_state);
    m_state->style()->polish(m_state);
    m_enabled->setChecked(view.switchChecked);
    m_enabled->setEnabled(view.switchEnabled);
}

void ChatBotPage::resetEvents(const QList<dss::EventRow> &rows)
{
    m_events->clear();
    QList<QTreeWidgetItem *> items;
    items.reserve(rows.size());
    for (const dss::EventRow &row : rows)
        items.append(eventItem(row));
    m_events->addTopLevelItems(items);
    m_events->scrollToBottom();
}

void ChatBotPage::appendEvent(const dss::RowsAppend &change, const dss::EventRow &row)
{
    appendItem(m_events, change.removedFront, change.appended ? eventItem(row) : nullptr);
}

void ChatBotPage::resetChat(const QList<dss::ChatRow> &rows)
{
    // Läuft auch bei neuen Emote-Listen (em-7): wie appendItem nur nach unten rollen, wenn die
    // Liste schon unten stand (leere Liste gilt als unten); sonst die Leseposition behalten.
    QScrollBar *bar = m_chat->verticalScrollBar();
    const bool follow = atBottom(m_chat);
    const int position = bar->value();
    m_chat->clear();
    QList<QTreeWidgetItem *> items;
    items.reserve(rows.size());
    for (const dss::ChatRow &row : rows)
        items.append(chatItem(row));
    m_chat->addTopLevelItems(items);
    if (follow) {
        m_chat->scrollToBottom();
    } else {
        // Bereich erst nach dem Layout der neuen Zeilen bekannt
        m_chat->doItemsLayout();
        bar->setValue(position);
    }
}

void ChatBotPage::appendChat(const dss::RowsAppend &change, const dss::ChatRow &row)
{
    // Echo der eigenen Nachricht: dieselbe Zeile mit neuen Abschnitten und Chatfarbe
    if (change.changed >= 0) {
        if (QTreeWidgetItem *item = m_chat->topLevelItem(change.changed))
            fillChatItem(item, row);
    }
    appendItem(m_chat, change.removedFront, change.appended ? chatItem(row) : nullptr);
}

void ChatBotPage::setEmoteImages(const dss::EmoteImages *images, const dss::EmoteImages *gifs)
{
    m_emoteImages = images; // liest der Delegat
    m_gifImages = gifs;
    emoteImagesChanged();
}

void ChatBotPage::emoteImagesChanged()
{
    refreshEmoteTimer();
    m_chat->viewport()->update();
}

void ChatBotPage::refreshEmoteTimer()
{
    // isVisible: Modul «Chat-Bot», Unterreiter «Live» und das Fenster sind sichtbar
    const bool run = ((m_emoteImages && m_emoteImages->hasAnimated()) || (m_gifImages && m_gifImages->hasAnimated()))
                     && m_chat->isVisible();
    if (run && !m_emoteTimer->isActive())
        m_emoteTimer->start();
    else if (!run)
        m_emoteTimer->stop();
}

bool ChatBotPage::eventFilter(QObject *watched, QEvent *event)
{
    // Show und Hide kommen auch, wenn ein Elternteil (Unterreiter, Modul, Fenster) wechselt
    if (watched == m_chat && event->type() == QEvent::Show)
        refreshEmoteTimer();
    else if (watched == m_chat && event->type() == QEvent::Hide)
        m_emoteTimer->stop();
    // Zeitspalte in Ziffern der Schrift der Liste: Das Stylesheet setzt sie erst beim Polieren
    // (FontChange), im Konstruktor gilt noch die Schrift der Anwendung. Polish ohne Stylesheet.
    else if (watched == m_chat && (event->type() == QEvent::Polish || event->type() == QEvent::FontChange))
        m_chat->setColumnWidth(0, m_chat->fontMetrics().horizontalAdvance(QLatin1Char('0')) * 10);
    return QWidget::eventFilter(watched, event);
}

void ChatBotPage::setChatSend(const dss::ChatSendView &view)
{
    m_chatCounter->setText(view.counter);
    m_chatCounter->setProperty("state", view.tooLong ? QStringLiteral("error") : QString());
    // Das Stylesheet wertet [state="…"] nur beim Polieren aus (wie setHeader).
    m_chatCounter->style()->unpolish(m_chatCounter);
    m_chatCounter->style()->polish(m_chatCounter);
    m_chatSendEnabled = view.sendEnabled;
    m_chatSend->setEnabled(view.sendEnabled);
    m_chatSend->setToolTip(view.problem);
    m_chatText->setToolTip(view.problem);
    // Konten: beim ersten Mal anlegen; ein ausgegrautes gewähltes Konto bleibt gewählt (problem).
    const QSignalBlocker block(m_chatAccount);
    if (m_chatAccount->count() != view.accounts.size()) {
        m_chatAccount->clear();
        for (const dss::ChatSendAccount &account : view.accounts)
            m_chatAccount->addItem(account.label, account.account);
    }
    auto *model = qobject_cast<QStandardItemModel *>(m_chatAccount->model());
    for (int i = 0; i < view.accounts.size(); ++i) {
        const dss::ChatSendAccount &account = view.accounts.at(i);
        m_chatAccount->setItemText(i, account.label);
        m_chatAccount->setItemData(i, account.account);
        m_chatAccount->setItemData(i, account.toolTip, Qt::ToolTipRole);
        if (QStandardItem *item = model ? model->item(i) : nullptr)
            item->setEnabled(account.enabled);
    }
}

QString ChatBotPage::chatText() const
{
    return m_chatText->text();
}

QString ChatBotPage::chatAccount() const
{
    return m_chatAccount->currentData().toString();
}

void ChatBotPage::clearChatText()
{
    m_chatText->clear();
}

void ChatBotPage::sendChat()
{
    // Enter wie «Senden»: nur mit freigegebenem Senden (Prüfung in dss::chatSendView)
    if (m_chatSendEnabled)
        emit chatSendClicked(m_chatText->text(), chatAccount());
}

void ChatBotPage::fillRunItem(QTreeWidgetItem *item, const dss::RunRow &row) const
{
    item->setText(0, row.time);
    item->setText(1, row.action);
    item->setText(2, row.trigger);
    item->setText(3, row.result);
    // Ergebnisfarbe nur aus den Zustandsfarben des Themes (Entscheid 2026-11-16); leer = Text
    if (row.state == QLatin1String("ok"))
        item->setForeground(3, m_theme.stateConnected);
    else if (row.state == QLatin1String("warning"))
        item->setForeground(3, m_theme.stateConnecting);
    else if (row.state == QLatin1String("error"))
        item->setForeground(3, m_theme.stateError);
    else
        item->setData(3, Qt::ForegroundRole, QVariant());
    item->setData(0, kRunIdRole, row.runId);
    item->setData(0, kActionIdRole, row.actionId);
    item->setData(0, kCancellableRole, row.cancellable);
}

QTreeWidgetItem *ChatBotPage::runItem(const dss::RunRow &row) const
{
    auto *item = new QTreeWidgetItem;
    fillRunItem(item, row);
    return item;
}

void ChatBotPage::resetRuns(const QList<dss::RunRow> &rows)
{
    const QTreeWidgetItem *current = m_runs->currentItem();
    const QUuid selected = current && current->isSelected() ? current->data(0, kRunIdRole).toUuid() : QUuid();
    m_runs->clear();
    QList<QTreeWidgetItem *> items;
    items.reserve(rows.size());
    QTreeWidgetItem *keep = nullptr;
    for (const dss::RunRow &row : rows) {
        items.append(runItem(row));
        if (!selected.isNull() && row.runId == selected)
            keep = items.last();
    }
    m_runs->addTopLevelItems(items);
    if (keep)
        m_runs->setCurrentItem(keep);
    m_runs->scrollToBottom();
    refreshRunButtons();
}

void ChatBotPage::applyRun(const dss::RunRows::Change &change, const dss::RunRow *row)
{
    const bool follow = atBottom(m_runs);
    for (int i = 0; i < change.removedFront && m_runs->topLevelItemCount() > 0; ++i)
        delete m_runs->takeTopLevelItem(0);
    if (row && change.index >= 0) {
        if (change.appended)
            m_runs->insertTopLevelItem(change.index, runItem(*row));
        else if (QTreeWidgetItem *item = m_runs->topLevelItem(change.index))
            fillRunItem(item, *row);
        if (change.appended && follow)
            m_runs->scrollToBottom();
    }
    refreshRunButtons();
}

void ChatBotPage::refreshRunButtons()
{
    // Ohne Auswahl ausgegraut; «Lauf abbrechen» nur bei wartenden und laufenden Läufen
    // (RunRow::cancellable), «Im Editor öffnen» nur mit einer Aktion zum Lauf.
    const QTreeWidgetItem *item = m_runs->currentItem();
    const bool selected = item && item->isSelected();
    m_openAction->setEnabled(selected && !item->data(0, kActionIdRole).toUuid().isNull());
    m_cancelRun->setEnabled(selected && item->data(0, kCancellableRole).toBool());
}
