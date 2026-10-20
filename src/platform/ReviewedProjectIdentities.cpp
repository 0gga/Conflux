#include "ReviewedProjectIdentities.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
namespace studio {
QHash<QString, QString> ReviewedProjectIdentities::load() {
    QFile file(QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                   .filePath("project-identities.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return {};
    const auto document = QJsonDocument::fromJson(file.readAll()).object();
    QHash<QString, QString> ids;
    QMap<QString, QMap<QString, QSet<QString>>> groups;
    for (auto it = document.begin(); it != document.end(); ++it) {
        const auto provider = it.key().section(':', 0, 0), id = it.key().section(':', 1),
                   group = it.value().toString();
        if ((provider == "modrinth" || provider == "curseforge") && !id.isEmpty() && !group.isEmpty()) {
            ids[it.key()] = group;
            groups[group][provider].insert(id);
        }
    }
    for (auto it = ids.begin(); it != ids.end();) {
        bool ambiguous = false;
        for (const auto &set : groups.value(it.value()))
            if (set.size() > 1)
                ambiguous = true;
        if (ambiguous)
            it = ids.erase(it);
        else
            ++it;
    }
    return ids;
}
QMap<QString, QString> ReviewedProjectIdentities::expand(QMap<QString, QString> ids) {
    const auto reviewed = load();
    QSet<QString> groups;
    for (auto it = ids.cbegin(); it != ids.cend(); ++it)
        if (reviewed.contains(it.key() + ':' + it.value()))
            groups.insert(reviewed.value(it.key() + ':' + it.value()));
    for (auto it = reviewed.cbegin(); it != reviewed.cend(); ++it)
        if (groups.contains(it.value())) {
            const auto provider = it.key().section(':', 0, 0), id = it.key().section(':', 1);
            if (!ids.contains(provider))
                ids[provider] = id;
        }
    return ids;
}
} // namespace studio
