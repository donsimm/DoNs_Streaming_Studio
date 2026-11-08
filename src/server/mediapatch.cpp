#include "mediapatch.h"

#include "imageorientation.h"

#include <QFile>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <optional>

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

// Matroska/WebM (EBML): IDs der Elemente (FFmpeg matroska.h)
constexpr quint32 kEbmlHeader = 0x1A45DFA3;
constexpr quint32 kSegment = 0x18538067;
constexpr quint32 kTracks = 0x1654AE6B;
constexpr quint32 kCluster = 0x1F43B675;
constexpr quint32 kTrackEntry = 0xAE;
constexpr quint32 kTrackVideo = 0xE0;
constexpr quint32 kProjection = 0x7670;
constexpr quint32 kProjectionType = 0x7671;
constexpr quint32 kProjectionPoseYaw = 0x7673;
constexpr quint32 kProjectionPoseRoll = 0x7675;
// Tracks ist klein (Angaben der Spuren); grösser lesen wir nicht in den Speicher.
constexpr qint64 kMaxTracksBytes = 16 * 1024 * 1024;

// Kopf eines EBML-Elements: ID (1 bis 4 Byte, mit Längenmarke) und Grösse (1 bis 8 Byte, ohne
// Längenmarke; alle Bits 1 = unbekannt).
struct EbmlHead
{
    quint32 id = 0;
    int length = 0; // Bytes von ID und Grösse
    quint64 size = 0;
    bool unknownSize = false;
};

int vintLength(uchar first)
{
    for (int n = 1; n <= 8; ++n)
        if (first & (0x80 >> (n - 1)))
            return n;
    return 0;
}

std::optional<EbmlHead> ebmlHead(const uchar *p, qint64 available)
{
    if (available < 2)
        return std::nullopt;
    const int idLength = vintLength(p[0]);
    if (idLength == 0 || idLength > 4 || idLength >= available)
        return std::nullopt;
    EbmlHead h;
    for (int i = 0; i < idLength; ++i)
        h.id = (h.id << 8) | p[i];
    const uchar *s = p + idLength;
    const int sizeLength = vintLength(s[0]);
    if (sizeLength == 0 || idLength + sizeLength > available)
        return std::nullopt;
    const quint64 mask = 0xFFu >> sizeLength;
    h.size = s[0] & mask;
    bool allOnes = h.size == mask;
    for (int i = 1; i < sizeLength; ++i) {
        h.size = (h.size << 8) | s[i];
        allOnes = allOnes && s[i] == 0xFF;
    }
    h.unknownSize = allOnes;
    h.length = idLength + sizeLength;
    return h;
}

struct EbmlElement
{
    quint32 id = 0;
    qint64 payload = 0; // Lage der Daten in data
    qint64 end = 0;
};

// Kinder in data[from, to); unbekannte Grösse oder kaputter Kopf beendet die Liste.
QList<EbmlElement> ebmlChildren(const QByteArray &data, qint64 from, qint64 to)
{
    QList<EbmlElement> result;
    const auto *p = reinterpret_cast<const uchar *>(data.constData());
    qint64 at = from;
    for (int i = 0; i < kMaxBoxes && at < to; ++i) {
        const std::optional<EbmlHead> h = ebmlHead(p + at, to - at);
        if (!h || h->unknownSize || qint64(h->size) > to - at - h->length)
            break;
        result.append({h->id, at + h->length, at + h->length + qint64(h->size)});
        at = result.last().end;
    }
    return result;
}

// Projection: Yaw und Roll → 0.0 (nur Typ «rechteckig», der Standard; die anderen Typen sind 360°
// und ergeben bei FFmpeg keine Display-Matrix). Gleiche Länge (4 oder 8 Byte Gleitkomma).
void projectionPatches(const QByteArray &tracks, qint64 base, const EbmlElement &projection, BytePatches *patches)
{
    const QList<EbmlElement> children = ebmlChildren(tracks, projection.payload, projection.end);
    for (const EbmlElement &c : children) {
        if (c.id != kProjectionType)
            continue;
        for (qint64 i = c.payload; i < c.end; ++i)
            if (tracks.at(i) != 0)
                return; // nicht rechteckig
    }
    for (const EbmlElement &c : children) {
        const qint64 size = c.end - c.payload;
        if ((c.id != kProjectionPoseYaw && c.id != kProjectionPoseRoll) || (size != 4 && size != 8))
            continue;
        const QByteArray zero(size, '\0');
        if (tracks.mid(c.payload, size) != zero)
            patches->append({base + c.payload, zero});
    }
}

void matroskaPatches(const QString &path, BytePatches *patches)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return;
    const qint64 fileSize = file.size();
    const auto headAt = [&](qint64 at) -> std::optional<EbmlHead> {
        if (!file.seek(at))
            return std::nullopt;
        const QByteArray h = file.read(12);
        return ebmlHead(reinterpret_cast<const uchar *>(h.constData()), h.size());
    };
    const std::optional<EbmlHead> ebml = headAt(0);
    if (!ebml || ebml->id != kEbmlHeader || ebml->unknownSize)
        return;
    const qint64 segmentAt = ebml->length + qint64(ebml->size);
    const std::optional<EbmlHead> segment = headAt(segmentAt);
    if (!segment || segment->id != kSegment)
        return;
    qint64 at = segmentAt + segment->length;
    const qint64 segmentEnd = segment->unknownSize ? fileSize : std::min(fileSize, at + qint64(segment->size));
    // Oberste Ebene des Segments nur über die Köpfe, bis Tracks; die Bilder (Cluster) folgen danach.
    for (int i = 0; i < kMaxBoxes && at < segmentEnd; ++i) {
        const std::optional<EbmlHead> h = headAt(at);
        if (!h || h->unknownSize || h->id == kCluster)
            return;
        const qint64 payload = at + h->length;
        if (qint64(h->size) > segmentEnd - payload)
            return;
        if (h->id == kTracks) {
            if (qint64(h->size) > kMaxTracksBytes || !file.seek(payload))
                return;
            const QByteArray tracks = file.read(qint64(h->size));
            if (tracks.size() != qint64(h->size))
                return;
            for (const EbmlElement &entry : ebmlChildren(tracks, 0, tracks.size())) {
                if (entry.id != kTrackEntry)
                    continue;
                for (const EbmlElement &video : ebmlChildren(tracks, entry.payload, entry.end)) {
                    if (video.id != kTrackVideo)
                        continue;
                    for (const EbmlElement &p : ebmlChildren(tracks, video.payload, video.end))
                        if (p.id == kProjection)
                            projectionPatches(tracks, payload, p, patches);
                }
            }
            return;
        }
        at = payload + qint64(h->size);
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
    else if (start.startsWith("\x1A\x45\xDF\xA3"))
        matroskaPatches(path, &patches);
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
