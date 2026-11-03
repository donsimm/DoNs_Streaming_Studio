#include "httpmessage.h"

namespace dss {

bool HttpRequest::keepAlive() const
{
    const QByteArray connection = headers.value("connection").toLower();
    if (version == "HTTP/1.0")
        return connection.contains("keep-alive");
    return !connection.contains("close");
}

ParseResult takeHttpRequest(QByteArray *buffer, HttpRequest *request)
{
    const qsizetype headEnd = buffer->indexOf("\r\n\r\n");
    if (headEnd < 0)
        return buffer->size() > kMaxHttpHeaderBytes ? ParseResult::Invalid : ParseResult::Incomplete;
    if (headEnd > kMaxHttpHeaderBytes)
        return ParseResult::Invalid;
    const QList<QByteArray> lines = buffer->left(headEnd).split('\n');
    HttpRequest r;
    const QList<QByteArray> first = lines.first().trimmed().split(' ');
    if (first.size() != 3 || !first.at(2).startsWith("HTTP/1."))
        return ParseResult::Invalid;
    r.method = first.at(0);
    r.target = first.at(1);
    r.version = first.at(2);
    const qsizetype query = r.target.indexOf('?');
    if (query >= 0)
        r.target.truncate(query);
    if (!r.target.startsWith('/'))
        return ParseResult::Invalid;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QByteArray line = lines.at(i).trimmed();
        const qsizetype colon = line.indexOf(':');
        if (colon <= 0)
            return ParseResult::Invalid;
        r.headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
    }
    if (r.headers.contains("transfer-encoding"))
        return ParseResult::Invalid; // Chunked-Anfragen braucht keine Seite
    qint64 length = 0;
    if (r.headers.contains("content-length")) {
        bool ok = false;
        length = r.headers.value("content-length").toLongLong(&ok);
        if (!ok || length < 0 || length > kMaxHttpBodyBytes)
            return ParseResult::Invalid;
    }
    const qsizetype total = headEnd + 4 + qsizetype(length);
    if (buffer->size() < total)
        return ParseResult::Incomplete;
    r.body = buffer->mid(headEnd + 4, qsizetype(length));
    buffer->remove(0, total);
    *request = r;
    return ParseResult::Complete;
}

std::optional<ByteRange> parseRange(const QByteArray &header, qint64 size)
{
    if (header.isEmpty())
        return std::nullopt;
    const ByteRange invalid{-1, -1};
    if (!header.startsWith("bytes=") || header.contains(','))
        return invalid;
    const QByteArray spec = header.mid(6).trimmed();
    const qsizetype dash = spec.indexOf('-');
    if (dash < 0)
        return invalid;
    const QByteArray a = spec.left(dash).trimmed();
    const QByteArray b = spec.mid(dash + 1).trimmed();
    bool okA = true;
    bool okB = true;
    if (a.isEmpty()) {
        const qint64 n = b.toLongLong(&okB);
        if (!okB || n <= 0 || size == 0)
            return invalid;
        return ByteRange{std::max<qint64>(0, size - n), size - 1};
    }
    const qint64 first = a.toLongLong(&okA);
    const qint64 last = b.isEmpty() ? size - 1 : b.toLongLong(&okB);
    if (!okA || !okB || first < 0 || first >= size || last < first)
        return invalid;
    return ByteRange{first, std::min(last, size - 1)};
}

QByteArray statusLine(int status)
{
    switch (status) {
    case 200: return "200 OK";
    case 204: return "204 No Content";
    case 206: return "206 Partial Content";
    case 302: return "302 Found";
    case 400: return "400 Bad Request";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 416: return "416 Range Not Satisfiable";
    default: return QByteArray::number(status) + " Fehler";
    }
}

QByteArray responseHead(int status, const QByteArray &contentType, qint64 length,
                        const QList<std::pair<QByteArray, QByteArray>> &extra)
{
    QByteArray h = "HTTP/1.1 " + statusLine(status) + "\r\n";
    if (!contentType.isEmpty())
        h += "Content-Type: " + contentType + "\r\n";
    if (length >= 0)
        h += "Content-Length: " + QByteArray::number(length) + "\r\n";
    for (const auto &[name, value] : extra)
        h += name + ": " + value + "\r\n";
    h += "\r\n";
    return h;
}

QByteArray contentTypeForSuffix(const QString &suffix)
{
    static const QHash<QString, QByteArray> types = {
        {QStringLiteral("html"), "text/html; charset=utf-8"},
        {QStringLiteral("js"), "text/javascript; charset=utf-8"},
        {QStringLiteral("css"), "text/css; charset=utf-8"},
        {QStringLiteral("png"), "image/png"},
        {QStringLiteral("jpg"), "image/jpeg"},
        {QStringLiteral("jpeg"), "image/jpeg"},
        {QStringLiteral("gif"), "image/gif"},
        {QStringLiteral("webp"), "image/webp"},
        {QStringLiteral("webm"), "video/webm"},
        {QStringLiteral("mp4"), "video/mp4"},
        {QStringLiteral("m4v"), "video/mp4"},
        {QStringLiteral("mov"), "video/quicktime"},
        {QStringLiteral("mkv"), "video/x-matroska"},
        {QStringLiteral("ttf"), "font/ttf"},
        {QStringLiteral("otf"), "font/otf"},
        {QStringLiteral("woff"), "font/woff"},
        {QStringLiteral("woff2"), "font/woff2"},
    };
    return types.value(suffix.toLower(), "application/octet-stream");
}

bool constantTimeEquals(QByteArrayView a, QByteArrayView b)
{
    if (a.size() != b.size())
        return false;
    // volatile, damit der Übersetzer die Schleife nicht vorzeitig abbricht (wie sodium_memcmp).
    volatile unsigned char diff = 0;
    for (qsizetype i = 0; i < a.size(); ++i)
        diff = diff | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]));
    return diff == 0;
}

} // namespace dss
