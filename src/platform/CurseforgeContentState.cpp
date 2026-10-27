#include "CurseforgeContentState.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <algorithm>
namespace studio {
namespace {
// Qt validates the JSON; this bounded token walk locates booleans so unrelated bytes stay exact.
class BooleanSpans {
  public:
    explicit BooleanSpans(const QByteArray &bytes) : data(bytes) {
        if (data.startsWith("\xEF\xBB\xBF"))
            pos = 3;
        value({}, 0);
    }
    QHash<QString, QPair<qsizetype, qsizetype>> spans;
    bool valid = true;

  private:
    const QByteArray &data;
    qsizetype pos = 0, nodes = 0;
    QSet<QString> lockFields;
    static bool whitespace(char c) {
        return c == ' ' || c == '\r' || c == '\n' || c == '\t';
    }
    static bool lockField(const QString &path) {
        return path.startsWith("/installedAddons/") && path.endsWith("/isLocked") && path.count('/') == 3;
    }
    void whitespace() {
        while (pos < data.size() && whitespace(data[pos]))
            ++pos;
    }
    QByteArrayView string() {
        const auto start = pos++;
        while (pos < data.size()) {
            const auto c = data[pos++];
            if (c == '\\')
                ++pos;
            else if (c == '"')
                break;
        }
        return QByteArrayView(data).sliced(start, pos - start);
    }
    void value(const QString &path, int depth) {
        whitespace();
        if (!valid || depth > 64 || ++nodes > 1000000 || pos >= data.size()) {
            valid = false;
            return;
        }
        const auto start = pos;
        if (data[pos] == '{') {
            ++pos;
            whitespace();
            while (valid && pos < data.size() && data[pos] != '}') {
                const auto token = string();
                const auto keyBytes = token.sliced(1, token.size() - 2);
                auto key = token.contains('\\')
                               ? QJsonDocument::fromJson('[' + QByteArray(token.data(), token.size()) + ']')
                                     .array()
                                     .first()
                                     .toString()
                               : QString::fromUtf8(keyBytes.data(), keyBytes.size());
                key.replace('~', "~0").replace('/', "~1");
                whitespace();
                ++pos;
                const auto field = path + '/' + key;
                if (field == "/installedAddons" || lockField(field)) {
                    if (lockFields.contains(field)) {
                        valid = false;
                        return;
                    }
                    lockFields.insert(field);
                }
                value(field, depth + 1);
                whitespace();
                if (pos < data.size() && data[pos] == ',') {
                    ++pos;
                    whitespace();
                } else
                    break;
            }
            ++pos;
        } else if (data[pos] == '[') {
            ++pos;
            whitespace();
            int index = 0;
            while (valid && pos < data.size() && data[pos] != ']') {
                value(path + '/' + QString::number(index++), depth + 1);
                whitespace();
                if (pos < data.size() && data[pos] == ',') {
                    ++pos;
                    whitespace();
                } else
                    break;
            }
            ++pos;
        } else if (data[pos] == '"')
            string();
        else {
            while (pos < data.size() && data[pos] != ',' && data[pos] != ']' && data[pos] != '}' &&
                   !whitespace(data[pos]))
                ++pos;
            const auto token = QByteArrayView(data).sliced(start, pos - start);
            if (lockField(path) && (token == "true" || token == "false")) {
                if (spans.contains(path))
                    valid = false;
                spans.insert(path, {start, pos - start});
            }
        }
    }
};
QString relativePath(const QJsonObject &addon, const QDir &root) {
    const auto name = addon.value("fileNameOnDisk").toString();
    if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains(':') ||
        name.contains(".."))
        return {};
    const auto folder = addon.value("modFolderPath").toString();
    auto relative = root.relativeFilePath(QDir::fromNativeSeparators(folder) + '/' + name);
    const auto category = relative.section('/', 0, 0);
    if (relative.count('/') != 1 ||
        !QStringList{"mods", "resourcepacks", "shaderpacks", "datapacks"}.contains(category))
        return {};
    return relative;
}
QJsonArray addons(const QByteArray &bytes, QString *error) {
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject() ||
        !doc.object().value("installedAddons").isArray()) {
        if (error)
            *error = "Could not read CurseForge content locks.";
        return {};
    }
    return doc.object().value("installedAddons").toArray();
}
QString sha1(const QJsonObject &addon) {
    for (const auto &value : addon.value("installedFile").toObject().value("hashes").toArray()) {
        const auto hash = value.toObject();
        if (hash.value("type").toInt(hash.value("algo").toInt()) == 1)
            return hash.value("value").toString().toLower();
    }
    return {};
}
} // namespace
QVariantList CurseforgeContentState::read(const QByteArray &bytes, const QVariantMap &pack, QString *error) {
    if (error)
        error->clear();
    QVariantList rows;
    const QDir root(pack.value("path").toString());
    for (const auto &value : addons(bytes, error)) {
        const auto addon = value.toObject();
        auto relative = relativePath(addon, root);
        const auto id = QString::number(addon.value("addonID").toInteger());
        if (relative.isEmpty() || id == "0" || !addon.value("isLocked").isBool())
            continue;
        const bool enabled = addon.value("isEnabled").toBool(true);
        if (!enabled && !relative.endsWith(".disabled") && !QFileInfo::exists(root.filePath(relative)))
            relative += ".disabled";
        rows << QVariantMap{
            {"relativePath", relative},
            {"category", relative.section('/', 0, 0)},
            {"sha1", sha1(addon)},
            {"enabled", enabled},
            {"provider", "curseforge"},
            {"projectId", id},
            {"sourceIds", QVariantMap{{"curseforge", id}}},
            {"versionId", QString::number(addon.value("installedFile").toObject().value("id").toInteger())},
            {"launcherLocked", addon.value("isLocked").toBool()},
            {"missing", !QFileInfo::exists(root.filePath(relative))}};
    }
    return rows;
}
QByteArray CurseforgeContentState::setLocked(const QByteArray &bytes, const QVariantMap &pack,
                                             const QVariantList &targets, bool locked, QString *error) {
    QString localError;
    if (!error)
        error = &localError;
    error->clear();
    const auto entries = addons(bytes, error);
    if (error && !error->isEmpty())
        return {};
    BooleanSpans tokens(bytes);
    if (!tokens.valid) {
        *error = "CurseForge lock metadata is ambiguous or too complex. No changes were made.";
        return {};
    }
    const QDir root(pack.value("path").toString());
    QHash<QString, QVector<int>> byPath, byIdentity;
    byPath.reserve(entries.size());
    byIdentity.reserve(entries.size());
    for (int i = 0; i < entries.size(); ++i) {
        const auto addon = entries[i].toObject();
        const auto path = relativePath(addon, root);
        const auto id = addon.value("addonID").toInteger();
        if (path.isEmpty() || id <= 0)
            continue;
        byPath[path].append(i);
        byIdentity[path + QChar(0x1f) + QString::number(id)].append(i);
    }
    QList<QPair<qsizetype, qsizetype>> replacements;
    QSet<qsizetype> selectedOffsets;
    replacements.reserve(targets.size());
    for (const auto &value : targets) {
        const auto target = value.toMap();
        auto relative = target.value("relativePath").toString();
        if (relative.endsWith(".disabled"))
            relative.chop(9);
        const auto id = target.value("sourceIds").toMap().value("curseforge").toString();
        const auto matches =
            id.isEmpty() ? byPath.value(relative) : byIdentity.value(relative + QChar(0x1f) + id);
        if (matches.size() != 1) {
            *error = "The content is not uniquely tracked by CurseForge. Refresh the source launcher first.";
            return {};
        }
        const auto index = matches.first();
        const auto addon = entries[index].toObject();
        const auto hash = sha1(addon);
        const auto field = "/installedAddons/" + QString::number(index) + "/isLocked";
        if (!addon.value("isLocked").isBool() || !tokens.spans.contains(field) ||
            (locked && !hash.isEmpty() && hash != target.value("sha1").toString().toLower())) {
            *error = "CurseForge content changed. Refresh before changing its lock.";
            return {};
        }
        const auto span = tokens.spans.value(field);
        if (!selectedOffsets.contains(span.first)) {
            selectedOffsets.insert(span.first);
            replacements.append(span);
        }
    }
    std::sort(replacements.begin(), replacements.end(), [](auto a, auto b) { return a.first < b.first; });
    QByteArray result;
    result.reserve(bytes.size() + replacements.size());
    qsizetype cursor = 0;
    for (const auto &span : replacements) {
        result.append(bytes.constData() + cursor, span.first - cursor);
        result.append(locked ? "true" : "false");
        cursor = span.first + span.second;
    }
    result.append(bytes.constData() + cursor, bytes.size() - cursor);
    return result;
}
} // namespace studio
