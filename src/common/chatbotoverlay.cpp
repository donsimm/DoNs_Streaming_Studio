#include "chatbotoverlay.h"

#include "actiontemplate.h"
#include "fontcatalog.h"
#include "overlaystore.h"

#include <QFileInfo>
#include <QRect>

#include <iterator>

namespace dss {

namespace {

// Lage der Elemente in 1920 × 1080: die Alerts nacheinander (Warteschlange «Alerts») an derselben
// Stelle oben in der Mitte, Begrüssung unten links, Shoutout unten rechts.
struct ElementLayout
{
    const char *text; // nullptr = Bild
    QRect box;
    int fontSize;
    TextAlign align;
};

// Gleiche Reihenfolge wie kChatBotElements
const ElementLayout kLayout[] = {
    {"Neuer Follower: %user%", {160, 60, 1600, 90}, 56, TextAlign::Center},
    {"Neues Abo: %user%", {160, 60, 1600, 90}, 56, TextAlign::Center},
    {"%user% verschenkt %gifts% Abos", {160, 60, 1600, 90}, 56, TextAlign::Center},
    {"%user%: %bits% Bits", {160, 60, 1600, 90}, 56, TextAlign::Center},
    {"Raid von %user% mit %viewers% Zuschauern", {160, 60, 1600, 90}, 56, TextAlign::Center},
    {nullptr, {80, 820, 180, 180}, 0, TextAlign::Left},
    {"Willkommen, %user%", {290, 865, 900, 90}, 48, TextAlign::Left},
    {nullptr, {1660, 820, 180, 180}, 0, TextAlign::Left},
    {"Shoutout: %targetUser%", {730, 865, 900, 90}, 48, TextAlign::Right},
};
static_assert(std::size(kLayout) == std::size(dss::actions::kChatBotElements));

} // namespace

Overlay chatBotOverlay(const QString &fontFile, const QString &imageFile)
{
    Overlay overlay;
    overlay.id = QUuid::fromString(QLatin1String(dss::actions::kChatBotOverlayId));
    overlay.name = QString::fromUtf8(kChatBotOverlayName);
    overlay.width = 1920;
    overlay.height = 1080;
    for (size_t i = 0; i < std::size(kLayout); ++i) {
        const ElementLayout &l = kLayout[i];
        Source s;
        s.id = QUuid::fromString(QLatin1String(dss::actions::kChatBotElements[i].id));
        s.name = QString::fromUtf8(dss::actions::kChatBotElements[i].name);
        s.x = l.box.x();
        s.y = l.box.y();
        s.width = l.box.width();
        s.height = l.box.height();
        s.visible = false;
        if (l.text) {
            s.kind = SourceKind::Text;
            s.text.content = QString::fromUtf8(l.text);
            s.text.fontFile = fontFile;
            s.text.fontSize = l.fontSize;
            s.text.color = QStringLiteral("#FFFFFFFF");
            s.text.align = l.align;
        } else {
            s.kind = SourceKind::Image;
            s.file = imageFile;
        }
        overlay.sources.append(s);
    }
    return overlay;
}

ChatBotOverlayResult ensureChatBotOverlay(OverlayStore *store, QString *error, QString *warning)
{
    const QUuid id = QUuid::fromString(QLatin1String(dss::actions::kChatBotOverlayId));
    if (store->overlay(id) || QFileInfo::exists(store->overlayFilePath(id)))
        return ChatBotOverlayResult::Exists;
    const QString font = store->importMedia(defaultTextFontFile(), MediaUse::Font, error);
    if (font.isEmpty())
        return ChatBotOverlayResult::Failed;
    const QString image = store->importMedia(QString::fromLatin1(kChatBotPlaceholderResource), MediaUse::Image, error);
    if (image.isEmpty())
        return ChatBotOverlayResult::Failed;
    if (!store->addOverlay(chatBotOverlay(font, image), error)) {
        if (!store->overlay(id))
            return ChatBotOverlayResult::Failed;
        // Angelegt, nur die Liste nicht gespeichert: trotzdem da
        *warning = *error;
        error->clear();
        return ChatBotOverlayResult::Created;
    }

    // Ordner «Chat-Bot»: vorhandenen gleichen Namens benutzen, sonst am Ende anlegen
    const QString folderName = QString::fromUtf8(kChatBotFolderName);
    QUuid folder;
    for (const ListEntry &e : store->layout().entries) {
        if (e.isFolder && e.folder.name == folderName) {
            folder = e.folder.id;
            break;
        }
    }
    QString folderError;
    if (folder.isNull())
        folder = store->createFolder(folderName, &folderError);
    const OverlayLayout layout = store->layout();
    const ListFolder *f = folder.isNull() ? nullptr : layout.folder(folder);
    if (!f || !store->moveOverlayInList(id, folder, int(f->overlays.size()), &folderError))
        *warning = QStringLiteral("Overlay «%1» nicht im Ordner «%2»: %3").arg(QString::fromUtf8(kChatBotOverlayName), folderName, folderError);
    return ChatBotOverlayResult::Created;
}

} // namespace dss
