#include "ContentActions.h"
#include "ContentLockIntent.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include "domain/ReleaseStages.h"
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
      m_contentVersions(m_catalogue, this), m_installer(std::move(installer)),
      m_preferences(std::move(preferences)) {
    connect(&m_operations, &OperationHistory::retryRequested, this, &ContentActions::retryOperation);
    connect(&m_operations, &OperationHistory::dismissed, this,
            [this](quint64 id) { m_failedOperations.remove(id); });
    m_executor = std::make_unique<ContentOperationExecutor>(m_files, m_installer, this);
    connect(this, &ContentActions::changed, this, &ContentActions::syncVisibleRows);
    connect(&m_contentVersions, &InstalledContentVersions::changed, this, &ContentActions::changed);
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
    m_policyRefresh.setSingleShot(true);
    m_policyRefresh.setInterval(150);
    connect(&m_policyRefresh, &QTimer::timeout, this, [this] { refresh(true); });
    clearFilters();
}
void ContentActions::setReleasePolicy(std::shared_ptr<PackReleasePolicy> policy) {
    if (m_releasePolicy)
        disconnect(m_releasePolicy.get(), nullptr, this, nullptr);
    m_releasePolicy = std::move(policy);
    if (!m_releasePolicy)
        return;
    connect(m_releasePolicy.get(), &PackReleasePolicy::policyChanged, this, [this](const QString &path) {
        if (!PackReleasePolicy::sameInstance(path, m_pack.value("path").toString()))
            return;
        ++m_generation;
        m_busy = false;
        for (auto &value : m_rows) {
            auto row = value.toMap();
            row["updateAvailable"] = false;
            row.remove("updateRelease");
            value = row;
        }
        m_checkUpdatesPending = true;
        m_policyRefresh.start();
        emit changed();
    });
}
void ContentActions::storeState(const QString &path, ContentFileState state) {
    m_sourceLocks.remember(state.pack);
    m_states.insert(path, std::move(state));
}
void ContentActions::seedState(const QString &path, ContentFileState state) {
    if (m_sourceLocks.contains(path))
        state.pack["sourceContent"] = m_sourceLocks.sourceRows(path);
    storeState(path, std::move(state));
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
    const bool changedVersionContext = changedPack || pack.value("minecraft") != m_pack.value("minecraft") ||
                                       pack.value("loader") != m_pack.value("loader");
    if (changedVersionContext)
        ++m_generation;
    m_pack = pack;
    m_sourceLocks.remember(pack);
    m_inventory = files;
    if (changedVersionContext) {
        const auto generation = m_generation;
        const QPointer<ContentActions> guard(this);
        m_contentVersions.reset();
        if (!guard || generation != m_generation)
            return;
    }
    if (changedPack) {
        m_policyRefresh.stop();
        m_selected.clear();
        m_query.clear();
        m_category = "all";
        m_rows.clear();
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
    refresh(changedVersionContext);
}
void ContentActions::refresh(bool checkUpdates) {
    m_policyRefresh.stop();
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
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this,
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
                m_sourceLocks.remember(state.pack);
                ++m_lockRevision;
                auto result = reconcileContentRows(rows, previous);
                const auto done = [guard, generation, checkUpdates](QVariantList rows, QString error) {
                    if (!guard || generation != guard->m_generation)
                        return;
                    guard->m_rows = reconcileContentRows(rows, guard->m_rows, {}, true);
                    guard->m_busy = false;
                    if (checkUpdates)
                        guard->m_checkUpdatesPending = false;
                    guard->m_notice = error;
                    guard->m_activity = error.isEmpty() ? "idle" : "error";
                    guard->applyPreferences();
                    guard->storeState(guard->m_pack.value("path").toString(),
                                      {guard->m_pack, guard->m_rows, {}});
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
                if (!guard || generation != guard->m_generation)
                    return;
                const auto requestedPack = m_pack;
                const auto lastPublished = std::make_shared<QVariantList>(result);
                const auto progress = [guard, generation, lastPublished](QVariantList indexed) {
                    if (!guard || generation != guard->m_generation)
                        return;
                    if (indexed == *lastPublished)
                        return;
                    *lastPublished = indexed;
                    guard->m_rows = reconcileContentRows(indexed, guard->m_rows, {}, true);
                    guard->applyPreferences();
                    emit guard->changed();
                };
                m_catalogue->identify(
                    requestedPack, result,
                    [guard, generation, requestedPack, checkUpdates, done, progress](QVariantList indexed,
                                                                                     QString metadataError) {
                        if (!guard || generation != guard->m_generation)
                            return;
                        if (!checkUpdates) {
                            done(std::move(indexed), metadataError);
                            return;
                        }
                        // Publish identities/authors before independent provider update requests.
                        progress(indexed);
                        if (!guard || generation != guard->m_generation)
                            return;
                        guard->m_catalogue->checkUpdates(
                            indexed, requestedPack.value("minecraft").toString(),
                            compatibleLoaders(requestedPack.value("loader").toString(), indexed).join('|'),
                            guard->m_releasePolicy
                                ? guard->m_releasePolicy->channels(requestedPack.value("path").toString())
                                : QStringList{},
                            [done, metadataError](QVariantList updated, QString updateError) {
                                QStringList errors;
                                if (!metadataError.isEmpty())
                                    errors << metadataError;
                                if (!updateError.isEmpty())
                                    errors << updateError;
                                done(std::move(updated), errors.join('\n'));
                            });
                    },
                    progress);
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
    prepareView();
    return m_view.authors();
}
QVariantList ContentActions::versionGroups() const {
    return m_contentVersions.groups();
}
void ContentActions::prepareView() const {
    const auto path = m_pack.value("path").toString();
    m_view.remember(m_rows, path, m_visibilityRevision, [this, path](const QVariantMap &row) {
        return m_visibility && m_visibility->hidden(row, path);
    });
}
QVariantList ContentActions::filteredRows() const {
    prepareView();
    return m_view.filter(m_query, m_category, m_sort, m_filters);
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
    ++m_visibilityRevision;
    if (hidden)
        m_selected.removeAll(path);
    emit changed();
}
void ContentActions::setFilter(const QString &key, const QVariant &value) {
    if (m_filters.contains(key)) {
        if (QStringList{"author", "environment", "state"}.contains(key)) {
            if (value.metaType().id() != QMetaType::QString)
                return;
            if (key == "environment" &&
                !QStringList{"", "client", "server", "both", "unknown"}.contains(value.toString()))
                return;
            if (key == "state" && !QStringList{"", "enabled", "disabled"}.contains(value.toString()))
                return;
        } else if (value.metaType().id() != QMetaType::Bool)
            return;
        if (m_filters.value(key) == value)
            return;
        m_filters[key] = value;
        emit changed();
    }
}
void ContentActions::clearFilters() {
    m_filters = {{"author", ""},        {"environment", ""},    {"state", ""},     {"updates", false},
                 {"openSource", false}, {"external", false},    {"hidden", false}, {"locked", false},
                 {"recent", false},     {"dependencies", false}};
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
    QHash<QString, bool> sourcePaths;
    for (const auto &value : pack.value("sourceContent").toList()) {
        const auto source = value.toMap();
        auto relative = source.value("relativePath").toString();
        if (relative.endsWith(".disabled"))
            relative.chop(9);
        sourcePaths[relative] = source.value("launcherLocked").toBool();
        if (source.value("launcherLocked").toBool() && !source.value("projectId").toString().isEmpty())
            sourceLocks.insert(source.value("provider", "modrinth").toString() + ':' +
                               source.value("projectId").toString());
    }
    for (auto &value : rows) {
        auto row = value.toMap();
        const auto identity = preferenceIdentity(row);
        auto relative = row.value("relativePath").toString();
        if (relative.endsWith(".disabled"))
            relative.chop(9);
        const auto ids = row.value("sourceIds").toMap();
        bool sourceLocked = pack.contains("sourceContent") ? sourcePaths.value(relative)
                                                           : row.value("launcherLocked").toBool();
        for (auto it = ids.cbegin(); it != ids.cend(); ++it)
            sourceLocked |= sourceLocks.contains(it.key() + ':' + it.value().toString());
        row["launcherLocked"] = sourceLocked;
        bool locked = row.value("launcherLocked").toBool() || m_preferences->locked(path, "path:" + relative);
        for (auto it = ids.cbegin(); !locked && it != ids.cend(); ++it)
            if (!it.value().toString().isEmpty())
                locked = m_preferences->locked(path, it.key() + ':' + it.value().toString());
        row["locked"] = row.contains("lockPending") ? row.value("lockPending").toBool() : locked;
        row["updatedAt"] = m_preferences->updatedAt(path, identity);
        row["frozen"] = m_preferences->frozen(path, identity);
        const auto updateStages =
            effectiveReleaseStages(m_releasePolicy ? m_releasePolicy->channels(path) : QStringList{});
        if (m_releasePolicy && row.value("updateAvailable").toBool() &&
            !updateStages.contains(row.value("updateRelease").toMap().value("channel").toString())) {
            row["updateAvailable"] = false;
            row.remove("updateRelease");
        }
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
void ContentActions::saveLockPreferences(const QString &pack, const QVariantList &rows, bool locked) {
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const auto ids = row.value("sourceIds").toMap();
        for (auto it = ids.cbegin(); it != ids.cend(); ++it)
            if (!it.value().toString().isEmpty())
                m_preferences->setLocked(pack, it.key() + ':' + it.value().toString(), locked);
        auto path = row.value("relativePath").toString();
        if (path.endsWith(".disabled"))
            path.chop(9);
        if (!path.isEmpty())
            m_preferences->setLocked(pack, "path:" + path, locked);
    }
}
void ContentActions::changeLocks(const QVariantMap &pack, const QVariantList &rows, bool locked,
                                 bool sourceOnly) {
    const auto intent = planContentLockIntent(pack, rows, locked, sourceOnly);
    if (intent.route == ContentLockIntent::Ignore)
        return;
    if (intent.route == ContentLockIntent::Reject) {
        reject(intent.error);
        return;
    }
    const auto path = pack.value("path").toString();
    if (intent.route == ContentLockIntent::Source) {
        if (!m_states.contains(path))
            seedState(path, {pack, rows, {}});
        enqueue(intent.operation);
        return;
    }
    saveLockPreferences(path, rows, locked);
    ++m_lockRevision;
    applyPreferences();
    if (m_states.contains(path))
        m_states[path].rows = withPreferences(pack, m_states.value(path).rows);
    syncLocks(path);
    emit changed();
}
void ContentActions::setLocked(const QString &path, bool value) {
    const auto row = find(path);
    if (!row.isEmpty())
        changeLocks(m_pack, {row}, value);
}
bool ContentActions::enqueue(ContentOperation operation) {
    if (m_pack.isEmpty() && operation.packPath.isEmpty())
        return false;
    operation.id = ++m_operationId;
    if (operation.packPath.isEmpty())
        operation.packPath = m_pack.value("path").toString();
    const auto instanceRows = operation.packPath == m_pack.value("path").toString()
                                  ? m_rows
                                  : m_states.value(operation.packPath).rows;
    operation.reservations = ContentOperationReservations::keysForTargets(
        operation, m_reservationTargets.resolve(instanceRows, operation.targets));
    if (!m_reservations.acquire(operation)) {
        reject("This content already has an active or queued operation. Wait for it to finish.");
        return false;
    }
    if (operation.packPath == m_pack.value("path").toString()) {
        if (!hasPending(operation.packPath))
            storeState(operation.packPath, {m_pack, reconcileContentRows(m_rows, m_rows),
                                            m_states.value(operation.packPath).error});
        ++m_generation;
        m_busy = false;
    }
    const auto state = m_states.value(operation.packPath);
    const auto confirmedTargets = m_confirmedTargets.resolve(state.rows, operation.targets);
    if (operation.expectedHashes.isEmpty())
        for (const auto &value : confirmedTargets) {
            const auto row = value.toMap();
            operation.expectedHashes.insert(contentRowKey(row), row.value("sha1").toString());
        }
    m_pendingOperations << operation;
    QStringList titles;
    if (!operation.predictions.isEmpty())
        for (const auto &value : operation.predictions)
            titles << value.toMap().value("title").toString();
    else
        for (const auto &value : confirmedTargets)
            titles << value.toMap().value("title").toString();
    if (titles.isEmpty() && !operation.sourceLock.isEmpty())
        titles << operation.sourceLock.value("relativePath").toString();
    m_operations.queued(operation.id, state.pack,
                        titles.size() == 1 ? titles.first()
                                           : QString::number(qMax(1, titles.size())) + " items",
                        operation.kind == ContentOperation::Install
                            ? (operation.targets.isEmpty() ? "Installing" : "Updating")
                        : operation.kind == ContentOperation::Lock    ? (operation.locked ? "Lock" : "Unlock")
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
    auto execute = [guard, operation, done](ContentFileState before) mutable {
        if (!guard)
            return;
        // Queued work uses the current policy for its original instance, even after navigation.
        for (auto &request : operation.requests) {
            auto selection =
                guard->m_releasePolicy ? guard->m_releasePolicy->channels(operation.packPath) : QStringList{};
            if (!releaseStageAllowed(request.release.channel, selection)) {
                guard->completeOperation(operation,
                                         {false, "This version is excluded by the modpack's release stages."},
                                         before, done);
                return;
            }
            request.allowedChannels = effectiveReleaseStages(selection);
        }
        before.rows = guard->withPreferences(before.pack, before.rows);
        if (before.error.isEmpty())
            guard->storeState(operation.packPath, before);
        guard->syncLocks(operation.packPath);
        guard->m_executor->execute(
            operation, before,
            [guard, operation, done](ContentWriteResult result, ContentFileState current) mutable {
                if (guard)
                    guard->completeOperation(operation, result, std::move(current), std::move(done));
            });
    };
    const auto before = m_states.value(operation.packPath);
    if (before.error.isEmpty() && !operation.dependencyCleanup) {
        execute(before);
        return;
    }
    // A transient post-write read failure must not poison every remaining item.
    // Recover a verified inventory on a worker before permitting the next write.
    auto *watcher = new QFutureWatcher<ContentFileState>(this);
    connect(watcher, &QFutureWatcher<ContentFileState>::finished, this, [watcher, execute]() mutable {
        const auto current = watcher->result();
        watcher->deleteLater();
        execute(current);
    });
    const auto files = m_files;
    watcher->setFuture(QtConcurrent::run([files, before] {
        try {
            auto current = files->reload(before.pack, before.rows, true);
            if (current.error.isEmpty())
                current.rows = reconcileContentRows(current.rows, before.rows);
            return current;
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
    const auto verificationError = result.ok ? current.error : QString{};
    if (!current.error.isEmpty() && !result.ok) {
        if (!result.error.isEmpty() && result.error != current.error)
            result.error += " ";
        if (result.error != current.error)
            result.error += current.error;
        current.rows = before.rows;
    }
    current.rows =
        reconcileContentRows(current.rows, before.rows, result.ok ? operation.predictions : QVariantList{});
    if (result.ok && operation.kind == ContentOperation::Lock)
        saveLockPreferences(operation.packPath,
                            operation.sourceLock.isEmpty()
                                ? resolveContentTargets(before.rows, operation.targets)
                                : QVariantList{operation.sourceLock},
                            false);
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
    storeState(operation.packPath, current);
    m_reservations.release(operation);
    if (m_pack.value("path") == operation.packPath) {
        m_pack = current.pack;
        m_inventory = current.rows;
    }
    for (int i = 0; i < m_pendingOperations.size(); ++i)
        if (m_pendingOperations.at(i).id == operation.id) {
            m_pendingOperations.removeAt(i);
            break;
        }
    m_anyChanged = m_anyChanged || (result.ok && operation.kind != ContentOperation::Lock) || result.changed;
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
    if (!verificationError.isEmpty()) {
        const auto message = "Changes were saved, but the content list could not be refreshed. " +
                             verificationError + " Refresh before changing more content.";
        const auto verificationId = ++m_operationId;
        m_operations.queued(verificationId, current.pack, "Content list", "Reading changes");
        m_operations.finish(verificationId, false, message, false);
        m_activity = "error";
        m_notice = message;
        emit operationFailed(message);
    }
    if (!result.warnings.isEmpty()) {
        const auto message = result.warnings.join('\n');
        const auto warningId = ++m_operationId;
        m_operations.queued(warningId, current.pack, "Dependencies", "Installing dependencies");
        m_operations.finish(warningId, false, message, false);
        m_notice = message;
        emit operationFailed(message);
    }
    m_activeOperationId = 0;
    ++m_lockRevision;
    publishProjection();
    if (current.error.isEmpty() && (result.ok || result.changed) &&
        (operation.kind == ContentOperation::Install || operation.kind == ContentOperation::Import)) {
        // Cache verified installation results even when the user leaves this instance.
        // This read-only work is independent of the mutation queue's completion.
        const QPointer<ContentActions> guard(this);
        m_catalogue->identify(
            current.pack, current.rows,
            [guard, path = operation.packPath, snapshot = current.rows](QVariantList indexed, QString error) {
                if (!guard)
                    return;
                auto accepted = guard->m_states.value(path);
                if (accepted.rows != snapshot)
                    return;
                accepted.rows = std::move(indexed);
                guard->storeState(path, std::move(accepted));
                if (guard->m_pack.value("path").toString() == path) {
                    guard->publishProjection();
                    if (!guard)
                        return;
                    if (!error.isEmpty()) {
                        guard->m_notice = error;
                        emit guard->operationFailed(error);
                    }
                }
            });
    }
    QVariantList completedReleases;
    if (operation.kind == ContentOperation::Install)
        for (const auto &request : operation.requests)
            completedReleases << request.release.toVariant();
    const QPointer<ContentActions> guard(this);
    if (operation.kind == ContentOperation::Remove)
        emit removalFinished(current.pack, before.rows, current.rows, operation.targets, result.error,
                             operation.dependencyCleanup);
    if (!guard)
        return;
    done();
    if (guard && !completedReleases.isEmpty())
        emit guard->installationFinished(operation.packPath, completedReleases, current.rows, result.error);
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
bool ContentActions::removeDependencies(const QVariantMap &pack, const QVariantList &rows) {
    const auto path = pack.value("path").toString();
    if (path.isEmpty() || rows.isEmpty() || !m_states.contains(path))
        return false;
    ContentOperation operation;
    operation.kind = ContentOperation::Remove;
    operation.packPath = path;
    operation.dependencyCleanup = true;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const auto key = contentRowKey(row);
        if (!operation.targets.contains(key))
            operation.targets << key;
        operation.expectedHashes[key] = row.value("sha1").toString();
    }
    return enqueue(std::move(operation));
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
bool ContentActions::install(const QVector<ContentInstallRequest> &requests, const QVariantMap &project) {
    if (requests.isEmpty())
        return false;
    for (const auto &request : requests) {
        const auto path = project.value("targetPack", m_pack).toMap().value("path").toString();
        if (!releaseStageAllowed(request.release.channel,
                                 m_releasePolicy ? m_releasePolicy->channels(path) : QStringList{})) {
            reject("This version is excluded by the modpack's release stages.", request.title);
            return false;
        }
        auto candidate = request.release.toVariant();
        candidate["sourceIds"] = request.sourceIds.toVariantMap();
        candidate["targetPack"] = project.value("targetPack", m_pack);
        if (projectLocked(candidate)) {
            reject("This content is locked. Unlock it before installing another version.", request.title);
            return false;
        }
    }
    ContentOperation operation;
    operation.kind = ContentOperation::Install;
    operation.requests = requests;
    const auto target = project.value("targetPack", m_pack).toMap();
    operation.packPath = target.value("path").toString();
    if (!m_states.contains(operation.packPath))
        seedState(operation.packPath, {target, project.value("targetRows").toList(), {}});
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
            match |= !request.release.sha1.isEmpty() && candidate.value("sha1").toString().compare(
                                                            request.release.sha1, Qt::CaseInsensitive) == 0;
            if (match) {
                request.resolveDependencies = false;
                request.installedPath = candidate.value("relativePath").toString();
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
        return false;
    return enqueue(operation);
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
    const auto ids = projectIds(project);
    if (m_sourceLocks.contains(path))
        return m_sourceLocks.find(path, ids);
    // A not-yet-opened target can supply metadata, but cannot overwrite accepted state.
    SourceLockIndex supplied;
    supplied.remember(target);
    return supplied.find(path, ids);
}
bool ContentActions::projectLocked(const QVariantMap &project) const {
    const auto targetPath = project.value("targetPack", m_pack).toMap().value("path").toString();
    const auto requestedIds = projectIds(project);
    for (const auto &operation : m_pendingOperations) {
        if (operation.kind != ContentOperation::Lock || operation.packPath != targetPath)
            continue;
        auto targets = resolveContentTargets(m_states.value(targetPath).rows, operation.targets);
        if (!operation.sourceLock.isEmpty())
            targets << operation.sourceLock;
        for (const auto &value : targets) {
            const auto ids = value.toMap().value("sourceIds").toMap();
            for (auto it = requestedIds.cbegin(); it != requestedIds.cend(); ++it)
                if (!it.value().toString().isEmpty() && ids.value(it.key()) == it.value())
                    return operation.locked;
        }
    }
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
    const auto target = project.value("targetPack", m_pack).toMap();
    const auto path = target.value("path").toString();
    const auto inventory = path == m_pack.value("path").toString() ? m_rows : m_states.value(path).rows;
    const auto ids = projectIds(project);
    QVariantList matches;
    for (const auto &value : inventory) {
        const auto row = value.toMap();
        const auto installed = row.value("sourceIds").toMap();
        for (auto it = ids.cbegin(); it != ids.cend(); ++it)
            if (!it.value().toString().isEmpty() && installed.value(it.key()) == it.value()) {
                matches << row;
                break;
            }
    }
    if (!matches.isEmpty()) {
        changeLocks(target, matches, locked);
        return;
    }
    const auto source = sourceLock(project);
    if (!source.isEmpty()) {
        changeLocks(target, {source}, locked, true);
        return;
    }
    if (target.value("provider") == "modrinth" || target.value("provider") == "curseforge") {
        reject("Install or restore this content before changing its source launcher lock.");
        return;
    }
    saveLockPreferences(path, {QVariantMap{{"sourceIds", ids}}}, locked);
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
            m_reservations.release(m_pendingOperations[i]);
            m_pendingOperations.removeAt(i);
            m_operations.finish(id, true, {});
            publishProjection();
            return;
        }
}
} // namespace studio
