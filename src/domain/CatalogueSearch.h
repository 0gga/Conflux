#pragma once
#include "Catalogue.h"
#include <functional>
namespace studio {
struct CatalogueQuery {
    QString text, provider = "modrinth", type = "mod", sort = "relevance";
    QStringList versions, loaders, tags, projectIds;
    QMap<QString, QString> authorIds;
};
struct CataloguePage {
    QVector<Project> projects;
    qint64 total = 0;
    QString error;
};
class ICatalogueSearch {
  public:
    using Callback = std::function<void(CataloguePage)>;
    virtual ~ICatalogueSearch() = default;
    virtual void fetch(const CatalogueQuery &, int offset, int limit, Callback) = 0;
    virtual void facets(std::function<void(QVariantMap, QString)>) = 0;
    virtual void setKey(const QByteArray &) = 0;
};
class IBrowseSettingsStore {
  public:
    virtual ~IBrowseSettingsStore() = default;
    virtual QVariant value(const QString &, const QVariant &fallback = {}) const = 0;
    virtual void save(const QString &, const QVariant &) = 0;
};
} // namespace studio
