#include "OptionalDependencies.h"
#include "domain/ContentCompatibility.h"
#include "domain/ContentTypes.h"
#include <QPointer>
#include <QTimer>
#include <exception>
namespace studio {
namespace {
QSet<QString> installedIdentities(const QVariantList &files) {
    QSet<QString> result;
    for (const auto &value : files) {
        const auto row = value.toMap();
        if (row.value("directory").toBool() || row.value("category") != "mods")
            continue;
        const auto pin = row.value("installedRelease").toMap();
        const auto ids = row.value("sourceIds").toMap();
        for (auto it = ids.cbegin(); it != ids.cend(); ++it)
            if (!it.value().toString().isEmpty())
                result.insert("project:" + it.key() + ':' + it.value().toString());
        if (!pin.value("provider").toString().isEmpty() && !pin.value("id").toString().isEmpty())
            result.insert("version:" + pin.value("provider").toString() + ':' + pin.value("id").toString());
    }
    return result;
}
QSet<QString> installedHashes(const QVariantList &files) {
    QSet<QString> result;
    for (const auto &value : files) {
        const auto row = value.toMap();
        const auto hash = row.value("sha1").toString();
        if (!row.value("directory").toBool() && !hash.isEmpty())
            result.insert(row.value("category").toString() + QChar(0x1f) + hash.toLower());
    }
    return result;
}
QString hashKey(const ContentInstallRequest &request) {
    return contentCategory(request.contentType) + QChar(0x1f) + request.release.sha1.toLower();
}
bool sameRelease(const QVariantMap &value, const Release &release) {
    return value.value("provider").toString() == release.provider &&
           value.value("projectId").toString() == release.projectId &&
           value.value("id").toString() == release.id &&
           value.value("sha1").toString().compare(release.sha1, Qt::CaseInsensitive) == 0;
}
} // namespace
OptionalDependencies::OptionalDependencies(std::shared_ptr<IOptionalDependencySource> source,
                                           std::shared_ptr<InstallationPreferences> preferences,
                                           std::shared_ptr<PackReleasePolicy> policy, QObject *parent)
    : QObject(parent), m_model(this), m_source(std::move(source)), m_preferences(std::move(preferences)),
      m_policy(std::move(policy)) {
    connect(this, &OptionalDependencies::changed, this, [this] { m_model.setRows(rows()); });
    connect(m_preferences.get(), &InstallationPreferences::changed, this, [this] {
        if (!m_preferences->promptOptionalDependencies()) {
            m_offers.clear();
            dismiss();
        }
    });
    connect(m_policy.get(), &PackReleasePolicy::policyChanged, this, [this](const QString &path) {
        if (!visible() || !PackReleasePolicy::sameInstance(path, m_pack.value("path").toString()))
            return;
        ++m_generation;
        m_resolving = false;
        const QPointer<OptionalDependencies> guard(this);
        try {
            m_source->cancel();
        } catch (...) {
        }
        if (!guard)
            return;
        for (auto &row : m_rows)
            if (row.state != "queued" && row.state != "installed") {
                row.state = "waiting";
                row.error.clear();
                row.request = {};
            }
        emit changed();
        if (guard)
            later();
    });
}
QVariantList OptionalDependencies::rows() const {
    QVariantList result;
    result.reserve(m_rows.size());
    for (const auto &row : m_rows) {
        auto value = row.project;
        value["key"] = row.key;
        value["contentKey"] = row.key;
        value["provider"] =
            row.request.release.provider.isEmpty() ? row.reference.provider : row.request.release.provider;
        value["title"] =
            row.request.title.isEmpty()
                ? row.project.value("title", row.reference.projectId.isEmpty() ? row.reference.versionId
                                                                               : row.reference.projectId)
                : row.request.title;
        value["version"] = row.request.release.version;
        value["state"] = row.state;
        value["error"] = row.error;
        result << value;
    }
    return result;
}
qsizetype OptionalDependencies::find(const QString &key) const {
    for (qsizetype i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).key == key)
            return i;
    return -1;
}
void OptionalDependencies::append(const QVariantList &references) {
    const auto present = installedIdentities(m_files);
    for (const auto &value : references) {
        const auto data = value.toMap();
        ContentDependency reference{data.value("provider").toString(), data.value("projectId").toString(),
                                    data.value("versionId").toString()};
        if ((reference.provider != "modrinth" && reference.provider != "curseforge") ||
            (reference.projectId.isEmpty() && reference.versionId.isEmpty()) ||
            (!reference.projectId.isEmpty() &&
             present.contains("project:" + reference.provider + ':' + reference.projectId)) ||
            (!reference.versionId.isEmpty() &&
             present.contains("version:" + reference.provider + ':' + reference.versionId)))
            continue;
        const auto key = reference.provider + ':' + reference.projectId + ':' + reference.versionId;
        if (find(key) < 0)
            m_rows << Row{key, reference};
    }
}
void OptionalDependencies::offer(const QVariantMap &pack, const QVariantList &files,
                                 const QVariantList &references) {
    if (!m_preferences->promptOptionalDependencies() || pack.value("path").toString().isEmpty() ||
        references.isEmpty())
        return;
    if (visible() &&
        PackReleasePolicy::sameInstance(m_pack.value("path").toString(), pack.value("path").toString())) {
        m_files = files;
        append(references);
        const QPointer<OptionalDependencies> guard(this);
        emit changed();
        if (guard)
            later();
        return;
    }
    for (auto &offer : m_offers)
        if (PackReleasePolicy::sameInstance(offer.pack.value("path").toString(),
                                            pack.value("path").toString())) {
            offer.files = files;
            offer.references += references;
            return;
        }
    m_offers << Offer{pack, files, references};
    if (!visible())
        nextOffer();
}
void OptionalDependencies::nextOffer() {
    if (!m_preferences->promptOptionalDependencies() || visible())
        return;
    while (!m_offers.isEmpty()) {
        auto next = m_offers.takeFirst();
        m_pack = std::move(next.pack);
        m_files = std::move(next.files);
        append(next.references);
        if (visible()) {
            const QPointer<OptionalDependencies> guard(this);
            emit changed();
            if (guard)
                later();
            return;
        }
    }
    m_pack.clear();
    m_files.clear();
}
void OptionalDependencies::later() {
    const auto generation = m_generation;
    QTimer::singleShot(0, this, [this, generation] {
        if (m_generation == generation)
            pump();
    });
}
void OptionalDependencies::pump() {
    if (m_resolving || !visible())
        return;
    QString key;
    ContentDependency reference;
    for (auto &row : m_rows)
        if (row.state == "waiting") {
            row.state = "checking";
            key = row.key;
            reference = row.reference;
            break;
        }
    if (key.isEmpty())
        return;
    m_resolving = true;
    const auto generation = m_generation;
    const QPointer<OptionalDependencies> guard(this);
    emit changed();
    if (!guard || generation != m_generation || !m_resolving)
        return;
    auto callback = [guard, generation, key, reference](ContentInstallRequest request, QVariantMap project,
                                                        QString error) {
        if (!guard || generation != guard->m_generation)
            return;
        const auto index = guard->find(key);
        if (index < 0 || guard->m_rows.at(index).state != "checking")
            return;
        guard->m_resolving = false;
        const auto &release = request.release;
        if (error.isEmpty() &&
            (release.provider != reference.provider || release.projectId.isEmpty() || release.id.isEmpty() ||
             (!reference.projectId.isEmpty() && reference.projectId != release.projectId) ||
             (!reference.versionId.isEmpty() && reference.versionId != release.id) ||
             release.sha1.isEmpty() || release.url.isEmpty() ||
             !release.gameVersions.contains(guard->m_pack.value("minecraft").toString()) ||
             !sharesLoader(release.loaders,
                           compatibleLoaders(guard->m_pack.value("loader").toString(), guard->m_files)) ||
             !guard->m_policy->allows(guard->m_pack.value("path").toString(), release.channel)))
            error = "No compatible optional version is available.";
        auto &row = guard->m_rows[index];
        row.request = std::move(request);
        row.project = std::move(project);
        row.error = error;
        row.state = !error.isEmpty()                                                 ? "error"
                    : installedHashes(guard->m_files).contains(hashKey(row.request)) ? "installed"
                                                                                     : "ready";
        emit guard->changed();
        if (guard)
            guard->later();
    };
    try {
        m_source->resolve(m_pack, m_files, reference, m_policy->channels(m_pack.value("path").toString()),
                          callback);
    } catch (const std::exception &error) {
        callback({}, {}, QString::fromUtf8(error.what()));
    } catch (...) {
        callback({}, {}, "Could not load optional content.");
    }
}
void OptionalDependencies::retry(const QString &key) {
    const auto index = find(key);
    if (index < 0 || m_rows.at(index).state != "error")
        return;
    m_rows[index].state = "waiting";
    m_rows[index].error.clear();
    const QPointer<OptionalDependencies> guard(this);
    emit changed();
    if (guard)
        later();
}
void OptionalDependencies::install(const QString &key) {
    const auto index = find(key);
    if (index < 0 || m_rows.at(index).state != "ready" || !m_install)
        return;
    const auto row = m_rows.at(index);
    if (!m_policy->allows(m_pack.value("path").toString(), row.request.release.channel)) {
        m_rows[index].state = "error";
        m_rows[index].error = "This version is excluded by the modpack's release stages.";
        emit changed();
        return;
    }
    auto project = row.project;
    QVariantMap ids;
    for (auto it = row.request.sourceIds.begin(); it != row.request.sourceIds.end(); ++it)
        ids[it.key()] = it.value().toVariant();
    ids[row.request.release.provider] = row.request.release.projectId;
    project["sourceIds"] = ids;
    project["title"] = row.request.title;
    project["type"] = row.request.contentType;
    project["targetPack"] = m_pack;
    project["targetRows"] = m_files;
    m_rows[index].state = "queued";
    const auto install = m_install;
    const QPointer<OptionalDependencies> guard(this);
    emit changed();
    if (!guard || find(key) < 0)
        return;
    bool accepted = false;
    try {
        accepted = install(row.request.release.toVariant(), project);
    } catch (...) {
    }
    if (guard && !accepted && find(key) >= 0) {
        auto &current = m_rows[find(key)];
        current.state = "error";
        current.error = "This content could not be queued.";
        emit changed();
    }
}
void OptionalDependencies::installationFinished(const QString &path, const QVariantList &releases,
                                                const QVariantList &files, const QString &error) {
    if (!visible() || !PackReleasePolicy::sameInstance(path, m_pack.value("path").toString()))
        return;
    m_files = files;
    const auto hashes = installedHashes(files);
    bool changedRows = false;
    for (auto &row : m_rows) {
        if (row.state != "queued") {
            if (row.state == "ready" && !row.request.release.sha1.isEmpty() &&
                hashes.contains(hashKey(row.request))) {
                row.state = "installed";
                row.error.clear();
                changedRows = true;
            }
            continue;
        }
        bool completed = false;
        for (const auto &value : releases)
            completed |= sameRelease(value.toMap(), row.request.release);
        if (!completed)
            continue;
        const bool installed = hashes.contains(hashKey(row.request));
        row.state = installed ? "installed" : "error";
        row.error = installed         ? QString{}
                    : error.isEmpty() ? QString("The optional content was not installed.")
                                      : error;
        changedRows = true;
    }
    if (changedRows)
        emit changed();
}
void OptionalDependencies::dismiss() {
    ++m_generation;
    m_resolving = false;
    const QPointer<OptionalDependencies> guard(this);
    try {
        m_source->cancel();
    } catch (...) {
    }
    if (!guard)
        return;
    m_rows.clear();
    m_pack.clear();
    m_files.clear();
    emit changed();
    if (guard)
        QTimer::singleShot(0, this, &OptionalDependencies::nextOffer);
}
} // namespace studio
