#include "mediapatch.h"

#include "imageorientation.h"

#include <QFile>
#include <QtEndian>

#include <array>

namespace dss {

namespace {

// Höchstens so viele Boxen je Ebene ansehen (Schutz vor kaputten Dateien).
constexpr int kMaxBoxes = 4096;
// moov ist klein (Kopf und Tabellen); grösser lesen wir nicht in den Speicher.
constexpr qint64 kMaxMoovBytes = 64 * 1024 * 1024;

// Einheitsmatrix: a, b, u, c, d, v, x, y, w mit a = d = 1.0 (16.16), w = 1.0 (2.30)
QByteArray identityMatrix()
{
    QByteArray m(36, '\0');
    auto *d = reinterpret_cast<uchar *>(m.data());
    qToBigEndian<quint32>(0x00010000, d + 0);
    qToBigEndian<quint32>(0x00010000, d + 16);
    qToBigEndian<quint32>(0x40000000, d + 32);
    return m;
}

void imagePatches(const QString &path, BytePatches *patches)
{
    const OrientationLocation l = imageOrientationLocation(path);
    if (l.orientation == kOrientationNormal || l.valueOffset < 0)
        return;
    QByteArray one(2, '\0');
    if (l.littleEndian)
        qToLittleEndian<quint16>(1, one.data());
    else
        qToBigEndian<quint16>(1, one.data());
    patches->append({l.valueOffset, one});
    if (l.pngChunk < 0)
        return;
    // PNG: CRC über Typ und Daten des Chunks neu rechnen.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || !file.seek(l.pngChunk + 4))
        return;
    QByteArray typeAndData = file.read(4 + qint64(l.pngChunkLength));
    if (typeAndData.size() != 4 + qint64(l.pngChunkLength))
        return;
    applyPatches(&typeAndData, l.pngChunk + 4, *patches);
    QByteArray crc(4, '\0');
    qToBigEndian<quint32>(pngCrc32(typeAndData), crc.data());
    patches->append({l.pngChunk + 8 + qint64(l.pngChunkLength), crc});
}

struct Box
{
    qint64 start = 0;   // Lage des Kopfs
    qint64 payload = 0; // Lage nach Kopf (Grösse, Typ, ggf. largesize)
    qint64 end = 0;
    QByteArray type;
};

// Boxen in data[from, to) (data beginnt bei base in der Datei).
QList<Box> boxes(const QByteArray &data, qint64 base, qint64 from, qint64 to)
{
    QList<Box> result;
    qint64 at = from;
    for (int i = 0; i < kMaxBoxes && at + 8 <= to; ++i) {
        const auto *p = reinterpret_cast<const uchar *>(data.constData()) + at;
        quint64 size = qFromBigEndian<quint32>(p);
        Box b;
        b.start = base + at;
        b.type = data.mid(at, 4 + 4).mid(4);
        qint64 header = 8;
        if (size == 1) {
            if (at + 16 > to)
                break;
            size = qFromBigEndian<quint64>(p + 8);
            header = 16;
        } else if (size == 0) {
            size = quint64(to - at);
        }
        if (size < quint64(header) || qint64(size) > to - at)
            break;
        b.payload = b.start + header;
        b.end = b.start + qint64(size);
        result.append(b);
        at += qint64(size);
    }
    return result;
}

void matrixPatch(const QByteArray &moov, qint64 base, const Box &box, qint64 offsetV0, qint64 offsetV1,
                 BytePatches *patches)
{
    const qint64 local = box.payload - base;
    if (local + 1 > moov.size())
        return;
    const int version = quint8(moov.at(local));
    const qint64 matrix = local + (version == 1 ? offsetV1 : offsetV0);
    if (matrix + 36 > box.end - base)
        return;
    static const QByteArray identity = identityMatrix();
    if (moov.mid(matrix, 36) != identity)
        patches->append({base + matrix, identity});
}

void moviePatches(const QString &path, BytePatches *patches)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    // Oberste Ebene ohne die ganze Datei zu lesen: nur die Köpfe.
    qint64 at = 0;
    const qint64 size = file.size();
    for (int i = 0; i < kMaxBoxes && at + 8 <= size; ++i) {
        if (!file.seek(at))
            return;
        const QByteArray head = file.read(16);
        if (head.size() < 8)
            return;
        const auto *p = reinterpret_cast<const uchar *>(head.constData());
        quint64 boxSize = qFromBigEndian<quint32>(p);
        if (boxSize == 1 && head.size() == 16)
            boxSize = qFromBigEndian<quint64>(p + 8);
        else if (boxSize == 0)
            boxSize = quint64(size - at);
        if (boxSize < 8 || qint64(boxSize) > size - at)
            return;
        if (head.mid(4, 4) == "moov") {
            if (qint64(boxSize) > kMaxMoovBytes)
                return;
            file.seek(at);
            const QByteArray moov = file.read(qint64(boxSize));
            const QList<Box> top = boxes(moov, at, 0, moov.size());
            if (top.isEmpty())
                return;
            const Box &m = top.first();
            // mvhd: version/flags 4, Zeiten 4+4 (v1: 8+8), timescale 4, duration 4 (v1: 8),
            // rate 4, volume 2, reserviert 10, dann Matrix
            // tkhd: version/flags 4, Zeiten 4+4 (v1: 8+8), track_ID 4, reserviert 4,
            // duration 4 (v1: 8), reserviert 8, layer 2, alternate_group 2, volume 2,
            // reserviert 2, dann Matrix
            for (const Box &child : boxes(moov, at, m.payload - at, m.end - at)) {
                if (child.type == "mvhd")
                    matrixPatch(moov, at, child, 36, 48, patches);
                if (child.type != "trak")
                    continue;
                for (const Box &t : boxes(moov, at, child.payload - at, child.end - at)) {
                    if (t.type == "tkhd")
                        matrixPatch(moov, at, t, 40, 52, patches);
                }
            }
            return;
        }
        at += qint64(boxSize);
    }
}

} // namespace

quint32 pngCrc32(QByteArrayView data)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 n = 0; n < 256; ++n) {
            quint32 c = n;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    quint32 c = 0xFFFFFFFFu;
    for (char ch : data)
        c = table[(c ^ quint8(ch)) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

BytePatches orientationPatches(const QString &path)
{
    BytePatches patches;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return patches;
    const QByteArray start = file.peek(12);
    file.close();
    if (start.startsWith("\xFF\xD8") || start.startsWith("\x89PNG\r\n\x1A\n"))
        imagePatches(path, &patches);
    else if (start.mid(4, 4) == "ftyp" || start.mid(4, 4) == "moov" || start.mid(4, 4) == "mdat"
             || start.mid(4, 4) == "wide" || start.mid(4, 4) == "free")
        moviePatches(path, &patches);
    return patches;
}

void applyPatches(QByteArray *data, qint64 offset, const BytePatches &patches)
{
    const qint64 end = offset + data->size();
    for (const BytePatch &p : patches) {
        const qint64 pEnd = p.offset + p.bytes.size();
        if (pEnd <= offset || p.offset >= end)
            continue;
        const qint64 from = std::max(p.offset, offset);
        const qint64 to = std::min(pEnd, end);
        std::copy(p.bytes.constBegin() + (from - p.offset), p.bytes.constBegin() + (to - p.offset),
                  data->begin() + (from - offset));
    }
}

} // namespace dss
