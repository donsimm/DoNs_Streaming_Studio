#pragma once

#include "configpaths.h"
#include "mediaimport.h"
#include "medialibrary.h"
#include "overlay.h"
#include "overlaylist.h"
#include "tokenstore.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <functional>
#include <optional>
#include <utility>

// Datenordner mit allen Overlays (E-01, E-02, E-07, Datenteil von E-08, K-01, K-03).
// Aufbau des Ordners: docs/06-overlay-format.md
//
//   <Datenordner>/overlays/<Kennung>.json   ein Overlay je Datei
//   <Datenordner>/media/<SHA-256>.<Endung>  Kopien der Dateien des Nutzers
//   <Datenordner>/tokens.json               Token pro Overlay, Dateirechte 0600
//   <Datenordner>/liste.json                Reihenfolge und Ordner der Overlay-Liste (P08)
//   <Datenordner>/medien.json               Namen und Ordner der Mediendateien (P10b)
//
// Jede Änderung wird sofort atomar gespeichert. Schlägt das Speichern fehl, bleibt das
// Overlay im Speicher unverändert.

namespace dss {

// Datenordner nach QStandardPaths::AppDataLocation mit dem Programmnamen appDirName(edition),
// unter Linux ~/.local/share/dons-streaming-studio (bzw. $XDG_DATA_HOME/…) im Einsatz und
// ~/.local/share/dons-streaming-studio-entwicklung in Arbeit (P10a, Teil B).
// https://doc.qt.io/qt-6.8/qstandardpaths.html
QString defaultDataDir(Edition edition = buildEdition());

class OverlayStore
{
public:
    explicit OverlayStore(const QString &dataDir);

    QString dataDir() const { return m_dataDir; }
    QString overlayFilePath(const QUuid &overlayId) const;
    // Absoluter Pfad zu einem Verweis "media/…"
    QString mediaFilePath(const QString &reference) const;

    // Legt die Unterordner an und lädt alle Overlays. false nur, wenn der Datenordner
    // oder tokens.json nicht lesbar ist. Kaputte Dateien stehen danach in loadErrors() (mit Dateiname),
    // werden übersprungen und nie überschrieben. Fehlende Mediendateien stehen je Quelle
    // in loadWarnings(); das Overlay wird trotzdem geladen. Hat ein Overlay keinen Token,
    // wird einer erzeugt und in loadWarnings() gemeldet.
    // LoadMode::ReadOnly schreibt nichts: keine Ordner anlegen, keinen Token erzeugen,
    // keine Dateirechte ändern (dss-cli bei laufendem Kern, Entscheid 2 in P07).
    enum class LoadMode { ReadWrite, ReadOnly };
    bool load(QString *error, LoadMode mode = LoadMode::ReadWrite);
    QStringList loadErrors() const { return m_loadErrors; }
    QStringList loadWarnings() const { return m_loadWarnings; }

    // Token des Overlays für die Browserquelle. Nie ins Protokoll schreiben (Regel 12).
    QString token(const QUuid &overlayId) const { return m_tokens.token(overlayId); }

    // Overlays sortiert nach Name (ohne Gross-/Kleinschreibung), dann Adresse.
    QList<Overlay> overlays() const { return m_overlays; }
    // Reihenfolge und Ordner der Liste (liste.json, P08 Teil B), passend zu den geladenen
    // Overlays. Ist liste.json kaputt, steht die Meldung in layoutError() (und in loadErrors()),
    // die Liste ist dann nach Name sortiert, und die Datei wird nicht überschrieben.
    OverlayLayout layout() const { return m_layout; }
    QString layoutError() const { return m_layoutError; }
    QString layoutFilePath() const;
    const Overlay *overlay(const QUuid &overlayId) const;
    const Overlay *overlayByAddress(const QString &address) const;

    // Overlays (E-01). Rückgabe der neuen Kennung, bei Fehler eine leere QUuid.
    QUuid createOverlay(const QString &name, int width, int height, QString *error);
    // changeAddress = false: Adresse bleibt (Entscheid 5).
    bool renameOverlay(const QUuid &overlayId, const QString &newName, bool changeAddress,
                       QString *error);
    // Neue Kennungen für Overlay und Quellen, Name «<Name> Kopie», eigene Adresse, neuer Token.
    // Die Kopie steht in der Liste direkt nach dem Original.
    QUuid copyOverlay(const QUuid &overlayId, QString *error);
    bool removeOverlay(const QUuid &overlayId, QString *error);
    // P09 (Entscheid 5): Auflösung ändern; die Quellen behalten ihre Pixelwerte.
    bool setOverlaySize(const QUuid &overlayId, int width, int height, QString *error);
    // P10 (E-24): Ausgabe an die Browserquelle ein/aus.
    bool setOverlayOutput(const QUuid &overlayId, bool output, QString *error);
    // P10 (Teil A 8): neuer Token für die Browserquelle, sofort in tokens.json gespeichert. Die
    // alte Adresse gilt danach nicht mehr.
    bool newToken(const QUuid &overlayId, QString *error);

    // Liste (P08, Teil B): jede Änderung wird sofort atomar in liste.json gespeichert. Ist
    // liste.json kaputt, werden diese Änderungen abgewiesen (die Datei wird nicht überschrieben).
    bool moveOverlayInList(const QUuid &overlayId, const QUuid &folderId, int index, QString *error);
    bool moveFolderInList(const QUuid &folderId, int index, QString *error);
    QUuid createFolder(const QString &name, QString *error);
    bool renameFolder(const QUuid &folderId, const QString &name, QString *error);
    // Die Overlays des Ordners kommen auf die oberste Ebene, sie werden nicht gelöscht.
    bool removeFolder(const QUuid &folderId, QString *error);
    bool setFolderExpanded(const QUuid &folderId, bool expanded, QString *error);

    // Namen und Ordner der Mediendateien (P10b, medien.json). load() liest die Datei und gleicht
    // sie im Speicher mit media/ ab (neue Dateien mit Ersatzname, fehlende fallen weg); geschrieben
    // wird erst bei der nächsten Änderung. Ist medien.json ungültig, steht die Meldung in
    // mediaLibraryError() (und in loadErrors()), alle Dateien haben Ersatznamen, Umbenennen und
    // Ordner werden abgewiesen und die Datei wird nicht überschrieben (wie liste.json).
    // Schreiben darf nur der Kern (Teil A 1): ohne setWritesMediaLibrary(true) ändern die
    // Funktionen unten nur den Speicher (dss-cli übernimmt Dateien ohne Namen; der Kern nimmt sie
    // beim nächsten Laden mit Ersatzname auf).
    void setWritesMediaLibrary(bool writes) { m_writesMedia = writes; }
    const MediaLibrary &mediaLibrary() const { return m_media; }
    QString mediaLibraryError() const { return m_mediaError; }
    QString mediaLibraryFilePath() const;
    // Grösse jeder Datei in Byte (aus dem letzten Abgleich)
    qint64 mediaBytes(const QString &reference) const { return m_mediaBytes.value(reference, -1); }
    // Name im Dock; leer, wenn die Datei nicht in der Liste steht.
    QString mediaName(const QString &reference) const;
    // Übernommene Datei aufnehmen (beim Übernehmen): gibt es sie schon, bleiben Name und Ordner.
    // folder null = oberste Ebene; ein Ordner, den es nicht mehr gibt, ebenso.
    bool recordMedia(const QString &reference, const QString &name, const QUuid &folder,
                     QString *error);
    bool renameMedia(const QString &reference, const QString &name, QString *error);
    bool moveMediaFile(const QString &reference, const QUuid &folder, QString *error);
    QUuid createMediaFolder(const QString &name, const QUuid &parent, QString *error);
    bool renameMediaFolder(const QUuid &folderId, const QString &name, QString *error);
    // Inhalt kommt eine Ebene höher (Teil A 1, Begründung in docs/06-overlay-format.md).
    bool removeMediaFolder(const QUuid &folderId, QString *error);
    bool moveMediaFolder(const QUuid &folderId, const QUuid &parent, QString *error);
    bool setMediaFolderExpanded(const QUuid &folderId, bool expanded, QString *error);
    // Löscht die Datei in media/ und ihren Eintrag (Teil A 2). Abgewiesen, wenn eine Quelle sie
    // nutzt; auch dann, wenn eine Overlay-Datei, die nicht geladen werden konnte, den Verweis
    // enthält.
    bool removeMedia(const QString &reference, QString *error);
    // Verwendung jeder Datei in den geladenen Overlays (Teil A 2)
    MediaUsages mediaUsages() const { return dss::mediaUsages(m_overlays); }
    // Neuer Abgleich mit media/ (nach dem Übernehmen). true, wenn sich die Liste geändert hat.
    bool reconcileMedia();

    // Kopiert eine Datei nach media/<SHA-256>.<Endung> (gleicher Inhalt → eine Kopie)
    // und gibt den Verweis "media/…" zurück, bei Fehler einen leeren Text. Läuft im Thread des
    // Aufrufs (dss-cli; der Kern übernimmt seit P10-vor mit MediaImporter in einem eigenen
    // Thread und gibt danach den Verweis weiter).
    // Seit P10b kommt die Datei mit ihrem Namen in die Medienliste, in folder (null = oberste
    // Ebene); gab es sie schon, bleiben Name und Ordner.
    QString importMedia(const QString &filePath, MediaUse use, QString *error,
                        const QUuid &folder = QUuid());
    // Ordner der Kopien: <Datenordner>/media
    QString mediaDir() const;

    // Liest die Grösse einer Bild- oder Videodatei in Pixeln (Bild: Pixel, Video: erstes Bild).
    // false mit Meldung, wenn die Datei nicht lesbar ist. Gesetzt von Kern und dss-cli
    // (dss_media, libavformat); Qt Gui und libav gehören nicht in dss_common (Regel 9).
    using MediaSizeReader =
        std::function<bool(const QString &path, int *width, int *height, QString *error)>;
    void setMediaSizeReader(MediaSizeReader reader) { m_mediaSizeReader = std::move(reader); }
    // Liest das Format (Codec) einer Videodatei als Name in FFmpeg (P10b, Punkt 81). Gesetzt vom
    // Kern (dss_media, readVideoCodec). Ohne: kein Hinweis auf Formate.
    using VideoCodecReader = std::function<bool(const QString &path, QString *codec, QString *error)>;
    void setVideoCodecReader(VideoCodecReader reader) { m_videoCodecReader = std::move(reader); }
    // Codec eines Videos aus der Medienliste (einmal gelesen, Dateien ändern sich nie); leer, wenn
    // unbekannt oder kein Video.
    QString videoCodec(const QString &reference) const;
    // Hinweis, wenn die Browserquelle die Datei nicht abspielt (videoformat.h); name ist der Name
    // im Hinweis (Dateiname beim Übernehmen, sonst der Name in der Medienliste). Leer = kein Problem.
    QString browserProblem(const QString &reference, const QString &name = QString()) const;
    // Wird beim Übernehmen einer Datei nach jedem MiB aufgerufen (P10a, Punkt 41): Der Kern
    // meldet sich damit beim Watchdog von systemd, weil eine grosse Datei die Hauptschleife lange
    // anhält (1 GiB etwa 16 s auf dem Studio-PC, Bericht P10a).
    void setProgressCallback(std::function<void()> callback) { m_progress = std::move(callback); }

    // Originalgrösse aller Bild- und Videodateien eines Overlays (P09b, Entscheid 3), für die
    // Nachrichten an die Fenster. Die Dateien in media/ sind nach Inhalt benannt und ändern sich
    // nie, darum liest der MediaSizeReader jede Datei nur einmal (auch eine unlesbare). Ohne
    // MediaSizeReader leer.
    MediaSizes mediaSizes(const Overlay &overlay) const;

    // Quellen (E-07). Neue Quellen liegen zuoberst an Lage 0/0. Bild und Video bekommen die
    // Grösse der Datei (P09, Entscheid 6), wenn ein MediaSizeReader gesetzt ist; kann er sie
    // nicht lesen, gilt die Grösse des Overlays und *warning (falls nicht nullptr) nennt den
    // Grund. Text und Bild/Video ohne MediaSizeReader: Grösse des Overlays.
    // Name: ursprünglicher Dateiname bzw. «Text: <erste Zeile>».
    // filePath (auch fontFilePath, unten filePath von replaceImageFile) ist ein Pfad (die Datei
    // wird kopiert) oder seit P10-vor ein Verweis "media/…" auf eine schon übernommene Kopie;
    // dann ist sourceName der Name der Quelle (leer: Name der Kopie).
    QUuid addImageSource(const QUuid &overlayId, const QString &filePath, QString *error,
                         QString *warning = nullptr, const QString &sourceName = QString());
    // loop: am Ende wiederholen (Standard im Modell: nicht wiederholen); holdAtEnd: am Ende
    // anhalten (P09, Standard aus).
    QUuid addVideoSource(const QUuid &overlayId, const QString &filePath, bool loop,
                         QString *error, QString *warning = nullptr, bool holdAtEnd = false,
                         const QString &sourceName = QString());
    QUuid addTextSource(const QUuid &overlayId, const QString &content,
                        const QString &fontFilePath, QString *error);
    bool removeSource(const QUuid &overlayId, const QUuid &sourceId, QString *error);
    bool moveSourceForward(const QUuid &overlayId, const QUuid &sourceId, QString *error);
    bool moveSourceBackward(const QUuid &overlayId, const QUuid &sourceId, QString *error);
    bool setSourceVisible(const QUuid &overlayId, const QUuid &sourceId, bool visible,
                          QString *error);
    bool setSourceGeometry(const QUuid &overlayId, const QUuid &sourceId, int x, int y,
                           int width, int height, int rotation, QString *error);
    // P08: Stelle in der Zeichenreihenfolge (0 = ganz hinten).
    bool moveSource(const QUuid &overlayId, const QUuid &sourceId, int index, QString *error);
    // P08: einzelne Eigenschaften (Name, Lage, Grösse, Drehung, sichtbar, Text, Wiederholen).
    bool updateSource(const QUuid &overlayId, const QUuid &sourceId, const SourceChanges &changes,
                      QString *error);
    // P08: Schriftdatei, Gewicht und kursiv einer Textquelle zusammen. fontFile ist ein Pfad (die
    // Datei wird kopiert, gleicher Inhalt → eine Kopie) oder ein Verweis "media/…" auf eine schon
    // vorhandene Kopie. *reference (falls nicht nullptr) bekommt den Verweis.
    bool setTextFont(const QUuid &overlayId, const QUuid &sourceId, const QString &fontFile,
                     int weight, bool italic, QString *error, QString *reference = nullptr);
    // P08: Bild einer Bildquelle ersetzen (Kopie im Datenordner wie beim Hinzufügen).
    bool replaceImageFile(const QUuid &overlayId, const QUuid &sourceId, const QString &filePath,
                          QString *error);

    // Verzögertes Speichern (P08, Teil A): Änderungen an bestehenden Overlays gelten sofort im
    // Speicher; die Datei schreibt erst flushPendingWrites(). Damit fasst der Kern häufige
    // Änderungen zusammen. Anlegen, Kopieren, Löschen, Token, Liste und Kopien im Datenordner
    // werden weiterhin sofort geschrieben. Ohne setDeferredWrites(true) wird sofort gespeichert
    // (dss-cli).
    void setDeferredWrites(bool deferred) { m_deferredWrites = deferred; }
    bool hasPendingWrites() const { return !m_pendingWrites.isEmpty(); }
    // Schreibt alle geänderten Overlays. Was nicht gespeichert werden kann, bleibt vorgemerkt.
    bool flushPendingWrites(QString *error);
    // Anzahl geschriebener Overlay-Dateien seit dem Anlegen dieses Objekts (für Prüfungen).
    int overlayWriteCount() const { return m_overlayWrites; }

private:
    QString overlaysDir() const;
    // Verweis "media/…" auf eine vorhandene Kopie mit erlaubter Endung, oder Pfad übernehmen.
    QString resolveMedia(const QString &file, MediaUse use, QString *error);
    QSet<QString> takenAddresses(const QUuid &except = QUuid()) const;
    bool writeOverlay(const Overlay &overlay, QString *error);
    // Speichert ein neues Overlay mit neuem Token; ohne Token wird die Datei wieder entfernt.
    bool addNewOverlay(const Overlay &overlay, QString *error);
    // Ändert eine Kopie, prüft und speichert sie; erst danach gilt sie im Speicher.
    bool modify(const QUuid &overlayId, const std::function<QString(Overlay *)> &change,
                QString *error);
    QUuid addMediaSource(const QUuid &overlayId, const QString &filePath, SourceKind kind,
                         bool loop, bool holdAtEnd, QString *error, QString *warning,
                         const QString &sourceName);
    // size: Grösse der neuen Quelle, ohne Angabe die des Overlays.
    QUuid addPreparedSource(const QUuid &overlayId, Source source, QString *error,
                            std::optional<std::pair<int, int>> size = std::nullopt);
    void insertSorted(const Overlay &overlay);
    int indexOf(const QUuid &overlayId) const;
    QList<QUuid> idsByName() const;
    void loadLayout();
    void loadMediaLibrary();
    // Änderung der Medienliste: auf einer Kopie, dann speichern (nur im Kern), dann übernehmen.
    // structural: Umbenennen und Ordner; bei ungültiger medien.json abgewiesen.
    bool changeMedia(const std::function<QString(MediaLibrary *)> &change, bool structural,
                     QString *error);
    bool writeMediaLibrary(const MediaLibrary &library, QString *error);
    bool writeLayout(const OverlayLayout &layout, QString *error);
    // Änderung der Liste durch den Nutzer: auf einer Kopie, dann speichern, dann übernehmen.
    bool changeLayout(const std::function<QString(OverlayLayout *)> &change, QString *error);
    // Liste nachführen, nachdem ein Overlay angelegt oder gelöscht wurde (nicht bei kaputter
    // liste.json). done beschreibt, was schon geschehen ist, für die Meldung bei einem Fehler.
    bool followLayout(const std::function<QString(OverlayLayout *)> &change, const QString &done,
                      QString *error);

    QString m_dataDir;
    // Grösse einer Datei aus dem Zwischenspeicher oder vom MediaSizeReader; nullopt mit Grund.
    std::optional<QSize> mediaSize(const QString &reference, QString *reason) const;

    MediaSizeReader m_mediaSizeReader;
    VideoCodecReader m_videoCodecReader;
    mutable QHash<QString, QString> m_videoCodecCache;
    std::function<void()> m_progress;
    struct CachedSize
    {
        std::optional<QSize> size;
        QString reason;
    };
    mutable QHash<QString, CachedSize> m_mediaSizeCache;
    TokenStore m_tokens;
    QList<Overlay> m_overlays;
    OverlayLayout m_layout;
    QString m_layoutError;
    MediaLibrary m_media;
    QString m_mediaError;
    QHash<QString, qint64> m_mediaBytes;
    bool m_writesMedia = false;
    // Overlay-Dateien, die load() nicht lesen konnte (Löschen von Mediendateien prüft sie roh)
    QStringList m_unreadableOverlayFiles;
    bool m_deferredWrites = false;
    QSet<QUuid> m_pendingWrites;
    int m_overlayWrites = 0;
    QStringList m_loadErrors;
    QStringList m_loadWarnings;
};

} // namespace dss
