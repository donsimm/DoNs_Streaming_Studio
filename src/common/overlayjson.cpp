#include "overlayjson.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

#include <cmath>
#include <limits>

namespace dss {

namespace {

QJsonObject geometryAndCommon(const Source &source)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), source.id.toString(QUuid::WithoutBraces));
    o.insert(QStringLiteral("kind"), sourceKindName(source.kind));
    o.insert(QStringLiteral("name"), source.name);
    o.insert(QStringLiteral("x"), source.x);
    o.insert(QStringLiteral("y"), source.y);
    o.insert(QStringLiteral("width"), source.width);
    o.insert(QStringLiteral("height"), source.height);
    o.insert(QStringLiteral("rotation"), source.rotation);
    o.insert(QStringLiteral("flip_horizontal"), source.flipHorizontal);
    o.insert(QStringLiteral("flip_vertical"), source.flipVertical);
    o.insert(QStringLiteral("visible"), source.visible);
    return o;
}

QJsonObject sourceToJson(const Source &source)
{
    QJsonObject o = geometryAndCommon(source);
    switch (source.kind) {
    case SourceKind::Text: {
        QJsonObject t;
        t.insert(QStringLiteral("content"), source.text.content);
        t.insert(QStringLiteral("font_file"), source.text.fontFile);
        t.insert(QStringLiteral("weight"), source.text.weight);
        t.insert(QStringLiteral("italic"), source.text.italic);
        t.insert(QStringLiteral("font_size"), source.text.fontSize);
        t.insert(QStringLiteral("color"), source.text.color);
        t.insert(QStringLiteral("align"), textAlignName(source.text.align));
        o.insert(QStringLiteral("text"), t);
        break;
    }
    case SourceKind::Image:
    case SourceKind::Video: {
        QJsonObject f;
        f.insert(QStringLiteral("file"), source.file);
        if (source.kind == SourceKind::Video) {
            f.insert(QStringLiteral("loop"), source.loop);
            f.insert(QStringLiteral("hold_at_end"), source.holdAtEnd);
        }
        o.insert(sourceKindName(source.kind), f);
        break;
    }
    }
    return o;
}

// Liest Felder eines JSON-Objekts streng. Der erste Fehler bleibt stehen, mit Pfad
// zum Feld (zum Beispiel «sources[2].text.weight»).
class Reader
{
public:
    Reader(const QJsonObject &object, QString path, QString *error)
        : m_object(object), m_path(std::move(path)), m_error(error)
    {
    }

    bool ok() const { return m_error->isEmpty(); }
    QString fieldPath(const QString &key) const
    {
        if (m_path.isEmpty() || key.isEmpty())
            return m_path.isEmpty() ? key : m_path;
        return m_path + QLatin1Char('.') + key;
    }

    void fail(const QString &key, const QString &message)
    {
        if (!ok())
            return;
        const QString field = fieldPath(key);
        *m_error = field.isEmpty() ? message
                                   : QStringLiteral("Feld «%1»: %2").arg(field, message);
    }

    // Erlaubt nur die genannten Schlüssel, alles andere ist ein Fehler.
    void allowOnly(const QStringList &keys)
    {
        for (auto it = m_object.begin(); it != m_object.end(); ++it) {
            if (!keys.contains(it.key()))
                fail(it.key(), QStringLiteral("unbekanntes Feld"));
        }
    }

    QJsonValue need(const QString &key)
    {
        const QJsonValue v = m_object.value(key);
        if (v.isUndefined())
            fail(key, QStringLiteral("fehlt"));
        return v;
    }

    QString string(const QString &key)
    {
        const QJsonValue v = need(key);
        if (!v.isUndefined() && !v.isString())
            fail(key, QStringLiteral("muss Text sein"));
        return v.toString();
    }

    bool boolean(const QString &key)
    {
        const QJsonValue v = need(key);
        if (!v.isUndefined() && !v.isBool())
            fail(key, QStringLiteral("muss true oder false sein"));
        return v.toBool();
    }

    // Ganze Zahl im Bereich von int. JSON kennt nur Gleitkommazahlen; 12.5 oder 1e12 wird abgewiesen.
    int integer(const QString &key)
    {
        const QJsonValue v = need(key);
        if (v.isUndefined())
            return 0;
        const double d = v.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!v.isDouble() || std::floor(d) != d || d < std::numeric_limits<int>::min()
            || d > std::numeric_limits<int>::max()) {
            fail(key, QStringLiteral("muss eine ganze Zahl sein"));
            return 0;
        }
        return int(d);
    }

    QUuid uuid(const QString &key)
    {
        const QString s = string(key);
        const QUuid id = QUuid::fromString(s);
        // Nur die Schreibweise ohne Klammern in Kleinbuchstaben, wie sie geschrieben wird.
        if (ok() && (id.isNull() || id.toString(QUuid::WithoutBraces) != s))
            fail(key, QStringLiteral("muss eine UUID in der Form xxxxxxxx-xxxx-… sein"));
        return id;
    }

    QJsonObject object(const QString &key)
    {
        const QJsonValue v = need(key);
        if (!v.isUndefined() && !v.isObject())
            fail(key, QStringLiteral("muss ein Objekt sein"));
        return v.toObject();
    }

    QJsonArray array(const QString &key)
    {
        const QJsonValue v = need(key);
        if (!v.isUndefined() && !v.isArray())
            fail(key, QStringLiteral("muss eine Liste sein"));
        return v.toArray();
    }

    // Meldung einer Prüfung aus overlay.h dem Feld zuordnen.
    void check(const QString &key, const QString &validationError)
    {
        if (!validationError.isEmpty())
            fail(key, validationError);
    }

private:
    QJsonObject m_object;
    QString m_path;
    QString *m_error;
};

bool sourceFromJson(const QJsonObject &object, const QString &path, int version, Source *source,
                    QString *error)
{
    Reader r(object, path, error);
    source->id = r.uuid(QStringLiteral("id"));
    const QString kindName = r.string(QStringLiteral("kind"));
    if (!r.ok())
        return false;
    if (!sourceKindFromName(kindName, &source->kind)) {
        r.fail(QStringLiteral("kind"),
               QStringLiteral("unbekannte Art «%1», erlaubt sind text, image, video").arg(kindName));
        return false;
    }
    const QString kindKey = sourceKindName(source->kind);
    QStringList keys = {QStringLiteral("id"), QStringLiteral("kind"), QStringLiteral("name"),
                        QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("width"),
                        QStringLiteral("height"), QStringLiteral("rotation"),
                        QStringLiteral("visible"), kindKey};
    // flip_horizontal und flip_vertical gibt es erst ab Version 3 (P09b); vorher gilt false.
    if (version >= 3)
        keys << QStringLiteral("flip_horizontal") << QStringLiteral("flip_vertical");
    r.allowOnly(keys);
    source->name = r.string(QStringLiteral("name"));
    if (r.ok() && source->name.trimmed().isEmpty())
        r.fail(QStringLiteral("name"), QStringLiteral("darf nicht leer sein"));
    source->x = r.integer(QStringLiteral("x"));
    source->y = r.integer(QStringLiteral("y"));
    source->width = r.integer(QStringLiteral("width"));
    source->height = r.integer(QStringLiteral("height"));
    source->rotation = r.integer(QStringLiteral("rotation"));
    if (version >= 3) {
        source->flipHorizontal = r.boolean(QStringLiteral("flip_horizontal"));
        source->flipVertical = r.boolean(QStringLiteral("flip_vertical"));
    }
    source->visible = r.boolean(QStringLiteral("visible"));
    if (r.ok())
        r.check(QString(), validateSourceGeometry(source->width, source->height, source->rotation));
    const QJsonObject props = r.object(kindKey);
    if (!r.ok())
        return false;

    Reader p(props, r.fieldPath(kindKey), error);
    if (source->kind == SourceKind::Text) {
        TextProperties &t = source->text;
        p.allowOnly({QStringLiteral("content"), QStringLiteral("font_file"), QStringLiteral("weight"),
                     QStringLiteral("italic"), QStringLiteral("font_size"), QStringLiteral("color"),
                     QStringLiteral("align")});
        t.content = p.string(QStringLiteral("content"));
        t.fontFile = p.string(QStringLiteral("font_file"));
        t.weight = p.integer(QStringLiteral("weight"));
        t.italic = p.boolean(QStringLiteral("italic"));
        t.fontSize = p.integer(QStringLiteral("font_size"));
        t.color = p.string(QStringLiteral("color"));
        const QString align = p.string(QStringLiteral("align"));
        if (p.ok() && !textAlignFromName(align, &t.align))
            p.fail(QStringLiteral("align"),
                   QStringLiteral("unbekannt «%1», erlaubt sind left, center, right").arg(align));
        if (p.ok())
            p.check(QString(), validateTextProperties(t));
    } else if (source->kind == SourceKind::Video) {
        // hold_at_end gibt es erst ab Version 2 (P09); in Version 1 gilt false.
        if (version >= 2) {
            p.allowOnly({QStringLiteral("file"), QStringLiteral("loop"), QStringLiteral("hold_at_end")});
        } else {
            p.allowOnly({QStringLiteral("file"), QStringLiteral("loop")});
        }
        source->file = p.string(QStringLiteral("file"));
        source->loop = p.boolean(QStringLiteral("loop"));
        if (version >= 2)
            source->holdAtEnd = p.boolean(QStringLiteral("hold_at_end"));
        if (p.ok())
            p.check(QStringLiteral("file"), validateMediaReference(source->file));
    } else {
        p.allowOnly({QStringLiteral("file")});
        source->file = p.string(QStringLiteral("file"));
        if (p.ok())
            p.check(QStringLiteral("file"), validateMediaReference(source->file));
    }
    return p.ok();
}

} // namespace

QJsonObject overlayToJson(const Overlay &overlay)
{
    QJsonObject o;
    o.insert(QStringLiteral("format_version"), kOverlayFormatVersion);
    o.insert(QStringLiteral("id"), overlay.id.toString(QUuid::WithoutBraces));
    o.insert(QStringLiteral("name"), overlay.name);
    o.insert(QStringLiteral("address"), overlay.address);
    o.insert(QStringLiteral("width"), overlay.width);
    o.insert(QStringLiteral("height"), overlay.height);
    QJsonArray sources;
    for (const Source &source : overlay.sources)
        sources.append(sourceToJson(source));
    o.insert(QStringLiteral("sources"), sources);
    return o;
}

bool overlayFromJson(const QJsonObject &object, Overlay *overlay, QString *error)
{
    error->clear();
    Reader r(object, QString(), error);
    // Zuerst die Version: Eine neuere Datei wird abgewiesen, nicht geraten.
    const int version = r.integer(QStringLiteral("format_version"));
    if (!r.ok())
        return false;
    if (version > kOverlayFormatVersion) {
        *error = QStringLiteral("format_version %1 ist neuer als die bekannte Version %2.")
                     .arg(version).arg(kOverlayFormatVersion);
        return false;
    }
    if (version < kOverlayFormatVersionMin) {
        *error = QStringLiteral("format_version %1 ist unbekannt, erwartet %2 bis %3.")
                     .arg(version).arg(kOverlayFormatVersionMin).arg(kOverlayFormatVersion);
        return false;
    }
    r.allowOnly({QStringLiteral("format_version"), QStringLiteral("id"), QStringLiteral("name"),
                 QStringLiteral("address"), QStringLiteral("width"), QStringLiteral("height"),
                 QStringLiteral("sources")});
    Overlay result;
    result.id = r.uuid(QStringLiteral("id"));
    result.name = r.string(QStringLiteral("name"));
    if (r.ok())
        r.check(QStringLiteral("name"), validateOverlayName(result.name));
    result.address = r.string(QStringLiteral("address"));
    if (r.ok())
        r.check(QStringLiteral("address"), validateAddress(result.address));
    result.width = r.integer(QStringLiteral("width"));
    result.height = r.integer(QStringLiteral("height"));
    if (r.ok())
        r.check(QString(), validateOverlaySize(result.width, result.height));
    const QJsonArray sources = r.array(QStringLiteral("sources"));
    if (!r.ok())
        return false;

    QSet<QUuid> ids;
    for (qsizetype i = 0; i < sources.size(); ++i) {
        const QString path = QStringLiteral("sources[%1]").arg(i);
        if (!sources.at(i).isObject()) {
            *error = QStringLiteral("Feld «%1»: muss ein Objekt sein").arg(path);
            return false;
        }
        Source source;
        if (!sourceFromJson(sources.at(i).toObject(), path, version, &source, error))
            return false;
        if (ids.contains(source.id)) {
            *error = QStringLiteral("Feld «%1.id»: Kennung kommt mehrfach vor").arg(path);
            return false;
        }
        ids.insert(source.id);
        result.sources.append(source);
    }
    // Zweite Sicherung: dieselbe Prüfung wie beim Bearbeiten.
    if (const QString e = validateOverlay(result); !e.isEmpty()) {
        *error = e;
        return false;
    }
    *overlay = result;
    return true;
}

QByteArray serializeOverlay(const Overlay &overlay)
{
    return QJsonDocument(overlayToJson(overlay)).toJson(QJsonDocument::Indented);
}

bool parseOverlay(const QByteArray &data, Overlay *overlay, QString *error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        *error = QStringLiteral("kein gültiges JSON (%1 bei Zeichen %2)")
                     .arg(parseError.errorString()).arg(parseError.offset);
        return false;
    }
    if (!doc.isObject()) {
        *error = QStringLiteral("kein JSON-Objekt");
        return false;
    }
    return overlayFromJson(doc.object(), overlay, error);
}

} // namespace dss
