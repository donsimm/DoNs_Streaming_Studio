#include "qmfile.h"

#include <QHash>
#include <QList>
#include <QtEndian>

#include <cstring>

namespace dss {

namespace {

// Werte aus qtranslator.cpp (Qt 6.8.2), siehe qmfile.h.
constexpr uchar kMagic[16] = {0x3c, 0xb8, 0x64, 0x18, 0xca, 0xef, 0x9c, 0x95,
                              0xcd, 0x21, 0x1c, 0xbf, 0x60, 0xa1, 0xbd, 0xdd};
constexpr uchar kBlockHashes = 0x42;
constexpr uchar kBlockMessages = 0x69;
enum Tag : uchar {
    TagEnd = 1,
    TagSourceText16 = 2,
    TagTranslation = 3,
    TagContext16 = 4,
    TagObsolete1 = 5,
    TagSourceText = 6,
    TagContext = 7,
    TagComment = 8,
    TagObsolete2 = 9,
};

quint32 read32(const char *p)
{
    return qFromBigEndian<quint32>(p);
}

void append32(QByteArray *out, quint32 value)
{
    char b[4];
    qToBigEndian(value, b);
    out->append(b, 4);
}

struct Block
{
    uchar tag = 0;
    QByteArray data;
};

// Übersetzung (UTF-16 big endian) mit «ss» statt «ß».
QByteArray replaceInTranslation(const QByteArray &utf16, int *count)
{
    QString text(utf16.size() / 2, Qt::Uninitialized);
    qFromBigEndian<char16_t>(utf16.constData(), text.size(), text.data());
    const qsizetype before = text.size();
    const int small = int(text.count(QChar(0x00DF)));
    const int capital = int(text.count(QChar(0x1E9E)));
    if (small + capital == 0)
        return utf16;
    *count += small + capital;
    text.replace(QChar(0x00DF), QStringLiteral("ss"));
    text.replace(QChar(0x1E9E), QStringLiteral("SS"));
    Q_ASSERT(text.size() == before + small + capital);
    QByteArray out(text.size() * 2, Qt::Uninitialized);
    qToBigEndian<char16_t>(text.constData(), text.size(), out.data());
    return out;
}

// Schreibt den Block Messages neu. *offsets: alte Stelle eines Eintrags → neue Stelle.
bool rewriteMessages(const QByteArray &in, QByteArray *out, QHash<quint32, quint32> *offsets,
                     int *count, QString *error)
{
    qsizetype pos = 0;
    bool entryStart = true;
    while (pos < in.size()) {
        if (entryStart) {
            offsets->insert(quint32(pos), quint32(out->size()));
            entryStart = false;
        }
        const uchar tag = uchar(in.at(pos));
        out->append(char(tag));
        ++pos;
        switch (tag) {
        case TagEnd:
            entryStart = true;
            break;
        case TagObsolete1:
            if (pos + 4 > in.size()) {
                *error = QStringLiteral("Eintrag bei %1 abgeschnitten").arg(pos);
                return false;
            }
            out->append(in.mid(pos, 4));
            pos += 4;
            break;
        case TagTranslation:
        case TagSourceText:
        case TagContext:
        case TagComment: {
            if (pos + 4 > in.size()) {
                *error = QStringLiteral("Eintrag bei %1 abgeschnitten").arg(pos);
                return false;
            }
            const quint32 length = read32(in.constData() + pos);
            pos += 4;
            // 0xffffffff = leere (null) Übersetzung ohne Inhalt; Qt übergeht ungerade Längen.
            if (length == 0xffffffffu) {
                append32(out, length);
                break;
            }
            if (pos + qsizetype(length) > in.size()) {
                *error = QStringLiteral("Feld bei %1 länger als der Block").arg(pos);
                return false;
            }
            QByteArray content = in.mid(pos, length);
            pos += length;
            if (tag == TagTranslation && length % 2 == 0)
                content = replaceInTranslation(content, count);
            append32(out, quint32(content.size()));
            out->append(content);
            break;
        }
        default:
            // Tag_SourceText16, Tag_Context16, Tag_Obsolete2 und Unbekanntes schreibt lrelease
            // heute nicht; nicht raten.
            *error = QStringLiteral("unbekanntes Feld %1 bei %2").arg(tag).arg(pos - 1);
            return false;
        }
    }
    if (!entryStart) {
        *error = QStringLiteral("letzter Eintrag ohne Ende");
        return false;
    }
    return true;
}

} // namespace

bool replaceSharpS(const QByteArray &qm, QByteArray *result, QString *error, int *replaced)
{
    result->clear();
    if (qm.size() < 16 || memcmp(qm.constData(), kMagic, 16) != 0) {
        *error = QStringLiteral("keine Übersetzungsdatei von Qt (Kennung fehlt)");
        return false;
    }
    QList<Block> blocks;
    qsizetype pos = 16;
    while (pos < qm.size()) {
        if (pos + 5 > qm.size()) {
            *error = QStringLiteral("Block bei %1 abgeschnitten").arg(pos);
            return false;
        }
        Block b;
        b.tag = uchar(qm.at(pos));
        const quint32 length = read32(qm.constData() + pos + 1);
        pos += 5;
        if (pos + qsizetype(length) > qm.size()) {
            *error = QStringLiteral("Block %1 länger als die Datei").arg(b.tag);
            return false;
        }
        b.data = qm.mid(pos, length);
        pos += length;
        blocks.append(b);
    }

    int count = 0;
    QHash<quint32, quint32> offsets;
    bool haveMessages = false;
    for (Block &b : blocks) {
        if (b.tag != kBlockMessages)
            continue;
        QByteArray messages;
        if (!rewriteMessages(b.data, &messages, &offsets, &count, error))
            return false;
        b.data = messages;
        haveMessages = true;
    }
    if (!haveMessages) {
        *error = QStringLiteral("Block Messages fehlt");
        return false;
    }
    for (Block &b : blocks) {
        if (b.tag != kBlockHashes)
            continue;
        if (b.data.size() % 8 != 0) {
            *error = QStringLiteral("Block Hashes hat keine ganzen Paare");
            return false;
        }
        for (qsizetype i = 0; i < b.data.size(); i += 8) {
            const quint32 old = read32(b.data.constData() + i + 4);
            if (!offsets.contains(old)) {
                *error = QStringLiteral("Hash zeigt auf %1, dort beginnt kein Eintrag").arg(old);
                return false;
            }
            qToBigEndian(offsets.value(old), b.data.data() + i + 4);
        }
    }

    result->append(reinterpret_cast<const char *>(kMagic), 16);
    for (const Block &b : blocks) {
        result->append(char(b.tag));
        append32(result, quint32(b.data.size()));
        result->append(b.data);
    }
    if (replaced)
        *replaced = count;
    return true;
}

} // namespace dss
