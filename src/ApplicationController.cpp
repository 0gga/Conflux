#include "ApplicationController.h"
#include "platform/ReviewedProjectIdentities.h"
#include "services/InstallationDiscovery.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStandardPaths>
#include <QTextDocument>
#include <QUrl>
#include <QtConcurrent>
#include <algorithm>

namespace studio {
ApplicationController::ApplicationController(QObject *parent, QString packsRootOverride,
                                             QString settingsFileOverride)
    : QObject(parent), m_client(this), m_installer(this),
      m_settings(settingsFileOverride.isEmpty()
                     ? std::make_unique<QSettings>("ModpackStudio", "ModpackStudio")
                     : std::make_unique<QSettings>(settingsFileOverride, QSettings::IniFormat)),
      m_packsRootOverride(std::move(packsRootOverride)) {
    m_client.setCurseforgeKey(qgetenv("CURSEFORGE_API_KEY"));
    connect(&m_installer, &ModInstaller::progress, this, [this](const QString &status) {
        m_installationStatus = status;
        emit stateChanged();
    });
    connect(&m_installer, &ModInstaller::finished, this, [this](const RepositoryResult &result) {
        m_notice = m_installCancelRequested && !result.ok ? QString{} : result.error;
        m_installCancelRequested = false;
        if (result.ok) {
            m_activePack = result.manifest.toVariantMap();
            m_activePack.insert("path", result.path);
            m_activePack.insert("managed", true);
            m_activePack.insert("provider", "conflux");
            m_activePack.insert("gitEnabled", result.gitEnabled);
            m_activeMods = result.manifest.value("mods").toArray().toVariantList();
            m_gitStatus = result.status;
            m_history = result.history;
            int installed = 0;
            for (const auto &mod : m_activeMods)
                if (!mod.toMap().value("installPath").toString().isEmpty())
                    ++installed;
            m_installationStatus =
                QString::number(installed) + " mods installed · required Modrinth dependencies included";
        } else
            m_installationStatus.clear();
        refreshPacks();
    });
    m_identities = ReviewedProjectIdentities::load();
    connect(&m_client, &CatalogueClient::searchFinished, this,
            [this](int generation, QVector<Project> projects, const QStringList &errors) {
                if (generation != m_searchGeneration)
                    return;
                m_catalogueBusy = false;
                m_projects.clear();
                std::stable_sort(projects.begin(), projects.end(), [](const Project &a, const Project &b) {
                    return a.provider == "modrinth" && b.provider != "modrinth";
                });
                m_projectData = mergeProjects(projects, m_identities);
                for (const auto &project : m_projectData)
                    m_projects.append(project.toVariant());
                m_catalogueNotice = errors.join('\n');
                emit stateChanged();
            });
    connect(&m_client, &CatalogueClient::detailFinished, this,
            [this](int generation, const ProjectDetail &detail) {
                if (generation != m_detailGeneration)
                    return;
                m_detailBusy = false;
                m_releases.clear();
                m_releaseData = detail.releases;
                for (const auto &release : detail.releases)
                    m_releases.append(release.toVariant());
                QTextDocument text;
                if (m_selectedProject.value("provider").toString() == "modrinth")
                    text.setMarkdown(detail.description);
                else
                    text.setHtml(detail.description);
                m_description = text.toPlainText();
                m_gallery = detail.images;
                m_detailNotice = detail.errors.join('\n');
                emit stateChanged();
            });
    refreshPacks();
}
QString ApplicationController::modrinthRoot() const {
    auto roaming = qEnvironmentVariable("APPDATA");
    auto fallback = QDir::homePath() + "/.config";
#ifdef Q_OS_MACOS
    fallback = QDir::homePath() + "/Library/Application Support";
#endif
    return m_settings
        ->value("modrinthRoot", QDir(roaming.isEmpty() ? fallback : roaming).filePath("ModrinthApp"))
        .toString();
}
QString ApplicationController::curseforgeRoot() const {
    return m_settings->value("curseforgeRoot", QDir::homePath() + "/curseforge/minecraft/Instances")
        .toString();
}
QString ApplicationController::packsRoot() const {
    return m_packsRootOverride.isEmpty()
               ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
                     .filePath("packs")
               : m_packsRootOverride;
}
void ApplicationController::refreshPacks() {
    m_packs.clear();
    for (const auto &folder :
         QDir(packsRoot()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        QFile file(QDir(folder.absoluteFilePath()).filePath("pack.json"));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        auto map = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
        if (map.value("schemaVersion").toInt() != 1)
            continue;
        map.insert("path", folder.absoluteFilePath());
        map.insert("modCount", map.value("mods").toList().size());
        map.insert("gitEnabled", QFileInfo::exists(QDir(folder.absoluteFilePath()).filePath(".git")));
        m_packs.append(map);
    }
    emit stateChanged();
}
static QString libraryKey(const QString &path) {
    auto canonical = QFileInfo(path).canonicalFilePath();
    if (canonical.isEmpty())
        canonical = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    canonical = canonical.toLower();
#endif
    return canonical;
}
QVariantList ApplicationController::libraryPacks() const {
    QVariantList result;
    QSet<QString> seen;
    const auto hidden = m_settings->value("library/hiddenPaths").toStringList();
    for (const auto &list : {m_packs, m_installations})
        for (const auto &entry : list) {
            auto row = entry.toMap();
            const bool managed = row.contains("schemaVersion");
            const auto key = libraryKey(row.value("path").toString());
            if (seen.contains(key))
                continue;
            seen.insert(key);
            if (managed) {
                row["title"] = row.value("name");
                row["gameVersion"] = row.value("minecraft");
                row["provider"] = "conflux";
            }
            row["managed"] = managed;
            row["hidden"] = hidden.contains(key);
            result.append(row);
        }
    return result;
}
int ApplicationController::visiblePackCount() const {
    int count = 0;
    for (const auto &row : libraryPacks())
        if (!row.toMap().value("hidden").toBool())
            ++count;
    return count;
}
int ApplicationController::hiddenPackCount() const {
    return libraryPacks().size() - visiblePackCount();
}
void ApplicationController::setPackHidden(const QString &path, bool hidden) {
    for (const auto &entry : libraryPacks())
        if (entry.toMap().value("path").toString() == path) {
            auto keys = m_settings->value("library/hiddenPaths").toStringList();
            const auto key = libraryKey(path);
            keys.removeAll(key);
            if (hidden)
                keys.append(key);
            m_settings->setValue("library/hiddenPaths", keys);
            emit stateChanged();
            return;
        }
}
void ApplicationController::openLibraryPack(const QString &path, bool explore) {
    if (navigationBusy())
        return;
    for (const auto &item : libraryPacks()) {
        const auto pack = item.toMap();
        if (pack.value("path").toString() != path)
            continue;
        if (pack.value("managed").toBool()) {
            m_exploreAfterOpen = explore;
            openPack(path);
            return;
        }
        m_activePack = pack;
        m_activePack["name"] = pack.value("title");
        m_activePack["minecraft"] = pack.value("gameVersion");
        // Normalize only recognized loader names; do not guess a missing game version.
        const auto raw = pack.value("loader").toString().toLower();
        for (const auto &loader : QStringList{"neoforge", "fabric", "quilt", "forge", "vanilla"})
            if (raw.startsWith(loader)) {
                m_activePack["loader"] = loader;
                break;
            }
        m_activeMods.clear();
        m_gitStatus.clear();
        m_history.clear();
        m_installationStatus.clear();
        emit stateChanged();
        emit packOpened();
        if (explore)
            emit exploreRequested();
        return;
    }
}
void ApplicationController::openContentFolder(const QString &relativePath) {
    const auto root = QFileInfo(m_activePack.value("path").toString()).canonicalFilePath();
    const auto path = QFileInfo(QDir(root).filePath(relativePath)).canonicalFilePath();
    if (!root.isEmpty() && path.startsWith(root + '/'))
        openFolder(QFileInfo(path).absolutePath());
}
void ApplicationController::refreshLibrary() {
    if (m_libraryBusy)
        return;
    m_libraryBusy = true;
    emit stateChanged();
    auto *watcher = new QFutureWatcher<DiscoveryResult>(this);
    connect(watcher, &QFutureWatcher<DiscoveryResult>::finished, this, [this, watcher] {
        const auto result = watcher->result();
        m_installations = result.installations;
        m_libraryBusy = false;
        if (!result.warnings.isEmpty())
            m_notice = result.warnings.join('\n');
        watcher->deleteLater();
        refreshPacks();
    });
    watcher->setFuture(QtConcurrent::run(&InstallationDiscovery::scan, modrinthRoot(), curseforgeRoot()));
}
void ApplicationController::refreshActivePack() {
    const auto path = m_activePack.value("path").toString();
    if (m_activePack.value("managed").toBool() && !path.isEmpty()) {
        const int generation = ++m_activeRefreshGeneration;
        m_activeRefreshPending = true;
        auto *watcher = new QFutureWatcher<RepositoryResult>(this);
        connect(watcher, &QFutureWatcher<RepositoryResult>::finished, this,
                [this, watcher, path, generation] {
                    const auto result = watcher->result();
                    watcher->deleteLater();
                    if (generation != m_activeRefreshGeneration)
                        return;
                    m_activeRefreshPending = false;
                    if (m_activePack.value("path") != path || m_repositoryBusy || m_contentBusy)
                        return;
                    if (!result.ok) {
                        reportContentError(result.error);
                        return;
                    }
                    m_activePack = result.manifest.toVariantMap();
                    m_activePack["path"] = path;
                    m_activePack["managed"] = true;
                    m_activePack["provider"] = "conflux";
                    m_activePack["gitEnabled"] = result.gitEnabled;
                    m_activeMods = result.manifest.value("mods").toArray().toVariantList();
                    m_gitStatus = result.status;
                    m_history = result.history;
                    emit stateChanged();
                });
        const bool historyEnabled = gitEnabled();
        watcher->setFuture(QtConcurrent::run(
            [path, historyEnabled] { return PackRepository::inspect(path, historyEnabled); }));
    }
    refreshLibrary();
}
void ApplicationController::search(const QString &query, const QString &source, const QString &game,
                                   const QString &loader, const QString &type) {
    closeProject();
    m_catalogueBusy = true;
    m_projects.clear();
    m_catalogueNotice.clear();
    m_searchGame = game.trimmed();
    m_searchLoader = loader;
    emit stateChanged();
    m_client.search(query.trimmed(), source, m_searchGame, m_searchLoader, ++m_searchGeneration, type);
}
void ApplicationController::selectProject(int index) {
    if (index < 0 || index >= m_projectData.size())
        return;
    m_selectedProject = m_projectData.at(index).toVariant();
    m_releases.clear();
    m_gallery.clear();
    m_description.clear();
    m_detailNotice.clear();
    m_detailBusy = true;
    emit stateChanged();
    m_client.loadProject(m_projectData.at(index), m_searchGame, m_searchLoader, ++m_detailGeneration);
}
void ApplicationController::closeProject() {
    ++m_detailGeneration;
    m_selectedProject.clear();
    m_releaseData.clear();
    m_releases.clear();
    m_gallery.clear();
    m_description.clear();
    m_detailBusy = false;
    emit stateChanged();
}
void ApplicationController::runRepository(std::function<RepositoryResult()> operation, bool navigate) {
    if (navigationBusy() || (!navigate && m_contentBusy))
        return;
    ++m_activeRefreshGeneration;
    m_activeRefreshPending = false;
    m_repositoryBusy = true;
    m_notice.clear();
    emit stateChanged();
    auto *watcher = new QFutureWatcher<RepositoryResult>(this);
    connect(watcher, &QFutureWatcher<RepositoryResult>::finished, this, [this, watcher, navigate] {
        const auto result = watcher->result();
        m_repositoryBusy = false;
        if (!result.ok)
            m_notice = result.error;
        else {
            m_activePack = result.manifest.toVariantMap();
            m_activePack.insert("path", result.path);
            m_activePack.insert("managed", true);
            m_activePack.insert("provider", "conflux");
            m_activePack.insert("gitEnabled", result.gitEnabled);
            m_activeMods = result.manifest.value("mods").toArray().toVariantList();
            m_gitStatus = result.status;
            m_history = result.history;
            if (!result.error.isEmpty())
                m_notice = result.error;
        }
        watcher->deleteLater();
        refreshPacks();
        if (result.ok && navigate) {
            emit packOpened();
            if (m_exploreAfterOpen)
                emit exploreRequested();
        }
        m_exploreAfterOpen = false;
    });
    watcher->setFuture(QtConcurrent::run(std::move(operation)));
}
void ApplicationController::createPack(const QString &name, const QString &game, const QString &loader) {
    const auto root = packsRoot();
    const bool track = gitEnabled();
    const auto author = gitAuthorName(), email = gitAuthorEmail();
    runRepository(
        [root, name, game, loader, track, author, email] {
            auto created = PackRepository::create(root, name, game, loader);
            if (!created.ok || !track)
                return created;
            const auto tracked = PackRepository::enableGit(created.path, author, email);
            if (tracked.ok)
                return tracked;
            created.error = "The modpack was created, but Git setup failed: " + tracked.error;
            return created;
        },
        true);
}
void ApplicationController::configureGit(bool enabled, const QString &name, const QString &email) {
    const auto error = enabled ? PackRepository::gitIdentityError(name, email) : QString{};
    if (!error.isEmpty()) {
        reportContentError(error);
        return;
    }
    ++m_activeRefreshGeneration;
    m_activeRefreshPending = false;
    m_settings->setValue("git/enabled", enabled);
    m_settings->setValue("git/authorName", name.trimmed());
    m_settings->setValue("git/authorEmail", email.trimmed());
    m_notice.clear();
    if (!enabled) {
        m_gitStatus.clear();
        m_history.clear();
    }
    emit gitConfigurationChanged();
    emit stateChanged();
}
void ApplicationController::enablePackGit() {
    if (!gitEnabled() || !m_activePack.value("managed").toBool())
        return;
    const auto path = m_activePack.value("path").toString();
    const auto author = gitAuthorName(), email = gitAuthorEmail();
    for (const auto &pack : m_packs)
        if (pack.toMap().value("path").toString() == path) {
            runRepository([path, author, email] { return PackRepository::enableGit(path, author, email); });
            return;
        }
}
void ApplicationController::openPack(const QString &path) {
    // UI can open only packs inventoried in our own managed root.
    for (const auto &pack : m_packs)
        if (pack.toMap().value("path").toString() == path) {
            const bool historyEnabled = gitEnabled();
            runRepository([path, historyEnabled] { return PackRepository::inspect(path, historyEnabled); },
                          true);
            return;
        }
}
void ApplicationController::pinRelease(int index) {
    if (repositoryBusy() || m_detailBusy || index < 0 || index >= m_releaseData.size() ||
        !m_activePack.value("managed").toBool())
        return;
    const auto release = m_releaseData.at(index);
    const auto path = m_activePack.value("path").toString();
    m_notice.clear();
    m_installCancelRequested = false;
    m_installer.start(path, release, m_selectedProject.value("title").toString(),
                      QJsonObject::fromVariantMap(m_selectedProject.value("sourceIds").toMap()));
}
void ApplicationController::cancelInstallation() {
    if (!m_installer.cancellable())
        return;
    m_installCancelRequested = true;
    m_installer.cancel();
}
void ApplicationController::removeMod(const QString &provider, const QString &projectId) {
    if (!m_activePack.value("managed").toBool())
        return;
    const auto path = m_activePack.value("path").toString();
    if (!path.isEmpty())
        runRepository(
            [path, provider, projectId] { return PackRepository::remove(path, provider, projectId); });
}
void ApplicationController::saveChanges(const QString &message) {
    if (!gitEnabled() || !m_activePack.value("managed").toBool())
        return;
    const auto path = m_activePack.value("path").toString();
    if (!path.isEmpty())
        runRepository([path, message] { return PackRepository::commit(path, message); });
}
void ApplicationController::setInstallationRoots(const QString &mr, const QString &cf) {
    m_settings->setValue("modrinthRoot", QDir::cleanPath(mr.trimmed()));
    m_settings->setValue("curseforgeRoot", QDir::cleanPath(cf.trimmed()));
    refreshLibrary();
    emit stateChanged();
}
void ApplicationController::configureCurseforge(const QString &key) {
    emit curseforgeKeyConfigured(key.trimmed().toUtf8());
    m_client.setCurseforgeKey(key.trimmed().toUtf8());
    emit stateChanged();
}
void ApplicationController::openFolder(const QString &path) {
    if (QDir(path).exists())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}
void ApplicationController::openWeb(const QString &url) {
    const QUrl target(url);
    if (target.scheme() == "https" &&
        (target.host() == "modrinth.com" || target.host() == "www.curseforge.com"))
        QDesktopServices::openUrl(target);
}
void ApplicationController::clearNotice() {
    m_notice.clear();
    emit stateChanged();
}
} // namespace studio
