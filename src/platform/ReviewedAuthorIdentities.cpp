#include "ReviewedAuthorIdentities.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>
namespace studio {
QMap<QString, QString> ReviewedAuthorIdentities::expand(QMap<QString, QString> ids, const QString &filePath) {
    QFile file(filePath.isEmpty() ? QDir(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation))
                                        .filePath("author-identities.json")
                                  : filePath);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024)
        return ids;
    const auto document = QJsonDocument::fromJson(file.readAll()).object();
    QMap<QString, QMap<QString, QSet<QString>>> groups;
    for (auto it = document.begin(); it != document.end(); ++it) {
        const auto provider = it.key().section(':', 0, 0), id = it.key().section(':', 1),
                   group = it.value().toString();
        if ((provider == "modrinth" || provider == "curseforge") && !id.isEmpty() && !group.isEmpty())
            groups[group][provider].insert(id);
    }
    QSet<QString> matches;
    for (auto it = ids.cbegin(); it != ids.cend(); ++it) {
        const auto group = document.value(it.key() + ':' + it.value()).toString();
        if (!group.isEmpty())
            matches.insert(group);
    }
    auto result = ids;
    for (const auto &group : matches) {
        const auto providers = groups.value(group);
        for (auto it = providers.cbegin(); it != providers.cend(); ++it)
            if (it.value().size() != 1 ||
                (result.contains(it.key()) && result.value(it.key()) != *it.value().cbegin()))
                return ids;
        for (auto it = providers.cbegin(); it != providers.cend(); ++it)
            result[it.key()] = *it.value().cbegin();
    }
    return result;
}
} // namespace studio
