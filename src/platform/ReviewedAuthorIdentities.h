#pragma once
#include <QMap>
#include <QString>
namespace studio {
class ReviewedAuthorIdentities {
  public:
    static QMap<QString, QString> expand(QMap<QString, QString> ids, const QString &filePath = {});
};
} // namespace studio
