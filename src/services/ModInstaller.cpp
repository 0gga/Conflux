#include "ModInstaller.h"
#include "domain/ContentCompatibility.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUrlQuery>
#include <QtConcurrent>

namespace studio {
ModInstaller::ModInstaller(QObject *parent, QUrl api)
    : QObject(parent), m_network(this), m_api(std::move(api)) {}
ModInstaller::~ModInstaller() {
    m_busy = false;
    ++m_generation;
    if (m_downloadFile) {
        m_downloadFile->cancelWriting();
        (void)m_downloadFile->commit();
        m_downloadFile.reset();
    }
    if (m_reply) {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
    }
    m_staging.reset();
}
void ModInstaller::fail(const QString &message) {
    if (!m_busy)
        return;
    m_busy = false;
    m_applying = false;
    ++m_generation;
    if (m_downloadFile) {
        m_downloadFile->cancelWriting();
        (void)m_downloadFile->commit();
        m_downloadFile.reset();
    }
    auto *reply = m_reply;
    m_reply = nullptr;
    if (reply)
        reply->abort();
    m_staging.reset();
    emit finished({false, message, m_path});
}
void ModInstaller::cancel() {
    if (cancellable())
        fail("Mod installation cancelled. The pack manifest was not changed.");
}
void ModInstaller::get(const QUrl &url, std::function<void(QJsonDocument)> callback) {
    const auto generation = m_generation;
    QNetworkRequest request(url);
    request.setTransferTimeout(20000);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "0gga/Conflux/0.1.0 (https://github.com/0gga/Conflux)");
    auto *reply = m_network.get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, callback = std::move(callback)] {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        if (!m_busy || generation != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 429
                     ? "Modrinth's rate limit was reached. Wait a moment, then retry."
                     : "Modrinth request failed: " + reply->errorString());
            return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError) {
            fail("Modrinth returned invalid JSON.");
            return;
        }
        callback(doc);
    });
}
void ModInstaller::start(const QString &path, const Release &release, const QString &title,
                         const QJsonObject &projectIds) {
    startBatch(path, {{release, title, projectIds}});
}
void ModInstaller::startBatch(const QString &path, const QVector<ContentInstallRequest> &requests,
                              bool allowFabricBridge) {
    if (m_busy)
        return;
    m_busy = true;
    m_applying = false;
    ++m_generation;
    m_path = path;
    m_downloadIndex = 0;
    m_resolved.clear();
    m_existing.clear();
    m_disabledReferences.clear();
    m_titles.clear();
    m_tasks.clear();
    m_references = {};
    QFile manifest(QDir(path).filePath("pack.json"));
    if (!manifest.open(QIODevice::ReadOnly)) {
        fail("Could not read this pack's manifest.");
        return;
    }
    m_expected = QJsonDocument::fromJson(manifest.readAll()).object();
    if (m_expected.value("schemaVersion").toInt() != 1) {
        fail("Unsupported pack manifest.");
        return;
    }
    if (requests.isEmpty()) {
        fail("Choose at least one release to install.");
        return;
    }
    for (const auto &request : requests)
        if (request.release.provider != "modrinth") {
            fail("CurseForge downloads will be connected once API access is approved. Modrinth downloads are "
                 "ready now.");
            return;
        }
    m_game = m_expected.value("minecraft").toString();
    m_loader = m_expected.value("loader").toString();
    m_allowedLoaders = {m_loader};
    if (allowFabricBridge && QStringList{"forge", "neoforge"}.contains(m_loader))
        m_allowedLoaders << "fabric";
    m_rootIdentities.clear();
    for (const auto &request : requests)
        m_rootIdentities[request.release.projectId] = request.sourceIds;
    m_staging = std::make_shared<QTemporaryDir>();
    if (!m_staging->isValid()) {
        fail("Could not create a temporary download folder.");
        return;
    }
    for (const auto &value : m_expected.value("mods").toArray()) {
        const auto existing = value.toObject();
        const auto provider = existing.value("provider").toString();
        const auto id = existing.value("projectId").toString();
        bool replacing = false;
        for (const auto &request : requests)
            if (id == request.sourceIds.value(provider).toString() ||
                (provider == "modrinth" && id == request.release.projectId))
                replacing = true;
        const bool disabled =
            provider == "modrinth" && existing.value("installPath").toString().endsWith(".disabled");
        if (disabled)
            m_disabledReferences[id] = existing;
        if (replacing)
            continue;
        if (disabled) {
            m_references.append(existing);
            continue;
        }
        if (provider != "modrinth") {
            m_references.append(existing);
            continue;
        }
        m_existing.insert(id, existing);
        m_tasks.enqueue({id, existing.value("id").toString()});
    }
    for (const auto &request : requests)
        m_titles.insert(request.release.projectId, request.title);
    emit progress("Resolving required dependencies…");
    for (const auto &request : requests)
        if (!addResolved(request.release))
            return;
    resolveNext();
}
bool ModInstaller::addResolved(const Release &release) {
    if (m_disabledReferences.contains(release.projectId) && !m_rootIdentities.contains(release.projectId)) {
        fail("A required dependency is disabled: " +
             m_disabledReferences.value(release.projectId).value("projectTitle").toString(release.projectId) +
             ". Enable it before retrying.");
        return false;
    }
    if (release.projectId.isEmpty() || release.id.isEmpty() || !release.gameVersions.contains(m_game) ||
        !sharesLoader(release.loaders, m_allowedLoaders)) {
        fail("A required release is incompatible with Minecraft " + m_game + " / " + m_loader +
             ". No changes were made.");
        return false;
    }
    if (!QRegularExpression("^[a-fA-F0-9]{40}$").match(release.sha1).hasMatch() ||
        !release.filename.endsWith(".jar", Qt::CaseInsensitive) || !safeDownloadUrl(QUrl(release.url))) {
        fail("A required release has no verifiable downloadable mod file.");
        return false;
    }
    if (m_resolved.contains(release.projectId)) {
        if (m_resolved.value(release.projectId).id != release.id) {
            fail("Dependency conflict: two mods require different releases of project " + release.projectId +
                 ".");
            return false;
        }
        return true;
    }
    m_resolved.insert(release.projectId, release);
    for (const auto &value : release.dependencyRefs) {
        const auto dependency = value.toObject();
        if (dependency.value("dependency_type").toString() != "required")
            continue;
        const auto project = dependency.value("project_id").toString(),
                   version = dependency.value("version_id").toString();
        if (project.isEmpty() && version.isEmpty()) {
            fail("A required dependency has no project or version identifier.");
            return false;
        }
        m_tasks.enqueue({project, version});
    }
    return true;
}
void ModInstaller::resolveNext() {
    if (!m_busy)
        return;
    if (m_tasks.isEmpty()) {
        for (const auto &release : m_resolved)
            for (const auto &value : release.dependencyRefs) {
                if (m_disabledReferences.contains(release.projectId))
                    continue;
                const auto dependency = value.toObject();
                if (dependency.value("dependency_type") == "required") {
                    for (const auto &other : m_resolved)
                        if (m_disabledReferences.contains(other.projectId) &&
                            (dependency.value("project_id").toString() == other.projectId ||
                             dependency.value("version_id").toString() == other.id)) {
                            fail("A required dependency is disabled: " + other.projectId +
                                 ". Enable it before installing these updates.");
                            return;
                        }
                }
                if (dependency.value("dependency_type").toString() != "incompatible")
                    continue;
                for (const auto &other : m_resolved) {
                    const auto project = dependency.value("project_id").toString(),
                               version = dependency.value("version_id").toString();
                    if ((!project.isEmpty() && project != other.projectId) ||
                        (!version.isEmpty() && version != other.id) ||
                        (project.isEmpty() && version.isEmpty()))
                        continue;
                    fail("Incompatible mods: " + m_titles.value(release.projectId, release.projectId) +
                         " and " + m_titles.value(other.projectId, other.projectId) + ".");
                    return;
                }
            }
        for (const auto &release : m_resolved) {
            auto reference = QJsonObject::fromVariantMap(release.toVariant());
            reference.insert("projectTitle", m_titles.value(release.projectId, release.projectId));
            reference.insert(
                "projectSourceIds",
                m_rootIdentities.value(release.projectId, QJsonObject{{"modrinth", release.projectId}}));
            bool enabled = true;
            for (const auto &value : m_expected.value("mods").toArray()) {
                const auto old = value.toObject();
                if (old.value("provider") == "modrinth" &&
                    old.value("projectId").toString() == release.projectId)
                    enabled = !old.value("installPath").toString().endsWith(".disabled");
            }
            reference.insert("enabled", enabled);
            reference.insert("installPath",
                             "mods/" + release.sha1.toLower() + ".jar" + (enabled ? "" : ".disabled"));
            m_references.append(reference);
        }
        downloadNext();
        return;
    }
    auto task = m_tasks.dequeue();
    if (m_resolved.contains(task.projectId)) {
        if (!task.versionId.isEmpty() && m_resolved.value(task.projectId).id != task.versionId) {
            fail("Dependency conflict: required version " + task.versionId +
                 " differs from the selected release of " + task.projectId + ".");
            return;
        }
        resolveNext();
        return;
    }
    if (m_existing.contains(task.projectId)) {
        const auto pinned = m_existing.value(task.projectId).value("id").toString();
        if (!task.versionId.isEmpty() && task.versionId != pinned) {
            fail("A required dependency conflicts with a version already pinned in this pack.");
            return;
        }
        task.versionId = pinned;
    }
    QUrl url;
    if (!task.versionId.isEmpty())
        url = m_api.resolved(QUrl("version/" + task.versionId));
    else {
        url = m_api.resolved(QUrl("project/" + task.projectId + "/version"));
        QUrlQuery query;
        query.addQueryItem(
            "game_versions",
            QString::fromUtf8(QJsonDocument(QJsonArray{m_game}).toJson(QJsonDocument::Compact)));
        query.addQueryItem(
            "loaders",
            QString::fromUtf8(
                QJsonDocument(QJsonArray::fromStringList(m_allowedLoaders)).toJson(QJsonDocument::Compact)));
        url.setQuery(query);
    }
    get(url, [this, task](const QJsonDocument &doc) {
        QJsonObject chosen;
        if (!task.versionId.isEmpty())
            chosen = doc.object();
        else {
            // Prefer the newest compatible stable release; use beta/alpha only if none exists.
            for (const auto &value : doc.array()) {
                const auto candidate = value.toObject();
                const auto release = modrinthRelease(candidate);
                if (!release.gameVersions.contains(m_game) ||
                    !sharesLoader(release.loaders, m_allowedLoaders))
                    continue;
                const bool stable = candidate.value("version_type").toString() == "release",
                           oldStable = chosen.value("version_type").toString() == "release";
                if (chosen.isEmpty() || (stable && !oldStable) ||
                    (stable == oldStable && candidate.value("date_published").toString() >
                                                chosen.value("date_published").toString()))
                    chosen = candidate;
            }
        }
        const auto release = modrinthRelease(chosen);
        if ((!task.projectId.isEmpty() && task.projectId != release.projectId) ||
            (!task.versionId.isEmpty() && task.versionId != release.id)) {
            fail("Modrinth returned an unexpected dependency identity.");
            return;
        }
        if (m_existing.contains(release.projectId) &&
            m_existing.value(release.projectId)
                    .value("sha1")
                    .toString()
                    .compare(release.sha1, Qt::CaseInsensitive) != 0) {
            fail("The file for a pinned release changed on Modrinth. Review that mod before retrying.");
            return;
        }
        if (!addResolved(release))
            return;
        const auto id = release.projectId;
        get(m_api.resolved(QUrl("project/" + id)), [this, id](const QJsonDocument &metadata) {
            m_titles.insert(id, metadata.object().value("title").toString(id));
            emit progress("Resolving dependencies · " + QString::number(m_resolved.size()) + " mods");
            resolveNext();
        });
    });
}
bool ModInstaller::safeDownloadUrl(const QUrl &url) const {
    if (url.scheme() == "https" && !url.host().isEmpty())
        return true;
    // Local fixture servers are allowed only when the API itself was injected as loopback.
    return m_api.host() == "127.0.0.1" && url.scheme() == "http" && url.host() == "127.0.0.1";
}
void ModInstaller::downloadNext() {
    if (!m_busy)
        return;
    if (m_downloadIndex >= m_references.size()) {
        apply();
        return;
    }
    const auto ref = m_references.at(m_downloadIndex++).toObject();
    if (ref.value("provider").toString() != "modrinth") {
        downloadNext();
        return;
    }
    const auto sha1 = ref.value("sha1").toString().toLower();
    const auto existingFile = QDir(m_path).filePath(ref.value("installPath").toString());
    if (QFileInfo::exists(existingFile)) {
        const auto generation = m_generation;
        emit progress("Checking installed " + ref.value("projectTitle").toString() + "…");
        auto *watcher = new QFutureWatcher<bool>(this);
        connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, generation] {
            const bool valid = watcher->result();
            watcher->deleteLater();
            if (!m_busy || generation != m_generation)
                return;
            if (!valid) {
                fail("An installed mod file was changed outside Conflux. It was left untouched; review it "
                     "before retrying.");
                return;
            }
            downloadNext();
        });
        watcher->setFuture(QtConcurrent::run([existingFile, ref] {
            QFile input(existingFile);
            if (QFileInfo(existingFile).isSymLink() || !input.open(QIODevice::ReadOnly))
                return false;
            const auto size = ref.value("fileSize").toInteger();
            if (size > 0 && input.size() != size)
                return false;
            const bool strong = !ref.value("sha512").toString().isEmpty();
            QCryptographicHash hash(strong ? QCryptographicHash::Sha512 : QCryptographicHash::Sha1);
            return hash.addData(&input) && QString::fromLatin1(hash.result().toHex()) ==
                                               ref.value(strong ? "sha512" : "sha1").toString().toLower();
        }));
        return;
    }
    const auto file = std::make_shared<QSaveFile>(QDir(m_staging->path()).filePath(sha1 + ".jar"));
    m_downloadFile = file;
    if (!file->open(QIODevice::WriteOnly)) {
        fail("Could not create a temporary mod file.");
        return;
    }
    const bool useSha512 = !ref.value("sha512").toString().isEmpty();
    const auto hash = std::make_shared<QCryptographicHash>(useSha512 ? QCryptographicHash::Sha512
                                                                     : QCryptographicHash::Sha1);
    const auto sha1Hash = std::make_shared<QCryptographicHash>(QCryptographicHash::Sha1);
    const auto bytesWritten = std::make_shared<qint64>(0);
    const auto expectedSize = ref.value("fileSize").toInteger();
    const auto generation = m_generation;
    QNetworkRequest request(QUrl(ref.value("url").toString()));
    request.setTransferTimeout(30000);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "0gga/Conflux/0.1.0 (https://github.com/0gga/Conflux)");
    auto *reply = m_network.get(request);
    m_reply = reply;
    const auto title = ref.value("projectTitle").toString();
    emit progress("Downloading " + title + " · " + QString::number(m_downloadIndex) + "/" +
                  QString::number(m_references.size()));
    auto consume = [this, reply, file, hash, sha1Hash, bytesWritten, expectedSize, generation] {
        if (!m_busy || m_generation != generation)
            return;
        const auto bytes = reply->readAll();
        *bytesWritten += bytes.size();
        if ((expectedSize > 0 && *bytesWritten > expectedSize) || file->write(bytes) != bytes.size()) {
            fail("A download exceeded its expected size or could not be saved.");
            return;
        }
        hash->addData(bytes);
        sha1Hash->addData(bytes);
    };
    connect(reply, &QNetworkReply::readyRead, this, consume);
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this, title, generation](qint64 received, qint64 total) {
                if (m_busy && generation == m_generation && total > 0)
                    emit progress("Downloading " + title + " · " + QString::number(received * 100 / total) +
                                  "%");
            });
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, consume, file, hash, sha1Hash, bytesWritten, expectedSize, ref, sha1, useSha512,
             generation] {
                reply->deleteLater();
                if (m_reply == reply)
                    m_reply = nullptr;
                if (!m_busy || generation != m_generation)
                    return;
                consume();
                if (!m_busy)
                    return;
                if (reply->error() != QNetworkReply::NoError) {
                    fail("Mod download failed: " + reply->errorString());
                    return;
                }
                const auto expectedHash = ref.value(useSha512 ? "sha512" : "sha1").toString().toLower();
                if ((expectedSize > 0 && *bytesWritten != expectedSize) ||
                    QString::fromLatin1(hash->result().toHex()) != expectedHash ||
                    QString::fromLatin1(sha1Hash->result().toHex()) != sha1) {
                    fail("A downloaded mod did not match its published checksum. No pack changes were made.");
                    return;
                }
                if (!file->commit()) {
                    fail("Could not finish saving a verified download.");
                    return;
                }
                m_downloadFile.reset();
                downloadNext();
            });
}
void ModInstaller::apply() {
    m_applying = true;
    emit progress("Applying verified mod files…");
    auto *watcher = new QFutureWatcher<RepositoryResult>(this);
    const auto path = m_path;
    const auto staging = m_staging;
    const auto manifest = m_expected;
    const auto refs = m_references;
    connect(watcher, &QFutureWatcher<RepositoryResult>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_busy = false;
        m_applying = false;
        m_staging.reset();
        emit finished(result);
    });
    watcher->setFuture(QtConcurrent::run([path, staging, manifest, refs] {
        return PackRepository::applyInstallation(path, manifest, refs, staging->path());
    }));
}
} // namespace studio
