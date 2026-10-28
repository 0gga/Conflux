#include "CommandPalette.h"
#include "domain/TextSearch.h"
#include <QPointer>
#include <QRegularExpression>
#include <algorithm>
namespace studio {
namespace {
bool relative(const QString &path) {
    if (path.isEmpty() || path.startsWith('/') || path.contains(':') || path.contains('\\'))
        return false;
    for (const auto &part : path.split('/'))
        if (part.isEmpty() || part == "." || part == "..")
            return false;
    return true;
}
QString parentFolder(const QString &path) {
    return path.contains('/') ? path.section('/', 0, -2) : QString{};
}
QString joined(QStringList values) {
    values.removeAll(QString{});
    return values.join(" · ");
}
} // namespace
CommandPalette::CommandPalette(QObject *parent) : QObject(parent), m_model(this) {
    m_rebuild.setSingleShot(true);
    m_rebuild.setInterval(0);
    connect(&m_rebuild, &QTimer::timeout, this, &CommandPalette::rebuild);
}
QVariantList CommandPalette::commands() {
    return {
        QVariantMap{{"key", "library"},
                    {"title", "Library"},
                    {"iconName", "library"},
                    {"page", "library"},
                    {"primary", true}},
        QVariantMap{{"key", "discover"},
                    {"title", "Explore content"},
                    {"iconName", "compass"},
                    {"page", "discover"},
                    {"primary", true}},
        QVariantMap{{"key", "content"},
                    {"title", "Instance content"},
                    {"iconName", "content"},
                    {"section", "content"}},
        QVariantMap{{"key", "configs"}, {"title", "Configs"}, {"iconName", "config"}, {"section", "configs"}},
        QVariantMap{{"key", "files"}, {"title", "Files"}, {"iconName", "folder"}, {"section", "files"}},
        QVariantMap{{"key", "logs"}, {"title", "Logs"}, {"iconName", "logs"}, {"section", "logs"}},
        QVariantMap{{"key", "worlds"}, {"title", "Worlds"}, {"iconName", "world"}, {"section", "worlds"}},
        QVariantMap{
            {"key", "settings"}, {"title", "Settings"}, {"iconName", "settings"}, {"page", "settings"}}};
}
QVariantList CommandPalette::primaryNavigation() const {
    QVariantList result;
    for (const auto &value : commands())
        if (value.toMap().value("primary").toBool())
            result << value;
    return result;
}
void CommandPalette::setLibraryPacks(const QVariantList &packs) {
    const bool changed = m_packs != packs;
    m_packs = packs; // Keep shared storage even after an equal-valued replacement.
    if (changed) {
        m_dirty = true;
        if (m_visible)
            m_rebuild.start();
    }
}
void CommandPalette::setContext(const QVariantMap &pack, const QVariantList &content,
                                const QVariantList &files) {
    const QVariantMap context{{"path", pack.value("path")},
                              {"name", pack.value("name", pack.value("title"))}};
    const bool changed = context != m_pack || content != m_content || files != m_files;
    m_pack = context;
    m_content = content;
    m_files = files;
    if (changed) {
        m_dirty = true;
        if (m_visible)
            m_rebuild.start();
    }
}
void CommandPalette::setFolderEntries(const QVariantList &files) {
    const bool changed = files != m_folderFiles;
    m_folderFiles = files;
    if (changed) {
        m_dirty = true;
        if (m_visible)
            m_rebuild.start();
    }
}
void CommandPalette::rebuild() {
    m_rebuild.stop();
    if (!m_dirty)
        return;
    m_dirty = false;
    m_entries.clear();
    m_positions.clear();
    const auto add = [&](QString key, QString title, QString detail, QString icon, QVariantMap route,
                         int kind) {
        if (m_entries.size() >= 20000 || key.isEmpty() || title.isEmpty() || m_positions.contains(key))
            return;
        title = title.left(256);
        detail = detail.left(512);
        Entry entry;
        entry.value = {
            {"key", key}, {"contentKey", key}, {"title", title}, {"subtitle", detail}, {"iconName", icon}};
        entry.route = std::move(route);
        entry.kind = kind;
        entry.normalizedTitle = normalizedSearch(title);
        entry.normalizedDetail = normalizedSearch(detail);
        static const QRegularExpression separator("[^\\p{L}\\p{N}\\p{M}]+");
        for (const auto &word : title.split(separator, Qt::SkipEmptyParts))
            entry.words << normalizedSearch(word);
        m_positions.insert(key, m_entries.size());
        m_entries << std::move(entry);
    };
    const auto packPath = m_pack.value("path").toString();
    for (const auto &value : commands()) {
        const auto command = value.toMap();
        if (command.contains("section") && packPath.isEmpty())
            continue;
        QVariantMap route{{"page", command.value("page", "workspace")}};
        if (command.contains("section"))
            route["section"] = command.value("section");
        if (!packPath.isEmpty() && (route.value("page") == "workspace" || route.value("page") == "discover"))
            route["packPath"] = packPath;
        add("command:" + command.value("key").toString(), command.value("title").toString(), "",
            command.value("iconName").toString(), route, 0);
    }
    for (const auto &value : m_packs) {
        if (m_entries.size() >= 20000)
            break;
        const auto pack = value.toMap();
        const auto path = pack.value("path").toString();
        if (path.isEmpty() || pack.value("hidden").toBool())
            continue;
        const auto title = pack.value("title", pack.value("name")).toString();
        const auto detail = joined({pack.value("gameVersion", pack.value("minecraft")).toString(),
                                    pack.value("loader").toString(), pack.value("provider").toString()});
        add("instance:" + path, title, detail, "content", {{"page", "workspace"}, {"packPath", path}}, 1);
    }
    if (!packPath.isEmpty()) {
        const auto addFile = [&](const QVariantMap &row, bool content) {
            const auto path = row.value("relativePath").toString();
            if (!relative(path))
                return;
            QVariantMap route{{"page", "workspace"}, {"packPath", packPath}};
            auto icon = row.value("iconName", "file").toString();
            const auto category = row.value("category").toString();
            int kind = 3;
            if (content && category != "configs" && !row.value("directory").toBool()) {
                route["page"] = "showcase";
                route["modPath"] = path;
                icon = "content";
                kind = 2;
            } else if (category == "configs" && row.value("editable").toBool()) {
                route["section"] = "configs";
                route["configPath"] = path;
                icon = "config";
            } else {
                route["section"] = "files";
                route["folder"] = row.value("directory").toBool() ? path : parentFolder(path);
                if (row.value("directory").toBool())
                    icon = "folder";
            }
            const auto title = row.value("title", row.value("fileName", path)).toString();
            const auto detail =
                joined({path, row.value("version").toString(), row.value("author").toString()});
            add("file:" + packPath + QChar(0x1f) + path, title, detail, icon, route, kind);
        };
        for (const auto &value : m_content) {
            if (m_entries.size() >= 20000)
                break;
            addFile(value.toMap(), true);
        }
        for (const auto &value : m_files) {
            if (m_entries.size() >= 20000)
                break;
            addFile(value.toMap(), false);
        }
        for (const auto &value : m_folderFiles) {
            if (m_entries.size() >= 20000)
                break;
            addFile(value.toMap(), false);
        }
    }
    filter();
}
void CommandPalette::filter() {
    const auto needle = normalizedSearch(m_query);
    struct Match {
        int position, rank;
    };
    QList<Match> matches;
    matches.reserve(m_entries.size());
    for (int i = 0; i < m_entries.size(); ++i) {
        const auto &entry = m_entries.at(i);
        int rank = -1;
        if (needle.isEmpty() || entry.normalizedTitle == needle)
            rank = 0;
        else if (entry.normalizedTitle.contains(needle)) {
            rank = entry.words.contains(needle) ? 1 : entry.normalizedTitle.startsWith(needle) ? 2 : 3;
            if (rank != 1)
                for (const auto &word : entry.words)
                    if (word.startsWith(needle)) {
                        rank = 2;
                        break;
                    }
        } else if (entry.normalizedDetail.contains(needle))
            rank = 4;
        if (rank >= 0)
            matches << Match{i, rank};
    }
    const auto count = qMin<qsizetype>(50, matches.size());
    std::partial_sort(
        matches.begin(), matches.begin() + count, matches.end(), [&](const Match &a, const Match &b) {
            if (a.rank != b.rank)
                return a.rank < b.rank;
            const auto &left = m_entries.at(a.position), &right = m_entries.at(b.position);
            if (left.kind != right.kind)
                return left.kind < right.kind;
            const int title = left.value.value("title").toString().compare(
                right.value.value("title").toString(), Qt::CaseInsensitive);
            return title ? title < 0
                         : left.value.value("key").toString() < right.value.value("key").toString();
        });
    QVariantList rows;
    rows.reserve(count);
    for (int i = 0; i < count; ++i)
        rows << m_entries.at(matches[i].position).value;
    const auto oldSelection = m_selectedKey;
    if (m_resetSelection || m_selectedKey.isEmpty())
        m_selectedKey = rows.isEmpty() ? QString{} : rows.first().toMap().value("key").toString();
    m_resetSelection = false;
    m_rows = rows;
    const QPointer<CommandPalette> guard(this);
    m_model.setRows(rows);
    if (guard && oldSelection != m_selectedKey)
        emit selectionChanged();
    if (guard)
        emit changed();
}
void CommandPalette::setQuery(const QString &query) {
    const auto text = query.left(256);
    if (text == m_query)
        return;
    m_query = text;
    m_resetSelection = true;
    const QPointer<CommandPalette> guard(this);
    emit queryChanged();
    if (!guard)
        return;
    if (m_dirty)
        rebuild();
    else
        filter();
}
void CommandPalette::open() {
    const QPointer<CommandPalette> guard(this);
    if (m_dirty)
        rebuild();
    if (!guard)
        return;
    m_visible = true;
    m_resetSelection = true;
    if (m_query.isEmpty())
        filter();
    else
        setQuery({});
}
bool CommandPalette::select(const QString &key) {
    if (!m_visible)
        return false;
    for (const auto &value : m_rows)
        if (value.toMap().value("key") == key) {
            if (m_selectedKey != key) {
                m_selectedKey = key;
                emit selectionChanged();
            }
            return true;
        }
    return false;
}
void CommandPalette::moveSelection(int delta) {
    const QPointer<CommandPalette> guard(this);
    if (m_dirty)
        rebuild();
    if (!guard || !m_visible || m_dirty || m_rows.isEmpty())
        return;
    int position = -1;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].toMap().value("key") == m_selectedKey) {
            position = i;
            break;
        }
    const auto target = qBound<qint64>(0, qint64(position) + delta, m_rows.size() - 1);
    select(m_rows[target].toMap().value("key").toString());
}
void CommandPalette::dismiss() {
    if (!m_visible)
        return;
    m_visible = false;
    m_rebuild.stop();
    emit changed();
}
bool CommandPalette::activate(const QString &key) {
    const QPointer<CommandPalette> guard(this);
    if (m_dirty)
        rebuild();
    if (!guard || m_dirty || !m_visible || !m_positions.contains(key))
        return false;
    bool listed = false;
    for (const auto &row : m_rows)
        listed |= row.toMap().value("key") == key;
    if (!listed)
        return false;
    const auto route = m_entries.at(m_positions.value(key)).route;
    const auto navigate = m_navigate;
    dismiss();
    return guard && navigate ? navigate(route) : false;
}
bool CommandPalette::navigate(const QString &command) {
    for (const auto &value : commands())
        if (value.toMap().value("key") == command) {
            const auto entry = value.toMap();
            QVariantMap route{{"page", entry.value("page", "workspace")}};
            if (entry.contains("section")) {
                if (m_pack.value("path").toString().isEmpty())
                    return false;
                route["section"] = entry.value("section");
            }
            if (!m_pack.value("path").toString().isEmpty() &&
                (route.value("page") == "workspace" || route.value("page") == "discover"))
                route["packPath"] = m_pack.value("path");
            const auto navigate = m_navigate;
            return navigate ? navigate(route) : false;
        }
    return false;
}
} // namespace studio
