#pragma once
#include "ContentVersionSource.h"
#include <QObject>
#include <memory>
namespace studio {
// Owns one installed-content version lookup and its derived display groups.
// Installation eligibility and file commands remain with the mutation workflow.
class InstalledContentVersions final : public QObject {
    Q_OBJECT
  public:
    explicit InstalledContentVersions(std::shared_ptr<IContentVersionSource>, QObject *parent = nullptr);
    void open(QVariantMap target, QString game, QString loaders);
    // The owning facade batches context publication. Invalidate before cancelling;
    // cancellation may synchronously deliver an obsolete callback.
    void reset();
    QVariantMap target() const {
        return m_target;
    }
    QVariantList versions() const {
        return m_versions;
    }
    QVariantList groups() const {
        return m_groups;
    }
    bool busy() const {
        return m_busy;
    }
    QString error() const {
        return m_error;
    }
  signals:
    void changed();

  private:
    std::shared_ptr<IContentVersionSource> m_source;
    QVariantMap m_target;
    QVariantList m_versions, m_groups;
    QString m_error;
    quint64 m_generation = 0;
    bool m_busy = false;
};
} // namespace studio
