#include "ContentActions.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include <QFutureWatcher>
#include <QHash>
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
void ContentActions::setContext(const QVariantMap &pack, const QVariantList &files, bool forceRefresh) {
    if (m_mutating)
        return;
    if (pack == m_pack && files == m_inventory && !m_dirty && !forceRefresh)
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
    m_notice.clear();
    m_activity = "checking";
    m_activityText = checkUpdates ? "Checking updates" : "Reading changes";
    emit changed();
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    const QPointer<ContentActions> guard(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this,
            [this, guard, watcher, generation, checkUpdates, previous] {
                const auto rows = watcher->result();
                watcher->deleteLater();
                if (generation != m_generation)
                    return;
                auto result = rows;
                QVariantList unidentified;
                const QStringList metadata{
                    "sourceIds",     "external",        "provider",   "projectId", "installedRelease",
                    "updateRelease", "updateAvailable", "projectUrl", "author",    "authorIcon",
                    "environment",   "openSource",      "authorIds"};
                // Category and binary hash are the identity; names and paths can change.
                QHash<QPair<QString, QString>, QVariantMap> previousByHash;
                previousByHash.reserve(previous.size());
                for (const auto &value : previous) {
                    const auto row = value.toMap();
                    const auto hash = row.value("sha1").toString();
                    const auto key = qMakePair(row.value("category").toString(), hash);
                    if (!hash.isEmpty() && !previousByHash.contains(key))
                        previousByHash.insert(key, row);
                }
                for (int i = 0; i < result.size(); ++i) {
                    auto row = result.at(i).toMap();
                    const auto old = previousByHash.constFind(
                        qMakePair(row.value("category").toString(), row.value("sha1").toString()));
                    if (old != previousByHash.cend())
                        for (const auto &key : metadata)
                            if (old->contains(key))
                                row[key] = old->value(key);
                    if (old == previousByHash.cend())
                        unidentified << row;
                    result[i] = row;
                }
                const auto done = [guard, generation](QVariantList rows, QString error) {
                    if (!guard || generation != guard->m_generation)
                        return;
                    guard->m_rows = rows;
                    guard->m_busy = false;
                    guard->m_notice = error;
                    guard->m_activity = error.isEmpty() ? "idle" : "error";
                    guard->applyPreferences();
                    QSet<QString> available;
                    for (const auto &value : rows)
                        available.insert(value.toMap().value("relativePath").toString());
                    QStringList retained;
                    for (const auto &path : guard->m_selected)
                        if (available.contains(path))
                            retained << path;
                    guard->m_selected = retained;
                    emit guard->changed();
                };
                m_rows = result;
                applyPreferences();
                emit changed();
                if (checkUpdates || !unidentified.isEmpty()) {
                    const auto lookup = checkUpdates ? result : unidentified;
                    m_catalogue->refresh(
                        lookup, m_pack.value("minecraft").toString(),
                        compatibleLoaders(m_pack.value("loader").toString(), result).join('|'),
                        [done, result, checkUpdates](QVariantList indexed, QString error) mutable {
                            if (checkUpdates) {
                                done(indexed, error);
                                return;
                            }
                            QHash<QPair<QString, QString>, QVariantMap> indexedByPath;
                            indexedByPath.reserve(indexed.size());
                            for (const auto &entry : indexed) {
                                const auto row = entry.toMap();
                                const auto key = qMakePair(row.value("relativePath").toString(),
                                                           row.value("sha1").toString());
                                if (!indexedByPath.contains(key))
                                    indexedByPath.insert(key, row);
                            }
                            for (auto &value : result) {
                                const auto row = value.toMap();
                                const auto item = indexedByPath.constFind(qMakePair(
                                    row.value("relativePath").toString(), row.value("sha1").toString()));
                                if (item != indexedByPath.cend())
                                    value = *item;
                            }
                            done(result, error);
                        });
                } else
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
        auto row = value.toMap();
        row["hidden"] = m_visibility && m_visibility->hidden(row, m_pack.value("path").toString());
        if (row.value("hidden").toBool() != m_filters.value("hidden").toBool())
            continue;
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
    if (QStringList{"all", "mods", "resourcepacks", "shaderpacks", "datapacks"}.contains(value) &&
        value != m_category) {
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
void ContentActions::hide(const QString &path, bool hidden) {
    const auto row = find(path);
    if (m_visibility && !row.isEmpty())
        m_visibility->setHidden(row, hidden, m_pack.value("path").toString());
    if (hidden)
        m_selected.removeAll(path);
    emit changed();
}
void ContentActions::setFilter(const QString &key, const QVariant &value) {
    if (m_filters.contains(key)) {
        m_filters[key] = value;
        emit changed();
    }
}
void ContentActions::clearFilters() {
    m_filters = {{"author", ""},        {"environment", ""}, {"state", ""},    {"updates", false},
                 {"openSource", false}, {"external", false}, {"hidden", false}};
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
    m_notice.clear();
    m_activity = "applying";
    m_activityText = "Applying changes";
    m_cancelRequested = false;
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
    m_dirty = result.ok || result.changed;
    m_activity = m_cancelRequested && !result.ok ? "cancelled"
                 : !result.error.isEmpty()       ? "error"
                 : result.ok                     ? "success"
                                                 : "error";
    m_notice = m_cancelRequested && !result.ok ? QString{} : result.error;
    m_activityText.clear();
    m_cancelRequested = false;
    emit changed();
    if (result.ok || result.changed)
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
    m_notice.clear();
    m_activity = "installing";
    m_activityText = "Preparing updates";
    m_cancelRequested = false;
    emit changed();
    const QPointer<ContentActions> guard(this);
    m_installer->install(m_pack, m_rows, requests, [guard](ContentWriteResult result) {
        if (guard)
            guard->finishWrite(std::move(result));
    });
}
ContentInstallRequest ContentActions::updateRequest(const QVariantMap &row) const {
    return {releaseFromVariant(row.value("updateRelease").toMap()), row.value("title").toString(),
            QJsonObject::fromVariantMap(row.value("sourceIds").toMap()),
            categoryType(row.value("category").toString())};
}
void ContentActions::update(const QString &path) {
    const auto row = find(path);
    if (!row.value("updateAvailable").toBool() || row.value("frozen").toBool())
        return;
    install({updateRequest(row)});
}
void ContentActions::updateAll() {
    QVector<ContentInstallRequest> requests;
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        if (row.value("updateAvailable").toBool() && !row.value("frozen").toBool())
            requests << updateRequest(row);
    }
    install(requests);
}
QVariantList ContentActions::selection() const {
    QVariantList rows;
    for (const auto &path : m_selected) {
        const auto row = find(path);
        if (!row.isEmpty())
            rows << row;
    }
    return rows;
}
void ContentActions::updateSelected() {
    QVector<ContentInstallRequest> requests;
    for (const auto &value : selection()) {
        const auto row = value.toMap();
        if (row.value("updateAvailable").toBool() && !row.value("frozen").toBool())
            requests << updateRequest(row);
    }
    install(requests);
}
void ContentActions::setSelectedEnabled(bool enabled) {
    const auto rows = selection();
    const auto files = m_files;
    const auto pack = m_pack;
    if (!rows.isEmpty())
        runWrite([files, pack, rows, enabled] { return files->setEnabledBatch(pack, rows, enabled); });
}
void ContentActions::copySelected(const QString &format) {
    QStringList text;
    for (const auto &value : selection()) {
        const auto row = value.toMap();
        const auto title = row.value("title").toString();
        const auto link = row.value("projectUrl").toString();
        if (format == "names")
            text << title;
        else if (format == "files")
            text << row.value("fileName").toString();
        else if (!link.isEmpty() && format == "links")
            text << link;
        else if (!link.isEmpty() && format == "markdown") {
            auto escaped = title;
            escaped.replace("\\", "\\\\").replace("[", "\\[").replace("]", "\\]");
            text << '[' + escaped + "](" + link + ')';
        }
    }
    if (!text.isEmpty())
        emit copyRequested(text.join('\n'));
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
                          compatibleLoaders(m_pack.value("loader").toString(), m_rows).join('|'),
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
    auto project = m_versionTarget;
    project["type"] = categoryType(m_versionTarget.value("category").toString());
    installRelease(m_versions.at(index).toMap(), project);
}
void ContentActions::installRelease(const QVariantMap &release, const QVariantMap &project) {
    auto type = project.value("type", "mod").toString();
    if (type == "datapack" && release.value("filename").toString().endsWith(".jar", Qt::CaseInsensitive))
        type = "mod";
    if (hasDataPackFormatConflict(contentCategory(type), project.value("sourceIds").toMap(), m_rows)) {
        m_notice = "This project is installed in another format. Remove that copy before switching between "
                   "ZIP and mod variants.";
        emit changed();
        return;
    }
    const auto id = project.value("sourceIds").toMap().value("modrinth").toString();
    for (const auto &value : m_rows)
        if (value.toMap().value("sourceIds").toMap().value("modrinth").toString() == id &&
            value.toMap().value("frozen").toBool()) {
            m_notice = "Unfreeze this mod's version before changing it.";
            emit changed();
            return;
        }
    install({{releaseFromVariant(release), project.value("title").toString(),
              QJsonObject::fromVariantMap(project.value("sourceIds").toMap()), type}});
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
        m_cancelRequested = true;
        m_installer->cancel();
        return;
    }
    ++m_generation;
    m_busy = false;
    m_notice.clear();
    m_activity = "cancelled";
    m_activityText.clear();
    emit changed();
}
} // namespace studio
