#include "fontcatalog.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace dss {

QString fontOriginName(FontOrigin origin)
{
    switch (origin) {
    case FontOrigin::Builtin:
        return QStringLiteral("builtin");
    case FontOrigin::System:
        return QStringLiteral("system");
    case FontOrigin::Media:
        return QStringLiteral("media");
    }
    return {};
}

bool fontOriginFromName(const QString &name, FontOrigin *origin)
{
    for (FontOrigin o : {FontOrigin::Builtin, FontOrigin::System, FontOrigin::Media}) {
        if (name == fontOriginName(o)) {
            *origin = o;
            return true;
        }
    }
    return false;
}

int fontconfigWeightToOpenType(double fcWeight)
{
    // Tabelle aus fontconfig 2.15.0, src/fcweight.c (FC_WEIGHT_* aus fontconfig.h):
    // THIN 0, EXTRALIGHT 40, LIGHT 50, DEMILIGHT 55, BOOK 75, REGULAR 80, MEDIUM 100,
    // DEMIBOLD 180, BOLD 200, EXTRABOLD 205, BLACK 210, EXTRABLACK 215.
    static constexpr struct {
        int ot;
        int fc;
    } map[] = {{0, 0},     {100, 0},   {200, 40},  {300, 50},  {350, 55},  {380, 75}, {400, 80},
               {500, 100}, {600, 180}, {700, 200}, {800, 205}, {900, 210}, {1000, 215}};
    if (!std::isfinite(fcWeight) || fcWeight < 0 || fcWeight > 215)
        return -1;
    std::size_t i = 1;
    while (fcWeight > map[i].fc)
        ++i;
    double ot = map[i].ot;
    if (fcWeight != map[i].fc)
        ot = map[i - 1].ot + (fcWeight - map[i - 1].fc) * (map[i].ot - map[i - 1].ot)
                                 / (map[i].fc - map[i - 1].fc);
    // FcWeightToOpenType rundet mit + 0.5; das Format erlaubt 100 bis 900.
    return std::clamp(int(ot + 0.5), 100, 900);
}

QList<FontFace> parseFontconfigOutput(const QByteArray &output, FontOrigin origin, int *skipped)
{
    static const QStringList allowed = {QStringLiteral("ttf"), QStringLiteral("otf")};
    QList<FontFace> faces;
    int skip = 0;
    for (const QByteArray &rawLine : output.split('\n')) {
        if (rawLine.trimmed().isEmpty())
            continue;
        const QList<QByteArray> f = rawLine.split('\t');
        bool indexOk = false;
        bool weightOk = false;
        bool slantOk = false;
        const qint64 index = f.size() == 7 ? f.at(1).toLongLong(&indexOk) : 0;
        // Bei variablen Schriften steht das Gewicht der Grundform als Bereich «[0 210]» da.
        const double weight = f.size() == 7 ? f.at(4).toDouble(&weightOk) : 0;
        const int slant = f.size() == 7 ? f.at(5).toInt(&slantOk) : 0;
        if (f.size() != 7 || !indexOk || !weightOk || !slantOk) {
            ++skip;
            continue;
        }
        FontFace face;
        face.file = QString::fromUtf8(f.at(0));
        face.family = QString::fromUtf8(f.at(2));
        face.style = QString::fromUtf8(f.at(3));
        face.weight = fontconfigWeightToOpenType(weight);
        // fontconfig: FC_SLANT_ROMAN 0, FC_SLANT_ITALIC 100, FC_SLANT_OBLIQUE 110
        face.italic = slant > 0;
        face.origin = origin;
        // Nur ttf und otf (erlaubte Endungen im Datenordner); in einer Sammlung nur der erste
        // Schnitt (untere 16 Bit des Index), benannte Schnitte variabler Schriften (obere Bit) bleiben.
        const bool variableBase = f.at(6) == "True";
        if (!allowed.contains(QFileInfo(face.file).suffix().toLower()) || (index & 0xFFFF) != 0
            || variableBase || face.weight < 0 || face.family.isEmpty()) {
            ++skip;
            continue;
        }
        faces.append(face);
    }
    if (skipped)
        *skipped = skip;
    return faces;
}

void sortFontFaces(QList<FontFace> *faces)
{
    std::stable_sort(faces->begin(), faces->end(), [](const FontFace &a, const FontFace &b) {
        const bool aBuiltin = a.origin == FontOrigin::Builtin;
        const bool bBuiltin = b.origin == FontOrigin::Builtin;
        if (aBuiltin != bBuiltin)
            return aBuiltin;
        if (const int c = a.family.compare(b.family, Qt::CaseInsensitive); c != 0)
            return c < 0;
        if (a.weight != b.weight)
            return a.weight < b.weight;
        if (a.italic != b.italic)
            return !a.italic;
        return int(a.origin) < int(b.origin);
    });
    QSet<QString> seen;
    faces->removeIf([&seen](const FontFace &f) {
        const QString key = f.family.toCaseFolded() + QLatin1Char('\t') + QString::number(f.weight)
                            + (f.italic ? QStringLiteral("i") : QStringLiteral("n"))
                            + QLatin1Char('\t') + f.file;
        if (seen.contains(key))
            return true;
        seen.insert(key);
        return false;
    });
}

QStringList fontFamilies(const QList<FontFace> &faces)
{
    QStringList families;
    QSet<QString> seen;
    for (const FontFace &f : faces) {
        if (!seen.contains(f.family.toCaseFolded())) {
            seen.insert(f.family.toCaseFolded());
            families.append(f.family);
        }
    }
    return families;
}

QList<FontFace> facesOfFamily(const QList<FontFace> &faces, const QString &family)
{
    QList<FontFace> result;
    for (const FontFace &f : faces) {
        if (f.family.compare(family, Qt::CaseInsensitive) == 0)
            result.append(f);
    }
    return result;
}

const FontFace *resolveFontFace(const QList<FontFace> &faces, const QString &family, int weight,
                                bool italic)
{
    QList<const FontFace *> candidates;
    for (const FontFace &f : faces) {
        if (f.family.compare(family, Qt::CaseInsensitive) == 0 && f.origin != FontOrigin::Media)
            candidates.append(&f);
    }
    if (candidates.isEmpty()) {
        for (const FontFace &f : faces) {
            if (f.family.compare(family, Qt::CaseInsensitive) == 0)
                candidates.append(&f);
        }
    }
    if (candidates.isEmpty())
        return nullptr;
    const bool hasMatchingStyle = std::any_of(candidates.cbegin(), candidates.cend(),
                                              [italic](const FontFace *f) { return f->italic == italic; });
    const FontFace *best = nullptr;
    for (const FontFace *f : std::as_const(candidates)) {
        if (hasMatchingStyle && f->italic != italic)
            continue;
        if (!best) {
            best = f;
            continue;
        }
        const int d = std::abs(f->weight - weight);
        const int bestD = std::abs(best->weight - weight);
        if (d < bestD || (d == bestD && f->weight > best->weight))
            best = f;
    }
    return best;
}

const FontFace *fontFaceForFile(const QList<FontFace> &faces, const QString &file)
{
    for (const FontFace &f : faces) {
        if (f.file == file)
            return &f;
    }
    return nullptr;
}

QString builtinFontDir()
{
    // DSS_BUILTIN_FONT_DIR kommt aus src/common/CMakeLists.txt (Ordner im Quellbaum).
    return QStringLiteral(DSS_BUILTIN_FONT_DIR);
}

QString defaultTextFontFile()
{
    return builtinFontDir() + QStringLiteral("/BeVietnamPro-Regular.ttf");
}

QStringList fontFilesIn(const QString &dir)
{
    QStringList files;
    const QDir d(dir);
    for (const QString &name : d.entryList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")},
                                           QDir::Files, QDir::Name))
        files.append(d.filePath(name));
    return files;
}

namespace {

QStringList queryArguments(const QStringList &files)
{
    return QStringList{QStringLiteral("--format"), QString::fromLatin1(kFontconfigFormat)} + files;
}

// fc-query nennt die Datei so, wie sie übergeben wurde; bei Kopien im Datenordner gilt danach
// der Verweis "media/…".
void replaceFiles(QList<FontFace> *faces, const QStringList &files, const QStringList &references)
{
    QHash<QString, QString> map;
    for (qsizetype i = 0; i < files.size() && i < references.size(); ++i)
        map.insert(files.at(i), references.at(i));
    for (FontFace &f : *faces)
        f.file = map.value(f.file, f.file);
}

} // namespace

QList<FontFace> queryFontFiles(const QStringList &files, FontOrigin origin, QString *error,
                               int timeoutMs)
{
    QProcess p;
    p.start(QStringLiteral("fc-query"), queryArguments(files));
    if (!p.waitForStarted(timeoutMs)) {
        *error = QStringLiteral("fc-query nicht ausführbar (%1)").arg(p.errorString());
        return {};
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(1000);
        *error = QStringLiteral("fc-query antwortet nicht innerhalb von %1 ms").arg(timeoutMs);
        return {};
    }
    const QList<FontFace> faces = parseFontconfigOutput(p.readAllStandardOutput(), origin);
    if (p.exitStatus() != QProcess::NormalExit || p.exitCode() != 0)
        *error = QStringLiteral("fc-query endete mit Exit-Code %1: %2")
                     .arg(p.exitCode())
                     .arg(QString::fromLocal8Bit(p.readAllStandardError()).trimmed().section(QLatin1Char('\n'), 0, 0));
    return faces;
}

FontCatalogLoader::FontCatalogLoader(QObject *parent)
    : QObject(parent)
{
}

FontCatalogLoader::~FontCatalogLoader()
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

bool FontCatalogLoader::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void FontCatalogLoader::start(const QStringList &mediaFiles, const QStringList &mediaReferences)
{
    if (isRunning())
        return;
    m_faces.clear();
    m_errors.clear();
    m_steps.clear();
    m_steps.append({QStringLiteral("fc-list"), {}, {}, FontOrigin::System});
    m_steps.append({QStringLiteral("fc-query"), fontFilesIn(builtinFontDir()), {}, FontOrigin::Builtin});
    if (!mediaFiles.isEmpty())
        m_steps.append({QStringLiteral("fc-query"), mediaFiles, mediaReferences, FontOrigin::Media});
    runNext();
}

void FontCatalogLoader::runNext()
{
    if (m_steps.isEmpty()) {
        sortFontFaces(&m_faces);
        emit finished(m_faces, m_errors.join(QStringLiteral("; ")));
        return;
    }
    m_current = m_steps.takeFirst();
    if (m_current.program == QLatin1String("fc-query") && m_current.files.isEmpty()) {
        m_errors.append(QStringLiteral("keine Schriftdatei in %1").arg(builtinFontDir()));
        runNext();
        return;
    }
    if (!m_process) {
        m_process = new QProcess(this);
        connect(m_process, &QProcess::finished, this, &FontCatalogLoader::onFinished);
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart)
                return;
            m_errors.append(QStringLiteral("%1 nicht ausführbar (%2)")
                                .arg(m_current.program, m_process->errorString()));
            // Ohne Start kommt kein finished(); mit dem nächsten Schritt weiter.
            QMetaObject::invokeMethod(this, &FontCatalogLoader::runNext, Qt::QueuedConnection);
        });
    }
    // https://doc.qt.io/qt-6.8/qprocess.html
    m_process->start(m_current.program, queryArguments(m_current.files));
}

void FontCatalogLoader::onFinished()
{
    QList<FontFace> faces = parseFontconfigOutput(m_process->readAllStandardOutput(), m_current.origin);
    if (m_current.origin == FontOrigin::Media)
        replaceFiles(&faces, m_current.files, m_current.references);
    m_faces.append(faces);
    if (m_process->exitStatus() != QProcess::NormalExit || m_process->exitCode() != 0)
        m_errors.append(QStringLiteral("%1 endete mit Exit-Code %2")
                            .arg(m_current.program).arg(m_process->exitCode()));
    m_process->readAllStandardError();
    // Nächster Schritt erst nach dem Ende dieses Signals von QProcess.
    QMetaObject::invokeMethod(this, &FontCatalogLoader::runNext, Qt::QueuedConnection);
}

} // namespace dss
