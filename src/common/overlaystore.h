#pragma once

#include "overlay.h"
#include "overlaylist.h"
#include "tokenstore.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

#include <functional>

// Datenordner mit allen Overlays (E-01, E-02, E-07, Datenteil von E-08, K-01, K-03).
// Aufbau des Ordners: docs/06-overlay-format.md
//
//   <Datenordner>/overlays/<Kennung>.json   ein Overlay je Datei
//   <Datenordner>/media/<SHA-256>.<Endung>  Kopien der Dateien des Nutzers
//   <Datenordner>/tokens.json               Token pro Overlay, Dateirechte 0600
//   <Datenordner>/liste.json                Reihenfolge und Ordner der Overlay-Liste (P08)
//
// Jede Änderung wird sofort atomar gespeichert. Schlägt das Speichern fehl, bleibt das
// Overlay im Speicher unverändert.

namespace dss {

// Wofür eine Datei des Nutzers verwendet wird; bestimmt die erlaubten Endungen.
enum class MediaUse { Image, Video, Font };

// Erlaubte Endungen (Kleinbuchstaben), siehe docs/06-overlay-format.md
QStringList allowedMediaSuffixes(MediaUse use);

// Datenordner nach QStandardPaths::AppDataLocation mit dem Programmnamen kDataDirName,
// unter Linux ~/.local/share/dons-streaming-studio (bzw. $XDG_DATA_HOME/dons-streaming-studio).
// https://doc.qt.io/qt-6.8/qstandardpaths.html
inline constexpr char kDataDirName[] = "dons-streaming-studio";
QString defaultDataDir();

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

    // Liste (P08, Teil B): jede Änderung wird sofort atomar in liste.json gespeichert. Ist
    // liste.json kaputt, werden diese Änderungen abgewiesen (die Datei wird nicht überschrieben).
    bool moveOverlayInList(const QUuid &overlayId, const QUuid &folderId, int index, QString *error);
    bool moveFolderInList(const QUuid &folderId, int index, QString *error);
    QUuid createFolder(const QString &name, QString *error);
    bool renameFolder(const QUuid &folderId, const QString &name, QString *error);
    // Die Overlays des Ordners kommen auf die oberste Ebene, sie werden nicht gelöscht.
    bool removeFolder(const QUuid &folderId, QString *error);
    bool setFolderExpanded(const QUuid &folderId, bool expanded, QString *error);

    // Kopiert eine Datei nach media/<SHA-256>.<Endung> (gleicher Inhalt → eine Kopie)
    // und gibt den Verweis "media/…" zurück, bei Fehler einen leeren Text.
    QString importMedia(const QString &filePath, MediaUse use, QString *error);

    // Quellen (E-07). Neue Quellen liegen zuoberst, an Lage 0/0 in der Grösse des Overlays.
    // Name: ursprünglicher Dateiname bzw. «Text: <erste Zeile>».
    QUuid addImageSource(const QUuid &overlayId, const QString &filePath, QString *error);
    // loop: am Ende wiederholen (Standard im Modell: nicht wiederholen)
    QUuid addVideoSource(const QUuid &overlayId, const QString &filePath, bool loop,
                         QString *error);
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
    QString mediaDir() const;
    QSet<QString> takenAddresses(const QUuid &except = QUuid()) const;
    bool writeOverlay(const Overlay &overlay, QString *error);
    // Speichert ein neues Overlay mit neuem Token; ohne Token wird die Datei wieder entfernt.
    bool addNewOverlay(const Overlay &overlay, QString *error);
    // Ändert eine Kopie, prüft und speichert sie; erst danach gilt sie im Speicher.
    bool modify(const QUuid &overlayId, const std::function<QString(Overlay *)> &change,
                QString *error);
    QUuid addMediaSource(const QUuid &overlayId, const QString &filePath, SourceKind kind,
                         bool loop, QString *error);
    QUuid addPreparedSource(const QUuid &overlayId, Source source, QString *error);
    void insertSorted(const Overlay &overlay);
    int indexOf(const QUuid &overlayId) const;
    QList<QUuid> idsByName() const;
    void loadLayout();
    bool writeLayout(const OverlayLayout &layout, QString *error);
    // Änderung der Liste durch den Nutzer: auf einer Kopie, dann speichern, dann übernehmen.
    bool changeLayout(const std::function<QString(OverlayLayout *)> &change, QString *error);
    // Liste nachführen, nachdem ein Overlay angelegt oder gelöscht wurde (nicht bei kaputter
    // liste.json). done beschreibt, was schon geschehen ist, für die Meldung bei einem Fehler.
    bool followLayout(const std::function<QString(OverlayLayout *)> &change, const QString &done,
                      QString *error);

    QString m_dataDir;
    TokenStore m_tokens;
    QList<Overlay> m_overlays;
    OverlayLayout m_layout;
    QString m_layoutError;
    bool m_deferredWrites = false;
    QSet<QUuid> m_pendingWrites;
    int m_overlayWrites = 0;
    QStringList m_loadErrors;
    QStringList m_loadWarnings;
};

} // namespace dss
