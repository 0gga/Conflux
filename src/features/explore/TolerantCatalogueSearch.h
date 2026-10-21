#pragma once
#include "domain/CatalogueSearch.h"
#include <QDeadlineTimer>
#include <QObject>
#include <memory>
namespace studio {
// Decorates provider search, without changing HTTP, compatibility or identity rules.
class TolerantCatalogueSearch final : public QObject, public ICatalogueSearch {
  public:
    explicit TolerantCatalogueSearch(std::shared_ptr<ICatalogueSearch> source)
        : m_source(std::move(source)) {}
    void fetch(const CatalogueQuery &, int, int, Callback) override;
    void facets(std::function<void(QVariantMap, QString)> callback) override {
        m_source->facets(std::move(callback));
    }
    void setKey(const QByteArray &key) override {
        m_cache.clear();
        ++m_generation;
        m_source->setKey(key);
    }

  private:
    std::shared_ptr<ICatalogueSearch> m_source;
    struct CachedPage {
        CataloguePage page;
        QDeadlineTimer expiry{60000};
    };
    QHash<QByteArray, CachedPage> m_cache;
    int m_generation = 0;
};
} // namespace studio
