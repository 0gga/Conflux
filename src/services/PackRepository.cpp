#include "PackRepository.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

namespace studio {
static QString fileHash(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha1);
    return hash.addData(&file) ? QString::fromLatin1(hash.result().toHex()) : QString{};
}
static bool managedModPath(const QString &relative, const QString &hash) {
    return QRegularExpression("^[a-f0-9]{40}$").match(hash).hasMatch() &&
           (relative == "mods/" + hash + ".jar" || relative == "mods/" + hash + ".jar.disabled");
}
RepositoryResult PackRepository::applyInstallation(const QString &path, const QJsonObject &expected,
                                                   const QJsonArray &references, const QString &staging) {
    auto current = inspect(path);
    if (!current.ok)
        return current;
    if (current.manifest != expected)
        return {false, "The pack changed while mods were downloading. Retry with its current contents.",
                path};
    const auto modsPath = QDir(path).filePath("mods");
    const QFileInfo modsInfo(modsPath);
    if (modsInfo.isSymLink() || modsInfo.isJunction() ||
        (!QDir(modsPath).exists() && !QDir().mkpath(modsPath)))
        return {false, "The managed mods folder is unavailable or is a symbolic link.", path};
    QStringList created;
    QSet<QString> keep;
    auto rollback = [&created] {
        for (const auto &file : created)
            QFile::remove(file);
    };
    for (const auto &item : references) {
        const auto ref = item.toObject();
        if (ref.value("provider").toString() != "modrinth")
            continue;
        const auto hash = ref.value("sha1").toString().toLower(),
                   relative = ref.value("installPath").toString();
        if (!managedModPath(relative, hash)) {
            rollback();
            return {false, "Invalid managed mod file path.", path};
        }
        const auto destination = QDir(path).filePath(relative);
        keep.insert(relative);
        if (QFileInfo::exists(destination)) {
            if (QFileInfo(destination).isSymLink() || fileHash(destination) != hash) {
                rollback();
                return {false, "An existing mod file differs from its expected hash. It was left untouched.",
                        path};
            }
            continue;
        }
        const auto source = QDir(staging).filePath(hash + ".jar");
        if (fileHash(source) != hash) {
            rollback();
            return {false, "A staged download failed verification.", path};
        }
        QFile input(source);
        QSaveFile output(destination);
        bool copied = input.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly);
        while (copied && !input.atEnd()) {
            const auto bytes = input.read(256 * 1024);
            copied = !bytes.isEmpty() && output.write(bytes) == bytes.size();
        }
        if (!copied || !output.commit()) {
            rollback();
            return {false, "Could not install the verified mod file.", path};
        }
        created << destination;
    }
    auto manifest = current.manifest;
    manifest.insert("mods", references);
    QSaveFile file(QDir(path).filePath("pack.json"));
    const auto data = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        rollback();
        return {false, "Could not save the installed pack. Its previous manifest was preserved.", path};
    }
    // Only remove superseded, unedited files previously owned by this manifest.
    QStringList warnings;
    for (const auto &item : expected.value("mods").toArray()) {
        const auto old = item.toObject();
        const auto relative = old.value("installPath").toString();
        if (old.value("provider").toString() != "modrinth")
            continue;
        const auto hash = old.value("sha1").toString().toLower();
        if (!managedModPath(relative, hash) || keep.contains(relative))
            continue;
        const auto absolute = QDir(path).filePath(relative);
        if (!QFileInfo::exists(absolute))
            continue;
        if (QFileInfo(absolute).isSymLink() || fileHash(absolute) != hash || !QFile::remove(absolute))
            warnings << "A superseded mod file was edited or could not be removed; review " + relative;
    }
    auto result = inspect(path);
    if (result.ok)
        result.error = warnings.join('\n');
    return result;
}
static QString git(const QString &path, const QStringList &arguments, bool *ok = nullptr) {
    QProcess process;
    process.setWorkingDirectory(path);
    process.setProgram("git");
    // No shell interpolation, no interactive credential prompts in a worker.
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("GIT_TERMINAL_PROMPT", "0");
    process.setProcessEnvironment(environment);
    process.setArguments(arguments);
    process.start();
    if (!process.waitForStarted(3000)) {
        if (ok)
            *ok = false;
        return "Git could not start. Install Git and restart the app.";
    }
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished(3000);
        if (ok)
            *ok = false;
        return "Git timed out.";
    }
    const bool success = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    if (ok)
        *ok = success;
    return QString::fromUtf8(success ? process.readAllStandardOutput() : process.readAllStandardError())
        .trimmed();
}
QString PackRepository::slug(const QString &name) {
    auto value = name.trimmed().toLower();
    value.replace(QRegularExpression("[^a-z0-9]+"), "-");
    value.remove(QRegularExpression("^-+|-+$"));
    if (value.isEmpty())
        value = "modpack";
    // Avoid Windows device names as directory names.
    if (QRegularExpression("^(con|prn|aux|nul|com[1-9]|lpt[1-9])$").match(value).hasMatch())
        value.prepend("pack-");
    return value.left(72);
}
RepositoryResult PackRepository::write(const QString &path, const QJsonObject &manifest) {
    QSaveFile file(QDir(path).filePath("pack.json"));
    const auto bytes = QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return {false, "Could not save pack.json: " + file.errorString(), path};
    return inspect(path);
}
RepositoryResult PackRepository::create(const QString &root, const QString &name, const QString &game,
                                        const QString &loader) {
    if (name.trimmed().isEmpty() ||
        !QRegularExpression("^[0-9]+\\.[0-9]+(?:\\.[0-9]+)?$").match(game).hasMatch() ||
        !QStringList{"fabric", "forge", "neoforge", "quilt", "vanilla"}.contains(loader))
        return {false,
                "Enter a pack name, a Minecraft release (for example 1.21.1), and a supported loader."};
    QDir parent(root);
    if (!parent.exists() && !QDir().mkpath(root))
        return {false, "Could not create the packs directory."};
    const auto directory = slug(name);
    const auto path = parent.absoluteFilePath(directory);
    // mkdir is exclusive: a collision never overwrites an existing pack or arbitrary directory.
    if (!parent.mkdir(directory))
        return {false, "A folder with this pack name already exists. Choose another name.", path};
    bool ok = false;
    const auto output = git(path, {"init", "--initial-branch=main"}, &ok);
    if (!ok)
        return {false, output, path};
    QFile ignore(QDir(path).filePath(".gitignore"));
    if (!ignore.open(QIODevice::WriteOnly) ||
        ignore.write(
            "mods/\nresourcepacks/\nshaderpacks/\ndatapacks/\n.minecraft/\nworlds/\nsaves/\nlogs/\ncache/"
            "\n.env\n*.jar\n") < 0)
        return {false, "Could not create the pack's .gitignore.", path};
    ignore.close();
    return write(path, {{"schemaVersion", 1},
                        {"id", QUuid::createUuid().toString(QUuid::WithoutBraces)},
                        {"name", name.trimmed()},
                        {"minecraft", game},
                        {"loader", loader},
                        {"loaderVersion", QJsonValue::Null},
                        {"mods", QJsonArray{}}});
}
RepositoryResult PackRepository::inspect(const QString &path) {
    QFile file(QDir(path).filePath("pack.json"));
    if (!file.open(QIODevice::ReadOnly))
        return {false, "This folder does not contain a readable pack.json.", path};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(file.readAll(), &error);
    const auto manifest = document.object();
    if (error.error != QJsonParseError::NoError || manifest.value("schemaVersion").toInt() != 1 ||
        manifest.value("name").toString().isEmpty() || !manifest.value("mods").isArray())
        return {false, "The pack manifest is invalid or uses an unsupported schema.", path};
    if (!QFileInfo::exists(QDir(path).filePath(".git")))
        return {false, "The pack folder is not a Git repository.", path};
    bool ok;
    const auto status = git(path, {"status", "--short"}, &ok);
    if (!ok)
        return {false, status, path};
    bool hasHead;
    git(path, {"rev-parse", "--verify", "HEAD"}, &hasHead);
    const auto history = hasHead ? git(path, {"log", "-8", "--format=%h  %s"})
                                 : "No saved changes yet. Your first save creates the initial commit.";
    return {true, {}, path, status, history, manifest};
}
RepositoryResult PackRepository::pin(const QString &path, const QJsonObject &reference) {
    auto current = inspect(path);
    if (!current.ok)
        return current;
    const auto provider = reference.value("provider").toString();
    if (!QStringList{"modrinth", "curseforge"}.contains(provider) ||
        reference.value("projectId").toString().isEmpty() || reference.value("id").toString().isEmpty() ||
        !QRegularExpression("^[a-fA-F0-9]{40}$").match(reference.value("sha1").toString()).hasMatch() ||
        !reference.value("url").toString().startsWith("https://"))
        return {false, "This release has no verified downloadable file reference.", path};
    if (!reference.value("gameVersions").toArray().contains(current.manifest.value("minecraft")) ||
        !reference.value("loaders").toArray().contains(current.manifest.value("loader")))
        return {false, "This release does not match the pack's Minecraft version and loader.", path};
    auto mods = current.manifest.value("mods").toArray();
    const auto projectId = reference.value("projectId").toString();
    for (qsizetype i = mods.size(); i > 0; --i) {
        const auto mod = mods.at(i - 1).toObject();
        // Compare all mapped project identities to prevent cross-provider duplicate pins.
        const auto ids = reference.value("projectSourceIds").toObject();
        if ((mod.value("provider").toString() == provider &&
             mod.value("projectId").toString() == projectId) ||
            ids.value(mod.value("provider").toString()).toString() == mod.value("projectId").toString())
            mods.removeAt(i - 1);
    }
    mods.append(reference);
    current.manifest.insert("mods", mods);
    return write(path, current.manifest);
}
RepositoryResult PackRepository::updateContentPins(const QString &path, const QJsonObject &expected,
                                                   const QJsonArray &pins) {
    return updateContentReferences(path, expected, pins, expected.value("assets").toArray());
}
RepositoryResult PackRepository::updateContentReferences(const QString &path, const QJsonObject &expected,
                                                         const QJsonArray &mods, const QJsonArray &assets) {
    auto current = inspect(path);
    if (!current.ok)
        return current;
    if (current.manifest != expected)
        return {false, "The pack manifest changed outside this operation. Refresh before retrying.", path};
    current.manifest["mods"] = mods;
    if (!assets.isEmpty() || expected.contains("assets"))
        current.manifest["assets"] = assets;
    QByteArray originalIgnore, updatedIgnore;
    const auto ignorePath = QDir(path).filePath(".gitignore");
    if (!assets.isEmpty()) {
        QFile ignore(ignorePath);
        if (QFileInfo(ignorePath).isSymLink() || !ignore.open(QIODevice::ReadOnly) || ignore.size() > 65536)
            return {false, "The managed .gitignore could not be safely updated.", path};
        originalIgnore = ignore.readAll();
        updatedIgnore = originalIgnore;
        for (const auto &directory :
             {QByteArray("resourcepacks/"), QByteArray("shaderpacks/"), QByteArray("datapacks/")})
            if (!originalIgnore.split('\n').contains(directory)) {
                if (!updatedIgnore.endsWith('\n'))
                    updatedIgnore += '\n';
                updatedIgnore += directory + '\n';
            }
        if (updatedIgnore != originalIgnore) {
            QSaveFile ignoreWrite(ignorePath);
            if (!ignoreWrite.open(QIODevice::WriteOnly) ||
                ignoreWrite.write(updatedIgnore) != updatedIgnore.size() || !ignoreWrite.commit())
                return {false, "Could not exclude downloaded packs from Git.", path};
        }
    }
    auto result = write(path, current.manifest);
    if (!result.ok && updatedIgnore != originalIgnore) {
        QSaveFile restore(ignorePath);
        if (!restore.open(QIODevice::WriteOnly) || restore.write(originalIgnore) != originalIgnore.size() ||
            !restore.commit())
            result.error += " The previous .gitignore could not be restored.";
    }
    return result;
}
RepositoryResult PackRepository::remove(const QString &path, const QString &provider,
                                        const QString &projectId) {
    auto current = inspect(path);
    if (!current.ok)
        return current;
    auto mods = current.manifest.value("mods").toArray();
    QJsonArray removed;
    for (qsizetype i = mods.size(); i > 0; --i) {
        const auto mod = mods.at(i - 1).toObject();
        if (mod.value("provider").toString() == provider && mod.value("projectId").toString() == projectId) {
            removed.append(mod);
            mods.removeAt(i - 1);
        }
    }
    current.manifest.insert("mods", mods);
    auto result = write(path, current.manifest);
    if (!result.ok)
        return result;
    const QFileInfo modsFolder(QDir(path).filePath("mods"));
    if (modsFolder.isSymLink() || modsFolder.isJunction()) {
        result.error = "The mods folder is linked elsewhere; its files were left untouched.";
        return result;
    }
    for (const auto &item : removed) {
        const auto ref = item.toObject();
        const auto relative = ref.value("installPath").toString();
        const auto hash = ref.value("sha1").toString().toLower();
        if (!managedModPath(relative, hash))
            continue;
        bool shared = false;
        for (const auto &retained : mods)
            if (retained.toObject().value("installPath").toString() == relative)
                shared = true;
        if (shared)
            continue;
        const auto file = QDir(path).filePath(relative);
        if (!QFileInfo::exists(file))
            continue;
        if (QFileInfo(file).isSymLink() || fileHash(file) != hash || !QFile::remove(file))
            result.error = "The removed mod's file was edited or could not be deleted. Review " + relative;
    }
    return result;
}
RepositoryResult PackRepository::commit(const QString &path, const QString &message) {
    auto current = inspect(path);
    if (!current.ok)
        return current;
    if (message.trimmed().isEmpty())
        return {false, "Describe this change before saving.", path};
    bool ok;
    const auto staged = git(path, {"diff", "--cached", "--name-only", "-z"}, &ok);
    if (!ok)
        return {false, staged, path};
    for (const auto &file : staged.split(QChar::Null, Qt::SkipEmptyParts)) {
        if (file != "pack.json" && file != ".gitignore")
            return {false,
                    "Other files are already staged in this repository. Review them in your Git client "
                    "before saving this pack.",
                    path};
    }
    // Only stage the files this foundation owns; never stage arbitrary files or credentials.
    auto output = git(path, {"add", "--", "pack.json", ".gitignore"}, &ok);
    if (!ok)
        return {false, output, path};
    output = git(path, {"commit", "-m", message.trimmed()}, &ok);
    if (!ok)
        return {false, output, path};
    return inspect(path);
}
} // namespace studio
