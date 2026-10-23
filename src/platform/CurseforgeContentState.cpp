#include "CurseforgeContentState.h"
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
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
    void whitespace() {
        while (pos < data.size() && QByteArray(" \r\n\t").contains(data[pos]))
            ++pos;
    }
    QByteArray string() {
        const auto start = pos++;
        while (pos < data.size()) {
            const auto c = data[pos++];
            if (c == '\\')
                ++pos;
            else if (c == '"')
                break;
        }
        return data.mid(start, pos - start);
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
                auto key = QJsonDocument::fromJson('[' + token + ']').array().first().toString();
                key.replace('~', "~0").replace('/', "~1");
                whitespace();
                ++pos;
                value(path + '/' + key, depth + 1);
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
            while (pos < data.size() && !QByteArray(",]} \r\n\t").contains(data[pos]))
                ++pos;
            const auto token = data.mid(start, pos - start);
            if (path.startsWith("/installedAddons/") && path.endsWith("/isLocked") &&
                (token == "true" || token == "false")) {
                if (spans.contains(path))
                    valid = false;
                spans.insert(path, {start, pos - start});
            }
        }
    }
};
QString relativePath(const QJsonObject &addon, const QVariantMap &pack) {
    const auto name = addon.value("fileNameOnDisk").toString();
    if (name.isEmpty() || name.contains('/') || name.contains('\\') || name.contains(':') ||
        name.contains(".."))
        return {};
    const auto root = QDir(pack.value("path").toString()).absolutePath();
    const auto folder = addon.value("modFolderPath").toString();
    auto relative = QDir(root).relativeFilePath(QDir::fromNativeSeparators(folder) + '/' + name);
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
            *error = "Could not read CurseForge content locks. No changes were made.";
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
    QVariantList rows;
    for (const auto &value : addons(bytes, error)) {
        const auto addon = value.toObject();
        auto relative = relativePath(addon, pack);
        const auto id = QString::number(addon.value("addonID").toInteger());
        if (relative.isEmpty() || id == "0" || !addon.value("isLocked").isBool())
            continue;
        const bool enabled = addon.value("isEnabled").toBool(true);
        const QDir root(pack.value("path").toString());
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
    const auto entries = addons(bytes, error);
    if (error && !error->isEmpty())
        return {};
    BooleanSpans tokens(bytes);
    if (!tokens.valid) {
        *error = "CurseForge lock metadata is ambiguous or too complex. No changes were made.";
        return {};
    }
    QList<QPair<qsizetype, qsizetype>> replacements;
    for (const auto &value : targets) {
        const auto target = value.toMap();
        auto relative = target.value("relativePath").toString();
        if (relative.endsWith(".disabled"))
            relative.chop(9);
        const auto id = target.value("sourceIds").toMap().value("curseforge").toString();
        int matches = 0;
        for (int i = 0; i < entries.size(); ++i) {
            const auto addon = entries[i].toObject();
            if (relativePath(addon, pack) != relative ||
                (!id.isEmpty() && QString::number(addon.value("addonID").toInteger()) != id))
                continue;
            const auto hash = sha1(addon);
            const auto field = "/installedAddons/" + QString::number(i) + "/isLocked";
            if (!tokens.spans.contains(field) ||
                (locked && !hash.isEmpty() && hash != target.value("sha1").toString().toLower())) {
                *error = "CurseForge content changed. Refresh before changing its lock.";
                return {};
            }
            ++matches;
            replacements << tokens.spans.value(field);
        }
        if (matches != 1) {
            *error = "The content is not uniquely tracked by CurseForge. Refresh the source launcher first.";
            return {};
        }
    }
    std::sort(replacements.begin(), replacements.end(), [](auto a, auto b) { return a.first > b.first; });
    auto result = bytes;
    for (const auto &span : replacements)
        result.replace(span.first, span.second, locked ? "true" : "false");
    return result;
}
} // namespace studio
