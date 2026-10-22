#include "ProjectAuthors.h"
#include <QJsonObject>
#include <QSet>
#include <algorithm>
namespace studio {
QVariantList ProjectAuthors::modrinthTeam(const QJsonArray &members) {
    QList<QJsonObject> ordered;
    for (const auto &value : members) {
        const auto member = value.toObject();
        if (member.value("accepted").toBool(true))
            ordered << member;
    }
    const auto owner = [](const QJsonObject &member) {
        return member.value("role").toString().compare("Owner", Qt::CaseInsensitive) == 0;
    };
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&](const auto &a, const auto &b) { return owner(a) && !owner(b); });
    QVariantList result;
    QSet<QString> seen;
    for (const auto &member : ordered) {
        const auto user = member.value("user").toObject();
        const auto name = user.value("username").toString(), id = user.value("id").toString();
        if (name.isEmpty() || (!id.isEmpty() && seen.contains(id)))
            continue;
        if (!id.isEmpty())
            seen.insert(id);
        result << QVariantMap{{"name", name},
                              {"icon", user.value("avatar_url").toString()},
                              {"ids", id.isEmpty() ? QVariantMap{} : QVariantMap{{"modrinth", id}}}};
    }
    return result;
}
QVariantList ProjectAuthors::curseforge(const QJsonArray &authors) {
    QVariantList result;
    QSet<qint64> seen;
    for (const auto &value : authors) {
        const auto author = value.toObject();
        const auto name = author.value("name").toString();
        const auto id = author.value("id").toInteger();
        if (name.isEmpty() || (id > 0 && seen.contains(id)))
            continue;
        if (id > 0)
            seen.insert(id);
        result << QVariantMap{
            {"name", name},
            {"ids", id > 0 ? QVariantMap{{"curseforge", QString::number(id)}} : QVariantMap{}}};
    }
    return result;
}
} // namespace studio
