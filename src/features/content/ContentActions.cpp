#include "ContentActions.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include <QDateTime>
#include <QFileInfo>
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
    m_executor = std::make_unique<ContentOperationExecutor>(m_files, m_installer, this);
    connect(this, &ContentActions::changed, this, &ContentActions::syncVisibleRows);
    connect(&m_queue, &ContentOperationQueue::changed, this, [this] {
        emit operationsChanged();
        emit changed();
        if (!m_queue.busy()) {
            if (m_anyChanged) {
                m_anyChanged = false;
                emit contentChanged();
            }
            if (m_checkUpdatesPending)
                refresh(true);
        }
    });
    clearFilters();
}
void ContentActions::setContext(const QVariantMap &pack, const QVariantList &files, bool forceRefresh) {
    const auto path = pack.value("path").toString();
    if (path == m_pack.value("path").toString() && hasPending(path)) {
        m_inventory = files;
        return;
    }
    if (pack == m_pack && files == m_inventory && !forceRefresh)
        return;
    const bool changedPack = pack.value("path") != m_pack.value("path");
    m_pack = pack;
    m_inventory = files;
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
    if (hasPending(path)) {
        publishProjection();
        return;
    }
    if (changedPack && files.isEmpty()) {
        ++m_generation;
        m_busy = false;
        m_checkUpdatesPending = !path.isEmpty();
        m_rows = withPreferences(m_pack, m_states.value(path).rows);
        if (m_locks)
            m_locks->setRows(path, m_rows);
        emit changed();
        return;
    }
    m_rows = reconcileContentRows(m_rows, m_rows);
    refresh(changedPack);
}
void ContentActions::refresh(bool checkUpdates) {
    m_checkUpdatesPending = m_checkUpdatesPending || checkUpdates;
    if (hasPending(m_pack.value("path").toString()) || m_pack.isEmpty())
        return;
    checkUpdates = m_checkUpdatesPending;
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
                const auto done = [guard, generation, checkUpdates](QVariantList rows, QString error) {
                    if (!guard || generation != guard->m_generation)
                        return;
                    guard->m_rows = reconcileContentRows(rows, guard->m_rows);
                    guard->m_busy = false;
                    if (checkUpdates)
                        guard->m_checkUpdatesPending = false;
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
                m_rows = reconcileContentRows(result, previous);
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
    m_rows = withPreferences(m_pack, m_rows);
    if (hasPending(m_pack.value("path").toString()))
        syncLocks(m_pack.value("path").toString());
    else if (m_locks)
        m_locks->setRows(m_pack.value("path").toString(), m_rows);
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
QVariantList ContentActions::filteredRows() const {
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
        if (m_filters.value("locked").toBool() && !row.value("locked").toBool())
            continue;
        if (m_filters.value("recent").toBool() &&
            row.value("updatedAt").toLongLong() <
                QDateTime::currentMSecsSinceEpoch() - 7LL * 24 * 60 * 60 * 1000)
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
        if (row.value("updateAvailable").toBool())
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
    m_filters = {{"author", ""},     {"environment", ""},   {"state", ""},
                 {"updates", false}, {"openSource", false}, {"external", false},
                 {"hidden", false},  {"locked", false},     {"recent", false}};
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
void ContentActions::syncVisibleRows() {
    const auto visible = filteredRows();
    if (visible == m_visibleRows)
        return;
    m_visibleRows = visible;
    m_model.setRows(visible);
    emit rowsChanged();
}
QVariantList ContentActions::withPreferences(const QVariantMap &pack, QVariantList rows) const {
    const auto path = pack.value("path").toString();
    for (auto &value : rows) {
        auto row = value.toMap();
        const auto identity = preferenceIdentity(row);
        auto relative = row.value("relativePath").toString();
        if (relative.endsWith(".disabled"))
            relative.chop(9);
        row["locked"] = (!identity.isEmpty() && m_preferences->locked(path, identity)) ||
                        m_preferences->locked(path, "path:" + relative);
        row["updatedAt"] = m_preferences->updatedAt(path, identity);
        row["frozen"] = m_preferences->frozen(path, identity);
        value = row;
    }
    return rows;
}
int ContentActions::actionableUpdateCount() const {
    int count = 0;
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        if (row.value("updateAvailable").toBool() && !row.value("locked").toBool() &&
            !row.value("frozen").toBool() && !row.value("pending").toBool())
            ++count;
    }
    return count;
}
bool ContentActions::hasPending(const QString &path) const {
    for (const auto &operation : m_pendingOperations)
        if (operation.packPath == path)
            return true;
    return false;
}
void ContentActions::syncLocks(const QString &path) {
    if (!m_locks || !m_states.contains(path))
        return;
    const auto state = m_states.value(path);
    QList<ContentOperation> pending;
    for (const auto &operation : m_pendingOperations)
        if (operation.packPath == path)
            pending << operation;
    m_locks->setRows(path,
                     withPreferences(state.pack, state.rows + projectContentOperations(state.rows, pending)));
}
void ContentActions::publishProjection() {
    const auto path = m_pack.value("path").toString();
    if (!m_states.contains(path))
        return;
    QList<ContentOperation> pending;
    for (const auto &operation : m_pendingOperations)
        if (operation.packPath == path)
            pending << operation;
    QSet<QString> selectedKeys;
    for (const auto &value : m_rows)
        if (m_selected.contains(value.toMap().value("relativePath").toString()))
            selectedKeys.insert(contentRowKey(value.toMap()));
    m_rows = withPreferences(m_pack, projectContentOperations(m_states.value(path).rows, pending));
    QStringList selected;
    for (const auto &value : m_rows)
        if (selectedKeys.contains(contentRowKey(value.toMap())))
            selected << value.toMap().value("relativePath").toString();
    m_selected = selected;
    syncLocks(path);
    emit changed();
}
void ContentActions::setLocked(const QString &path, bool value) {
    const auto row = find(path);
    if (row.isEmpty())
        return;
    const auto pack = m_pack.value("path").toString();
    auto relative = row.value("relativePath").toString();
    if (relative.endsWith(".disabled"))
        relative.chop(9);
    const auto identity = preferenceIdentity(row);
    if (!identity.isEmpty())
        m_preferences->setLocked(pack, identity, value);
    m_preferences->setLocked(pack, "path:" + relative, value);
    applyPreferences();
    if (m_states.contains(pack))
        m_states[pack].rows = withPreferences(m_pack, m_states.value(pack).rows);
    syncLocks(pack);
    emit changed();
}
void ContentActions::enqueue(ContentOperation operation) {
    if (m_pack.isEmpty() && operation.packPath.isEmpty())
        return;
    operation.id = ++m_operationId;
    if (operation.packPath.isEmpty())
        operation.packPath = m_pack.value("path").toString();
    if (operation.packPath == m_pack.value("path").toString()) {
        if (!hasPending(operation.packPath))
            m_states[operation.packPath] = {m_pack, reconcileContentRows(m_rows, m_rows), {}};
        ++m_generation;
        m_busy = false;
    }
    m_pendingOperations << operation;
    publishProjection();
    m_queue.enqueue([this, operation](ContentOperationQueue::Completion done) {
        startOperation(operation, std::move(done));
    });
}
void ContentActions::startOperation(ContentOperation operation, ContentOperationQueue::Completion done) {
    auto before = m_states.value(operation.packPath);
    before.rows = withPreferences(before.pack, before.rows);
    syncLocks(operation.packPath);
    m_activity = operation.kind == ContentOperation::Install ? "installing" : "applying";
    m_cancelRequested = false;
    const QPointer<ContentActions> guard(this);
    m_executor->execute(
        operation, before,
        [guard, operation, done](ContentWriteResult result, ContentFileState current) mutable {
            if (guard)
                guard->completeOperation(operation, result, std::move(current), std::move(done));
        });
}
void ContentActions::completeOperation(ContentOperation operation, ContentWriteResult result,
                                       ContentFileState current, ContentOperationQueue::Completion done) {
    const auto before = m_states.value(operation.packPath);
    if (current.pack.isEmpty())
        current.pack = before.pack;
    if (!current.error.isEmpty()) {
        if (!result.error.isEmpty())
            result.error += " ";
        result.error += current.error;
        current.rows = before.rows;
    }
    current.rows =
        reconcileContentRows(current.rows, before.rows, result.ok ? operation.predictions : QVariantList{});
    if (result.ok && operation.kind == ContentOperation::Install) {
        for (const auto &value : current.rows) {
            const auto row = value.toMap();
            for (const auto &old : before.rows)
                if (contentRowKey(old.toMap()) == contentRowKey(row) &&
                    old.toMap().value("sha1") != row.value("sha1"))
                    m_preferences->setUpdatedAt(operation.packPath, preferenceIdentity(row),
                                                QDateTime::currentMSecsSinceEpoch());
        }
    }
    m_states[operation.packPath] = current;
    if (m_pack.value("path") == operation.packPath) {
        m_pack = current.pack;
        m_inventory = current.rows;
    }
    for (int i = 0; i < m_pendingOperations.size(); ++i)
        if (m_pendingOperations.at(i).id == operation.id) {
            m_pendingOperations.removeAt(i);
            break;
        }
    m_anyChanged = m_anyChanged || result.ok || result.changed;
    const bool cancelled = m_cancelRequested && !result.ok;
    m_activity = cancelled ? "cancelled" : result.error.isEmpty() ? "success" : "error";
    m_activityText.clear();
    m_cancelRequested = false;
    if (!cancelled && !result.error.isEmpty()) {
        m_notice = result.error;
        emit operationFailed(result.error);
    }
    publishProjection();
    done();
}
void ContentActions::setEnabled(const QString &path, bool value) {
    const auto row = find(path);
    if (row.isEmpty() || row.value("locked").toBool() || row.value("enabled").toBool() == value)
        return;
    ContentOperation operation;
    operation.kind = ContentOperation::Enable;
    operation.targets = {contentRowKey(row)};
    operation.enabled = value;
    enqueue(operation);
}
void ContentActions::remove(const QString &path) {
    const auto row = find(path);
    if (row.isEmpty() || row.value("locked").toBool())
        return;
    ContentOperation operation;
    operation.kind = ContentOperation::Remove;
    operation.targets = {contentRowKey(row)};
    enqueue(operation);
}
void ContentActions::removeSelected() {
    ContentOperation operation;
    operation.kind = ContentOperation::Remove;
    for (const auto &value : selection())
        if (!value.toMap().value("locked").toBool())
            operation.targets << contentRowKey(value.toMap());
    if (!operation.targets.isEmpty())
        enqueue(operation);
}
void ContentActions::importFiles(const QList<QUrl> &urls, const QString &category) {
    if (urls.isEmpty())
        return;
    ContentOperation operation;
    operation.kind = ContentOperation::Import;
    operation.urls = urls;
    operation.category = category;
    for (const auto &url : urls) {
        const auto path = category + '/' + url.fileName();
        QVariantMap row{{"relativePath", path},
                        {"fileName", url.fileName()},
                        {"title", url.fileName()},
                        {"category", category},
                        {"enabled", true}};
        row["contentKey"] = contentRowKey(row);
        operation.predictions << row;
    }
    enqueue(operation);
}
void ContentActions::install(const QVector<ContentInstallRequest> &requests, const QVariantMap &project) {
    if (requests.isEmpty())
        return;
    ContentOperation operation;
    operation.kind = ContentOperation::Install;
    operation.requests = requests;
    const auto target = project.value("targetPack", m_pack).toMap();
    operation.packPath = target.value("path").toString();
    if (!m_states.contains(operation.packPath))
        m_states[operation.packPath] = {target, project.value("targetRows").toList(), {}};
    const auto candidates = operation.packPath == m_pack.value("path").toString()
                                ? m_rows
                                : m_states.value(operation.packPath).rows;
    for (const auto &request : requests) {
        QVariantMap row = project;
        row.remove("targetRows");
        row.remove("targetPack");
        const auto category = contentCategory(request.contentType);
        for (const auto &value : candidates) {
            const auto candidate = value.toMap();
            if (candidate.value("category") != category)
                continue;
            const auto ids = candidate.value("sourceIds").toMap();
            bool match = false;
            for (auto it = request.sourceIds.begin(); it != request.sourceIds.end(); ++it)
                if (!it.value().toString().isEmpty() &&
                    ids.value(it.key()).toString() == it.value().toString())
                    match = true;
            if (match) {
                row = candidate;
                break;
            }
        }
        if (row.value("locked").toBool() || row.value("frozen").toBool())
            continue;
        if (!row.value("contentKey").toString().isEmpty())
            operation.targets << contentRowKey(row);
        row["title"] = request.title;
        row["category"] = category;
        row["sourceIds"] = request.sourceIds.toVariantMap();
        row["provider"] = request.release.provider;
        row["projectId"] = request.release.projectId;
        if (!row.contains("relativePath"))
            row["relativePath"] = category + '/' + request.release.filename;
        row["fileName"] = request.release.filename;
        row["version"] = request.release.version;
        row["sha1"] = request.release.sha1;
        row["installedRelease"] = request.release.toVariant();
        row["updateAvailable"] = false;
        row["contentKey"] = contentRowKey(row);
        if (!row.contains("enabled"))
            row["enabled"] = true;
        operation.predictions << row;
    }
    if (operation.predictions.size() != requests.size())
        return;
    enqueue(operation);
}
ContentInstallRequest ContentActions::updateRequest(const QVariantMap &row) const {
    return {releaseFromVariant(row.value("updateRelease").toMap()), row.value("title").toString(),
            QJsonObject::fromVariantMap(row.value("sourceIds").toMap()),
            categoryType(row.value("category").toString())};
}
void ContentActions::update(const QString &path) {
    const auto row = find(path);
    if (!row.value("updateAvailable").toBool() || row.value("frozen").toBool() ||
        row.value("locked").toBool())
        return;
    install({updateRequest(row)});
}
void ContentActions::updateAll() {
    QVector<ContentInstallRequest> requests;
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        if (row.value("updateAvailable").toBool() && !row.value("frozen").toBool() &&
            !row.value("locked").toBool() && !row.value("pending").toBool())
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
        if (row.value("updateAvailable").toBool() && !row.value("frozen").toBool() &&
            !row.value("locked").toBool() && !row.value("pending").toBool())
            requests << updateRequest(row);
    }
    install(requests);
}
void ContentActions::setSelectedEnabled(bool enabled) {
    ContentOperation operation;
    operation.kind = ContentOperation::Enable;
    operation.enabled = enabled;
    for (const auto &value : selection()) {
        const auto row = value.toMap();
        if (!row.value("locked").toBool() && row.value("enabled").toBool() != enabled)
            operation.targets << contentRowKey(row);
    }
    if (!operation.targets.isEmpty())
        enqueue(operation);
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
    if (index < 0 || index >= m_versions.size() || m_versionTarget.value("frozen").toBool() ||
        find(m_versionTarget.value("relativePath").toString()).value("locked").toBool())
        return;
    auto project = m_versionTarget;
    project["type"] = categoryType(m_versionTarget.value("category").toString());
    installRelease(m_versions.at(index).toMap(), project);
}
void ContentActions::installRelease(const QVariantMap &release, const QVariantMap &project) {
    auto type = project.value("type", "mod").toString();
    if (type == "datapack" && release.value("filename").toString().endsWith(".jar", Qt::CaseInsensitive))
        type = "mod";
    const auto target = project.value("targetPack", m_pack).toMap();
    const auto path = target.value("path").toString();
    const auto rows = path == m_pack.value("path").toString() ? m_rows : m_states.value(path).rows;
    auto ids = project.value("sourceIds").toMap();
    if (ids.isEmpty())
        ids[release.value("provider").toString()] = release.value("projectId");
    if (hasDataPackFormatConflict(contentCategory(type), ids, rows)) {
        m_notice = "This project is installed in another format. Remove that copy before switching formats.";
        emit operationFailed(m_notice);
        emit changed();
        return;
    }
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const auto installed = row.value("sourceIds").toMap();
        bool match = false;
        for (auto it = ids.begin(); it != ids.end(); ++it)
            if (!it.value().toString().isEmpty() && installed.value(it.key()) == it.value())
                match = true;
        if (match && (row.value("locked").toBool() || row.value("frozen").toBool())) {
            m_notice = "Unlock this content or unfreeze its version before changing it.";
            emit operationFailed(m_notice);
            emit changed();
            return;
        }
    }
    auto destination = project;
    destination["targetPack"] = target;
    install({{releaseFromVariant(release), project.value("title").toString(),
              QJsonObject::fromVariantMap(ids), type}},
            destination);
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
    if (m_queue.busy()) {
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
