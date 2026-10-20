#pragma once
#include "domain/CatalogueSearch.h"
#include <QObject>
#include <memory>
namespace studio {
class IContentVisibility {
  public:
    virtual ~IContentVisibility() = default;
    virtual QStringList hiddenIdentities() const {
        return {};
    }
    virtual bool hidden(const QVariantMap &, const QString &instance = {}) const = 0;
    virtual void setHidden(const QVariantMap &, bool, const QString &instance = {}) = 0;
};
class ContentVisibility final : public QObject, public IContentVisibility {
    Q_OBJECT
  public:
    explicit ContentVisibility(std::shared_ptr<IBrowseSettingsStore>, QObject *parent = nullptr);
    QStringList hiddenIdentities() const override {
        return m_hidden;
    }
    bool hidden(const QVariantMap &, const QString &instance = {}) const override;
    Q_INVOKABLE void setHidden(const QVariantMap &, bool, const QString &instance = {}) override;
  signals:
    void changed();

  private:
    QStringList identities(const QVariantMap &, const QString &) const;
    std::shared_ptr<IBrowseSettingsStore> m_store;
    QStringList m_hidden;
};
} // namespace studio
