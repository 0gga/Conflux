#pragma once
#include <QHash>
#include <QMap>
#include <QVariantList>
#include <QVariantMap>
namespace studio {
class ConfigAssociation final {
  public:
    explicit ConfigAssociation(const QVariantList &mods);
    QVariantMap identify(const QString &relativePath) const;

  private:
    QHash<QString, QMap<QString, QVariantMap>> m_index;
};
} // namespace studio
