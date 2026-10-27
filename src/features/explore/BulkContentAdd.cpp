#include "BulkContentAdd.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include "domain/ReleaseStages.h"
#include <QPointer>
#include <QSet>
#include <algorithm>
#include <exception>
#include <utility>
namespace studio {
namespace {
QVariantMap ids(const Project &project) {
    auto result = project.toVariant().value("sourceIds").toMap();
    result[project.provider] = project.id;
    return result;
}
bool sharesProject(const QVariantMap &a, const QVariantMap &b) {
    bool matched = false;
    for (auto it = a.cbegin(); it != a.cend(); ++it)
        if (!it.value().toString().isEmpty() && !b.value(it.key()).toString().isEmpty()) {
            if (b.value(it.key()) != it.value())
                return false;
            matched = true;
        }
    return matched;
}
} // namespace
BulkContentAdd::BulkContentAdd(std::shared_ptr<ICatalogueSearch> search,
                               std::shared_ptr<IContentCatalogue> versions,
                               std::shared_ptr<PackReleasePolicy> policy, QHash<QString, QString> identities,
                               QObject *parent)
    : QObject(parent), m_search(std::move(search)), m_versions(std::move(versions)),
      m_policy(std::move(policy)), m_identities(std::move(identities)), m_model(this) {
    m_publish.setSingleShot(true);
    connect(&m_publish, &QTimer::timeout, this, [this] {
        const QPointer<BulkContentAdd> guard(this);
        m_model.setRows(rows());
        if (guard)
            emit changed();
    });
}
void BulkContentAdd::publish() {
    m_publish.start();
}
void BulkContentAdd::setInput(const QString &input) {
    if (m_input == input)
        return;
    m_input = input;
    emit inputChanged();
}
void BulkContentAdd::setCurseforgeKey(const QByteArray &key) {
    const QPointer<BulkContentAdd> guard(this);
    const auto search = m_search;
    const auto versions = m_versions;
    m_curseforge = !key.trimmed().isEmpty();
    search->setKey(key);
    if (guard)
        versions->setCurseforgeKey(key);
}
void BulkContentAdd::open(const QVariantMap &pack, const QVariantList &files, const QString &type) {
    ++m_generation;
    m_searches.clear();
    m_rows.clear();
    m_pack = pack;
    m_files = files;
    m_type = QStringList{"mod", "resourcepack", "shader", "datapack"}.contains(type) ? type : "mod";
    m_notice.clear();
    m_visible = !pack.value("path").toString().isEmpty();
    const QPointer<BulkContentAdd> guard(this);
    setInput({});
    if (guard)
        publish();
}
void BulkContentAdd::dismiss() {
    ++m_generation;
    m_searches.clear();
    m_rows.clear();
    m_visible = false;
    publish(); // Accepted install jobs keep their captured instance and continue.
}
qsizetype BulkContentAdd::find(const QString &key) const {
    for (qsizetype i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].key == key)
            return i;
    return -1;
}
bool BulkContentAdd::installed(const Project &project) const {
    const auto wanted = ids(project);
    for (const auto &value : m_files) {
        const auto file = value.toMap();
        if (!file.value("directory").toBool() &&
            QStringList{"mods", "resourcepacks", "shaderpacks", "datapacks"}.contains(
                file.value("category").toString()) &&
            sharesProject(wanted, file.value("sourceIds").toMap()))
            return true;
    }
    return false;
}
bool BulkContentAdd::locked(const Project &project) const {
    const auto wanted = ids(project);
    for (const auto &value : m_files) {
        const auto file = value.toMap();
        if (file.value("locked").toBool() && sharesProject(wanted, file.value("sourceIds").toMap()))
            return true;
    }
    for (const auto &value : m_pack.value("sourceContent").toList()) {
        const auto file = value.toMap();
        if (file.value("launcherLocked").toBool() && sharesProject(wanted, file.value("sourceIds").toMap()))
            return true;
    }
    return false;
}
void BulkContentAdd::updateState(Row &row) {
    if (row.accepted)
        return;
    row.state = row.pending > 0 ? "checking" : row.candidates.isEmpty() ? "error" : "ready";
    if (row.pending == 0 && row.candidates.isEmpty() && row.error.isEmpty())
        row.error = "No matching content found.";
    if (!row.candidates.isEmpty() && row.pending == 0) {
        row.error.clear();
        const auto &project = row.candidates.at(row.candidate);
        if (installed(project))
            row.state = "installed";
        else if (locked(project))
            row.state = "locked";
        else
            for (const auto &other : m_rows)
                if (&other != &row && other.accepted && other.state == "queued" &&
                    !other.candidates.isEmpty() &&
                    sharesProject(ids(project), ids(other.candidates.at(other.candidate)))) {
                    row.state = "queued";
                    row.accepted = true;
                    row.release = other.release;
                    break;
                }
    }
}
QVariantList BulkContentAdd::rows() const {
    QVariantList result;
    result.reserve(m_rows.size());
    for (const auto &row : m_rows) {
        auto value = row.candidates.isEmpty() ? QVariantMap{} : row.candidates.at(row.candidate).toVariant();
        value["key"] = value["contentKey"] = row.key;
        if (row.candidates.isEmpty())
            value["title"] = row.reference.raw;
        value["reference"] = row.reference.raw;
        value["state"] = row.state;
        value["error"] = row.error;
        value["selected"] = row.selected && row.state == "ready";
        value["candidateCount"] = row.candidates.size();
        value["candidateNumber"] = row.candidate + 1;
        value["canCycle"] = !row.accepted && row.pending == 0 && row.candidates.size() > 1;
        result << value;
    }
    return result;
}
int BulkContentAdd::selectedCount() const {
    return int(std::count_if(m_rows.cbegin(), m_rows.cend(),
                             [](const Row &row) { return row.selected && row.state == "ready"; }));
}
void BulkContentAdd::queueSearch(Row &row) {
    row.error.clear();
    row.candidates.clear();
    row.candidate = 0;
    QStringList providers;
    if (!row.reference.provider.isEmpty())
        providers << row.reference.provider;
    else {
        providers << "modrinth";
        if (m_curseforge)
            providers << "curseforge";
    }
    if (providers == QStringList{"curseforge"} && !m_curseforge) {
        row.state = "error";
        row.error = "Connect CurseForge in Settings to resolve this link.";
        return;
    }
    row.pending = providers.size();
    row.state = "checking";
    for (const auto &provider : providers)
        m_searches.enqueue({m_generation, row.key, provider});
}
void BulkContentAdd::resolveInput() {
    if (!m_visible)
        return;
    ++m_generation;
    m_rows.clear();
    m_searches.clear();
    const auto parsed = parseBulkContentInput(m_input, m_type);
    m_notice = parsed.error;
    for (const auto &reference : parsed.references) {
        Row row;
        row.key = QString::number(m_generation) + ':' + QString::number(m_rows.size());
        row.reference = reference;
        if (!reference.error.isEmpty()) {
            row.state = "error";
            row.error = reference.error;
        } else
            queueSearch(row);
        m_rows << std::move(row);
    }
    publish();
    QTimer::singleShot(0, this, &BulkContentAdd::pumpSearches);
}
void BulkContentAdd::pumpSearches() {
    const QPointer<BulkContentAdd> guard(this);
    for (int dispatched = 0; guard && dispatched < 4 && m_activeSearches < 4 && !m_searches.isEmpty();
         ++dispatched) {
        const auto job = m_searches.dequeue();
        const auto position = find(job.key);
        if (job.generation != m_generation || position < 0)
            continue;
        const auto reference = m_rows.at(position).reference;
        CatalogueQuery query;
        query.text = reference.query;
        query.provider = job.provider;
        query.type = reference.type;
        query.versions = {m_pack.value("minecraft").toString()};
        query.loaders = compatibleLoaders(m_pack.value("loader").toString(), m_files);
        ++m_activeSearches;
        const auto delivered = std::make_shared<bool>(false);
        auto finish = [guard, job, delivered](CataloguePage page) {
            if (!guard || std::exchange(*delivered, true))
                return;
            --guard->m_activeSearches;
            const auto index = guard->find(job.key);
            if (job.generation == guard->m_generation && index >= 0) {
                auto &row = guard->m_rows[index];
                --row.pending;
                if (!page.error.isEmpty())
                    row.error = page.error;
                for (const auto &project : page.projects)
                    if ((project.provider == job.provider || project.sourceIds.contains(job.provider)) &&
                        bulkContentMatchRank(row.reference, project) >= 0)
                        row.candidates << project;
                row.candidates = mergeProjects(row.candidates, guard->m_identities);
                std::stable_sort(row.candidates.begin(), row.candidates.end(),
                                 [&row](const Project &a, const Project &b) {
                                     const auto ar = bulkContentMatchRank(row.reference, a),
                                                br = bulkContentMatchRank(row.reference, b);
                                     if (ar != br)
                                         return ar < br;
                                     if (a.provider != b.provider)
                                         return a.provider == "modrinth";
                                     if (a.totalDownloads() != b.totalDownloads())
                                         return a.totalDownloads() > b.totalDownloads();
                                     return a.id < b.id;
                                 });
                if (row.candidates.size() > 16)
                    row.candidates.resize(16);
                guard->updateState(row);
            }
            guard->publish();
            QTimer::singleShot(0, guard, &BulkContentAdd::pumpSearches);
        };
        try {
            const auto source = m_search;
            source->fetch(query, 0, 16, finish);
        } catch (...) {
            finish({{}, 0, "Could not look up this entry. Retry it."});
        }
    }
    if (guard && m_activeSearches < 4 && !m_searches.isEmpty())
        QTimer::singleShot(0, this, &BulkContentAdd::pumpSearches);
}
void BulkContentAdd::setSelected(const QString &key, bool selected) {
    const auto index = find(key);
    if (index >= 0 && m_rows[index].state == "ready") {
        m_rows[index].selected = selected;
        publish();
    }
}
void BulkContentAdd::selectAll(bool selected) {
    for (auto &row : m_rows)
        if (row.state == "ready")
            row.selected = selected;
    publish();
}
void BulkContentAdd::nextCandidate(const QString &key) {
    const auto index = find(key);
    if (index < 0)
        return;
    auto &row = m_rows[index];
    if (row.accepted || row.pending > 0 || row.candidates.size() < 2)
        return;
    row.candidate = (row.candidate + 1) % row.candidates.size();
    updateState(row);
    publish();
}
void BulkContentAdd::retry(const QString &key) {
    const auto index = find(key);
    if (index < 0 || m_rows[index].pending > 0 || m_rows[index].accepted ||
        !m_rows[index].reference.error.isEmpty())
        return;
    if (!m_rows[index].candidates.isEmpty()) {
        m_rows[index].state = "ready";
        m_rows[index].error.clear();
        enqueueInstall(m_rows[index]);
        publish();
        QTimer::singleShot(0, this, &BulkContentAdd::pumpInstalls);
        return;
    }
    queueSearch(m_rows[index]);
    publish();
    QTimer::singleShot(0, this, &BulkContentAdd::pumpSearches);
}
void BulkContentAdd::installSelected() {
    if (!m_install)
        return;
    for (auto &row : m_rows) {
        if (!row.selected || row.state != "ready")
            continue;
        enqueueInstall(row);
    }
    publish();
    QTimer::singleShot(0, this, &BulkContentAdd::pumpInstalls);
}
void BulkContentAdd::enqueueInstall(Row &row) {
    auto project = row.candidates.at(row.candidate).toVariant();
    project["sourceIds"] = ids(row.candidates.at(row.candidate));
    for (const auto &other : m_rows)
        if (&other != &row && other.accepted && other.state == "queued" && !other.candidates.isEmpty() &&
            sharesProject(project.value("sourceIds").toMap(), ids(other.candidates.at(other.candidate)))) {
            row.state = "queued";
            row.accepted = true;
            row.release = other.release;
            return;
        }
    m_installs.enqueue({++m_nextInstall,
                        m_generation,
                        {row.key},
                        m_pack,
                        project,
                        m_files,
                        compatibleLoaders(m_pack.value("loader").toString(), m_files)});
    row.state = "queued";
    row.accepted = true;
}
void BulkContentAdd::pumpInstalls() {
    if (m_installing || m_installs.isEmpty())
        return;
    const auto job = m_installs.dequeue();
    m_installing = true;
    m_activeInstall = job.id;
    auto project = job.project;
    project["category"] = contentCategory(project.value("type", "mod").toString());
    const auto channels = effectiveReleaseStages(
        m_policy ? m_policy->channels(job.pack.value("path").toString()) : QStringList{});
    const QPointer<BulkContentAdd> guard(this);
    const auto delivered = std::make_shared<bool>(false);
    auto finish = [guard, job, channels, delivered](QVector<Release> releases, QString error) {
        if (!guard || std::exchange(*delivered, true) || !guard->m_installing ||
            guard->m_activeInstall != job.id)
            return;
        Release chosen;
        for (const auto &release : releases) {
            const auto type = job.project.value("type", "mod").toString();
            const bool mod = type == "mod" ||
                             (type == "datapack" && release.filename.endsWith(".jar", Qt::CaseInsensitive));
            if (job.project.value("sourceIds").toMap().value(release.provider).toString() ==
                    release.projectId &&
                !release.id.isEmpty() && !release.url.isEmpty() && !release.sha1.isEmpty() &&
                channels.contains(release.channel) &&
                release.gameVersions.contains(job.pack.value("minecraft").toString()) &&
                (!mod || sharesLoader(release.loaders, job.loaders))) {
                chosen = release;
                break;
            }
        }
        if (chosen.id.isEmpty() && error.isEmpty())
            error = "No compatible download matches this modpack's release stages.";
        if (!chosen.id.isEmpty())
            error.clear();
        bool accepted = false;
        if (error.isEmpty()) {
            auto target = job.project;
            target["targetPack"] = job.pack;
            target["targetRows"] = job.files;
            if (target.value("type") == "datapack" && chosen.filename.endsWith(".jar", Qt::CaseInsensitive))
                target["type"] = "mod";
            const auto install = guard->m_install;
            // Publish the immutable release before a synchronous installer completion can arrive.
            if (job.generation == guard->m_generation)
                for (auto &row : guard->m_rows)
                    if (row.accepted && !row.candidates.isEmpty() &&
                        sharesProject(ids(row.candidates.at(row.candidate)),
                                      job.project.value("sourceIds").toMap()))
                        row.release = chosen.toVariant();
            try {
                accepted = install && install(chosen.toVariant(), target);
            } catch (...) {
            }
            if (!guard)
                return;
            if (!accepted)
                error = "This content could not be queued. Check its lock or active task.";
        }
        if (job.generation == guard->m_generation && !error.isEmpty())
            for (auto &row : guard->m_rows)
                if (row.accepted && !row.candidates.isEmpty() &&
                    sharesProject(ids(row.candidates.at(row.candidate)),
                                  job.project.value("sourceIds").toMap())) {
                    row.state = "error";
                    row.error = error;
                    row.accepted = false;
                }
        guard->m_installing = false;
        guard->publish();
        QTimer::singleShot(0, guard, &BulkContentAdd::pumpInstalls);
        if (chosen.id.isEmpty() && !error.isEmpty())
            emit guard->operationFailed(job.project.value("title").toString() + ": " + error);
    };
    const auto source = m_versions;
    try {
        source->versions(project, job.pack.value("minecraft").toString(), job.loaders.join(','), finish);
    } catch (...) {
        finish({}, "Could not read compatible versions. Retry this entry.");
    }
}
void BulkContentAdd::installationFinished(const QString &path, const QVariantList &releases,
                                          const QVariantList &files, const QString &error) {
    if (!PackReleasePolicy::sameInstance(path, m_pack.value("path").toString()))
        return;
    m_files = files;
    for (auto &row : m_rows) {
        if (!row.accepted) {
            if (row.state != "error" ||
                (!row.candidates.isEmpty() && installed(row.candidates.at(row.candidate))))
                updateState(row);
            continue;
        }
        if (row.state != "queued" || row.release.isEmpty())
            continue;
        if (std::any_of(releases.cbegin(), releases.cend(), [&](const QVariant &value) {
                const auto release = value.toMap();
                return release.value("provider") == row.release.value("provider") &&
                       release.value("projectId") == row.release.value("projectId") &&
                       release.value("id") == row.release.value("id");
            })) {
            row.state = error.isEmpty() ? "installed" : "error";
            row.error = error;
            row.accepted = error.isEmpty();
        }
    }
    publish();
}
} // namespace studio
