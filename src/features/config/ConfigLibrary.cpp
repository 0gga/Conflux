#include "ConfigLibrary.h"
#include <QFileInfo>
#include <QMap>
#include <algorithm>
namespace studio {
ConfigLibrary::ConfigLibrary(std::shared_ptr<IConfigOwnershipStore> store, QObject *parent,
                             ConfigCodecs codecs)
    : QObject(parent), m_store(std::move(store)) {
    for (const auto &codec : codecs)
        m_extensions.append(codec->extensions());
}
void ConfigLibrary::setContext(const QVariantMap &pack, const QVariantList &inventory) {
    m_pack = pack;
    m_inventory = inventory;
    rebuild();
    emit changed();
}
QVariantList ConfigLibrary::mods() const {
    return m_mods;
}
QVariantList ConfigLibrary::files() const {
    return m_files;
}
void ConfigLibrary::rebuild() {
    QMap<QString, QVariantMap> unique;
    for (const auto &value : m_inventory) {
        const auto row = value.toMap();
        const auto id = row.value("modId").toString();
        if (row.value("category") == "mods" && !id.isEmpty())
            unique[id] = row;
    }
    QVariantList mods;
    for (const auto &row : unique)
        mods << row;
    std::sort(mods.begin(), mods.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("title").toString().compare(b.toMap().value("title").toString(),
                                                           Qt::CaseInsensitive) < 0;
    });
    QMap<QString, QVariantMap> owners;
    for (const auto &mod : mods)
        owners[mod.toMap().value("modId").toString()] = mod.toMap();
    QVariantList result;
    for (const auto &value : m_inventory) {
        auto row = value.toMap();
        if (row.value("category") != "configs")
            continue;
        const auto path = row.value("relativePath").toString();
        const auto id = m_store->owner(m_pack.value("path").toString(), path);
        if (!id.isEmpty() && owners.contains(id)) {
            const auto owner = owners.value(id);
            row["relatedModId"] = id;
            row["relatedModTitle"] = owner.value("title");
            row["icon"] = owner.value("icon");
            row["association"] = "Mod selected by you";
        }
        row["supported"] = m_extensions.contains(QFileInfo(path).suffix().toLower());
        row["editable"] = row.value("supported").toBool() && !row.value("relatedModId").toString().isEmpty();
        result << row;
    }
    std::stable_sort(result.begin(), result.end(), [](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap(), right = b.toMap();
        if (left.value("editable") != right.value("editable"))
            return left.value("editable").toBool();
        return (left.value("relatedModTitle").toString() + left.value("fileName").toString())
                   .compare(right.value("relatedModTitle").toString() + right.value("fileName").toString(),
                            Qt::CaseInsensitive) < 0;
    });
    QHash<QString, qsizetype> indices;
    indices.reserve(result.size());
    for (qsizetype i = 0; i < result.size(); ++i) {
        const auto path = result.at(i).toMap().value("relativePath").toString();
        // Preserve the first entry in published sort order for duplicate paths.
        if (!indices.contains(path))
            indices.insert(path, i);
    }
    m_mods = std::move(mods);
    m_files = std::move(result);
    m_fileIndices = std::move(indices);
}
QVariantMap ConfigLibrary::file(const QString &path) const {
    const auto found = m_fileIndices.constFind(path);
    return found == m_fileIndices.cend() ? QVariantMap{} : m_files.at(*found).toMap();
}
bool ConfigLibrary::identify(const QString &path, const QString &modId) {
    if (file(path).isEmpty())
        return false;
    for (const auto &mod : mods())
        if (mod.toMap().value("modId") == modId) {
            m_store->setOwner(m_pack.value("path").toString(), path, modId);
            rebuild();
            emit changed();
            return true;
        }
    return false;
}
} // namespace studio
