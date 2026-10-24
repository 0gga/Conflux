#include "CurseforgeInstallSource.h"
#include "domain/ContentCompatibility.h"
#include "domain/ReleaseStages.h"
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimer>
#include <QUrlQuery>

namespace studio {
CurseforgeInstallSource::CurseforgeInstallSource(QObject *parent, QUrl api)
    : QObject(parent), m_network(this), m_api(std::move(api)), m_key(qgetenv("CURSEFORGE_API_KEY")) {}
void CurseforgeInstallSource::setKey(const QByteArray &key) {
    if (m_key == key.trimmed())
        return;
    m_key = key.trimmed();
    if (busy())
        cancel();
}
void CurseforgeInstallSource::finish(QString error) {
    ++m_generation;
    auto callback = std::move(m_callback);
    m_callback = {};
    auto *reply = m_reply;
    m_reply = nullptr;
    if (reply)
        reply->abort();
    auto prepared = std::move(m_prepared);
    m_pending.clear();
    m_selected.clear();
    m_shallow.clear();
    m_rows.clear();
    if (callback)
        callback(error.isEmpty() ? prepared : QVector<ContentInstallRequest>{}, error);
}
void CurseforgeInstallSource::cancel() {
    finish("Installation cancelled before changing installed files.");
}
void CurseforgeInstallSource::get(const QString &path, std::function<void(QJsonObject)> callback) {
    if (m_key.isEmpty()) {
        finish("Connect an approved CurseForge API key in Settings.");
        return;
    }
    QNetworkRequest request(m_api.resolved(QUrl(path)));
    request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      "0gga/Conflux/0.1.0 (https://github.com/0gga/Conflux)");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("x-api-key", m_key);
    auto *reply = m_reply = m_network.get(request);
    const int generation = m_generation;
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 16 * 1024 * 1024)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, callback] {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        if (!busy() || generation != m_generation)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            finish(status == 429 ? "CurseForge rate limit reached. Retry later."
                                 : "CurseForge request failed (HTTP " + QString::number(status) + ").");
            return;
        }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(reply->readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            finish("CurseForge returned invalid file metadata.");
            return;
        }
        callback(doc.object());
    });
}
void CurseforgeInstallSource::prepare(const QVariantMap &pack, const QVariantList &rows,
                                      const QVector<ContentInstallRequest> &requests, Callback callback) {
    if (busy()) {
        callback({}, "Another content operation is in progress.");
        return;
    }
    m_callback = std::move(callback);
    ++m_generation;
    m_game = pack.value("minecraft").toString();
    m_loaders = compatibleLoaders(pack.value("loader").toString(), rows);
    m_rows = rows;
    m_prepared.clear();
    m_pending.clear();
    m_selected.clear();
    m_shallow.clear();
    for (const auto &request : requests) {
        if (request.release.provider != "curseforge") {
            m_prepared << request;
            continue;
        }
        const auto &release = request.release;
        if (!request.resolveDependencies || request.dependenciesHandled)
            m_shallow.insert(release.projectId);
        if (!QRegularExpression("^[1-9][0-9]*$").match(release.projectId).hasMatch() ||
            !QRegularExpression("^[1-9][0-9]*$").match(release.id).hasMatch()) {
            finish("This CurseForge release has no valid project or file identity.");
            return;
        }
        if (m_selected.contains(release.projectId) && m_selected.value(release.projectId) != release.id) {
            finish("Conflicting CurseForge versions were selected for one project.");
            return;
        }
        m_selected[release.projectId] = release.id;
        if (m_selected.size() > 256) {
            finish("This install has too many required dependencies.");
            return;
        }
        m_pending.enqueue(request);
    }
    next();
}
void CurseforgeInstallSource::resolveDependency(const QVariantMap &pack, const QVariantList &rows,
                                                ContentInstallRequest request, Callback callback) {
    if (busy()) {
        callback({}, "Another dependency lookup is in progress.");
        return;
    }
    if (request.release.provider != "curseforge" ||
        !QRegularExpression("^[1-9][0-9]*$").match(request.release.projectId).hasMatch() ||
        !request.release.id.isEmpty()) {
        callback({}, "This dependency has no valid CurseForge project identity.");
        return;
    }
    m_callback = std::move(callback);
    ++m_generation;
    m_game = pack.value("minecraft").toString();
    m_loaders = compatibleLoaders(pack.value("loader").toString(), rows);
    m_rows = rows;
    m_prepared.clear();
    m_pending.clear();
    m_selected.clear();
    m_shallow = {request.release.projectId};
    request.dependenciesHandled = true;
    m_pending.enqueue(std::move(request));
    next();
}
void CurseforgeInstallSource::next() {
    if (!busy())
        return;
    if (m_pending.isEmpty()) {
        for (const auto &request : m_prepared) {
            if (request.release.provider != "curseforge")
                continue;
            // Required graphs are expanded only for initial installs. Updates deliberately retain
            // the selected version and do not reconsider its dependency graph.
            if (m_shallow.contains(request.release.projectId))
                continue;
            for (const auto &value : request.release.dependencyRefs) {
                const auto dependency = value.toObject();
                if (dependency.value("relationType").toInt() != 5)
                    continue;
                const auto id = QString::number(dependency.value("modId").toInteger());
                bool conflict = m_selected.contains(id);
                for (const auto &value : m_rows) {
                    const auto row = value.toMap();
                    conflict |= row.value("enabled", true).toBool() && row.value("category") == "mods" &&
                                row.value("sourceIds").toMap().value("curseforge").toString() == id;
                }
                if (conflict) {
                    finish("This CurseForge install is incompatible with another installed project.");
                    return;
                }
            }
        }
        finish();
        return;
    }
    const auto request = m_pending.dequeue();
    const auto id = request.release.projectId;
    for (const auto &prepared : m_prepared)
        if (prepared.release.provider == "curseforge" && prepared.release.projectId == id) {
            next();
            return;
        }
    get("mods/" + id, [this, request, id](QJsonObject doc) {
        const auto project = doc.value("data").toObject();
        if (QString::number(project.value("id").toInteger()) != id ||
            project.value("gameId").toInt() != 432) {
            finish("CurseForge returned an unexpected project identity.");
            return;
        }
        if (project.value("allowModDistribution") == false || project.value("isAvailable") == false) {
            finish("The author has disabled third-party downloads for " + project.value("name").toString() +
                   ".");
            return;
        }
        if (request.release.id.isEmpty()) {
            select(request, project);
            return;
        }
        get("mods/" + id + "/files/" + request.release.id, [this, request, project](QJsonObject file) {
            auto selected = request;
            selected.title = project.value("name").toString(request.title);
            accept(selected, file.value("data").toObject());
        });
    });
}
void CurseforgeInstallSource::select(ContentInstallRequest request, const QJsonObject &project, int offset,
                                     Release best) {
    QUrlQuery query;
    query.addQueryItem("gameVersion", m_game);
    query.addQueryItem("index", QString::number(offset));
    query.addQueryItem("pageSize", "50");
    get("mods/" + request.release.projectId + "/files?" + query.toString(QUrl::FullyEncoded),
        [this, request, project, offset, best](QJsonObject doc) mutable {
            const auto files = doc.value("data").toArray();
            for (const auto &value : files) {
                auto file = value.toObject();
                const auto release = curseforgeRelease(file);
                if (!releaseStageAllowed(release.channel, request.allowedChannels) ||
                    release.projectId != request.release.projectId ||
                    !release.gameVersions.contains(m_game) || !sharesLoader(release.loaders, m_loaders) ||
                    file.value("isAvailable") == false || release.url.isEmpty())
                    continue;
                if (best.id.isEmpty() || (release.channel == "release" && best.channel != "release") ||
                    ((release.channel == "release") == (best.channel == "release") &&
                     release.published > best.published))
                    best = release;
            }
            const auto pagination = doc.value("pagination").toObject();
            if (!files.isEmpty() && offset + files.size() < pagination.value("totalCount").toInteger()) {
                if (offset >= 950) {
                    finish("Too many dependency versions to resolve safely.");
                    return;
                }
                select(request, project, offset + files.size(), best);
                return;
            }
            if (best.id.isEmpty()) {
                finish("No compatible downloadable CurseForge dependency is available for Minecraft " +
                       m_game + ".");
                return;
            }
            request.release = best;
            request.title = project.value("name").toString(request.title);
            // Re-read the exact selected file, enforcing the same distribution/identity checks as roots.
            get("mods/" + best.projectId + "/files/" + best.id,
                [this, request](QJsonObject file) { accept(request, file.value("data").toObject()); });
        });
}
void CurseforgeInstallSource::accept(ContentInstallRequest request, const QJsonObject &file) {
    auto release = curseforgeRelease(file);
    if (release.projectId != request.release.projectId || release.id != request.release.id ||
        (!request.release.sha1.isEmpty() &&
         release.sha1.compare(request.release.sha1, Qt::CaseInsensitive))) {
        finish("CurseForge returned different file metadata. Refresh and retry.");
        return;
    }
    if (!releaseStageAllowed(release.channel, request.allowedChannels)) {
        finish("A required version is excluded by the modpack's release stages. No changes were made.");
        return;
    }
    if (file.value("isAvailable") == false || release.url.isEmpty()) {
        finish("The author has not made this CurseForge file available for third-party downloads.");
        return;
    }
    if (!release.gameVersions.contains(m_game) ||
        (request.contentType == "mod" && !sharesLoader(release.loaders, m_loaders))) {
        finish("This CurseForge file does not support the instance's Minecraft version and loader.");
        return;
    }
    if (!QRegularExpression("^[a-fA-F0-9]{40}$").match(release.sha1).hasMatch()) {
        finish("This CurseForge file has no verifiable SHA1 hash.");
        return;
    }
    if (m_selected.size() >= 256 && !m_selected.contains(release.projectId)) {
        finish("This install has too many required dependencies.");
        return;
    }
    m_selected[release.projectId] = release.id;
    if (request.resolveDependencies && !request.dependenciesHandled && request.contentType == "mod") {
        for (const auto &dependency : release.requiredDependencies) {
            if (m_selected.contains(dependency))
                continue;
            bool retained = false;
            for (const auto &value : m_rows) {
                const auto row = value.toMap();
                if (row.value("sourceIds").toMap().value("curseforge").toString() != dependency ||
                    row.value("category") != "mods")
                    continue;
                if (!row.value("enabled", true).toBool()) {
                    retained = true;
                    continue;
                }
                const auto installed = releaseFromVariant(row.value("installedRelease").toMap());
                if (installed.gameVersions.contains(m_game) && sharesLoader(installed.loaders, m_loaders))
                    retained = true;
            }
            if (retained)
                continue;
            ContentInstallRequest next;
            next.allowedChannels = request.allowedChannels;
            next.release.provider = "curseforge";
            next.release.projectId = dependency;
            next.sourceIds = {{"curseforge", dependency}};
            if (m_selected.size() >= 256) {
                finish("This install has too many required dependencies.");
                return;
            }
            m_selected[dependency] = {};
            m_pending.enqueue(next);
        }
    } else if (request.resolveDependencies && !request.dependenciesHandled &&
               !release.requiredDependencies.isEmpty()) {
        finish("This ZIP requires unsupported dependencies.");
        return;
    }
    request.release = release;
    if (!request.dependenciesHandled)
        request.resolveDependencies = false;
    request.sourceIds["curseforge"] = release.projectId;
    m_prepared << request;
    // Yield between projects; network completions never synchronously walk a large graph.
    const auto generation = m_generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (generation == m_generation)
            next();
    });
}
} // namespace studio
