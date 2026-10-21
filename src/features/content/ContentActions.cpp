#include "ContentActions.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include "domain/TextSearch.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QtConcurrent>
#include <algorithm>
#include <exception>
namespace studio {
ContentActions::ContentActions(std::shared_ptr<const IContentFileStore> files,
                               std::shared_ptr<IContentCatalogue> catalogue,
                               std::shared_ptr<IContentInstaller> installer,
                               std::shared_ptr<IContentPreferences> preferences, QObject *parent)
    : QObject(parent), m_files(std::move(files)), m_catalogue(std::move(catalogue)),
      m_installer(std::move(installer)), m_preferences(std::move(preferences)) {
    connect(&m_operations, &OperationHistory::retryRequested, this, &ContentActions::retryOperation);
    connect(&m_operations, &OperationHistory::dismissed, this,
            [this](quint64 id) { m_failedOperations.remove(id); });
    m_executor = std::make_unique<ContentOperationExecutor>(m_files, m_installer, this);
    connect(this, &ContentActions::changed, this, &ContentActions::syncVisibleRows);
    connect(&m_queue, &ContentOperationQueue::changed, this, [this] {
        emit operationsChanged();
        if (!m_batchingUpdates)
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
    auto *watcher = new QFutureWatcher<ContentFileState>(this);
    const QPointer<ContentActions> guard(this);
    connect(
        watcher, &QFutureWatcher<QVariantList>::finished, this,
        [this, guard, watcher, generation, checkUpdates, previous] {
            const auto state = watcher->result();
            const auto rows = state.rows;
            watcher->deleteLater();
            if (generation != m_generation)
                return;
            if (!state.error.isEmpty()) {
                m_busy = false;
                reject(state.error, "Reading content");
                return;
            }
            m_pack = state.pack;
            ++m_lockRevision;
            auto result = rows;
            QVariantList unidentified;
            const QStringList metadata{"sourceIds",        "external",      "provider",        "projectId",
                                       "installedRelease", "updateRelease", "updateAvailable", "projectUrl",
                                       "author",           "authorIcon",    "environment",     "openSource",
                                       "authorIds"};
            // Prefer an unchanged path/hash pair; a shared binary alone can be ambiguous.
            QHash<QPair<QString, QString>, QVariantMap> previousByHash, previousByPath;
            QSet<QPair<QString, QString>> ambiguousHashes;
            previousByHash.reserve(previous.size());
            for (const auto &value : previous) {
                const auto row = value.toMap();
                const auto hash = row.value("sha1").toString();
                const auto key = qMakePair(row.value("category").toString(), hash);
                previousByPath.insert(
                    {row.value("category").toString(), row.value("relativePath").toString()}, row);
                if (!hash.isEmpty()) {
                    if (previousByHash.contains(key))
                        ambiguousHashes.insert(key);
                    previousByHash.insert(key, row);
                }
            }
            for (int i = 0; i < result.size(); ++i) {
                auto row = result.at(i).toMap();
                const auto key = qMakePair(row.value("category").toString(), row.value("sha1").toString());
                auto old = previousByPath.value(
                    {row.value("category").toString(), row.value("relativePath").toString()});
                if (row.value("sha1").toString().isEmpty())
                    old.clear();
                else if (old.value("sha1") != row.value("sha1"))
                    old = ambiguousHashes.contains(key) ? QVariantMap{} : previousByHash.value(key);
                if (!old.isEmpty())
                    for (const auto &key : metadata)
                        if (old.contains(key))
                            row[key] = old.value(key);
                if (old.isEmpty())
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
                            const auto key =
                                qMakePair(row.value("relativePath").toString(), row.value("sha1").toString());
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
    watcher->setFuture(QtConcurrent::run([files, pack, inventory] {
        try {
            return files->reload(pack, inventory, true);
        } catch (const std::exception &error) {
            return ContentFileState{
                pack, {}, QString("Could not read content: ") + QString::fromUtf8(error.what())};
        } catch (...) {
            return ContentFileState{pack, {}, "Could not read content."};
        }
    }));
}
QString ContentActions::preferenceIdentity(const QVariantMap &row) const {
    const auto ids = row.value("sourceIds").toMap();
    if (!ids.value("modrinth").toString().isEmpty())
        return "modrinth:" + ids.value("modrinth").toString();
    if (!ids.value("curseforge").toString().isEmpty())
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
    const TextQuery search(m_query);
    for (const auto &value : m_rows) {
        auto row = value.toMap();
        row["hidden"] = m_visibility && m_visibility->hidden(row, m_pack.value("path").toString());
        if (row.value("hidden").toBool() != m_filters.value("hidden").toBool())
            continue;
        if (m_category != "all" && row.value("category") != m_category)
            continue;
        if (!search.matches(row.value("title").toString() + ' ' + row.value("fileName").toString() + ' ' +
                            row.value("author").toString() + ' ' + row.value("version").toString()))
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
    QSet<QString> sourceLocks;
    for (const auto &value : pack.value("sourceContent").toList()) {
        const auto source = value.toMap();
        if (source.value("launcherLocked").toBool() && !source.value("projectId").toString().isEmpty())
            sourceLocks.insert(source.value("projectId").toString());
    }
    for (auto &value : rows) {
        auto row = value.toMap();
        const auto identity = preferenceIdentity(row);
        auto relative = row.value("relativePath").toString();
        if (relative.endsWith(".disabled"))
            relative.chop(9);
        row["launcherLocked"] = row.value("launcherLocked").toBool();
        const auto ids = row.value("sourceIds").toMap();
        row["launcherLocked"] =
            row.value("launcherLocked").toBool() || sourceLocks.contains(ids.value("modrinth").toString());
        bool locked = row.value("launcherLocked").toBool() || m_preferences->locked(path, "path:" + relative);
        for (auto it = ids.cbegin(); !locked && it != ids.cend(); ++it)
            if (!it.value().toString().isEmpty())
                locked = m_preferences->locked(path, it.key() + ':' + it.value().toString());
        row["locked"] = locked;
        row["updatedAt"] = m_preferences->updatedAt(path, identity);
        row["frozen"] = m_preferences->frozen(path, identity);
        value = row;
    }
    return rows;
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
    auto rows = withPreferences(state.pack, state.rows + projectContentOperations(state.rows, pending));
    for (const auto &value : state.pack.value("sourceContent").toList()) {
        auto source = value.toMap();
        if (source.value("launcherLocked").toBool()) {
            source["locked"] = true;
            rows << source;
        }
    }
    m_locks->setRows(path, rows);
}
void ContentActions::publishProjection() {
    if (m_batchingUpdates)
        return;
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
    if (row.value("launcherLocked").toBool()) {
        reject("This lock is managed by Modrinth. Unlock it in Modrinth.", row.value("title").toString());
        return;
    }
    if (hasPending(m_pack.value("path").toString())) {
        reject("Wait for this instance's content operation before changing locks.",
               row.value("title").toString());
        return;
    }
    const auto pack = m_pack.value("path").toString();
    ++m_lockRevision;
    auto relative = row.value("relativePath").toString();
    if (relative.endsWith(".disabled"))
        relative.chop(9);
    const auto identity = preferenceIdentity(row);
    const auto ids = row.value("sourceIds").toMap();
    for (auto it = ids.cbegin(); it != ids.cend(); ++it)
        if (!it.value().toString().isEmpty())
            m_preferences->setLocked(pack, it.key() + ':' + it.value().toString(), value);
    m_preferences->setLocked(pack, "path:" + relative, value);
    applyPreferences();
    if (m_states.contains(pack))
        m_states[pack].rows = withPreferences(m_pack, m_states.value(pack).rows);
    syncLocks(pack);
    emit changed();
}
bool ContentActions::enqueue(ContentOperation operation) {
    if (m_pack.isEmpty() && operation.packPath.isEmpty())
        return false;
    operation.id = ++m_operationId;
    if (operation.packPath.isEmpty())
        operation.packPath = m_pack.value("path").toString();
    operation.reservations = reservations(operation);
    for (const auto &pending : m_pendingOperations)
        if (QDir::cleanPath(pending.packPath)
                .compare(QDir::cleanPath(operation.packPath), Qt::CaseInsensitive) == 0)
            for (const auto &key : operation.reservations)
                if (pending.reservations.contains(key)) {
                    reject("This content already has an active or queued operation. Wait for it to finish.");
                    return false;
                }
    if (operation.packPath == m_pack.value("path").toString()) {
        if (!hasPending(operation.packPath))
            m_states[operation.packPath] = {m_pack, reconcileContentRows(m_rows, m_rows), {}};
        ++m_generation;
        m_busy = false;
    }
    const auto state = m_states.value(operation.packPath);
    if (operation.expectedHashes.isEmpty())
        for (const auto &value : resolveContentTargets(state.rows, operation.targets)) {
            const auto row = value.toMap();
            operation.expectedHashes.insert(contentRowKey(row), row.value("sha1").toString());
        }
    m_pendingOperations << operation;
    QStringList titles;
    if (!operation.predictions.isEmpty())
        for (const auto &value : operation.predictions)
            titles << value.toMap().value("title").toString();
    else
        for (const auto &value : resolveContentTargets(state.rows, operation.targets))
            titles << value.toMap().value("title").toString();
    if (titles.isEmpty() && !operation.sourceLock.isEmpty())
        titles << operation.sourceLock.value("relativePath").toString();
    m_operations.queued(operation.id, state.pack,
                        titles.size() == 1 ? titles.first()
                                           : QString::number(qMax(1, titles.size())) + " items",
                        operation.kind == ContentOperation::Install
                            ? (operation.targets.isEmpty() ? "Installing" : "Updating")
                        : operation.kind == ContentOperation::Remove  ? "Remove"
                        : operation.kind == ContentOperation::Import  ? "Import"
                        : operation.kind == ContentOperation::Restore ? "Restore locked version"
                        : operation.enabled                           ? "Enable"
                                                                      : "Disable");
    publishProjection();
    m_queue.enqueue([this, operation](ContentOperationQueue::Completion done) {
        startOperation(operation, std::move(done));
    });
    return true;
}
void ContentActions::startOperation(ContentOperation operation, ContentOperationQueue::Completion done) {
    bool accepted = false;
    for (const auto &pending : m_pendingOperations)
        if (pending.id == operation.id)
            accepted = true;
    if (!accepted) {
        done();
        return;
    }
    m_activeOperationId = operation.id;
    m_operations.start(operation.id);
    m_activity = operation.kind == ContentOperation::Install ? "installing" : "applying";
    m_cancelRequested = false;
    const QPointer<ContentActions> guard(this);
    auto execute = [guard, operation, done](ContentFileState before) {
        if (!guard)
            return;
        before.rows = guard->withPreferences(before.pack, before.rows);
        if (before.error.isEmpty())
            guard->m_states[operation.packPath] = before;
        guard->syncLocks(operation.packPath);
        guard->m_executor->execute(
            operation, before,
            [guard, operation, done](ContentWriteResult result, ContentFileState current) mutable {
                if (guard)
                    guard->completeOperation(operation, result, std::move(current), std::move(done));
            });
    };
    const auto before = m_states.value(operation.packPath);
    if (before.error.isEmpty()) {
        execute(before);
        return;
    }
    // A transient post-write read failure must not poison every remaining item.
    // Recover a verified inventory on a worker before permitting the next write.
    auto *watcher = new QFutureWatcher<ContentFileState>(this);
    connect(watcher, &QFutureWatcher<ContentFileState>::finished, this, [watcher, execute] {
        const auto current = watcher->result();
        watcher->deleteLater();
        execute(current);
    });
    const auto files = m_files;
    watcher->setFuture(QtConcurrent::run([files, before] {
        try {
            return files->reload(before.pack, before.rows, true);
        } catch (...) {
            return ContentFileState{before.pack, before.rows, "Could not recover the content inventory."};
        }
    }));
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
        result.changed = result.changed || result.ok;
        result.ok = false;
        current.rows = before.rows;
    }
    current.rows =
        reconcileContentRows(current.rows, before.rows, result.ok ? operation.predictions : QVariantList{});
    if (result.ok && operation.kind == ContentOperation::Install) {
        QHash<QString, QVariant> previousHashes;
        previousHashes.reserve(before.rows.size());
        for (const auto &value : before.rows)
            previousHashes.insert(contentRowKey(value.toMap()), value.toMap().value("sha1"));
        for (const auto &value : current.rows) {
            const auto row = value.toMap();
            const auto previous = previousHashes.constFind(contentRowKey(row));
            if (previous != previousHashes.cend() && *previous != row.value("sha1"))
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
    if (!result.ok && !cancelled && !result.changed)
        m_failedOperations[operation.id] = operation;
    m_operations.finish(operation.id, result.ok || cancelled, cancelled ? QString{} : result.error,
                        !result.changed);
    m_activeOperationId = 0;
    ++m_lockRevision;
    publishProjection();
    done();
}
void ContentActions::setEnabled(const QString &path, bool value) {
    const auto row = find(path);
    if (row.isEmpty())
        return;
    if (row.value("locked").toBool()) {
        reject("Unlock this content before changing it.", row.value("title").toString());
        return;
    }
    if (row.value("enabled").toBool() == value)
        return;
    ContentOperation operation;
    operation.kind = ContentOperation::Enable;
    operation.targets = {contentRowKey(row)};
    operation.enabled = value;
    enqueue(operation);
}
void ContentActions::remove(const QString &path) {
    const auto row = find(path);
    if (row.isEmpty())
        return;
    if (row.value("locked").toBool()) {
        reject("Unlock this content before removing it.", row.value("title").toString());
        return;
    }
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
    for (const auto &request : requests) {
        auto candidate = request.release.toVariant();
        candidate["sourceIds"] = request.sourceIds.toVariantMap();
        candidate["targetPack"] = project.value("targetPack", m_pack);
        if (projectLocked(candidate)) {
            reject("This content is locked. Unlock it before installing another version.", request.title);
            return;
        }
    }
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
    for (auto &request : operation.requests) {
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
                request.resolveDependencies = false;
                row = candidate;
                break;
            }
        }
        if (row.value("locked").toBool() || row.value("frozen").toBool())
            continue;
        if (!row.value("contentKey").toString().isEmpty())
            operation.targets << contentRowKey(row);
        operation.predictions << contentInstallPrediction(request, target, row);
    }
    if (operation.predictions.size() != requests.size())
        return;
    enqueue(operation);
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
static QVariantMap projectIds(const QVariantMap &project) {
    auto ids = project.value("sourceIds").toMap();
    const auto provider = project.value("provider").toString(),
               id = project.value("projectId", project.value("id")).toString();
    if (!provider.isEmpty() && !id.isEmpty())
        ids[provider] = id;
    return ids;
}
QVariantMap ContentActions::sourceLock(const QVariantMap &project) const {
    const auto target = project.value("targetPack", m_pack).toMap();
    const auto path = target.value("path").toString();
    const auto pack = target.contains("sourceContent") ? target
                      : path == m_pack.value("path").toString()
                          ? m_pack
                          : (m_states.contains(path) ? m_states.value(path).pack : target);
    const auto ids = projectIds(project);
    for (const auto &value : pack.value("sourceContent").toList()) {
        const auto lock = value.toMap();
        if (lock.value("launcherLocked").toBool() && !lock.value("projectId").toString().isEmpty() &&
            ids.value("modrinth") == lock.value("projectId"))
            return lock;
    }
    return {};
}
bool ContentActions::projectLocked(const QVariantMap &project) const {
    if (!sourceLock(project).isEmpty())
        return true;
    const auto target = project.value("targetPack", m_pack).toMap();
    const auto ids = projectIds(project);
    for (auto it = ids.cbegin(); it != ids.cend(); ++it)
        if (!it.value().toString().isEmpty() &&
            m_preferences->locked(target.value("path").toString(), it.key() + ':' + it.value().toString()))
            return true;
    return false;
}
void ContentActions::setProjectLocked(const QVariantMap &project, bool locked) {
    if (!sourceLock(project).isEmpty()) {
        reject("This lock is managed by Modrinth. Unlock it in Modrinth.");
        return;
    }
    if (hasPending(m_pack.value("path").toString())) {
        reject("Wait for this instance's content operation before changing locks.");
        return;
    }
    const auto ids = projectIds(project);
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        const auto installed = row.value("sourceIds").toMap();
        for (auto it = ids.cbegin(); it != ids.cend(); ++it)
            if (!it.value().toString().isEmpty() && installed.value(it.key()) == it.value()) {
                setLocked(row.value("relativePath").toString(), locked);
                break;
            }
    }
    for (auto it = ids.cbegin(); it != ids.cend(); ++it)
        if (!it.value().toString().isEmpty())
            m_preferences->setLocked(m_pack.value("path").toString(), it.key() + ':' + it.value().toString(),
                                     locked);
    ++m_lockRevision;
    applyPreferences();
    emit changed();
}
void ContentActions::restoreLocked(const QVariantMap &project) {
    const auto lock = sourceLock(project);
    if (lock.isEmpty() || !lock.value("missing").toBool()) {
        reject("There is no missing source-locked version to restore.");
        return;
    }
    ContentOperation operation;
    operation.kind = ContentOperation::Restore;
    operation.sourceLock = lock;
    enqueue(operation);
}
void ContentActions::reject(const QString &message, const QString &title) {
    m_notice = message;
    const auto id = ++m_operationId;
    m_operations.queued(id, m_pack, title.isEmpty() ? "Content" : title, "Blocked action");
    m_operations.finish(id, false, message, false);
    emit operationFailed(message);
    emit changed();
}
QStringList ContentActions::reservations(const ContentOperation &operation) const {
    QSet<QString> keys;
    auto add = [&](const QVariantMap &row) {
        auto path = row.value("relativePath").toString();
        if (path.endsWith(".disabled"))
            path.chop(9);
#ifdef Q_OS_WIN
        path = path.toCaseFolded();
#endif
        if (!path.isEmpty())
            keys.insert("path:" + path);
        const auto ids = projectIds(row);
        for (auto it = ids.cbegin(); it != ids.cend(); ++it)
            if (!it.value().toString().isEmpty())
                keys.insert("project:" + row.value("category").toString() + ':' + it.key() + ':' +
                            it.value().toString());
    };
    const auto state = m_states.value(operation.packPath);
    for (const auto &value : resolveContentTargets(state.rows + m_rows, operation.targets))
        add(value.toMap());
    for (const auto &value : operation.predictions)
        add(value.toMap());
    for (const auto &request : operation.requests) {
        auto row = request.release.toVariant();
        row["sourceIds"] = request.sourceIds.toVariantMap();
        row["category"] = contentCategory(request.contentType);
        add(row);
    }
    if (!operation.sourceLock.isEmpty()) {
        auto row = operation.sourceLock;
        row["category"] = row.value("relativePath").toString().section('/', 0, 0);
        add(row);
    }
    return keys.values();
}
void ContentActions::retryOperation(quint64 id) {
    if (!m_failedOperations.contains(id))
        return;
    auto operation = m_failedOperations.value(id);
    if (enqueue(operation)) {
        m_failedOperations.remove(id);
        m_operations.dismiss(id);
    }
}
void ContentActions::cancelOperation(quint64 id) {
    if (id == m_activeOperationId) {
        if (cancellable())
            cancel();
        return;
    }
    for (int i = 0; i < m_pendingOperations.size(); ++i)
        if (m_pendingOperations[i].id == id) {
            m_pendingOperations.removeAt(i);
            m_operations.finish(id, true, {});
            publishProjection();
            return;
        }
}
} // namespace studio
