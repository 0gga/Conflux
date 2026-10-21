#pragma once
#include "ProjectShowcaseSource.h"
#include <QAbstractListModel>
#include <QHash>
#include <QQueue>
#include <memory>
namespace studio {
class VersionChangelogs final : public QAbstractListModel {
    Q_OBJECT
  public:
    explicit VersionChangelogs(std::shared_ptr<IProjectShowcaseSource>, QObject *parent = nullptr);
    void setReleases(const QVector<Release> &);
    void clear();
    void select(const QVariantMap &);
    void request(int index, bool retry = false);
    int selectedIndex() const {
        return m_selected;
    }
    QVariantMap selectedRelease() const;
    QVariantList selectedBlocks() const;
    QString selectedNotice() const;
    bool selectedBusy() const;
    QVariantList versions() const;
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : m_entries.size();
    }
    QVariant data(const QModelIndex &, int) const override;
    QHash<int, QByteArray> roleNames() const override {
        return {{Qt::UserRole + 1, "modelData"}};
    }
  signals:
    void selectionChanged();

  private:
    struct Entry {
        Release release;
        QString label;
        QStringList aliases;
        QVariantList blocks;
        QString error;
        bool requested = false, loaded = false;
    };
    void pump();
    void finish(int index, int generation, QVariantList blocks, QString error = {});
    void notify(int index);
    std::shared_ptr<IProjectShowcaseSource> m_source;
    QVector<Entry> m_entries;
    QVariantList m_versions;
    QHash<QString, int> m_aliases;
    QQueue<int> m_pending;
    int m_selected = -1, m_active = 0, m_generation = 0;
};
} // namespace studio
