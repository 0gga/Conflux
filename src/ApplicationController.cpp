#include "ApplicationController.h"
#include "services/InstallationDiscovery.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTextDocument>
#include <QUrl>
#include <QtConcurrent>
#include <algorithm>

namespace studio {
ApplicationController::ApplicationController(QObject* parent, QString packsRootOverride)
    : QObject(parent), m_client(this), m_installer(this), m_settings("ModpackStudio", "ModpackStudio"), m_packsRootOverride(std::move(packsRootOverride)) {
    m_client.setCurseforgeKey(qgetenv("CURSEFORGE_API_KEY"));
    connect(&m_installer, &ModInstaller::progress, this, [this](const QString& status) {
        m_installationStatus = status; emit stateChanged();
    });
    connect(&m_installer, &ModInstaller::finished, this, [this](const RepositoryResult& result) {
        m_notice = result.error;
        if (result.ok) {
            m_activePack = result.manifest.toVariantMap(); m_activePack.insert("path", result.path);
            m_activeMods = result.manifest.value("mods").toArray().toVariantList();
            m_gitStatus = result.status; m_history = result.history;
            int installed = 0;
            for (const auto& mod : m_activeMods) if (!mod.toMap().value("installPath").toString().isEmpty()) ++installed;
            m_installationStatus = QString::number(installed) + " mods installed · required Modrinth dependencies included";
        } else m_installationStatus.clear();
        refreshPacks();
    });
    QFile mappings(QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)).filePath("project-identities.json"));
    if (mappings.open(QIODevice::ReadOnly)) {
        const auto json = QJsonDocument::fromJson(mappings.readAll()).object();
        for (auto it = json.begin(); it != json.end(); ++it) if (it.value().isString() && !it.value().toString().isEmpty()) m_identities.insert(it.key(), it.value().toString());
    }
    connect(&m_client, &CatalogueClient::searchFinished, this, [this](int generation, QVector<Project> projects, const QStringList& errors) {
        if (generation != m_searchGeneration) return;
        m_catalogueBusy = false; m_projects.clear();
        std::stable_sort(projects.begin(), projects.end(), [](const Project& a, const Project& b) {
            return a.provider == "modrinth" && b.provider != "modrinth";
        });
        m_projectData = mergeProjects(projects, m_identities);
        for (const auto& project : m_projectData) m_projects.append(project.toVariant());
        m_catalogueNotice = errors.join('\n'); emit stateChanged();
    });
    connect(&m_client, &CatalogueClient::detailFinished, this, [this](int generation, const ProjectDetail& detail) {
        if (generation != m_detailGeneration) return;
        m_detailBusy = false; m_releases.clear(); m_releaseData = detail.releases;
        for (const auto& release : detail.releases) m_releases.append(release.toVariant());
        QTextDocument text;
        if (m_selectedProject.value("provider").toString() == "modrinth") text.setMarkdown(detail.description);
        else text.setHtml(detail.description);
        m_description = text.toPlainText(); m_gallery = detail.images;
        m_detailNotice = detail.errors.join('\n'); emit stateChanged();
    });
    refreshPacks();
}
QString ApplicationController::modrinthRoot() const {
    auto roaming = qEnvironmentVariable("APPDATA");
    auto fallback = QDir::homePath() + "/.config";
#ifdef Q_OS_MACOS
    fallback = QDir::homePath() + "/Library/Application Support";
#endif
    return m_settings.value("modrinthRoot", QDir(roaming.isEmpty() ? fallback : roaming).filePath("ModrinthApp")).toString();
}
QString ApplicationController::curseforgeRoot() const { return m_settings.value("curseforgeRoot", QDir::homePath() + "/curseforge/minecraft/Instances").toString(); }
QString ApplicationController::packsRoot() const {
    return m_packsRootOverride.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("packs") : m_packsRootOverride;
}
void ApplicationController::refreshPacks() {
    m_packs.clear();
    for (const auto& folder : QDir(packsRoot()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile file(QDir(folder.absoluteFilePath()).filePath("pack.json"));
        if (!file.open(QIODevice::ReadOnly)) continue;
        auto map = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        if (map.value("schemaVersion").toInt() != 1) continue;
        map.insert("path", folder.absoluteFilePath()); map.insert("modCount", map.value("mods").toList().size());
        m_packs.append(map);
    }
    emit stateChanged();
}
void ApplicationController::refreshLibrary() {
    if (m_libraryBusy) return;
    m_libraryBusy = true; emit stateChanged();
    auto* watcher = new QFutureWatcher<DiscoveryResult>(this);
    connect(watcher, &QFutureWatcher<DiscoveryResult>::finished, this, [this, watcher] {
        const auto result = watcher->result(); m_installations = result.installations; m_libraryBusy = false;
        if (!result.warnings.isEmpty()) m_notice = result.warnings.join('\n');
        watcher->deleteLater(); refreshPacks();
    });
    watcher->setFuture(QtConcurrent::run(&InstallationDiscovery::scan, modrinthRoot(), curseforgeRoot()));
}
void ApplicationController::search(const QString& query, const QString& source, const QString& game, const QString& loader) {
    closeProject();
    m_catalogueBusy = true; m_projects.clear(); m_catalogueNotice.clear();
    m_searchGame = game.trimmed(); m_searchLoader = loader;
    emit stateChanged(); m_client.search(query.trimmed(), source, m_searchGame, m_searchLoader, ++m_searchGeneration);
}
void ApplicationController::selectProject(int index) {
    if (index < 0 || index >= m_projectData.size()) return;
    m_selectedProject = m_projectData.at(index).toVariant(); m_releases.clear(); m_gallery.clear();
    m_description.clear(); m_detailNotice.clear(); m_detailBusy = true;
    emit stateChanged(); m_client.loadProject(m_projectData.at(index), m_searchGame, m_searchLoader, ++m_detailGeneration);
}
void ApplicationController::closeProject() {
    ++m_detailGeneration; m_selectedProject.clear(); m_releaseData.clear(); m_releases.clear();
    m_gallery.clear(); m_description.clear(); m_detailBusy = false; emit stateChanged();
}
void ApplicationController::runRepository(std::function<RepositoryResult()> operation, bool navigate) {
    if (repositoryBusy()) return;
    m_repositoryBusy = true; m_notice.clear(); emit stateChanged();
    auto* watcher = new QFutureWatcher<RepositoryResult>(this);
    connect(watcher, &QFutureWatcher<RepositoryResult>::finished, this, [this, watcher, navigate] {
        const auto result = watcher->result(); m_repositoryBusy = false;
        if (!result.ok) m_notice = result.error;
        else {
            m_activePack = result.manifest.toVariantMap(); m_activePack.insert("path", result.path);
            m_activeMods = result.manifest.value("mods").toArray().toVariantList();
            m_gitStatus = result.status; m_history = result.history;
            if (!result.error.isEmpty()) m_notice = result.error;
        }
        watcher->deleteLater(); refreshPacks();
        if (result.ok && navigate) emit packOpened();
    });
    watcher->setFuture(QtConcurrent::run(std::move(operation)));
}
void ApplicationController::createPack(const QString& name, const QString& game, const QString& loader) {
    const auto root = packsRoot();
    runRepository([root, name, game, loader] { return PackRepository::create(root, name, game, loader); }, true);
}
void ApplicationController::openPack(const QString& path) {
    // UI can open only packs inventoried in our own managed root.
    for (const auto& pack : m_packs) if (pack.toMap().value("path").toString() == path) {
        runRepository([path] { return PackRepository::inspect(path); }, true); return;
    }
}
void ApplicationController::pinRelease(int index) {
    if (repositoryBusy() || m_detailBusy || index < 0 || index >= m_releaseData.size() || m_activePack.isEmpty()) return;
    const auto release = m_releaseData.at(index);
    const auto path = m_activePack.value("path").toString();
    m_notice.clear();
    m_installer.start(path, release, m_selectedProject.value("title").toString(),
        QJsonObject::fromVariantMap(m_selectedProject.value("sourceIds").toMap()));
}
void ApplicationController::cancelInstallation() { m_installer.cancel(); }
void ApplicationController::removeMod(const QString& provider, const QString& projectId) {
    const auto path = m_activePack.value("path").toString();
    if (!path.isEmpty()) runRepository([path, provider, projectId] { return PackRepository::remove(path, provider, projectId); });
}
void ApplicationController::saveChanges(const QString& message) {
    const auto path = m_activePack.value("path").toString();
    if (!path.isEmpty()) runRepository([path, message] { return PackRepository::commit(path, message); });
}
void ApplicationController::setInstallationRoots(const QString& mr, const QString& cf) {
    m_settings.setValue("modrinthRoot", QDir::cleanPath(mr.trimmed()));
    m_settings.setValue("curseforgeRoot", QDir::cleanPath(cf.trimmed()));
    refreshLibrary(); emit stateChanged();
}
void ApplicationController::configureCurseforge(const QString& key) {
    m_client.setCurseforgeKey(key.trimmed().toUtf8()); emit stateChanged();
}
void ApplicationController::openFolder(const QString& path) { if (QDir(path).exists()) QDesktopServices::openUrl(QUrl::fromLocalFile(path)); }
void ApplicationController::openWeb(const QString& url) {
    const QUrl target(url);
    if (target.scheme() == "https" && (target.host() == "modrinth.com" || target.host() == "www.curseforge.com")) QDesktopServices::openUrl(target);
}
void ApplicationController::clearNotice() { m_notice.clear(); emit stateChanged(); }
}
