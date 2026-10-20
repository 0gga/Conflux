#include "ContentActions.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QSet>
#include <QtConcurrent>
#include <algorithm>
namespace studio {
ContentActions::ContentActions(std::shared_ptr<const IContentFileStore> files,
                               std::shared_ptr<IContentCatalogue> catalogue,
                               std::shared_ptr<IContentInstaller> installer,
                               std::shared_ptr<IContentPreferences> preferences, QObject *parent)
    : QObject(parent), m_files(std::move(files)), m_catalogue(std::move(catalogue)),
      m_installer(std::move(installer)), m_preferences(std::move(preferences)) {
    clearFilters();
}
void ContentActions::setContext(const QVariantMap &pack, const QVariantList &files) {
    if (m_mutating)
        return;
    if (pack == m_pack && files == m_inventory && !m_dirty)
        return;
    const bool changedPack = pack.value("path") != m_pack.value("path");
    m_pack = pack;
    m_inventory = files;
    m_dirty = false;
    if (changedPack) {
        m_selected.clear();
        m_query.clear();
        m_category = "all";
        m_rows.clear();
        m_versions.clear();
        ++m_versionGeneration;
        clearFilters();
    }
    if (changedPack || m_rows.isEmpty()) {
        m_rows.clear();
        for (const auto &value : files) {
            auto row = value.toMap();
            if (row.value("category") == "configs")
                continue;
            if (!row.contains("enabled"))
                row["enabled"] = !row.value("relativePath").toString().endsWith(".disabled");
            m_rows << row;
        }
    }
    refresh(changedPack);
}
void ContentActions::refresh(bool checkUpdates) {
    if (m_mutating || m_pack.isEmpty())
        return;
    const int generation = ++m_generation;
    const auto pack = m_pack;
    const auto inventory = m_inventory;
    const auto files = m_files;
    const auto previous = m_rows;
    m_busy = true;
    m_notice = checkUpdates ? "Checking compatible updates…" : "Reading content changes…";
    emit changed();
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    const QPointer<ContentActions> guard(this);
    connect(
        watcher, &QFutureWatcher<QVariantList>::finished, this,
        [this, guard, watcher, generation, checkUpdates, previous] {
            const auto rows = watcher->result();
            watcher->deleteLater();
            if (generation != m_generation)
                return;
            auto result = rows;
            bool newFiles = false;
            const QStringList metadata{"sourceIds",        "external",      "provider",        "projectId",
                                       "installedRelease", "updateRelease", "updateAvailable", "projectUrl",
                                       "author",           "authorIcon",    "environment",     "openSource"};
            for (int i = 0; i < result.size(); ++i) {
                auto row = result.at(i).toMap();
                bool found = false;
                for (const auto &value : previous) {
                    const auto old = value.toMap();
                    if (!row.value("sha1").toString().isEmpty() && old.value("sha1") == row.value("sha1")) {
                        for (const auto &key : metadata)
                            if (old.contains(key))
                                row[key] = old.value(key);
                        found = true;
                        break;
                    }
                }
                if (!found && row.value("category") == "mods")
                    newFiles = true;
                result[i] = row;
            }
            const auto done = [guard, generation](QVariantList rows, QString error) {
                if (!guard || generation != guard->m_generation)
                    return;
                guard->m_rows = rows;
                guard->m_busy = false;
                guard->m_notice = error;
                guard->applyPreferences();
                QStringList retained;
                for (const auto &path : guard->m_selected)
                    if (!guard->find(path).isEmpty())
                        retained << path;
                guard->m_selected = retained;
                emit guard->changed();
            };
            m_rows = result;
            applyPreferences();
            emit changed();
            if (checkUpdates || newFiles)
                m_catalogue->refresh(result, m_pack.value("minecraft").toString(),
                                     m_pack.value("loader").toString(), done);
            else
                done(result, {});
        });
    watcher->setFuture(
        QtConcurrent::run([files, pack, inventory] { return files->snapshot(pack, inventory); }));
}
QString ContentActions::preferenceIdentity(const QVariantMap &row) const {
    const auto ids = row.value("sourceIds").toMap();
    if (ids.contains("modrinth"))
        return "modrinth:" + ids.value("modrinth").toString();
    if (ids.contains("curseforge"))
        return "curseforge:" + ids.value("curseforge").toString();
    return row.value("sha1").toString();
}
void ContentActions::applyPreferences() {
    for (int i = 0; i < m_rows.size(); ++i) {
        auto row = m_rows.at(i).toMap();
        const auto pack = m_pack.value("path").toString(), identity = preferenceIdentity(row),
                   hash = row.value("sha1").toString();
        bool frozen = false;
        if (m_preferences->hasFrozenPreference(pack, identity))
            frozen = m_preferences->frozen(pack, identity);
        else if (!hash.isEmpty() && m_preferences->frozen(pack, hash)) {
            frozen = true;
            m_preferences->setFrozen(pack, identity, true);
        }
        row["frozen"] = frozen;
        m_rows[i] = row;
    }
}
QVariantMap ContentActions::find(const QString &path) const {
    for (const auto &value : m_rows)
        if (value.toMap().value("relativePath") == path)
            return value.toMap();
    return {};
}
QStringList ContentActions::authors() const {
    QStringList names;
    for (const auto &value : m_rows) {
        const auto name = value.toMap().value("author").toString();
        if (!name.isEmpty() && !names.contains(name))
            names << name;
    }
    names.sort(Qt::CaseInsensitive);
    return names;
}
QVariantList ContentActions::versionGroups() const {
    QVector<Release> releases;
    for (const auto &value : m_versions)
        releases << releaseFromVariant(value.toMap());
    return groupReleaseVariants(releases);
}
QVariantList ContentActions::rows() const {
    QVariantList result;
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        if (m_category != "all" && row.value("category") != m_category)
            continue;
        if (!(row.value("title").toString() + ' ' + row.value("fileName").toString() + ' ' +
              row.value("author").toString())
                 .contains(m_query, Qt::CaseInsensitive))
            continue;
        if (!m_filters.value("author").toString().isEmpty() &&
            row.value("author") != m_filters.value("author"))
            continue;
        if (!m_filters.value("environment").toString().isEmpty() &&
            row.value("environment", "unknown") != m_filters.value("environment"))
            continue;
        const auto state = m_filters.value("state").toString();
        if ((state == "enabled" && !row.value("enabled").toBool()) ||
            (state == "disabled" && row.value("enabled").toBool()))
            continue;
        if (m_filters.value("updates").toBool() && !row.value("updateAvailable").toBool())
            continue;
        if (m_filters.value("openSource").toBool() && !row.value("openSource").toBool())
            continue;
        if (m_filters.value("external").toBool() && !row.value("external").toBool())
            continue;
        result << row;
    }
    std::stable_sort(result.begin(), result.end(), [this](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap(), right = b.toMap();
        if (m_sort == "updates" && left.value("updateAvailable") != right.value("updateAvailable"))
            return left.value("updateAvailable").toBool();
        const auto key = m_sort == "author" ? "author" : m_sort == "filename" ? "fileName" : "title";
        const int order =
            QString::compare(left.value(key).toString(), right.value(key).toString(), Qt::CaseInsensitive);
        return m_sort == "name-desc" ? order > 0 : order < 0;
    });
    return result;
}
int ContentActions::updateCount() const {
    int count = 0;
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        if (row.value("updateAvailable").toBool() && !row.value("frozen").toBool())
            ++count;
    }
    return count;
}
void ContentActions::setQuery(const QString &value) {
    if (value != m_query) {
        m_query = value;
        emit changed();
    }
}
void ContentActions::setCategory(const QString &value) {
    if (QStringList{"all", "mods", "resourcepacks", "shaderpacks"}.contains(value) && value != m_category) {
        m_category = value;
        emit changed();
    }
}
void ContentActions::setSort(const QString &value) {
    if (QStringList{"name-asc", "name-desc", "author", "filename", "updates"}.contains(value) &&
        value != m_sort) {
        m_sort = value;
        emit changed();
    }
}
void ContentActions::setFilter(const QString &key, const QVariant &value) {
    if (m_filters.contains(key)) {
        m_filters[key] = value;
        emit changed();
    }
}
void ContentActions::clearFilters() {
    m_filters = {{"author", ""},     {"environment", ""},   {"state", ""},
                 {"updates", false}, {"openSource", false}, {"external", false}};
    emit changed();
}
void ContentActions::select(const QString &path, bool value) {
    if (find(path).isEmpty())
        return;
    m_selected.removeAll(path);
    if (value)
        m_selected << path;
    emit changed();
}
void ContentActions::selectVisible(bool value) {
    for (const auto &entry : rows()) {
        const auto path = entry.toMap().value("relativePath").toString();
        m_selected.removeAll(path);
        if (value)
            m_selected << path;
    }
    emit changed();
}
void ContentActions::freeze(const QString &path, bool value) {
    const auto row = find(path);
    if (row.isEmpty())
        return;
    const auto identity = preferenceIdentity(row);
    if (identity.isEmpty()) {
        m_notice = "This content has no version identity to freeze.";
        emit changed();
        return;
    }
    m_preferences->setFrozen(m_pack.value("path").toString(), identity, value);
    if (!row.value("sha1").toString().isEmpty())
        m_preferences->setFrozen(m_pack.value("path").toString(), row.value("sha1").toString(), value);
    applyPreferences();
    emit changed();
}
void ContentActions::runWrite(std::function<ContentWriteResult()> work) {
    if (m_busy)
        return;
    m_busy = true;
    m_mutating = true;
    m_notice = "Applying content change…";
    emit changed();
    auto *watcher = new QFutureWatcher<ContentWriteResult>(this);
    connect(watcher, &QFutureWatcher<ContentWriteResult>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        finishWrite(result);
    });
    watcher->setFuture(QtConcurrent::run(std::move(work)));
}
void ContentActions::finishWrite(ContentWriteResult result) {
    m_busy = false;
    m_mutating = false;
    m_dirty = result.ok;
    m_notice = result.ok ? (result.backupPath.isEmpty() ? "Content updated."
                                                        : "Content updated. Originals were backed up.")
                         : result.error;
    if (result.ok && !result.error.isEmpty())
        m_notice += '\n' + result.error;
    emit changed();
    if (result.ok)
        emit contentChanged();
}
void ContentActions::setEnabled(const QString &path, bool value) {
    const auto row = find(path), pack = m_pack;
    const auto files = m_files;
    if (!row.isEmpty())
        runWrite([files, pack, row, value] { return files->setEnabled(pack, row, value); });
}
void ContentActions::remove(const QString &path) {
    const auto row = find(path), pack = m_pack;
    const auto files = m_files;
    if (!row.isEmpty())
        runWrite([files, pack, row] { return files->remove(pack, {row}); });
}
void ContentActions::removeSelected() {
    QVariantList rows;
    for (const auto &path : m_selected) {
        const auto row = find(path);
        if (!row.isEmpty())
            rows << row;
    }
    const auto files = m_files;
    const auto pack = m_pack;
    if (!rows.isEmpty())
        runWrite([files, pack, rows] { return files->remove(pack, rows); });
}
void ContentActions::importFiles(const QList<QUrl> &urls, const QString &category) {
    const auto files = m_files;
    const auto pack = m_pack;
    runWrite([files, pack, urls, category] { return files->importFiles(pack, urls, category); });
}
void ContentActions::install(const QVector<ContentInstallRequest> &requests) {
    if (m_busy || requests.isEmpty())
        return;
    m_busy = true;
    m_mutating = true;
    m_notice = "Resolving updates and required dependencies…";
    emit changed();
    const QPointer<ContentActions> guard(this);
    m_installer->install(m_pack, m_rows, requests, [guard](ContentWriteResult result) {
        if (guard)
            guard->finishWrite(std::move(result));
    });
}
void ContentActions::update(const QString &path) {
    const auto row = find(path);
    if (!row.value("updateAvailable").toBool() || row.value("frozen").toBool())
        return;
    install({{releaseFromVariant(row.value("updateRelease").toMap()), row.value("title").toString(),
              QJsonObject::fromVariantMap(row.value("sourceIds").toMap())}});
}
void ContentActions::updateAll() {
    QVector<ContentInstallRequest> requests;
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        if (row.value("updateAvailable").toBool() && !row.value("frozen").toBool())
            requests << ContentInstallRequest{releaseFromVariant(row.value("updateRelease").toMap()),
                                              row.value("title").toString(),
                                              QJsonObject::fromVariantMap(row.value("sourceIds").toMap())};
    }
    install(requests);
}
void ContentActions::openVersions(const QString &path) {
    m_versionTarget = find(path);
    m_versions.clear();
    m_versionError.clear();
    if (m_versionTarget.isEmpty())
        return;
    const int generation = ++m_versionGeneration;
    m_versionsBusy = true;
    emit changed();
    const QPointer<ContentActions> guard(this);
    m_catalogue->versions(m_versionTarget, m_pack.value("minecraft").toString(),
                          m_pack.value("loader").toString(),
                          [guard, generation](QVector<Release> releases, QString error) {
                              if (!guard || generation != guard->m_versionGeneration)
                                  return;
                              guard->m_versions.clear();
                              for (const auto &release : releases)
                                  guard->m_versions << release.toVariant();
                              guard->m_versionError = error;
                              guard->m_versionsBusy = false;
                              emit guard->changed();
                          });
}
void ContentActions::installVersion(int index) {
    if (index < 0 || index >= m_versions.size() || m_versionTarget.value("frozen").toBool())
        return;
    install({{releaseFromVariant(m_versions.at(index).toMap()), m_versionTarget.value("title").toString(),
              QJsonObject::fromVariantMap(m_versionTarget.value("sourceIds").toMap())}});
}
void ContentActions::installRelease(const QVariantMap &release, const QVariantMap &project) {
    const auto id = project.value("sourceIds").toMap().value("modrinth").toString();
    for (const auto &value : m_rows)
        if (value.toMap().value("sourceIds").toMap().value("modrinth").toString() == id &&
            value.toMap().value("frozen").toBool()) {
            m_notice = "Unfreeze this mod's version before changing it.";
            emit changed();
            return;
        }
    install({{releaseFromVariant(release), project.value("title").toString(),
              QJsonObject::fromVariantMap(project.value("sourceIds").toMap())}});
}
void ContentActions::copyLink(const QString &path) {
    const auto row = find(path);
    const auto url = row.value("projectUrl").toString();
    if (!url.isEmpty())
        emit copyRequested(url);
}
void ContentActions::cancel() {
    if (!cancellable())
        return;
    if (m_mutating) {
        m_installer->cancel();
        return;
    }
    ++m_generation;
    m_busy = false;
    m_notice = "Refresh cancelled.";
    emit changed();
}
} // namespace studio
