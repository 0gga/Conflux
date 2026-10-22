#pragma once
#include <QJsonArray>
#include <QVariantList>
namespace studio {
// Provider rosters retain exact identities; display-name punctuation is never parsed.
class ProjectAuthors final {
  public:
    static QVariantList modrinthTeam(const QJsonArray &);
    static QVariantList curseforge(const QJsonArray &);
};
} // namespace studio
