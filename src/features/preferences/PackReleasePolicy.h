#pragma once
#include "domain/CatalogueSearch.h"
#include "domain/ReleaseStages.h"
#include <QHash>
#include <QObject>
#include <memory>
namespace studio {
// Shared per-instance policy; no launcher metadata or pack manifest is rewritten.
class PackReleasePolicy final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QStringList choices READ choices CONSTANT)
  public:
    explicit PackReleasePolicy(std::shared_ptr<IBrowseSettingsStore>, QObject *parent = nullptr);
    QStringList choices() const {
        return releaseStages();
    }
    int revision() const {
        return m_revision;
    }
    static bool sameInstance(const QString &a, const QString &b) {
        return instanceKey(a) == instanceKey(b);
    }
    Q_INVOKABLE QStringList channels(const QString &path) const;
    Q_INVOKABLE void setChannels(const QString &path, const QStringList &channels);
    Q_INVOKABLE bool allows(const QString &path, const QString &channel) const;
  signals:
    void changed();
    void policyChanged(QString path);

  private:
    static QString instanceKey(const QString &);
    std::shared_ptr<IBrowseSettingsStore> m_store;
    mutable QHash<QString, QStringList> m_cache;
    int m_revision = 0;
};
} // namespace studio
