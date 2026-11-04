#pragma once

#include <QHash>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <functional>
#include <memory>

// Übernehmen von Dateien des Nutzers in den Datenordner (K-03): Kopie nach
// media/<SHA-256>.<Endung>, gleicher Inhalt gibt eine Kopie (docs/06-overlay-format.md). Nur Qt
// Core (CLAUDE.md Regel 9).
// Seit P10-vor (Punkt 67) läuft das Übernehmen im Kern in einem eigenen Thread (MediaImporter),
// damit die Hauptschleife während einer grossen Datei (1 GiB etwa 16 s, Bericht P10a) weiter
// Nachrichten beantwortet. Nur der Kern schreibt Dateien; Overlays und liste.json ändert er
// weiterhin nur in der Hauptschleife, wenn das Übernehmen fertig ist.
//
// Quellen:
//   QSaveFile: https://doc.qt.io/qt-6.8/qsavefile.html (vorläufige Datei im selben Ordner,
//     atomar umbenannt; ohne commit() verworfen)
//   Name der vorläufigen Datei: https://doc.qt.io/qt-6.8/qtemporaryfile.html («If the file name
//     doesn't contain "XXXXXX", QTemporaryFile will append the generated part to the file
//     name»; unter Linux zuerst eine Datei ohne Namen) und der Quelltext von Qt 6.8.2:
//     src/corelib/io/qsavefile.cpp (QTemporaryFileEngine mit dem endgültigen Namen als Vorlage),
//     src/corelib/io/qtemporaryfile.cpp (QTemporaryFileName hängt «.XXXXXX» an, generateNext
//     füllt mit A–Z und a–z), https://github.com/qt/qtbase/tree/v6.8.2/src/corelib/io
//   QThreadPool: https://doc.qt.io/qt-6.8/qthreadpool.html

namespace dss {

// Wofür eine Datei des Nutzers verwendet wird; bestimmt die erlaubten Endungen.
enum class MediaUse { Image, Video, Font };

// Erlaubte Endungen (Kleinbuchstaben), siehe docs/06-overlay-format.md
QStringList allowedMediaSuffixes(MediaUse use);

struct MediaImportResult
{
    QString reference;     // "media/…", leer bei Fehler oder Abbruch
    QString error;         // Grund auf Deutsch, wenn reference leer ist
    bool existed = false;  // gleicher Inhalt lag schon im Datenordner, nichts kopiert
    bool canceled = false; // abgebrochen (Kern beendet sich)
    qint64 bytes = 0;      // Grösse der Datei
};

// Stand eines Übernehmens (P10b, Punkt 73), von einem anderen Thread lesbar: done von total
// Byte. Gezählt werden beide Durchgänge (SHA-256 und Kopie), total ist also die doppelte Grösse;
// liegt der Inhalt schon vor, springt done am Ende auf total.
struct ImportProgress
{
    std::atomic<qint64> done{0};
    std::atomic<qint64> total{0};
};

// Kopiert filePath nach mediaDir/<SHA-256>.<Endung>: zuerst SHA-256 der Datei, dann Kopie über
// QSaveFile mit zweitem SHA-256 (hat sich die Datei geändert, wird nichts gespeichert). Läuft in
// jedem Thread; gleichzeitige Aufrufe mit demselben Ziel (gleicher Inhalt) warten aufeinander,
// der zweite findet dann die fertige Kopie (existed). cancel (darf nullptr sein) wird nach jedem
// MiB geprüft; bei Abbruch bleibt weder eine Datei unter dem endgültigen Namen noch die
// vorläufige Datei. progress (darf leer sein) wird nach jedem MiB aufgerufen, im Thread des
// Aufrufs; state (darf nullptr sein) bekommt den Stand.
MediaImportResult importMediaFile(const QString &mediaDir, const QString &filePath, MediaUse use,
                                  const std::atomic<bool> *cancel = nullptr,
                                  const std::function<void()> &progress = {},
                                  ImportProgress *state = nullptr);

// Vorläufige Dateien von QSaveFile, die das Übernehmen nach einem Absturz oder SIGKILL in
// media/ hinterlassen kann (Punkt 68): <64 Zeichen 0-9a-f>.<erlaubte Endung>.<6 Zeichen A-Z a-z>.
bool isSaveFileLeftover(const QString &fileName);

struct LeftoverCleanup
{
    int count = 0;
    qint64 bytes = 0;
    QStringList removed;   // Dateinamen
    QStringList failed;    // «Name (Grund)»
};

// Löscht in mediaDir nur die regulären Dateien, deren Name isSaveFileLeftover erfüllt. Darf nur
// laufen, solange kein Übernehmen läuft (Kern beim Start, nachdem er die Sperre hält).
LeftoverCleanup removeSaveFileLeftovers(const QString &mediaDir);

// Übernimmt Dateien in Threads eines eigenen Pools (höchstens maxThreads gleichzeitig, weitere
// warten). finished() kommt im Thread dieses Objekts (Hauptschleife des Kerns). Der Destruktor
// bricht laufende Aufträge ab und wartet auf sie.
class MediaImporter : public QObject
{
    Q_OBJECT

public:
    MediaImporter(const QString &mediaDir, int maxThreads, QObject *parent = nullptr);
    ~MediaImporter() override;

    // Beginnt das Übernehmen; Rückgabe: Nummer des Auftrags (ab 1) für finished().
    quint64 start(const QString &filePath, MediaUse use);
    // Laufende und wartende Aufträge.
    int pending() const { return int(m_pending.size()); }
    // Bricht einen Auftrag ab (P10b, Punkt 73): Der Thread hört nach dem nächsten MiB auf, danach
    // kommt finished() wie sonst, mit canceled (oder ohne, wenn er schon fertig war: dann muss
    // der Aufrufer die neue Kopie selbst entfernen). false, wenn es den Auftrag nicht gibt.
    bool cancel(quint64 job);
    // Stand eines Auftrags: done und total in Byte (siehe ImportProgress); false, wenn es den
    // Auftrag nicht gibt.
    bool progress(quint64 job, qint64 *done, qint64 *total) const;
    // Bricht alle Aufträge ab und wartet, bis die Threads fertig sind (beim Beenden des Kerns).
    // Rückgabe: Anzahl abgebrochener Aufträge. Danach kommt kein finished() mehr.
    int cancelAll();

signals:
    void finished(quint64 job, const dss::MediaImportResult &result);

private:
    struct Job
    {
        QString file;
        std::shared_ptr<std::atomic<bool>> cancel;
        std::shared_ptr<ImportProgress> state;
    };
    QString m_mediaDir;
    QThreadPool m_pool;
    quint64 m_nextJob = 1;
    QHash<quint64, Job> m_pending;
};

} // namespace dss

Q_DECLARE_METATYPE(dss::MediaImportResult)
