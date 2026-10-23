#include "SourceLockIndex.h"
#include <QDir>
namespace studio {
QString SourceLockIndex::key(const QString &path) {
    auto result = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}
void SourceLockIndex::remember(const QVariantMap &pack) {
    const auto path = pack.value("path").toString();
    // A raw workspace context is not an authoritative empty source snapshot.
    if (path.isEmpty() || pack.value("sourceContent").metaType().id() != QMetaType::QVariantList)
        return;
    const auto packKey = key(path);
    const auto rows = pack.value("sourceContent").toList();
    const auto previous = m_packs.constFind(packKey);
    if (previous != m_packs.cend() && previous->rows == rows)
        return;
    Snapshot snapshot;
    snapshot.rows = rows;
    for (int i = 0; i < rows.size(); ++i) {
        const auto row = rows[i].toMap();
        const auto provider = row.value("provider", "modrinth").toString();
        const auto project = row.value("projectId").toString();
        if (!row.value("launcherLocked").toBool() || provider.isEmpty() || project.isEmpty())
            continue;
        const auto identity = provider + ':' + project;
        if (!snapshot.projects.contains(identity))
            snapshot.projects.insert(identity, {i, row});
    }
    m_packs.insert(packKey, std::move(snapshot));
}
bool SourceLockIndex::contains(const QString &path) const {
    return !path.isEmpty() && m_packs.contains(key(path));
}
QVariantList SourceLockIndex::sourceRows(const QString &path) const {
    const auto snapshot = m_packs.constFind(key(path));
    return snapshot == m_packs.cend() ? QVariantList{} : snapshot->rows;
}
QVariantMap SourceLockIndex::find(const QString &path, const QVariantMap &ids) const {
    const auto snapshot = m_packs.constFind(key(path));
    if (path.isEmpty() || snapshot == m_packs.cend())
        return {};
    const Lock *first = nullptr;
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
        if (it.value().toString().isEmpty())
            continue;
        const auto found = snapshot->projects.constFind(it.key() + ':' + it.value().toString());
        if (found != snapshot->projects.cend() && (!first || found->order < first->order))
            first = &found.value();
    }
    return first ? first->row : QVariantMap{};
}
} // namespace studio
