#include "BulkContentInput.h"
#include "domain/TextSearch.h"
#include <QRegularExpression>
#include <QUrl>
namespace studio {
namespace {
QString slug(const Project &project) {
    const auto parts = QUrl(project.url).path().split('/', Qt::SkipEmptyParts);
    return parts.value(parts.size() - 1);
}
QString plain(QString text) {
    return text.trimmed().normalized(QString::NormalizationForm_KC).toCaseFolded();
}
BulkContentReference reference(QString raw, int line, const QString &type) {
    static const QRegularExpression bullet("^(?:[-*+>\\x{2022}]\\s+|[0-9]+[.)]\\s+)");
    raw = raw.trimmed();
    raw.remove(bullet);
    BulkContentReference result{raw, raw, {}, type, {}, {}, line};
    if (raw.size() > 2048) {
        result.error = "This entry is too long.";
        return result;
    }
    auto urlText = raw;
    if (raw.startsWith("modrinth.com/") || raw.startsWith("www.modrinth.com/") ||
        raw.startsWith("curseforge.com/") || raw.startsWith("www.curseforge.com/"))
        urlText.prepend("https://");
    if (urlText.contains("://")) {
        const QUrl url(urlText, QUrl::StrictMode);
        const auto host = url.host().toLower();
        auto parts = url.path().split('/', Qt::SkipEmptyParts);
        QMap<QString, QString> types;
        if (host == "modrinth.com" || host == "www.modrinth.com") {
            result.provider = "modrinth";
            types = {{"mod", "mod"},
                     {"resourcepack", "resourcepack"},
                     {"datapack", "datapack"},
                     {"shader", "shader"}};
        } else if (host == "curseforge.com" || host == "www.curseforge.com") {
            result.provider = "curseforge";
            if (!parts.isEmpty() && parts.first() == "minecraft")
                parts.removeFirst();
            else
                parts.clear();
            types = {{"mc-mods", "mod"},
                     {"texture-packs", "resourcepack"},
                     {"data-packs", "datapack"},
                     {"shaders", "shader"}};
        }
        if (!url.isValid() || (url.scheme() != "https" && url.scheme() != "http") ||
            !url.userInfo().isEmpty() || url.port() != -1 || parts.size() < 2 ||
            !types.contains(parts.value(0)) || parts.value(1).isEmpty()) {
            result.error = "Use a Modrinth or CurseForge content link.";
            return result;
        }
        result.type = types.value(parts[0]);
        result.slug = result.query = parts[1];
        return result;
    }
    if (raw.endsWith(".jar", Qt::CaseInsensitive) || raw.endsWith(".zip", Qt::CaseInsensitive)) {
        result.filename = true;
        auto name = raw.section(QRegularExpression("[/\\\\]"), -1);
        name.chop(4);
        const auto parts = name.split(QRegularExpression("[-_+ ]+"), Qt::SkipEmptyParts);
        static const QRegularExpression version(
            "^(?:(?:mc|v)?[0-9]+\\.[0-9]|(?:fabric|forge|neoforge|quilt|universal)$)",
            QRegularExpression::CaseInsensitiveOption);
        QStringList words;
        for (const auto &part : parts) {
            if (!words.isEmpty() && version.match(part).hasMatch())
                break;
            words << part;
        }
        result.query = words.join(' ');
    }
    if (normalizedSearch(result.query).isEmpty())
        result.error = "Enter a content name or link.";
    return result;
}
} // namespace
BulkContentInput parseBulkContentInput(const QString &text, const QString &defaultType) {
    BulkContentInput result;
    if (text.size() > 65536) {
        result.error = "Keep the list under 64,000 characters.";
        return result;
    }
    int line = 0;
    for (const auto &sourceLine : text.split('\n')) {
        ++line;
        const auto trimmed = sourceLine.trimmed();
        const bool singleUrl = (trimmed.startsWith("https://") || trimmed.startsWith("http://")) &&
                               !trimmed.contains(QRegularExpression("\\s"));
        const auto entries = singleUrl ? QStringList{trimmed}
                                       : sourceLine.split(QRegularExpression("[,;]"), Qt::SkipEmptyParts);
        for (auto raw : entries) {
            raw = raw.trimmed();
            if (raw.isEmpty())
                continue;
            if (result.references.size() == 500) {
                result.references.clear();
                result.error = "Add up to 500 entries at a time.";
                return result;
            }
            result.references << reference(raw, line, defaultType);
        }
    }
    return result;
}
int bulkContentMatchRank(const BulkContentReference &wanted, const Project &project) {
    if (!wanted.error.isEmpty() || project.id.isEmpty() || project.type != wanted.type ||
        (project.provider != "modrinth" && project.provider != "curseforge"))
        return -1;
    const auto projectSlug = slug(project);
    if (!wanted.provider.isEmpty()) {
        const auto sourceUrl = project.sourceUrls.value(
            wanted.provider, project.provider == wanted.provider ? project.url : QString{});
        const auto sourceParts = QUrl(sourceUrl).path().split('/', Qt::SkipEmptyParts);
        const auto sourceSlug = sourceParts.value(sourceParts.size() - 1);
        const auto sourceId = project.sourceIds.value(
            wanted.provider, project.provider == wanted.provider ? project.id : QString{});
        return sourceSlug == wanted.slug ||
                       (wanted.provider == "modrinth" && !sourceId.isEmpty() && sourceId == wanted.slug)
                   ? 0
                   : -1;
    }
    const auto query = plain(wanted.query);
    if (plain(project.title) == query || plain(projectSlug) == query)
        return 0;
    if (normalizedSearch(project.title) == normalizedSearch(query) ||
        normalizedSearch(projectSlug) == normalizedSearch(query))
        return 1;
    const TextQuery matcher(wanted.query);
    const auto titleRank = matcher.rank(project.title), slugRank = matcher.rank(projectSlug);
    const auto rank = titleRank < 0 ? slugRank : slugRank < 0 ? titleRank : qMin(titleRank, slugRank);
    return rank < 0 ? -1 : rank + 2;
}
} // namespace studio
