#include "NativeContentStore.h"
#include "ContentAccess.h"
#include "ContentFileCache.h"
#include "LauncherContentState.h"
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
static QString contentKey(const QString &path) {
#ifdef Q_OS_WIN
    return path.toCaseFolded();
#else
    return path;
#endif
}
static QString sha1File(const QString &path) {
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    if (!hash.addData(&file))
        return {};
    return QString::fromLatin1(hash.result().toHex());
}
static bool atomicCopy(const QString &from, const QString &to) {
    QFile input(from);
    QSaveFile output(to);
    output.setDirectWriteFallback(false);
    if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly))
        return false;
    while (!input.atEnd()) {
        const auto bytes = input.read(1024 * 1024);
        if (bytes.isEmpty() && input.error() != QFileDevice::NoError)
            return false;
        if (output.write(bytes) != bytes.size())
            return false;
    }
    return output.commit();
}
static bool contentPath(const QString &relative) {
    static const QSet<QString> categories{"mods", "resourcepacks", "shaderpacks", "datapacks"};
    static const QRegularExpression invalid("[<>:\"\\\\|?*\\x00-\\x1f]");
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",
                                             QRegularExpression::CaseInsensitiveOption);
    const auto parts = relative.split('/');
    return parts.size() == 2 && categories.contains(parts.first()) && !parts.last().isEmpty() &&
           parts.last() != "." && parts.last() != ".." && !parts.last().endsWith('.') &&
           !parts.last().endsWith(' ') && !parts.last().contains(invalid) &&
           !reserved.match(parts.last()).hasMatch();
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
    ContentAccess access(pack.value("path").toString());
    if (!access.ok())
        return {pack, {}, access.error()};
    auto current = pack;
    QString policyError;
    current["sourceContent"] = LauncherContentState::read(pack, &policyError);
    if (!policyError.isEmpty())
        return {pack, {}, policyError};
    if (pack.value("managed").toBool()) {
        const auto repository = PackRepository::inspectContent(pack.value("path").toString());
        if (!repository.ok)
            return {pack, {}, repository.error};
        current["mods"] = repository.manifest.value("mods").toArray().toVariantList();
        current["assets"] = repository.manifest.value("assets").toArray().toVariantList();
    }
    if (!inspectInventory)
        return {current, previous, {}};
    return {current, snapshot(current, PackContent::contentFiles(current)), {}};
}
QVariantList NativeContentStore::snapshot(const QVariantMap &pack, const QVariantList &files) const {
    ContentAccess access(pack.value("path").toString());
    if (!access.ok())
        return {};
    const auto sourceRows = pack.contains("sourceContent") ? pack.value("sourceContent").toList()
                                                           : LauncherContentState::read(pack);
    QHash<QString, QVariantMap> sourceIndex;
    for (const auto &value : sourceRows) {
        const auto row = value.toMap();
        const auto path = row.value("relativePath").toString();
        sourceIndex[path + '\n' + row.value("sha1").toString()] = row;
        if (!path.endsWith(".disabled", Qt::CaseInsensitive))
            sourceIndex[path + ".disabled\n" + row.value("sha1").toString()] = row;
    }
    QVariantList rows;
    for (const auto &value : files) {
        auto row = value.toMap();
        if (row.value("category") == "configs")
            continue;
        const auto relative = row.value("relativePath").toString();
        row["enabled"] = !relative.endsWith(".disabled", Qt::CaseInsensitive);
        row["external"] = true;
        if (!row.value("directory").toBool() && contentPath(relative)) {
            const auto path = NativePackFileStore::resolve(pack.value("path").toString(), relative);
            if (path.isEmpty() || !QFileInfo(path).isFile())
                continue;
            row["sha1"] = ContentFileCache::instance().sha1(path);
            if (row.value("sha1").toString().isEmpty())
                continue;
            const auto source = sourceIndex.value(relative + '\n' + row.value("sha1").toString());
            if (!source.isEmpty()) {
                row["sourceIds"] = source.value("sourceIds");
                row["launcherLocked"] = source.value("launcherLocked");
                row["provider"] = source.value("provider");
                row["projectId"] = source.value("projectId");
                row["external"] = false;
            }
            if (row.value("category") == "mods" && m_identity) {
                const auto identity = m_identity->inspectVerified(pack, row, row.value("sha1").toString());
                QVariantMap ids = row.value("sourceIds").toMap();
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
        // Managed pins identify every content type, including provider-only mods. Attach a
        // release only when both its contained path and published hash match the actual file.
        {
            const auto kind = row.value("category") == "mods" ? QString("mods") : QString("assets");
            for (const auto &value : pack.value(kind).toList()) {
                const auto pin = value.toMap();
                if (contentKey(pin.value("installPath").toString()) == contentKey(relative) &&
                    pin.value("sha1") == row.value("sha1")) {
                    auto ids = row.value("sourceIds").toMap();
                    const auto pinnedIds = pin.value("projectSourceIds").toMap();
                    for (auto it = pinnedIds.cbegin(); it != pinnedIds.cend(); ++it)
                        ids[it.key()] = it.value();
                    const auto provider = pin.value("provider").toString();
                    const auto id = pin.value("projectId").toString();
                    if (!provider.isEmpty() && !id.isEmpty())
                        ids[provider] = id;
                    row["sourceIds"] = ids;
                    row["installedRelease"] = pin;
                    row["projectId"] = pin.value("projectId");
                    row["provider"] = pin.value("provider");
                    row["external"] = false;
                    row["version"] = pin.value("version");
                    row["title"] = pin.value("projectTitle", row.value("title"));
                }
            }
        }
        rows << row;
    }
    return rows;
}
ContentWriteResult NativeContentStore::apply(const QVariantMap &pack, QList<Edit> edits,
                                             const QJsonArray *replacementPins,
                                             const QJsonArray *replacementAssets) const {
    ContentAccess access(pack.value("path").toString(), ContentAccess::Write);
    if (!access.ok())
        return {false, access.error()};
    if (edits.isEmpty() && !replacementPins && !replacementAssets)
        return {false, "Select content first."};
    const auto root = QFileInfo(pack.value("path").toString()).canonicalFilePath();
    if (root.isEmpty())
        return {false, "The pack folder is unavailable."};
    // A known same-name destination is an atomic replacement, including its backup and lock checks.
    QSet<QString> removals;
    for (const auto &edit : edits)
        if (!edit.from.isEmpty() && edit.to.isEmpty())
            removals.insert(contentKey(edit.from));
    for (auto &edit : edits) {
        if (edit.source.isEmpty() || !edit.from.isEmpty() || removals.contains(contentKey(edit.to)))
            continue;
        const auto destination = NativePackFileStore::resolve(root, edit.to);
        if (!destination.isEmpty() && QFileInfo(destination).isFile()) {
            edit.from = edit.to;
            if (edit.expectedSha1.isEmpty())
                edit.expectedSha1 = sha1File(destination);
        } else if (!edit.expectedSha1.isEmpty()) {
            return {false, "The destination changed before replacement. Refresh and retry."};
        }
    }
    QJsonObject originalManifest;
    if (pack.value("managed").toBool()) {
        const auto current = PackRepository::inspectContent(root);
        if (!current.ok)
            return {false, current.error};
        originalManifest = current.manifest;
    }
    QString policyError;
    const auto sourceRows = LauncherContentState::read(pack, &policyError);
    if (!policyError.isEmpty())
        return {false, policyError};
    const auto locked = [&](const Edit &edit) {
        for (const auto &value : sourceRows) {
            const auto source = value.toMap();
            if (source.value("launcherLocked").toBool() &&
                (contentKey(source.value("relativePath").toString()) == contentKey(edit.from) ||
                 contentKey(source.value("relativePath").toString()) == contentKey(edit.to))) {
                if (edit.from.isEmpty() && !QFileInfo::exists(QDir(root).filePath(edit.to)) &&
                    !edit.source.isEmpty() && sha1File(edit.source) == source.value("sha1").toString())
                    continue;
                return true;
            }
        }
        return m_locks && (m_locks->locked(pack.value("path").toString(), edit.from) ||
                           m_locks->locked(pack.value("path").toString(), edit.to));
    };
    for (const auto &edit : edits)
        if (locked(edit))
            return {false, "Unlock " + (edit.from.isEmpty() ? edit.to : edit.from) + " before changing it."};
    QSet<QString> targets, removedPaths;
    for (const auto &edit : edits)
        if (!edit.from.isEmpty() && edit.to.isEmpty())
            removedPaths.insert(contentKey(edit.from));
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
            if (targets.contains(contentKey(edit.to)))
                return {false, "Two files would have the same destination."};
            targets.insert(contentKey(edit.to));
            const auto parent = edit.to.section('/', 0, -2);
            const auto existingParent = NativePackFileStore::resolve(root, parent);
            if (existingParent.isEmpty() && QFileInfo::exists(QDir(root).filePath(parent)))
                return {false, "The content folder is linked or unsafe."};
            if (QFileInfo::exists(QDir(root).filePath(edit.to)) &&
                contentKey(edit.to) != contentKey(edit.from) && !removedPaths.contains(contentKey(edit.to)))
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
    QMap<QString, std::shared_ptr<QSaveFile>> prepared;
    for (const auto &edit : edits)
        if (!edit.source.isEmpty()) {
            const auto destination = QDir(root).filePath(edit.to);
            if (!QDir().mkpath(QFileInfo(destination).absolutePath()))
                return {false, "Could not prepare the content folder.", backup};
            auto output = std::make_shared<QSaveFile>(destination);
            output->setDirectWriteFallback(false);
            QFile input(edit.source);
            if (!input.open(QIODevice::ReadOnly) || !output->open(QIODevice::WriteOnly))
                return {false, "Could not stage a complete content file.", backup};
            while (!input.atEnd()) {
                const auto bytes = input.read(1024 * 1024);
                if (bytes.isEmpty() && input.error() != QFileDevice::NoError)
                    return {false, "Could not read staged content.", backup};
                if (output->write(bytes) != bytes.size())
                    return {false, "Could not stage content; no installed files were changed.", backup};
            }
            prepared[edit.to] = output;
        }
    QList<Edit> applied;
    auto rollback = [&] {
        bool ok = true;
        for (auto it = applied.crbegin(); it != applied.crend(); ++it) {
            if (!it->to.isEmpty() && QFileInfo::exists(QDir(root).filePath(it->to)))
                ok = QFile::remove(QDir(root).filePath(it->to)) && ok;
            if (!it->from.isEmpty())
                ok = atomicCopy(QDir(backup).filePath(it->from), QDir(root).filePath(it->from)) && ok;
        }
        return ok;
    };
    for (const auto &edit : edits) {
        if (locked(edit)) {
            const bool restored = rollback();
            return {false,
                    restored ? "Content was locked before this operation completed. Changes were restored."
                             : "Content was locked; recovery needs attention. Backups: " + backup,
                    backup, !restored};
        }
        // Recheck immediately before each change; never overwrite a new destination.
        const auto from = QDir(root).filePath(edit.from), to = QDir(root).filePath(edit.to);
        if (!edit.from.isEmpty() && sha1File(from) != edit.expectedSha1) {
            const bool restored = rollback();
            return {false,
                    restored ? "Content changed during the operation. Earlier changes were restored."
                             : "Content changed; recovery needs attention. Backups: " + backup,
                    backup, !restored};
        }
        if (edit.from.isEmpty() && !edit.to.isEmpty() && QFileInfo::exists(to)) {
            const bool restored = rollback();
            return {false, "The destination changed during this operation. Refresh and retry.", backup,
                    !restored};
        }
        if (!edit.to.isEmpty())
            QDir().mkpath(QFileInfo(to).absolutePath());
        bool ok = true;
        if (!edit.source.isEmpty())
            ok = prepared.value(edit.to)->commit();
        else if (!edit.from.isEmpty() && !edit.to.isEmpty())
            ok = QFile::rename(from, to);
        else if (!edit.from.isEmpty())
            ok = QFile::remove(from);
        if (!ok) {
            const bool restored = rollback();
            return {false,
                    restored ? "The file operation failed. Earlier changes were restored."
                             : "Recovery needs attention. Backups: " + backup,
                    backup, !restored};
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
                        pin["enabled"] = !edit.to.endsWith(".disabled", Qt::CaseInsensitive);
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
                        pin["enabled"] = !edit.to.endsWith(".disabled", Qt::CaseInsensitive);
                    }
                }
            if (!removed)
                assets << pin;
        }
        QMap<QString, QString> written;
        for (const auto &edit : edits)
            if (!edit.source.isEmpty())
                written[contentKey(edit.to)] = sha1File(edit.source);
        const auto clean = [&](const QJsonArray &refs) {
            QJsonArray kept;
            QSet<QString> seen;
            for (auto it = refs.constEnd(); it != refs.constBegin();) {
                const auto ref = (*--it).toObject();
                const auto path = ref.value("installPath").toString();
                if (seen.contains(contentKey(path)) ||
                    (written.contains(contentKey(path)) &&
                     written.value(contentKey(path)) != ref.value("sha1").toString()))
                    continue;
                seen.insert(contentKey(path));
                kept.prepend(ref);
            }
            return kept;
        };
        const auto result = PackRepository::updateContentReferences(
            root, originalManifest, clean(replacementPins ? *replacementPins : updated),
            clean(replacementAssets ? *replacementAssets : assets));
        if (!result.ok) {
            const bool restored = rollback();
            return {false, result.error + (restored ? " Files were restored." : " Check backups: " + backup),
                    backup, !restored};
        }
    }
    return {true, {}, backup, true};
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
        const bool currentlyEnabled = !path.endsWith(".disabled", Qt::CaseInsensitive);
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
        const auto current = PackRepository::inspectContent(root);
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
        managedRef["enabled"] = !target.endsWith(".disabled", Qt::CaseInsensitive);
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
        const auto current = PackRepository::inspectContent(root);
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
        ref["enabled"] = !target.endsWith(".disabled", Qt::CaseInsensitive);
        pins << ref;
    }
    return apply(pack, edits, nullptr, pack.value("managed").toBool() ? &pins : nullptr);
}
ContentWriteResult NativeContentStore::restoreLocked(const QVariantMap &pack,
                                                     const QVariantMap &request) const {
    ContentAccess access(pack.value("path").toString(), ContentAccess::Write);
    if (!access.ok())
        return {false, access.error()};
    const auto root = pack.value("path").toString();
    for (const auto &value : LauncherContentState::read(pack)) {
        const auto lock = value.toMap();
        const auto relative = lock.value("relativePath").toString(), hash = lock.value("sha1").toString();
        if (!lock.value("launcherLocked").toBool() || lock.value("projectId") != request.value("projectId") ||
            !contentPath(relative))
            continue;
        const auto target = QDir(root).filePath(relative);
        if (QFileInfo::exists(target))
            return {false, "The locked file already exists. Refresh content before repairing it."};
        for (const auto &directory :
             QDir(m_backupRoot).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Time)) {
            const auto source = QDir(m_backupRoot).filePath(directory + '/' + relative);
            if (sha1File(source) != hash)
                continue;
            if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !atomicCopy(source, target))
                return {false, "Could not restore the verified locked file."};
            return {true, {}};
        }
        return {false, "No verified backup of this locked version is available."};
    }
    return {false, "The source lock changed. Refresh before restoring content."};
}
ContentWriteResult NativeContentStore::commitStagedContent(const QVariantMap &pack,
                                                           const QVariantList &expected, const QString &stage,
                                                           const QJsonObject &manifest,
                                                           const QSet<QString> &shallowProjects,
                                                           bool selectedOnly) const {
    ContentAccess access(pack.value("path").toString(), ContentAccess::Write);
    if (!access.ok())
        return {false, access.error()};
    const auto root = pack.value("path").toString();
    QMap<QString, QString> beforeHashes, actualHashes;
    QHash<QString, QVariantMap> originalRows;
    for (const auto &value : expected) {
        const auto row = value.toMap();
        const auto path = row.value("relativePath").toString();
        if (contentPath(path) && !row.value("directory").toBool()) {
            beforeHashes[path] = row.value("sha1").toString();
            originalRows.insert(path, row);
        }
    }
    for (const auto &value : snapshot(pack, PackContent::contentFiles(pack))) {
        const auto row = value.toMap();
        const auto path = row.value("relativePath").toString();
        if (contentPath(path) && !row.value("directory").toBool())
            actualHashes[path] = row.value("sha1").toString();
    }
    if (beforeHashes != actualHashes)
        return {false, "Content changed during download. Nothing was applied; refresh and retry."};
    const auto stageFiles = PackContent::contentFiles({{"path", stage}, {"loader", pack.value("loader")}});
    QMap<QString, int> beforeIds, afterIds;
    QSet<QString> disabledProjects, disabledVersions;
    for (const auto &value : expected) {
        const auto row = value.toMap();
        if (row.value("category") != "mods")
            continue;
        if (row.value("enabled").toBool()) {
            if (!row.value("modId").toString().isEmpty())
                ++beforeIds[row.value("modId").toString()];
        } else {
            disabledProjects.insert(row.value("sourceIds").toMap().value("modrinth").toString());
            disabledVersions.insert(row.value("installedRelease").toMap().value("id").toString());
        }
    }
    for (const auto &value : stageFiles) {
        const auto row = value.toMap();
        if (row.value("category") == "mods" && row.value("enabled").toBool() &&
            !row.value("modId").toString().isEmpty())
            ++afterIds[row.value("modId").toString()];
    }
    const auto sourceLocks = LauncherContentState::read(pack);
    QList<Edit> removals, additions;
    QJsonArray mods, assets;
    QSet<QString> replaced, retained;
    for (const auto &kind : {QString("mods"), QString("assets")})
        for (const auto &value : manifest.value(kind).toArray()) {
            auto ref = value.toObject();
            const auto replacePath = ref.take("confluxReplacePath").toString();
            const auto provider = ref.value("provider").toString(), id = ref.value("projectId").toString(),
                       hash = ref.value("sha1").toString();
            const auto category = kind == "mods" ? QString("mods") : ref.value("category").toString();
            const auto file = ref.value("filename").toString();
            const auto source = NativePackFileStore::resolve(stage, ref.value("installPath").toString());
            if (hash.isEmpty() || !contentPath(category + '/' + file))
                return {false, "Staged content failed final verification. Nothing was applied."};
            if (ref.contains("confluxRetainedPath")) {
                const auto originalPath = ref.take("confluxRetainedPath").toString();
                const auto original = originalRows.value(originalPath);
                if (original.isEmpty() || original.value("sha1").toString() != hash ||
                    original.value("category").toString() != category || retained.contains(originalPath) ||
                    replaced.contains(originalPath))
                    return {false, "Retained content differs from its verified input. Nothing was applied."};
                retained.insert(originalPath);
                ref["installPath"] = originalPath;
                ref["enabled"] = original.value("enabled").toBool();
                if (kind == "mods")
                    mods << ref;
                else
                    assets << ref;
                continue;
            }
            if (source.isEmpty() || sha1File(source) != hash)
                return {false, "Staged content failed final verification. Nothing was applied."};
            bool unchangedInput = false;
            for (const auto &value : expected)
                if (value.toMap().value("sha1").toString() == hash)
                    unchangedInput = true;
            if (!unchangedInput && !shallowProjects.contains(ref.value("projectId").toString()))
                for (const auto &dependency : ref.value("dependencyRefs").toArray()) {
                    const auto dep = dependency.toObject();
                    if (dep.value("dependency_type") == "required" &&
                        (disabledProjects.contains(dep.value("project_id").toString()) ||
                         disabledVersions.contains(dep.value("version_id").toString())))
                        return {false, "A required dependency is disabled. Nothing was applied."};
                }
            QVariantMap old;
            int matches = 0;
            for (const auto &item : expected) {
                const auto row = item.toMap();
                if (row.value("category") != category)
                    continue;
                const auto installedIds = row.value("sourceIds").toMap();
                auto ids = ref.value("projectSourceIds").toObject().toVariantMap();
                if (!provider.isEmpty() && !id.isEmpty())
                    ids[provider] = id;
                bool same = false;
                for (auto it = ids.cbegin(); it != ids.cend(); ++it)
                    if (!it.value().toString().isEmpty() && installedIds.value(it.key()) == it.value())
                        same = true;
                if ((!replacePath.isEmpty() && row.value("relativePath").toString() == replacePath) ||
                    (replacePath.isEmpty() &&
                     (same || (installedIds.isEmpty() &&
                               row.value("relativePath").toString() == ref.value("installPath").toString() &&
                               row.value("sha1").toString() == hash)))) {
                    old = row;
                    ++matches;
                }
            }
            if (matches > 1)
                return {false,
                        "Multiple installed files match one project. Review content before replacing it."};
            if (!replacePath.isEmpty() && old.isEmpty())
                return {false, "The selected content no longer exists. Refresh before updating it."};
            QString target = category + '/' +
                             (category == "mods" && pack.value("managed").toBool() ? hash + ".jar" : file);
            if (!old.isEmpty() && !old.value("enabled").toBool())
                target += ".disabled";
            const bool unchanged = !old.isEmpty() && old.value("sha1").toString() == hash;
            if (unchanged)
                target = old.value("relativePath").toString();
            bool repair = false;
            for (const auto &entry : sourceLocks) {
                const auto lock = entry.toMap();
                if (!lock.value("launcherLocked").toBool() || lock.value("projectId").toString() != id ||
                    provider != "modrinth")
                    continue;
                if (unchanged)
                    continue;
                if (old.isEmpty() && lock.value("missing").toBool() &&
                    hash == lock.value("sha1").toString()) {
                    target = lock.value("relativePath").toString();
                    repair = true;
                } else
                    return {false, "This content is locked by Modrinth. Restore its locked version or unlock "
                                   "it in Modrinth."};
            }
            if (!unchanged && !repair &&
                (old.value("locked").toBool() || old.value("frozen").toBool() ||
                 (m_locks && m_locks->projectLocked(root, provider, id))))
                return {false, "A locked project or dependency would be changed. Nothing was applied."};
            QString destinationHash;
            for (auto it = originalRows.cbegin(); it != originalRows.cend(); ++it)
                if (contentKey(it.key()) == contentKey(target)) {
                    destinationHash = it.value().value("sha1").toString();
                    if (!unchanged &&
                        (it.value().value("locked").toBool() || it.value().value("frozen").toBool()))
                        return {false, "A locked destination would be changed. Nothing was applied."};
                }
            ref["installPath"] = target;
            ref["enabled"] = !target.endsWith(".disabled", Qt::CaseInsensitive);
            if (kind == "mods")
                mods << ref;
            else
                assets << ref;
            if (unchanged)
                continue;
            if (!old.isEmpty()) {
                const auto path = old.value("relativePath").toString();
                if (replaced.contains(path) || retained.contains(path))
                    return {false, "Multiple projects would replace the same file."};
                replaced.insert(path);
                removals << Edit{path, {}, {}, old.value("sha1").toString()};
            }
            additions << Edit{{}, target, source, destinationHash};
        }
    if (pack.value("managed").toBool()) {
        const auto original = PackRepository::inspectContent(root);
        if (!original.ok)
            return {false, original.error};
        for (const auto &kind : {QString("mods"), QString("assets")}) {
            auto &refs = kind == "mods" ? mods : assets;
            for (const auto &value : original.manifest.value(kind).toArray()) {
                const auto old = value.toObject();
                if (!selectedOnly && beforeHashes.contains(old.value("installPath").toString()))
                    continue;
                if (selectedOnly && replaced.contains(old.value("installPath").toString()))
                    continue;
                bool included = false;
                for (const auto &entry : refs) {
                    const auto ref = entry.toObject();
                    if (selectedOnly ? contentKey(ref.value("installPath").toString()) ==
                                           contentKey(old.value("installPath").toString())
                                     : (ref.value("provider") == old.value("provider") &&
                                        ref.value("projectId") == old.value("projectId")))
                        included = true;
                }
                if (!included)
                    refs << old;
            }
        }
    }
    if (selectedOnly) {
        auto counts = beforeIds;
        for (const auto &edit : removals) {
            const auto row = originalRows.value(edit.from);
            if (row.value("enabled").toBool() && !row.value("modId").toString().isEmpty())
                --counts[row.value("modId").toString()];
        }
        for (const auto &edit : additions) {
            if (!edit.to.startsWith("mods/") || edit.to.endsWith(".disabled", Qt::CaseInsensitive))
                continue;
            QVariantMap overwritten;
            bool alreadyRemoved = false;
            for (auto it = originalRows.cbegin(); it != originalRows.cend(); ++it)
                if (contentKey(it.key()) == contentKey(edit.to)) {
                    overwritten = it.value();
                    alreadyRemoved = replaced.contains(it.key());
                    break;
                }
            if (!alreadyRemoved && overwritten.value("enabled").toBool() &&
                !overwritten.value("modId").toString().isEmpty())
                --counts[overwritten.value("modId").toString()];
        }
        // Only selected files that will actually be written contribute new IDs.
        // Reinstalling identical bytes leaves the original ID count unchanged.
        for (const auto &edit : additions) {
            if (!edit.to.startsWith("mods/") || edit.to.endsWith(".disabled", Qt::CaseInsensitive))
                continue;
            for (const auto &value : stageFiles) {
                const auto row = value.toMap();
                if (QDir(stage).filePath(row.value("relativePath").toString()) == edit.source &&
                    !row.value("modId").toString().isEmpty())
                    ++counts[row.value("modId").toString()];
            }
        }
        afterIds = counts;
    }
    for (auto it = afterIds.cbegin(); it != afterIds.cend(); ++it)
        if (it.value() > qMax(1, beforeIds.value(it.key())))
            return {false, "This transaction would introduce duplicate mod ID " + it.key() +
                               ". Nothing was applied."};
    if (removals.isEmpty() && additions.isEmpty() && !pack.value("managed").toBool())
        return {true, {}};
    return apply(pack, removals + additions, pack.value("managed").toBool() ? &mods : nullptr,
                 pack.value("managed").toBool() ? &assets : nullptr);
}
} // namespace studio
