#include "ContentDependencyGraph.h"
#include "ContentOperationState.h"
#include "domain/Catalogue.h"
#include <QQueue>
namespace studio {
namespace {
bool verified(const QVariantMap &row) {
    const auto pin = row.value("installedRelease").toMap();
    const auto hash = row.value("sha1").toString();
    return !hash.isEmpty() && pin.value("sha1").toString().compare(hash, Qt::CaseInsensitive) == 0;
}
} // namespace
ContentDependencyGraph::ContentDependencyGraph(const QVariantList &rows)
    : m_rows(rows), m_children(rows.size()), m_parents(rows.size()) {
    QHash<QString, QList<int>> identities;
    for (int i = 0; i < rows.size(); ++i) {
        const auto row = rows[i].toMap();
        m_keys[contentRowKey(row)] << i;
        if (row.value("directory").toBool())
            continue;
        const auto ids = row.value("sourceIds").toMap();
        for (auto it = ids.cbegin(); it != ids.cend(); ++it)
            if (!it.value().toString().isEmpty())
                identities["project:" + it.key() + ':' + it.value().toString()] << i;
        if (verified(row)) {
            const auto pin = row.value("installedRelease").toMap();
            if (!pin.value("provider").toString().isEmpty() && !pin.value("id").toString().isEmpty())
                identities["version:" + pin.value("provider").toString() + ':' + pin.value("id").toString()]
                    << i;
        }
        if (row.value("category") == "mods") {
            auto names = row.value("modIds").toStringList();
            names << row.value("modId").toString() << row.value("provides").toStringList();
            names.removeDuplicates();
            for (const auto &name : names)
                if (!name.isEmpty())
                    identities["runtime:" + name] << i;
        }
    }
    for (auto it = identities.cbegin(); it != identities.cend(); ++it)
        if (it.value().size() > 1)
            for (const int index : it.value())
                m_ambiguous.insert(index);
    for (int i = 0; i < rows.size(); ++i) {
        const auto row = rows[i].toMap();
        if (row.value("directory").toBool())
            continue;
        QHash<int, bool> edges;
        const auto add = [&](const QString &identity, bool required) {
            const auto matches = identities.value(identity);
            if (matches.size() > 1)
                return;
            for (const int target : matches)
                if (target != i)
                    edges[target] = edges.value(target) || required;
        };
        if (verified(row)) {
            const auto release = releaseFromVariant(row.value("installedRelease").toMap());
            QSet<QString> missing;
            const auto addProvider = [&](const QString &project, const QString &version, bool required) {
                if (release.provider != "modrinth" && release.provider != "curseforge")
                    return;
                const auto identity = !project.isEmpty() ? "project:" + release.provider + ':' + project
                                                         : "version:" + release.provider + ':' + version;
                if (project.isEmpty() && version.isEmpty())
                    return;
                add(identity, required);
                if (!required || !row.value("enabled").toBool() || m_ambiguous.contains(i) ||
                    !identities.value(identity).isEmpty() || missing.contains(identity) ||
                    (!version.isEmpty() &&
                     !identities.value("version:" + release.provider + ':' + version).isEmpty()))
                    return;
                missing.insert(identity);
                m_missing << QVariantMap{{"parent", row},
                                         {"reference", QVariantMap{{"provider", release.provider},
                                                                   {"projectId", project},
                                                                   {"versionId", version}}}};
            };
            for (const auto &value : release.dependencyRefs) {
                const auto ref = value.toObject();
                const auto kind = ref.value("dependency_type").toString();
                const int relation = ref.value("relationType").toInt();
                const bool required = release.provider == "modrinth" ? kind == "required" : relation == 3;
                const bool optional = release.provider == "modrinth" ? kind == "optional" : relation == 2;
                if (!required && !optional)
                    continue;
                const auto project = release.provider == "modrinth" ? ref.value("project_id").toString()
                                     : ref.value("modId").toInteger() > 0
                                         ? QString::number(ref.value("modId").toInteger())
                                         : QString{};
                addProvider(project,
                            release.provider == "modrinth" ? ref.value("version_id").toString() : QString{},
                            required);
            }
            if (release.dependencyRefs.isEmpty())
                for (const auto &project : release.requiredDependencies)
                    addProvider(project, {}, true);
        }
        if (row.value("category") == "mods")
            for (const auto &value : row.value("runtimeDependencies").toList()) {
                const auto ref = value.toMap();
                add("runtime:" + ref.value("id").toString(), ref.value("required", true).toBool());
            }
        for (auto it = edges.cbegin(); it != edges.cend(); ++it) {
            m_children[i] << Edge{it.key(), it.value()};
            m_parents[it.key()] << Edge{i, it.value()};
        }
    }
}
QVariantMap ContentDependencyGraph::relations(int position) const {
    QStringList required, optional;
    if (position >= 0 && position < m_parents.size())
        for (const auto &edge : m_parents[position]) {
            const auto row = m_rows[edge.node].toMap();
            auto &names = edge.required ? required : optional;
            const auto name = row.value("title", row.value("fileName")).toString();
            if (!name.isEmpty() && !names.contains(name))
                names << name;
        }
    required.sort(Qt::CaseInsensitive);
    optional.sort(Qt::CaseInsensitive);
    QStringList hint;
    if (!required.isEmpty())
        hint << "Required by " + required.join(", ");
    if (!optional.isEmpty())
        hint << "Used by " + optional.join(", ");
    return {{"requiredBy", required},
            {"optionalFor", optional},
            {"isDependency", !hint.isEmpty()},
            {"dependencyHint", hint.join('\n')}};
}
QVariantList ContentDependencyGraph::disabledRequirements() const {
    QVariantList result;
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto row = m_rows[i].toMap();
        if (row.value("enabled").toBool() || row.value("directory").toBool() || m_ambiguous.contains(i))
            continue;
        QStringList names;
        for (const auto &edge : m_parents[i]) {
            const auto parent = m_rows[edge.node].toMap();
            if (edge.required && parent.value("enabled").toBool() && !m_ambiguous.contains(edge.node))
                names << parent.value("title", parent.value("fileName")).toString();
        }
        names.removeAll(QString{});
        names.removeDuplicates();
        names.sort(Qt::CaseInsensitive);
        if (!names.isEmpty())
            result << QVariantMap{{"dependency", row}, {"requiredBy", names}};
    }
    return result;
}
QStringList ContentDependencyGraph::descendants(const QStringList &roots) const {
    QSet<int> selected, visited;
    QQueue<int> pending;
    for (const auto &key : roots)
        for (const int position : m_keys.value(key)) {
            selected.insert(position);
            pending.enqueue(position);
        }
    visited = selected;
    while (!pending.isEmpty())
        for (const auto &edge : m_children[pending.dequeue()])
            if (!visited.contains(edge.node)) {
                visited.insert(edge.node);
                pending.enqueue(edge.node);
            }
    QStringList result;
    QSet<QString> emitted;
    for (int i = 0; i < m_rows.size(); ++i)
        if (visited.contains(i) && !selected.contains(i)) {
            const auto key = contentRowKey(m_rows[i].toMap());
            if (!emitted.contains(key)) {
                emitted.insert(key);
                result << key;
            }
        }
    return result;
}
QStringList ContentDependencyGraph::unused(const QStringList &candidates) const {
    QSet<int> selected, blocked;
    QQueue<int> pending;
    for (const auto &key : candidates)
        for (const int i : m_keys.value(key))
            selected.insert(i);
    for (const int i : selected) {
        const auto row = m_rows[i].toMap();
        bool keep = m_ambiguous.contains(i) || row.value("locked").toBool() ||
                    row.value("pending").toBool() || row.value("directory").toBool() ||
                    row.value("sha1").toString().isEmpty();
        for (const auto &edge : m_parents[i])
            keep |= !selected.contains(edge.node);
        if (keep) {
            blocked.insert(i);
            pending.enqueue(i);
        }
    }
    while (!pending.isEmpty())
        for (const auto &edge : m_children[pending.dequeue()])
            if (selected.contains(edge.node) && !blocked.contains(edge.node)) {
                blocked.insert(edge.node);
                pending.enqueue(edge.node);
            }
    QStringList result;
    QSet<QString> emitted;
    for (int i = 0; i < m_rows.size(); ++i)
        if (selected.contains(i) && !blocked.contains(i)) {
            const auto key = contentRowKey(m_rows[i].toMap());
            bool ambiguous = false;
            for (const int other : m_keys.value(key))
                ambiguous |= blocked.contains(other);
            if (!ambiguous && !emitted.contains(key)) {
                emitted.insert(key);
                result << key;
            }
        }
    return result;
}
bool ContentDependencyGraph::individuallyUnused(const QString &key) const {
    const auto positions = m_keys.constFind(key);
    if (positions == m_keys.cend())
        return false;
    for (const int position : *positions) {
        const auto row = m_rows[position].toMap();
        if (m_ambiguous.contains(position) || !m_parents[position].isEmpty() ||
            row.value("locked").toBool() || row.value("pending").toBool() ||
            row.value("directory").toBool() || row.value("sha1").toString().isEmpty())
            return false;
    }
    return true;
}
} // namespace studio
