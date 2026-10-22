#include "ContentLockRegistry.h"
#include <QDir>
#include <QRegularExpression>
namespace studio {
QString ContentLockRegistry::packKey(const QString &path) {
    auto key = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    key = key.toCaseFolded();
#endif
    return key;
}
QString ContentLockRegistry::pathKey(QString path) {
    if (path.endsWith(".disabled", Qt::CaseInsensitive))
        path.chop(9);
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    return path;
}
void ContentLockRegistry::setRows(const QString &pack, const QVariantList &rows) {
    QSet<QString> paths, projects;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        if (row.value("locked").toBool()) {
            paths.insert(pathKey(row.value("relativePath").toString()));
            const auto ids = row.value("sourceIds").toMap();
            for (auto it = ids.begin(); it != ids.end(); ++it)
                if (!it.value().toString().isEmpty())
                    projects.insert(it.key() + ':' + it.value().toString());
            const auto hash = row.value("sha1").toString();
            static const QRegularExpression sha1("^[a-fA-F0-9]{40}$");
            if (row.value("pending").toBool() && row.value("category") == "mods" &&
                sha1.match(hash).hasMatch())
                paths.insert(pathKey("mods/" + hash + ".jar"));
        }
    }
    QWriteLocker guard(&m_mutex);
    m_paths[packKey(pack)] = std::move(paths);
    m_projects[packKey(pack)] = std::move(projects);
}
bool ContentLockRegistry::locked(const QString &pack, const QString &relative) const {
    if (relative.isEmpty())
        return false;
    QReadLocker guard(&m_mutex);
    return m_paths.value(packKey(pack)).contains(pathKey(relative));
}
bool ContentLockRegistry::projectLocked(const QString &pack, const QString &provider,
                                        const QString &id) const {
    if (provider.isEmpty() || id.isEmpty())
        return false;
    QReadLocker guard(&m_mutex);
    return m_projects.value(packKey(pack)).contains(provider + ':' + id);
}
} // namespace studio
