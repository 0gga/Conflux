#pragma once
#include <QHash>
#include <QMap>
#include <QString>
namespace studio {
class ReviewedProjectIdentities {
  public:
    static QHash<QString, QString> load();
    static QMap<QString, QString> expand(QMap<QString, QString>);
};
} // namespace studio
