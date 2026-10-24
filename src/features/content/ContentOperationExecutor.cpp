#include "ContentOperationExecutor.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent>
#include <exception>
namespace studio {
void ContentOperationExecutor::execute(ContentOperation operation, ContentFileState before,
                                       Callback callback) {
    auto targets = resolveContentTargets(before.rows, operation.targets);
    QString blocked = before.error;
    if (operation.kind == ContentOperation::Lock && !operation.sourceLock.isEmpty())
        targets = {operation.sourceLock};
    if (operation.sourceLock.isEmpty() && targets.size() != operation.targets.size())
        blocked = "The selected content changed before this action could run.";
    for (const auto &value : targets) {
        const auto row = value.toMap();
        const auto expected = operation.expectedHashes.constFind(contentRowKey(row));
        if (expected != operation.expectedHashes.cend() && row.value("sha1").toString() != *expected)
            blocked = "This content changed after the action was queued. Refresh before trying again.";
        else if (operation.kind != ContentOperation::Lock && row.value("locked").toBool())
            blocked = "Unlock this content before changing it.";
        else if (operation.kind == ContentOperation::Install && row.value("frozen").toBool())
            blocked = "Unfreeze this version before changing it.";
    }
    if (!blocked.isEmpty()) {
        callback({false, blocked}, before);
        return;
    }
    if (operation.kind == ContentOperation::Enable) {
        for (int i = targets.size() - 1; i >= 0; --i)
            if (targets.at(i).toMap().value("enabled").toBool() == operation.enabled)
                targets.removeAt(i);
        if (targets.isEmpty()) {
            callback({true, {}}, before);
            return;
        }
    }
    const QPointer<ContentOperationExecutor> guard(this);
    const auto files = m_files;
    const auto launcherLocks = m_launcherLocks;
    const auto started = std::make_shared<bool>(false);
    auto reload = [guard, operation, before, files, callback, started](ContentWriteResult result) mutable {
        if (!guard || *started)
            return;
        *started = true;
        auto predicted = result.ok ? projectContentOperations(before.rows, {operation}) : before.rows;
        if (result.ok)
            predicted += result.installedPredictions;
        const bool inspectInventory = !result.ok || (operation.kind != ContentOperation::Enable &&
                                                     operation.kind != ContentOperation::Remove &&
                                                     operation.kind != ContentOperation::Lock);
        if (result.ok && operation.kind == ContentOperation::Enable)
            for (auto &value : predicted) {
                auto row = value.toMap();
                if (operation.targets.contains(contentRowKey(row))) {
                    auto path = row.value("relativePath").toString();
                    if (path.endsWith(".disabled"))
                        path.chop(9);
                    if (!operation.enabled)
                        path += ".disabled";
                    row["relativePath"] = path;
                    row["fileName"] = path.section('/', -1);
                    value = row;
                }
            }
        auto *watcher = new QFutureWatcher<ContentFileState>(guard);
        QObject::connect(watcher, &QFutureWatcher<ContentFileState>::finished, guard,
                         [watcher, result, callback]() mutable {
                             auto current = watcher->result();
                             watcher->deleteLater();
                             callback(result, std::move(current));
                         });
        const auto installed = result.installedPredictions;
        watcher->setFuture(QtConcurrent::run([files, before, predicted, inspectInventory, installed] {
            try {
                auto current = files->reload(before.pack, predicted, inspectInventory);
                if (current.error.isEmpty() && !installed.isEmpty())
                    current.rows = reconcileContentRows(current.rows, {}, installed);
                return current;
            } catch (const std::exception &e) {
                return ContentFileState{before.pack, before.rows,
                                        QString("Could not verify content: ") + QString::fromUtf8(e.what())};
            } catch (...) {
                return ContentFileState{before.pack, before.rows,
                                        "Could not verify content after this operation."};
            }
        }));
    };
    if (operation.kind == ContentOperation::Install) {
        try {
            m_installer->install(before.pack, before.rows, operation.requests, reload);
        } catch (const std::exception &e) {
            reload({false, QString("Could not install content: ") + QString::fromUtf8(e.what())});
        } catch (...) {
            reload({false, "Could not install content."});
        }
        return;
    }
    auto *watcher = new QFutureWatcher<ContentWriteResult>(this);
    connect(watcher, &QFutureWatcher<ContentWriteResult>::finished, this,
            [watcher, reload = std::move(reload)]() mutable {
                const auto result = watcher->result();
                watcher->deleteLater();
                reload(result);
            });
    watcher->setFuture(QtConcurrent::run([files, launcherLocks, before, operation, targets] {
        try {
            if (operation.kind == ContentOperation::Lock)
                return launcherLocks ? launcherLocks->setLocked(before.pack, targets, operation.locked)
                                     : ContentWriteResult{
                                           false, "Source launcher lock synchronization is unavailable."};
            if (operation.kind == ContentOperation::Restore)
                return files->restoreLocked(before.pack, operation.sourceLock);
            if (operation.kind == ContentOperation::Enable)
                return files->setEnabledBatch(before.pack, targets, operation.enabled);
            if (operation.kind == ContentOperation::Remove)
                return files->remove(before.pack, targets);
            return files->importFiles(before.pack, operation.urls, operation.category);
        } catch (const std::exception &e) {
            return ContentWriteResult{false,
                                      QString("Content operation failed: ") + QString::fromUtf8(e.what())};
        } catch (...) {
            return ContentWriteResult{false, "Content operation failed."};
        }
    }));
}
} // namespace studio
