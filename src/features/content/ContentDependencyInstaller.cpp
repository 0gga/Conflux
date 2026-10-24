#include "ContentDependencyInstaller.h"
#include "ContentOperationState.h"
#include "domain/ContentCompatibility.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>
#include <QtConcurrent>
#include <exception>
namespace studio {
namespace {
QString projectKey(const QString &provider, const QString &project) {
    return provider + ':' + project;
}
QString label(const ContentDependency &reference) {
    return reference.provider + ':' +
           (reference.projectId.isEmpty() ? reference.versionId : reference.projectId);
}
} // namespace
ContentDependencyInstaller::ContentDependencyInstaller(std::shared_ptr<IContentInstaller> leaf,
                                                       std::shared_ptr<IContentDependencySource> source,
                                                       std::shared_ptr<const IContentFileStore> files,
                                                       QObject *parent)
    : QObject(parent), m_leaf(std::move(leaf)), m_source(std::move(source)), m_files(std::move(files)) {}
void ContentDependencyInstaller::install(const QVariantMap &pack, const QVariantList &rows,
                                         const QVector<ContentInstallRequest> &requests, Callback callback) {
    if (m_callback) {
        callback({false, "Another content operation is in progress."});
        return;
    }
    if (requests.isEmpty()) {
        callback({false, "Choose content to install first."});
        return;
    }
    ++m_generation;
    m_callback = std::move(callback);
    m_pack = pack;
    m_rows = rows;
    m_predictions.clear();
    m_optional.clear();
    m_optionalSeen.clear();
    m_cancelled = m_changed = false;
    m_errors.clear();
    m_warnings.clear();
    m_seen.clear();
    m_selected.clear();
    m_roots.clear();
    m_dependencies.clear();
    m_passthrough = true;
    for (const auto &request : requests)
        m_passthrough &= !request.resolveDependencies || request.dependenciesHandled;
    if (m_passthrough) {
        m_leafActive = true;
        const QPointer<ContentDependencyInstaller> guard(this);
        const auto generation = m_generation;
        auto completed = [guard, generation](ContentWriteResult result) mutable {
            if (!guard || guard->m_generation != generation || !guard->m_callback)
                return;
            auto callback = std::move(guard->m_callback);
            guard->m_leafActive = guard->m_passthrough = false;
            guard->m_pack.clear();
            guard->m_rows.clear();
            ++guard->m_generation;
            callback(std::move(result));
        };
        try {
            m_leaf->install(pack, rows, requests, completed);
        } catch (const std::exception &e) {
            completed({false, QString::fromUtf8(e.what())});
        } catch (...) {
            completed({false, "Could not install content."});
        }
        return;
    }
    for (const auto &request : requests)
        m_roots.enqueue(request);
    later();
}
bool ContentDependencyInstaller::cancellable() const {
    return bool(m_callback) && (m_leafActive ? m_leaf->cancellable() : true);
}
void ContentDependencyInstaller::cancel() {
    if (!cancellable())
        return;
    m_cancelled = true;
    if (m_resolving) {
        m_resolving = false;
        m_source->cancel();
        later();
    } else if (m_leafActive)
        m_leaf->cancel();
    else if (!m_reloading)
        later();
}
void ContentDependencyInstaller::later() {
    const auto generation = m_generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (m_callback && generation == m_generation)
            next();
    });
}
bool ContentDependencyInstaller::retained(const ContentDependency &reference) const {
    for (const auto &value : m_rows) {
        const auto row = value.toMap();
        const auto installed = releaseFromVariant(row.value("installedRelease").toMap());
        const bool project =
            !reference.projectId.isEmpty() &&
            row.value("sourceIds").toMap().value(reference.provider).toString() == reference.projectId;
        const bool version = !reference.versionId.isEmpty() && installed.provider == reference.provider &&
                             installed.id == reference.versionId;
        if ((!project && !version) || row.value("category") != "mods")
            continue;
        // A disabled dependency is an explicit user choice, including an older pinned edition.
        if (!row.value("enabled", true).toBool())
            return true;
        if ((reference.versionId.isEmpty() || version) &&
            installed.gameVersions.contains(m_pack.value("minecraft").toString()) &&
            sharesLoader(installed.loaders, compatibleLoaders(m_pack.value("loader").toString(), m_rows)))
            return true;
    }
    return false;
}
void ContentDependencyInstaller::expand(const ContentInstallRequest &request) {
    if (!request.resolveDependencies)
        return;
    const auto &release = request.release;
    auto add = [this, &request](ContentDependency reference) {
        if (reference.projectId.isEmpty() && reference.versionId.isEmpty()) {
            m_warnings << "A declared dependency has no project or version identity.";
            return;
        }
        const auto key = label(reference) + ':' + reference.versionId;
        if (m_seen.contains(key))
            return;
        if (m_seen.size() >= 256) {
            if (!m_warnings.contains(
                    "The dependency limit was reached; remaining dependencies were skipped."))
                m_warnings << "The dependency limit was reached; remaining dependencies were skipped.";
            return;
        }
        m_seen.insert(key);
        m_dependencies.enqueue({std::move(reference), request.allowedChannels});
    };
    for (const auto &value : release.dependencyRefs) {
        const auto ref = value.toObject();
        if ((release.provider == "modrinth" && ref.value("dependency_type") == "optional") ||
            (release.provider == "curseforge" && ref.value("relationType").toInt() == 2)) {
            const auto project = release.provider == "modrinth" ? ref.value("project_id").toString()
                                 : ref.value("modId").toInteger() > 0
                                     ? QString::number(ref.value("modId").toInteger())
                                     : QString{};
            const auto version =
                release.provider == "modrinth" ? ref.value("version_id").toString() : QString{};
            const auto key = release.provider + ':' + project + ':' + version;
            if ((!project.isEmpty() || !version.isEmpty()) && !m_optionalSeen.contains(key) &&
                m_optional.size() < 256) {
                m_optionalSeen.insert(key);
                m_optional << QVariantMap{
                    {"provider", release.provider}, {"projectId", project}, {"versionId", version}};
            }
        }
        if (release.provider == "modrinth" && ref.value("dependency_type") == "required")
            add({"modrinth", ref.value("project_id").toString(), ref.value("version_id").toString()});
        else if (release.provider == "curseforge" && ref.value("relationType").toInt() == 3) {
            const auto id = ref.value("modId").toInteger();
            add({"curseforge", id > 0 ? QString::number(id) : QString{}, {}});
        }
    }
    if (release.dependencyRefs.isEmpty())
        for (const auto &id : release.requiredDependencies)
            add({release.provider, id, {}});
}
void ContentDependencyInstaller::next() {
    if (!m_callback || m_leafActive || m_resolving || m_reloading)
        return;
    if (m_cancelled) {
        if (m_changed)
            m_warnings << "Remaining dependency installation was cancelled. Installed content was kept.";
        else
            m_errors << "Installation cancelled.";
        finish();
        return;
    }
    if (!m_roots.isEmpty()) {
        apply(m_roots.dequeue(), true);
        return;
    }
    if (m_dependencies.isEmpty()) {
        finish();
        return;
    }
    const auto pending = m_dependencies.dequeue();
    if (retained(pending.reference)) {
        later();
        return;
    }
    const auto key = projectKey(pending.reference.provider, pending.reference.projectId);
    if (!pending.reference.projectId.isEmpty() && m_selected.contains(key) &&
        (pending.reference.versionId.isEmpty() || m_selected.value(key) == pending.reference.versionId)) {
        later();
        return;
    }
    m_resolving = true;
    const QPointer<ContentDependencyInstaller> guard(this);
    const auto generation = m_generation;
    emit progress("Checking dependency " + label(pending.reference) + "...");
    if (!guard || !m_callback || m_cancelled || generation != m_generation)
        return;
    const auto completed = std::make_shared<bool>(false);
    auto callback = [guard, generation, pending, completed](ContentInstallRequest request, QString error) {
        if (!guard || generation != guard->m_generation || !guard->m_callback || *completed)
            return;
        *completed = true;
        guard->m_resolving = false;
        if (guard->m_cancelled) {
            guard->later();
            return;
        }
        const auto &reference = pending.reference;
        const auto &release = request.release;
        if (error.isEmpty() &&
            (release.provider != reference.provider || release.projectId.isEmpty() || release.id.isEmpty() ||
             (!reference.projectId.isEmpty() && release.projectId != reference.projectId) ||
             (!reference.versionId.isEmpty() && release.id != reference.versionId)))
            error = "The provider returned an unexpected dependency identity.";
        if (!error.isEmpty()) {
            guard->m_warnings << label(reference) + ": " + error;
            guard->later();
            return;
        }
        request.allowedChannels = pending.channels;
        request.resolveDependencies = true;
        request.sourceIds[release.provider] = release.projectId;
        // Exact shared bytes establish cross-platform identity without reinstalling the same mod.
        if (!release.sha1.isEmpty())
            for (const auto &value : guard->m_rows) {
                const auto row = value.toMap();
                if (row.value("category") == "mods" &&
                    row.value("sha1").toString().compare(release.sha1, Qt::CaseInsensitive) == 0) {
                    auto metadata = contentInstallPrediction(request, guard->m_pack, row);
                    auto ids = row.value("sourceIds").toMap();
                    for (auto it = request.sourceIds.begin(); it != request.sourceIds.end(); ++it)
                        ids[it.key()] = it.value().toVariant();
                    metadata["sourceIds"] = ids;
                    guard->m_predictions << metadata;
                    guard->later();
                    return;
                }
            }
        const auto key = projectKey(release.provider, release.projectId);
        if (guard->retained({release.provider, release.projectId, release.id})) {
            guard->later();
            return;
        }
        if (guard->m_selected.contains(key)) {
            if (guard->m_selected.value(key) != release.id)
                guard->m_warnings << label(reference) +
                                         ": Another version of this dependency was already selected.";
            guard->later();
            return;
        }
        guard->apply(std::move(request), false);
    };
    try {
        m_source->resolve(m_pack, m_rows, pending.reference, pending.channels, callback);
    } catch (const std::exception &e) {
        callback({}, QString::fromUtf8(e.what()));
    } catch (...) {
        callback({}, "Dependency lookup failed.");
    }
}
void ContentDependencyInstaller::apply(ContentInstallRequest request, bool root) {
    request.dependenciesHandled = true;
    m_leafActive = true;
    const QPointer<ContentDependencyInstaller> guard(this);
    const auto generation = m_generation;
    const auto completed = std::make_shared<bool>(false);
    auto callback = [guard, generation, completed, request, root](ContentWriteResult result) mutable {
        if (!guard || generation != guard->m_generation || !guard->m_callback || *completed)
            return;
        *completed = true;
        guard->m_leafActive = false;
        guard->m_warnings += result.warnings;
        if (!result.ok) {
            const auto error = request.title + ": " + result.error;
            if (root)
                guard->m_errors << error;
            else
                guard->m_warnings << error;
            if (result.changed) {
                guard->m_changed = true;
                guard->m_warnings << "Content may have changed; remaining dependencies were skipped.";
                guard->m_roots.clear();
                guard->m_dependencies.clear();
            }
            guard->later();
            return;
        }
        guard->m_changed = true;
        guard->m_selected.insert(projectKey(request.release.provider, request.release.projectId),
                                 request.release.id);
        if (!result.error.isEmpty())
            guard->m_warnings << result.error;
        // The leaf may have revalidated richer exact provider metadata (notably CurseForge).
        auto installed = request;
        QVariantList predictions = result.installedPredictions;
        if (predictions.isEmpty())
            predictions << contentInstallPrediction(request, guard->m_pack, {});
        guard->m_predictions += predictions;
        for (const auto &value : predictions) {
            const auto release = releaseFromVariant(value.toMap().value("installedRelease").toMap());
            if (release.provider == request.release.provider && release.id == request.release.id)
                installed.release = release;
        }
        const auto pack = guard->m_pack;
        const auto previous = guard->m_rows;
        const auto files = guard->m_files;
        guard->m_reloading = true;
        auto *watcher = new QFutureWatcher<ContentFileState>(guard);
        QObject::connect(watcher, &QFutureWatcher<ContentFileState>::finished, guard,
                         [guard, generation, watcher, installed, previous, predictions] {
                             auto current = watcher->result();
                             watcher->deleteLater();
                             if (!guard || generation != guard->m_generation || !guard->m_callback)
                                 return;
                             guard->m_reloading = false;
                             if (!current.error.isEmpty()) {
                                 guard->m_warnings
                                     << "Installed content was kept, but dependencies were skipped: " +
                                            current.error;
                                 guard->m_roots.clear();
                                 guard->m_dependencies.clear();
                             } else {
                                 guard->m_pack = current.pack;
                                 guard->m_rows = reconcileContentRows(current.rows, previous, predictions);
                                 guard->expand(installed);
                             }
                             guard->later();
                         });
        watcher->setFuture(QtConcurrent::run([files, pack, previous] {
            try {
                return files->reload(pack, previous);
            } catch (const std::exception &e) {
                return ContentFileState{pack, previous, QString::fromUtf8(e.what())};
            } catch (...) {
                return ContentFileState{pack, previous, "Could not refresh installed content."};
            }
        }));
    };
    try {
        m_leaf->install(m_pack, m_rows, {request}, callback);
    } catch (const std::exception &e) {
        callback({false, QString::fromUtf8(e.what())});
    } catch (...) {
        callback({false, "Could not install content."});
    }
}
void ContentDependencyInstaller::finish() {
    ContentWriteResult result{m_errors.isEmpty(), m_errors.join('\n')};
    result.changed = m_changed;
    result.warnings = m_warnings;
    result.installedPredictions = std::move(m_predictions);
    const auto optional = !m_cancelled && m_changed ? m_optional : QVariantList{};
    const auto pack = m_pack;
    const auto rows = m_rows;
    auto callback = std::move(m_callback);
    m_callback = {};
    m_pack.clear();
    m_rows.clear();
    m_roots.clear();
    m_dependencies.clear();
    m_seen.clear();
    m_selected.clear();
    m_optional.clear();
    m_optionalSeen.clear();
    ++m_generation;
    const QPointer<ContentDependencyInstaller> guard(this);
    callback(std::move(result));
    if (guard && !optional.isEmpty())
        emit optionalDependencies(pack, rows, optional);
}
} // namespace studio
