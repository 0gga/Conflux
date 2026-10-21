#include "NativeContentStore.h"
#include "domain/ContentTypes.h"
#include "platform/PackFileStore.h"
#include "services/PackContent.h"
#include "services/PackRepository.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QUuid>
namespace studio {
static QString sha1File(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}
static bool contentPath(const QString &relative) {
    const auto parts = relative.split('/');
    return parts.size() == 2 &&
           QStringList{"mods", "resourcepacks", "shaderpacks", "datapacks"}.contains(parts.first()) &&
           !parts.last().isEmpty() && parts.last() != "." && parts.last() != ".." &&
           !parts.last().contains('\\');
}
NativeContentStore::NativeContentStore(std::shared_ptr<const IInstalledModIdentity> identity, QString backups,
                                       std::shared_ptr<ContentLockRegistry> locks)
    : m_identity(std::move(identity)),
      m_backupRoot(backups.isEmpty()
                       ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                             "/content-backups"
                       : std::move(backups)),
      m_locks(std::move(locks)) {}
ContentFileState NativeContentStore::reload(const QVariantMap &pack, const QVariantList &previous,
                                            bool inspectInventory) const {
    auto current = pack;
    if (pack.value("managed").toBool()) {
        const auto repository = PackRepository::inspect(pack.value("path").toString());
        if (!repository.ok)
            return {pack, {}, repository.error};
        current["mods"] = repository.manifest.value("mods").toArray().toVariantList();
        current["assets"] = repository.manifest.value("assets").toArray().toVariantList();
    }
    if (!inspectInventory)
        return {current, previous, {}};
    const auto inventory = PackContent::scan(current);
    return {current, snapshot(current, inventory.files), {}};
}
QVariantList NativeContentStore::snapshot(const QVariantMap &pack, const QVariantList &files) const {
    QVariantList rows;
    for (const auto &value : files) {
        auto row = value.toMap();
        if (row.value("category") == "configs")
            continue;
        const auto relative = row.value("relativePath").toString();
        row["enabled"] = !relative.endsWith(".disabled");
        row["external"] = true;
        if (!row.value("directory").toBool() && contentPath(relative)) {
            const auto path = NativePackFileStore::resolve(pack.value("path").toString(), relative);
            row["sha1"] = path.isEmpty() ? QString{} : sha1File(path);
            if (row.value("category") == "mods") {
                const auto identity = m_identity->inspect(pack, row);
                QVariantMap ids;
                for (auto it = identity.sourceIds.begin(); it != identity.sourceIds.end(); ++it)
                    ids[it.key()] = it.value();
                row["sourceIds"] = ids;
                row["external"] = ids.isEmpty();
                if (!ids.isEmpty()) {
                    const auto provider = ids.contains("modrinth") ? "modrinth" : "curseforge";
                    row["provider"] = provider;
                    row["projectId"] = ids.value(provider);
                    row["projectUrl"] = provider == QString("modrinth")
                                            ? "https://modrinth.com/mod/" + ids.value(provider).toString()
                                            : identity.projectUrl;
                }
            }
        }
        if (row.value("category") != "mods")
            for (const auto &value : pack.value("assets").toList()) {
                const auto pin = value.toMap();
                if (pin.value("installPath") == relative && pin.value("sha1") == row.value("sha1")) {
                    row["sourceIds"] = pin.value("projectSourceIds");
                    row["installedRelease"] = pin;
                    row["projectId"] = pin.value("projectId");
                    row["provider"] = pin.value("provider");
                    row["external"] = false;
                    row["version"] = pin.value("version");
                    row["title"] = pin.value("projectTitle", row.value("title"));
                }
            }
        rows << row;
    }
    return rows;
}
ContentWriteResult NativeContentStore::apply(const QVariantMap &pack, const QList<Edit> &edits,
                                             const QJsonArray *replacementPins,
                                             const QJsonArray *replacementAssets) const {
    if (edits.isEmpty() && !replacementPins && !replacementAssets)
        return {false, "Select content first."};
    const auto root = QFileInfo(pack.value("path").toString()).canonicalFilePath();
    if (root.isEmpty())
        return {false, "The pack folder is unavailable."};
    QJsonObject originalManifest;
    if (pack.value("managed").toBool()) {
        const auto current = PackRepository::inspect(root);
        if (!current.ok)
            return {false, current.error};
        originalManifest = current.manifest;
    }
    const auto locked = [&](const Edit &edit) {
        return m_locks && (m_locks->locked(pack.value("path").toString(), edit.from) ||
                           m_locks->locked(pack.value("path").toString(), edit.to));
    };
    for (const auto &edit : edits)
        if (locked(edit))
            return {false, "Unlock this content before changing it."};
    QSet<QString> targets, removedPaths;
    for (const auto &edit : edits)
        if (!edit.from.isEmpty() && edit.to.isEmpty())
            removedPaths.insert(edit.from);
    for (const auto &edit : edits) {
        if ((!edit.from.isEmpty() && !contentPath(edit.from)) ||
            (!edit.to.isEmpty() && !contentPath(edit.to)))
            return {false, "Unsafe content path."};
        if (!edit.from.isEmpty()) {
            const auto path = NativePackFileStore::resolve(root, edit.from);
            if (path.isEmpty() || !QFileInfo(path).isFile() || edit.expectedSha1.isEmpty() ||
                sha1File(path) != edit.expectedSha1)
                return {false, "Content changed outside Conflux. Refresh before making this change."};
        }
        if (!edit.to.isEmpty()) {
            if (targets.contains(edit.to))
                return {false, "Two files would have the same destination."};
            targets.insert(edit.to);
            const auto parent = edit.to.section('/', 0, -2);
            const auto existingParent = NativePackFileStore::resolve(root, parent);
            if (existingParent.isEmpty() && QFileInfo::exists(QDir(root).filePath(parent)))
                return {false, "The content folder is linked or unsafe."};
            if (QFileInfo::exists(QDir(root).filePath(edit.to)) && edit.to != edit.from &&
                !removedPaths.contains(edit.to))
                return {false, "A file already exists at " + edit.to + ". Remove or rename it first."};
        }
        if (!edit.source.isEmpty() &&
            (!QFileInfo(edit.source).isFile() || QFileInfo(edit.source).isSymLink() ||
             QFileInfo(edit.source).isJunction()))
            return {false, "The source file is unavailable or linked."};
    }
    const auto backup = QDir(m_backupRoot).filePath(QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(backup))
        return {false, "Could not create the content backup."};
    for (const auto &edit : edits)
        if (!edit.from.isEmpty()) {
            const auto saved = QDir(backup).filePath(edit.from);
            QDir().mkpath(QFileInfo(saved).absolutePath());
            if (!QFile::copy(QDir(root).filePath(edit.from), saved))
                return {false, "Could not back up " + edit.from + ". Nothing was changed.", backup};
        }
    QList<Edit> applied;
    auto rollback = [&] {
        bool ok = true;
        for (auto it = applied.crbegin(); it != applied.crend(); ++it) {
            if (!it->to.isEmpty() && QFileInfo::exists(QDir(root).filePath(it->to)))
                ok = QFile::remove(QDir(root).filePath(it->to)) && ok;
            if (!it->from.isEmpty())
                ok = QFile::copy(QDir(backup).filePath(it->from), QDir(root).filePath(it->from)) && ok;
        }
        return ok;
    };
    for (const auto &edit : edits) {
        if (locked(edit)) {
            const bool restored = rollback();
            return {false,
                    restored ? "Content was locked before this operation completed. Changes were restored."
                             : "Content was locked; recovery needs attention. Backups: " + backup,
                    backup};
        }
        // Recheck immediately before each change; never overwrite a new destination.
        const auto from = QDir(root).filePath(edit.from), to = QDir(root).filePath(edit.to);
        if (!edit.from.isEmpty() && sha1File(from) != edit.expectedSha1) {
            const bool restored = rollback();
            return {false,
                    restored ? "Content changed during the operation. Earlier changes were restored."
                             : "Content changed; recovery needs attention. Backups: " + backup,
                    backup};
        }
        if (!edit.to.isEmpty())
            QDir().mkpath(QFileInfo(to).absolutePath());
        bool ok = true;
        if (!edit.source.isEmpty())
            ok = QFile::copy(edit.source, to);
        else if (!edit.from.isEmpty() && !edit.to.isEmpty())
            ok = QFile::rename(from, to);
        else if (!edit.from.isEmpty())
            ok = QFile::remove(from);
        if (!ok) {
            const bool restored = rollback();
            return {false,
                    restored ? "The file operation failed. Earlier changes were restored."
                             : "Recovery needs attention. Backups: " + backup,
                    backup};
        }
        applied << edit;
    }
    if (!originalManifest.isEmpty()) {
        auto pins = originalManifest.value("mods").toArray();
        QJsonArray updated;
        for (const auto &value : pins) {
            auto pin = value.toObject();
            bool removed = false;
            for (const auto &edit : edits)
                if (pin.value("installPath").toString() == edit.from && !edit.from.isEmpty()) {
                    if (edit.to.isEmpty())
                        removed = true;
                    else {
                        pin["installPath"] = edit.to;
                        pin["enabled"] = !edit.to.endsWith(".disabled");
                    }
                }
            if (!removed)
                updated << pin;
        }
        QJsonArray assets;
        for (const auto &value : originalManifest.value("assets").toArray()) {
            auto pin = value.toObject();
            bool removed = false;
            for (const auto &edit : edits)
                if (!edit.from.isEmpty() && pin.value("installPath").toString() == edit.from) {
                    if (edit.to.isEmpty())
                        removed = true;
                    else {
                        pin["installPath"] = edit.to;
                        pin["enabled"] = !edit.to.endsWith(".disabled");
                    }
                }
            if (!removed)
                assets << pin;
        }
        const auto result = PackRepository::updateContentReferences(
            root, originalManifest, replacementPins ? *replacementPins : updated,
            replacementAssets ? *replacementAssets : assets);
        if (!result.ok) {
            const bool restored = rollback();
            return {false, result.error + (restored ? " Files were restored." : " Check backups: " + backup),
                    backup};
        }
    }
    return {true, {}, backup};
}
ContentWriteResult NativeContentStore::setEnabled(const QVariantMap &pack, const QVariantMap &row,
                                                  bool enabled) const {
    return setEnabledBatch(pack, {row}, enabled);
}
ContentWriteResult NativeContentStore::setEnabledBatch(const QVariantMap &pack, const QVariantList &rows,
                                                       bool enabled) const {
    QList<Edit> edits;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const auto path = row.value("relativePath").toString();
        if (row.value("locked").toBool())
            return {false, "Unlock this content before changing it."};
        if (row.value("directory").toBool() || row.value("sha1").toString().isEmpty())
            return {false, "Only verified content files can be enabled or disabled."};
        const bool currentlyEnabled = !path.endsWith(".disabled");
        if (currentlyEnabled == enabled)
            continue;
        const auto target = enabled ? path.chopped(9) : path + ".disabled";
        edits << Edit{path, target, {}, row.value("sha1").toString()};
    }
    return apply(pack, edits);
}
ContentWriteResult NativeContentStore::remove(const QVariantMap &pack, const QVariantList &rows) const {
    QList<Edit> edits;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        if (row.value("locked").toBool())
            return {false, "Unlock this content before removing it."};
        edits << Edit{row.value("relativePath").toString(), {}, {}, row.value("sha1").toString()};
    }
    return apply(pack, edits);
}
ContentWriteResult NativeContentStore::importFiles(const QVariantMap &pack, const QList<QUrl> &files,
                                                   const QString &category) const {
    if (!QStringList{"mods", "resourcepacks", "shaderpacks", "datapacks"}.contains(category))
        return {false, "Choose a content type."};
    QList<Edit> edits;
    for (const auto &url : files) {
        const QFileInfo file(url.toLocalFile());
        if (!url.isLocalFile() ||
            (category == "mods" ? file.suffix().toLower() != "jar" : file.suffix().toLower() != "zip"))
            return {false, "Choose JAR files for mods and ZIP files for packs."};
        edits << Edit{{}, category + '/' + file.fileName(), file.absoluteFilePath(), {}};
    }
    return apply(pack, edits);
}
ContentWriteResult NativeContentStore::applyStaged(const QVariantMap &pack, const QVariantList &expected,
                                                   const QString &stage, const QJsonObject &manifest) const {
    const auto root = pack.value("path").toString();
    QSet<QString> expectedPaths;
    for (const auto &value : expected) {
        const auto row = value.toMap();
        if (row.value("category") != "mods")
            continue;
        const auto relative = row.value("relativePath").toString();
        if (sha1File(NativePackFileStore::resolve(root, relative)) != row.value("sha1").toString())
            return {false,
                    "The installed mods changed during download. No update was applied; refresh and retry."};
        expectedPaths.insert(relative);
    }
    QSet<QString> actualPaths;
    for (const auto &name :
         QDir(QDir(root).filePath("mods")).entryList({"*.jar", "*.jar.disabled"}, QDir::Files))
        actualPaths.insert("mods/" + name);
    if (actualPaths != expectedPaths)
        return {false, "The mod list changed during download. Refresh before retrying."};
    QList<Edit> removals, additions;
    QJsonArray managedPins;
    if (pack.value("managed").toBool()) {
        const auto current = PackRepository::inspect(root);
        if (!current.ok)
            return {false, current.error};
        for (const auto &pin : current.manifest.value("mods").toArray())
            if (pin.toObject().value("provider") != "modrinth")
                managedPins << pin;
    }
    QSet<QString> replaced;
    for (const auto &value : manifest.value("mods").toArray()) {
        const auto ref = value.toObject();
        if (ref.value("provider") != "modrinth")
            continue;
        const auto hash = ref.value("sha1").toString();
        const auto source = NativePackFileStore::resolve(stage, ref.value("installPath").toString());
        if (source.isEmpty() || sha1File(source) != hash)
            return {false, "A staged mod failed final verification."};
        QVariantMap old;
        for (const auto &item : expected) {
            const auto row = item.toMap();
            if (row.value("category") != "mods")
                continue;
            if (row.value("sourceIds").toMap().value("modrinth").toString() ==
                    ref.value("projectId").toString() ||
                row.value("sha1").toString() == hash) {
                old = row;
                break;
            }
        }
        const auto filename = ref.value("filename").toString();
        if (filename.isEmpty() || QFileInfo(filename).fileName() != filename || !filename.endsWith(".jar"))
            return {false, "The provider supplied an unsafe file name."};
        const auto target = "mods/" + (pack.value("managed").toBool() ? hash + ".jar" : filename) +
                            (!old.isEmpty() && !old.value("enabled").toBool() ? ".disabled" : "");
        auto managedRef = ref;
        managedRef["installPath"] = target;
        managedRef["enabled"] = !target.endsWith(".disabled");
        managedPins << managedRef;
        if (!old.isEmpty() && old.value("sha1").toString() == hash &&
            (!pack.value("managed").toBool() || old.value("relativePath").toString() == target))
            continue;
        if (old.value("locked").toBool() ||
            (m_locks && m_locks->projectLocked(root, ref.value("provider").toString(),
                                               ref.value("projectId").toString())))
            return {false, "A locked dependency would be changed. Unlock it before installing this content."};
        if (!old.isEmpty()) {
            const auto oldPath = old.value("relativePath").toString();
            if (replaced.contains(oldPath))
                return {false, "Multiple staged projects matched one installed file."};
            replaced.insert(oldPath);
            removals << Edit{oldPath, {}, {}, old.value("sha1").toString()};
        }
        additions << Edit{{}, target, source, {}};
    }
    if (removals.isEmpty() && additions.isEmpty() && !pack.value("managed").toBool())
        return {true, {}, {}};
    return apply(pack, removals + additions, pack.value("managed").toBool() ? &managedPins : nullptr);
}
ContentWriteResult NativeContentStore::installAsset(const QVariantMap &pack, const QVariantList &rows,
                                                    const ContentInstallRequest &request,
                                                    const QString &source) const {
    const auto &release = request.release;
    const auto category = contentCategory(request.contentType);
    if (!QStringList{"resourcepacks", "shaderpacks", "datapacks"}.contains(category) ||
        QFileInfo(release.filename).fileName() != release.filename ||
        release.filename.contains(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1f]"))) ||
        !release.filename.endsWith(".zip", Qt::CaseInsensitive))
        return {false, "This content is not a supported ZIP pack."};
    const auto root = pack.value("path").toString();
    QString directory = category;
    if (sha1File(source) != release.sha1)
        return {false, "The downloaded pack failed verification."};
    QVariantMap old;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        if (row.value("category") != category ||
            (category == "datapacks" && !row.value("relativePath").toString().startsWith(directory + '/')))
            continue;
        if (row.value("sourceIds").toMap().value(release.provider).toString() == release.projectId) {
            if (!old.isEmpty())
                return {false,
                        "Multiple installed files match this project. Review Content before replacing it."};
            old = row;
        }
    }
    if (!old.isEmpty() && old.value("sha1").toString() == release.sha1)
        return {true, {}};
    if (old.value("locked").toBool() ||
        (m_locks && m_locks->projectLocked(root, release.provider, release.projectId)))
        return {false, "Unlock this content before changing it."};
    const auto target = directory + '/' + release.filename +
                        (!old.isEmpty() && !old.value("enabled").toBool() ? ".disabled" : "");
    QList<Edit> edits;
    if (!old.isEmpty())
        edits << Edit{old.value("relativePath").toString(), {}, {}, old.value("sha1").toString()};
    edits << Edit{{}, target, source, {}};
    QJsonArray pins;
    if (pack.value("managed").toBool()) {
        const auto current = PackRepository::inspect(root);
        if (!current.ok)
            return {false, current.error};
        if (current.manifest.value("assets").toArray() !=
            QJsonArray::fromVariantList(pack.value("assets").toList()))
            return {false, "The asset manifest changed during download. Refresh and retry."};
        for (const auto &value : current.manifest.value("assets").toArray()) {
            const auto ref = value.toObject();
            if (ref.value("installPath") != old.value("relativePath").toString())
                pins << ref;
        }
        auto ref = QJsonObject::fromVariantMap(release.toVariant());
        ref["category"] = category;
        ref["installPath"] = target;
        ref["projectTitle"] = request.title;
        ref["projectSourceIds"] = request.sourceIds;
        ref["enabled"] = !target.endsWith(".disabled");
        pins << ref;
    }
    return apply(pack, edits, nullptr, pack.value("managed").toBool() ? &pins : nullptr);
}
} // namespace studio
