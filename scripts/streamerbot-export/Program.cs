// Export-Werkzeug für die Zuschauer-Daten aus Streamer.bot (Plan P14, Abschnitt 9.2, Empfehlung A).
// Nicht Teil des Studios: kein CMake, kein ctest. Läuft nur auf einer Kopie des Ordners data/ ohne auth*.db.
//
// Aufruf:
//   streamerbot-export --struktur QUELLORDNER            Sammlungen, Anzahlen, Feldnamen und Typen, ohne Werte
//   streamerbot-export --export QUELLORDNER ZIELORDNER   je Sammlung ZIELORDNER/<datei>/<sammlung>.json
//
// Öffnen der Datenbanken: ReadOnly=true mit Connection=direct, nicht Connection=shared.
// - LiteDB 5 liest beim Öffnen die bestätigten Transaktionen aus der Log-Datei <name>-log.db in den
//   WAL-Index (WalIndexService.RestoreIndex, «Load all confirmed transactions from log file»). Lesende
//   Abfragen sehen damit den neuesten Stand, auch ohne Checkpoint.
// - ReadOnly öffnet die Dateien schreibgeschützt («Open datafile in readonly mode»); jeder Schreibversuch
//   scheitert, statt die Kopie zu verändern.
// - Connection=shared öffnet und schliesst die Engine bei jedem Vorgang (SharedEngine.OpenDatabase und
//   CloseDatabase). Beim Schliessen macht LiteDB einen Checkpoint («do a checkpoint at shutdown», Standard
//   1000 Seiten), kopiert also die Log-Datei in die Datendatei und schreibt damit. Darum nicht shared.
// Quelle: XML-Dokumentation im Paket LiteDB 5.0.21 (lib/netstandard2.0/LiteDB.xml), https://www.litedb.org/docs/
// Zusätzlich prüft das Werkzeug mit SHA-256 vor und nach dem Lesen, dass Datendatei, Log-Datei und
// Temp-Datei unverändert sind.

using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Encodings.Web;
using System.Text.Json;
using LiteDB;

return Exporter.Run(args);

enum FileFormat { LiteDb, Json, Unknown }

sealed class FieldInfo
{
    public SortedSet<string> Types { get; } = new(StringComparer.Ordinal);
    public int Documents { get; set; }
}

static class Exporter
{
    const string LiteDbHeader = "** This is a LiteDB file **";
    static readonly string[] DatFiles = { "quotes.dat", "users.dat" };

    public static int Run(string[] args)
    {
        if (args.Length == 2 && args[0] == "--struktur")
            return ForEachFile(args[1], null);
        if (args.Length == 3 && args[0] == "--export")
            return ForEachFile(args[1], args[2]);

        Console.Error.WriteLine("Aufruf:");
        Console.Error.WriteLine("  streamerbot-export --struktur QUELLORDNER");
        Console.Error.WriteLine("  streamerbot-export --export QUELLORDNER ZIELORDNER");
        return 1;
    }

    // targetDir == null: nur Struktur ausgeben
    static int ForEachFile(string sourceDir, string? targetDir)
    {
        if (!Directory.Exists(sourceDir))
        {
            Console.Error.WriteLine($"Fehler: Quellordner nicht gefunden: {sourceDir}");
            return 1;
        }

        var files = new List<string>();
        int skippedAuth = 0;
        foreach (var path in Directory.EnumerateFiles(sourceDir).OrderBy(p => p, StringComparer.Ordinal))
        {
            var name = Path.GetFileName(path);
            var lower = name.ToLowerInvariant();
            // auth*.db wird nie geöffnet, auch nicht zum Prüfen des Formats
            if (lower.StartsWith("auth") && lower.EndsWith(".db"))
            {
                skippedAuth++;
                continue;
            }
            if (lower.EndsWith(".db") && !lower.EndsWith("-backup.db") && !lower.EndsWith("-log.db")
                && !lower.EndsWith("-tmp.db"))
                files.Add(path);
            else if (DatFiles.Contains(lower))
                files.Add(path);
        }

        if (skippedAuth > 0)
            Console.WriteLine($"Übersprungen: {skippedAuth} Datei(en) auth*.db (nie geöffnet)");
        if (files.Count == 0)
        {
            Console.Error.WriteLine($"Fehler: keine Datenbanken (*.db, quotes.dat, users.dat) in {sourceDir}");
            return 1;
        }

        if (targetDir != null)
            Directory.CreateDirectory(targetDir);

        int errors = 0;
        foreach (var path in files)
        {
            try
            {
                if (!ProcessFile(path, targetDir))
                    errors++;
            }
            catch (Exception ex)
            {
                Console.Error.WriteLine($"Fehler: {Path.GetFileName(path)}: {ex.GetType().Name}: {ex.Message}");
                errors++;
            }
        }

        Console.WriteLine();
        Console.WriteLine($"{files.Count} Datei(en), {errors} mit Fehlern");
        return errors == 0 ? 0 : 1;
    }

    static bool ProcessFile(string path, string? targetDir)
    {
        var name = Path.GetFileName(path);
        var baseName = Path.GetFileNameWithoutExtension(path);
        var (format, head) = DetectFormat(path);

        switch (format)
        {
            case FileFormat.LiteDb:
                return ProcessLiteDb(path, targetDir);

            case FileFormat.Json:
                if (targetDir == null)
                {
                    PrintJsonStructure(path);
                }
                else
                {
                    var target = Path.Combine(targetDir, baseName + ".json");
                    File.Copy(path, target, overwrite: true);
                    Console.WriteLine($"{name}: JSON-Text unverändert kopiert nach {target}");
                }
                return true;

            default:
                Console.Error.WriteLine($"Fehler: {name}: Format unbekannt, erste 16 Bytes: {Hex(head)}");
                return false;
        }
    }

    // Erkennt LiteDB am Kopftext, JSON-Text an der ersten Klammer (nach BOM und Leerraum) und gültigem Inhalt
    static (FileFormat, byte[]) DetectFormat(string path)
    {
        byte[] buffer = new byte[4096];
        int read;
        using (var stream = new FileStream(path, FileMode.Open, FileAccess.Read, FileShare.ReadWrite))
            read = stream.ReadAtLeast(buffer, buffer.Length, throwOnEndOfStream: false);
        var head = buffer.AsSpan(0, Math.Min(16, read)).ToArray();

        var header = Encoding.ASCII.GetBytes(LiteDbHeader);
        if (buffer.AsSpan(0, read).IndexOf(header) >= 0)
            return (FileFormat.LiteDb, head);

        int i = 0;
        if (read >= 3 && buffer[0] == 0xEF && buffer[1] == 0xBB && buffer[2] == 0xBF)
            i = 3;
        while (i < read && (buffer[i] == ' ' || buffer[i] == '\t' || buffer[i] == '\r' || buffer[i] == '\n'))
            i++;
        if (i < read && (buffer[i] == '{' || buffer[i] == '['))
        {
            try
            {
                using var doc = JsonDocument.Parse(File.ReadAllBytes(path).AsMemory(i));
                return (FileFormat.Json, head);
            }
            catch (JsonException)
            {
                // kein gültiges JSON: Format unbekannt
            }
        }
        return (FileFormat.Unknown, head);
    }

    static bool ProcessLiteDb(string path, string? targetDir)
    {
        var name = Path.GetFileName(path);
        var before = Snapshot(path);
        bool hasLog = File.Exists(LogPath(path));

        var connection = new ConnectionString
        {
            Filename = path,
            Connection = ConnectionType.Direct,
            ReadOnly = true,
        };

        using (var db = new LiteDatabase(connection))
        {
            var collections = db.GetCollectionNames().OrderBy(c => c, StringComparer.Ordinal).ToList();
            if (targetDir == null)
            {
                Console.WriteLine();
                Console.WriteLine($"Datei: {name} (LiteDB, Log-Datei {(hasLog ? "vorhanden" : "keine")}, {collections.Count} Sammlung(en))");
            }

            foreach (var collectionName in collections)
            {
                var collection = db.GetCollection(collectionName);
                if (targetDir == null)
                    PrintCollectionStructure(collectionName, collection.FindAll());
                else
                    ExportCollection(name, collectionName, collection.FindAll(), targetDir);
            }
        }

        var after = Snapshot(path);
        foreach (var (file, hash) in before)
        {
            if (after[file] != hash)
            {
                Console.Error.WriteLine($"Fehler: {name}: {Path.GetFileName(file)} hat sich beim Lesen verändert");
                return false;
            }
        }
        return true;
    }

    // Datendatei, Log-Datei und Temp-Datei von LiteDB: Prüfsumme oder «fehlt»
    static Dictionary<string, string> Snapshot(string path)
    {
        var result = new Dictionary<string, string>();
        foreach (var file in new[] { path, LogPath(path), SiblingPath(path, "-tmp") })
        {
            if (!File.Exists(file))
            {
                result[file] = "fehlt";
                continue;
            }
            using var stream = new FileStream(file, FileMode.Open, FileAccess.Read, FileShare.ReadWrite);
            result[file] = Convert.ToHexString(SHA256.HashData(stream));
        }
        return result;
    }

    static string LogPath(string path) => SiblingPath(path, "-log");

    static string SiblingPath(string path, string suffix) =>
        Path.Combine(Path.GetDirectoryName(path) ?? ".",
            Path.GetFileNameWithoutExtension(path) + suffix + Path.GetExtension(path));

    // ---- Struktur ----

    static void PrintCollectionStructure(string collectionName, IEnumerable<BsonDocument> documents)
    {
        var fields = new SortedDictionary<string, FieldInfo>(StringComparer.Ordinal);
        int count = 0;
        foreach (var document in documents)
        {
            count++;
            var seen = new HashSet<string>(StringComparer.Ordinal);
            CollectBson(document, "", fields, seen);
            foreach (var path in seen)
                fields[path].Documents++;
        }
        Console.WriteLine($"  Sammlung: {collectionName} ({count} Dokumente)");
        PrintFields(fields);
    }

    static void CollectBson(BsonValue value, string path, SortedDictionary<string, FieldInfo> fields, HashSet<string> seen)
    {
        if (value.IsDocument)
        {
            foreach (var (key, child) in value.AsDocument)
            {
                var childPath = path.Length == 0 ? key : path + "." + key;
                Note(fields, seen, childPath, child.Type.ToString());
                CollectBson(child, childPath, fields, seen);
            }
        }
        else if (value.IsArray)
        {
            var itemPath = path + "[]";
            foreach (var item in value.AsArray)
            {
                Note(fields, seen, itemPath, item.Type.ToString());
                CollectBson(item, itemPath, fields, seen);
            }
        }
    }

    // JSON-Text: Wurzel-Array zählt jeden Eintrag als Dokument, ein Wurzel-Objekt als ein Dokument
    static void PrintJsonStructure(string path)
    {
        using var doc = JsonDocument.Parse(File.ReadAllBytes(path), new JsonDocumentOptions { AllowTrailingCommas = true });
        var root = doc.RootElement;
        var entries = root.ValueKind == JsonValueKind.Array ? root.EnumerateArray().ToList() : new List<JsonElement> { root };

        var fields = new SortedDictionary<string, FieldInfo>(StringComparer.Ordinal);
        foreach (var entry in entries)
        {
            var seen = new HashSet<string>(StringComparer.Ordinal);
            if (entry.ValueKind == JsonValueKind.Object || entry.ValueKind == JsonValueKind.Array)
                CollectJson(entry, "", fields, seen);
            else
                Note(fields, seen, "(Eintrag)", JsonTypeName(entry));
            foreach (var p in seen)
                fields[p].Documents++;
        }

        Console.WriteLine();
        Console.WriteLine($"Datei: {Path.GetFileName(path)} (JSON-Text, Wurzel {JsonTypeName(root)}, {entries.Count} Einträge)");
        PrintFields(fields);
    }

    static void CollectJson(JsonElement value, string path, SortedDictionary<string, FieldInfo> fields, HashSet<string> seen)
    {
        if (value.ValueKind == JsonValueKind.Object)
        {
            foreach (var property in value.EnumerateObject())
            {
                var childPath = path.Length == 0 ? property.Name : path + "." + property.Name;
                Note(fields, seen, childPath, JsonTypeName(property.Value));
                CollectJson(property.Value, childPath, fields, seen);
            }
        }
        else if (value.ValueKind == JsonValueKind.Array)
        {
            var itemPath = path + "[]";
            foreach (var item in value.EnumerateArray())
            {
                Note(fields, seen, itemPath, JsonTypeName(item));
                CollectJson(item, itemPath, fields, seen);
            }
        }
    }

    static string JsonTypeName(JsonElement value) => value.ValueKind switch
    {
        JsonValueKind.True or JsonValueKind.False => "Boolean",
        _ => value.ValueKind.ToString(),
    };

    static void Note(SortedDictionary<string, FieldInfo> fields, HashSet<string> seen, string path, string type)
    {
        if (!fields.TryGetValue(path, out var info))
            fields[path] = info = new FieldInfo();
        info.Types.Add(type);
        seen.Add(path);
    }

    static void PrintFields(SortedDictionary<string, FieldInfo> fields)
    {
        if (fields.Count == 0)
            return;
        int nameWidth = Math.Max("Feld".Length, fields.Keys.Max(k => k.Length));
        var types = fields.ToDictionary(f => f.Key, f => string.Join("|", f.Value.Types));
        int typeWidth = Math.Max("Typ".Length, types.Values.Max(t => t.Length));
        Console.WriteLine($"    {"Feld".PadRight(nameWidth)}  {"Typ".PadRight(typeWidth)}  in Dokumenten");
        foreach (var (path, info) in fields)
            Console.WriteLine($"    {path.PadRight(nameWidth)}  {types[path].PadRight(typeWidth)}  {info.Documents}");
    }

    // ---- Export ----

    static void ExportCollection(string fileName, string collectionName, IEnumerable<BsonDocument> documents, string targetDir)
    {
        var dir = Path.Combine(targetDir, Path.GetFileNameWithoutExtension(fileName));
        Directory.CreateDirectory(dir);
        var target = Path.Combine(dir, collectionName + ".json");

        int count = 0;
        using (var stream = new FileStream(target, FileMode.Create, FileAccess.Write))
        using (var writer = new Utf8JsonWriter(stream, new JsonWriterOptions
        {
            Indented = true,
            Encoder = JavaScriptEncoder.UnsafeRelaxedJsonEscaping,
        }))
        {
            writer.WriteStartArray();
            foreach (var document in documents)
            {
                WriteBson(writer, document);
                count++;
            }
            writer.WriteEndArray();
        }
        Console.WriteLine($"{fileName}/{collectionName}: {count} Dokumente nach {target}");
    }

    // BSON nach JSON: Datum als ISO-8601 in UTC, ObjectId und Guid als Text, Binärdaten als Base64.
    // Werte ohne JSON-Entsprechung (MinValue, MaxValue, NaN, Unendlich) als Text.
    static void WriteBson(Utf8JsonWriter writer, BsonValue value)
    {
        switch (value.Type)
        {
            case BsonType.Null:
                writer.WriteNullValue();
                break;
            case BsonType.MinValue:
                writer.WriteStringValue("$minValue");
                break;
            case BsonType.MaxValue:
                writer.WriteStringValue("$maxValue");
                break;
            case BsonType.Int32:
                writer.WriteNumberValue(value.AsInt32);
                break;
            case BsonType.Int64:
                writer.WriteNumberValue(value.AsInt64);
                break;
            case BsonType.Double:
                var number = value.AsDouble;
                if (double.IsFinite(number))
                    writer.WriteNumberValue(number);
                else
                    writer.WriteStringValue(number.ToString(CultureInfo.InvariantCulture));
                break;
            case BsonType.Decimal:
                writer.WriteNumberValue(value.AsDecimal);
                break;
            case BsonType.String:
                writer.WriteStringValue(value.AsString);
                break;
            case BsonType.Document:
                writer.WriteStartObject();
                foreach (var (key, child) in value.AsDocument)
                {
                    writer.WritePropertyName(key);
                    WriteBson(writer, child);
                }
                writer.WriteEndObject();
                break;
            case BsonType.Array:
                writer.WriteStartArray();
                foreach (var item in value.AsArray)
                    WriteBson(writer, item);
                writer.WriteEndArray();
                break;
            case BsonType.Binary:
                writer.WriteBase64StringValue(value.AsBinary);
                break;
            case BsonType.ObjectId:
                writer.WriteStringValue(value.AsObjectId.ToString());
                break;
            case BsonType.Guid:
                writer.WriteStringValue(value.AsGuid.ToString("D"));
                break;
            case BsonType.Boolean:
                writer.WriteBooleanValue(value.AsBoolean);
                break;
            case BsonType.DateTime:
                // LiteDB liefert Lokalzeit; für den Export einheitlich UTC mit «Z»
                var date = value.AsDateTime;
                if (date.Kind == DateTimeKind.Local)
                    date = date.ToUniversalTime();
                writer.WriteStringValue(date.ToString("o", CultureInfo.InvariantCulture));
                break;
            default:
                throw new InvalidOperationException($"BSON-Typ {value.Type} nicht unterstützt");
        }
    }

    static string Hex(byte[] bytes) =>
        string.Join(" ", bytes.Select(b => b.ToString("x2", CultureInfo.InvariantCulture)));
}
