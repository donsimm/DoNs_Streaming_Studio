#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Ausrichtung in ausgelieferten Mediendateien neutralisieren (P10, Teil B 2). Geprüft am
// 2026-11-03 mit Qt WebEngine 6.8.2 (Chromium 122): Chromium dreht JPEG nach EXIF, PNG mit eXIf
// nicht; H.264 in MP4 nach der Display-Matrix, VP9 in MP4 nicht (Bericht P10). Damit das Ergebnis
// nicht vom Format und von der Chromium-Version abhängt, setzt der Server die Angaben in den
// ausgelieferten Bytes auf «keine Drehung» und die Seite wendet die Ausrichtung an, die der Kern
// mitschickt (dieselbe wie im Editor). Die Datei im Datenordner bleibt unverändert; geändert
// wird nur die Kopie im Speicher bzw. die gesendeten Bytes, bei gleicher Länge (Bereiche für
// Videos bleiben gültig).
//
// Quellen:
//   EXIF 2.32, Tag Orientation 0x0112: https://www.cipa.jp/std/documents/e/DC-X008-Translation-2019-E.pdf
//   PNG eXIf und CRC-32 (Anhang D): https://www.w3.org/TR/png-3/#eXIf, https://www.w3.org/TR/png-3/#samplecrc
//   ISO/IEC 14496-12 (Boxen mvhd, tkhd mit Matrix), Felder wie FFmpeg n7.1 mov_read_mvhd und
//   mov_read_tkhd: https://raw.githubusercontent.com/FFmpeg/FFmpeg/n7.1/libavformat/mov.c

namespace dss {

struct BytePatch
{
    qint64 offset = 0;
    QByteArray bytes;
    bool operator==(const BytePatch &) const = default;
};
using BytePatches = QList<BytePatch>;

// Ersetzungen für die Datei: JPEG/PNG Ausrichtung → 1 (PNG mit neuer CRC des Chunks), MP4/MOV
// Matrix in mvhd und jedem tkhd → Einheitsmatrix. Leer, wenn nichts zu ändern ist oder das
// Format unbekannt ist (WebM/Matroska: [OFFEN], P10-Bericht).
BytePatches orientationPatches(const QString &path);

// Wendet die Ersetzungen auf data an, das ab offset in der Datei beginnt.
void applyPatches(QByteArray *data, qint64 offset, const BytePatches &patches);

// CRC-32 wie in PNG (Anhang D).
quint32 pngCrc32(QByteArrayView data);

} // namespace dss
