#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

// Schriften für die Quelle Text (P08, Teil F; E-11, TX-02). Nur Qt Core (CLAUDE.md Regel 9).
//
// Zur Auswahl stehen die mitgelieferte Be Vietnam Pro, alle installierten Schriften des
// Studio-PCs und Schriftdateien, die schon im Datenordner liegen. Zu Name und Schnitt braucht es
// die Datei, weil der Kern sie in den Datenordner kopiert (K-03). Qt nennt keinen Pfad; die Liste
// kommt deshalb von fontconfig über die Programme fc-list und fc-query (Paket fontconfig 2.15.0,
// schon installiert). Die Bibliothek von fontconfig lässt sich nicht direkt nutzen, weil die
// Header (libfontconfig-dev) fehlen.
// https://www.freedesktop.org/software/fontconfig/fontconfig-user.html (Muster und Format)

namespace dss {

enum class FontOrigin { Builtin, System, Media };
// "builtin", "system", "media"
QString fontOriginName(FontOrigin origin);
bool fontOriginFromName(const QString &name, FontOrigin *origin);

// Ein Schnitt einer Schrift in einer Datei.
struct FontFace
{
    QString family;      // erster Familienname laut fontconfig, zum Beispiel «DejaVu Sans»
    QString style;       // erster Name des Schnitts, zum Beispiel «Bold Oblique»
    int weight = 400;    // Gewicht 100 bis 900 wie in der Overlay-Datei (OpenType/CSS)
    bool italic = false; // kursiv oder schräg
    // Builtin und System: absoluter Pfad. Media: Verweis "media/<SHA-256>.<Endung>".
    QString file;
    FontOrigin origin = FontOrigin::System;

    bool operator==(const FontFace &) const = default;
};

// Ausgabeformat für fc-list und fc-query: eine Zeile je Schnitt, Felder durch Tabulator getrennt.
inline constexpr char kFontconfigFormat[] =
    "%{file}\\t%{index}\\t%{family[0]}\\t%{style[0]}\\t%{weight}\\t%{slant}\\t%{variable}\\n";

// Gewicht von fontconfig (0 bis 215) in das Gewicht von OpenType (100 bis 900), wie
// FcWeightToOpenType in fontconfig 2.15.0 (Tabelle und Interpolation aus src/fcweight.c,
// https://gitlab.freedesktop.org/fontconfig/fontconfig/-/blob/2.15.0/src/fcweight.c), danach
// auf 100 bis 900 begrenzt. -1 bei einem ungültigen Wert.
int fontconfigWeightToOpenType(double fcWeight);

// Liest die Ausgabe von fc-list bzw. fc-query im Format kFontconfigFormat. Übernommen werden
// nur Dateien mit erlaubter Endung (ttf, otf, wie im Datenordner), keine variable Grundform
// (variable = True, Gewicht als Bereich); benannte Schnitte variabler Schriften bleiben (gleiche
// Datei, eigenes Gewicht). Übergangene Zeilen zählt *skipped (falls nicht nullptr).
QList<FontFace> parseFontconfigOutput(const QByteArray &output, FontOrigin origin,
                                      int *skipped = nullptr);

// Sortiert: Mitgelieferte zuerst, dann nach Familie (ohne Gross-/Kleinschreibung), Gewicht,
// kursiv. Doppelte (gleiche Familie, Gewicht, kursiv und Datei) fallen weg.
void sortFontFaces(QList<FontFace> *faces);

// Familien in der Reihenfolge von sortFontFaces, jede einmal.
QStringList fontFamilies(const QList<FontFace> &faces);
QList<FontFace> facesOfFamily(const QList<FontFace> &faces, const QString &family);

// Auflösung Name und Schnitt → Datei: der Schnitt der Familie mit passendem kursiv und dem
// nächsten Gewicht (bei gleichem Abstand das schwerere). Gibt es nur aufrechte oder nur kursive
// Schnitte, zählt das Gewicht allein. Mitgelieferte und installierte Dateien gehen Kopien im
// Datenordner vor. nullptr, wenn es die Familie nicht gibt.
const FontFace *resolveFontFace(const QList<FontFace> &faces, const QString &family, int weight,
                                bool italic);
// Schnitt zu einer Datei (Pfad oder Verweis "media/…"), nullptr wenn unbekannt.
const FontFace *fontFaceForFile(const QList<FontFace> &faces, const QString &file);

// Ordner der mitgelieferten Schrift Be Vietnam Pro (src/common/schriften/be-vietnam-pro/).
QString builtinFontDir();
// Standardschrift neuer Textquellen (E-11): Be Vietnam Pro Regular.
QString defaultTextFontFile();
// Alle Schriftdateien (ttf, otf) eines Ordners, nach Name sortiert.
QStringList fontFilesIn(const QString &dir);

// Fragt einzelne Dateien mit fc-query ab (blockiert, höchstens timeoutMs). Für eine Datei, die
// gerade in den Datenordner kopiert wurde. Leere Liste und *error bei einem Fehler.
QList<FontFace> queryFontFiles(const QStringList &files, FontOrigin origin, QString *error,
                               int timeoutMs = 5000);

// Lädt die ganze Auswahl ohne zu blockieren: fc-list (installiert), dann fc-query für die
// mitgelieferten Dateien und die Schriftdateien im Datenordner (mediaFiles: absolute Pfade,
// mediaReferences: die passenden Verweise "media/…").
class FontCatalogLoader : public QObject
{
    Q_OBJECT

public:
    explicit FontCatalogLoader(QObject *parent = nullptr);
    ~FontCatalogLoader() override;

    bool isRunning() const;
    void start(const QStringList &mediaFiles, const QStringList &mediaReferences);

signals:
    // faces sortiert (sortFontFaces). error: Meldung auf Deutsch, wenn ein Teil fehlt (zum
    // Beispiel fc-list nicht ausführbar); die übrigen Schnitte sind trotzdem da.
    void finished(const QList<dss::FontFace> &faces, const QString &error);

private:
    void runNext();
    void onFinished();

    struct Step
    {
        QString program;
        QStringList files;      // für fc-query
        QStringList references; // Verweise statt Pfaden (Media)
        FontOrigin origin;
    };
    QList<Step> m_steps;
    Step m_current;
    QProcess *m_process = nullptr;
    QList<FontFace> m_faces;
    QStringList m_errors;
};

} // namespace dss
