#include "ConfigAssociation.h"
#include <QFileInfo>
#include <QRegularExpression>
namespace studio {
static QString normalize(QString value) {
    static const QRegularExpression nonIdentifier("[^a-z0-9]");
    return value.toLower().remove(nonIdentifier);
}
ConfigAssociation::ConfigAssociation(const QVariantList &mods) {
    for (const auto &item : mods) {
        const auto mod = item.toMap();
        const auto id = mod.value("modId").toString();
        if (id.isEmpty())
            continue;
        m_index[normalize(id)][id] = mod;
        const auto title = normalize(mod.value("title").toString());
        if (!title.isEmpty())
            m_index[title][id] = mod;
    }
}
QVariantMap ConfigAssociation::identify(const QString &path) const {
    static const QRegularExpression sideSuffix("[-_.](client|server|common)$");
    auto stem = QFileInfo(path).completeBaseName().toLower();
    stem.remove(sideSuffix);
    const QStringList candidates{normalize(stem), normalize(path.section('/', 1, 1).section('.', 0, 0))};
    QMap<QString, QVariantMap> matches;
    for (const auto &candidate : candidates) {
        const auto found = m_index.constFind(candidate);
        if (found != m_index.cend())
            for (auto it = found->begin(); it != found->end(); ++it)
                matches[it.key()] = it.value();
    }
    if (matches.size() != 1)
        return {};
    const auto mod = matches.first();
    return {{"relatedModId", mod.value("modId")},
            {"relatedModTitle", mod.value("title")},
            {"icon", mod.value("icon")},
            {"association", "Matched by file name"}};
}
} // namespace studio
