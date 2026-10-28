#pragma once
#include "features/content/ContentListModel.h"
#include <QHash>
#include <QTimer>
#include <functional>
namespace studio {
// Local read-only index. Activation delegates to the existing guarded navigation port.
class CommandPalette final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    Q_PROPERTY(QString selectedKey READ selectedKey NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
    Q_PROPERTY(QVariantList primaryNavigation READ primaryNavigation CONSTANT)
  public:
    using Navigate = std::function<bool(QVariantMap)>;
    explicit CommandPalette(QObject *parent = nullptr);
    void setNavigator(Navigate navigate) {
        m_navigate = std::move(navigate);
    }
    void setLibraryPacks(const QVariantList &);
    void setFolderEntries(const QVariantList &);
    void setContext(const QVariantMap &, const QVariantList &content, const QVariantList &files);
    bool visible() const {
        return m_visible;
    }
    QString query() const {
        return m_query;
    }
    QString selectedKey() const {
        return m_selectedKey;
    }
    QVariantList rows() const {
        return m_rows;
    }
    QAbstractItemModel *model() {
        return &m_model;
    }
    QVariantList primaryNavigation() const;
    void setQuery(const QString &);
    Q_INVOKABLE void open();
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE bool activate(const QString &);
    Q_INVOKABLE bool navigate(const QString &command);
    Q_INVOKABLE bool select(const QString &key);
    Q_INVOKABLE void moveSelection(int delta);
  signals:
    void changed();
    void queryChanged();
    void selectionChanged();

  private:
    struct Entry {
        QVariantMap value, route;
        QString normalizedTitle, normalizedDetail;
        QStringList words;
        int kind = 0;
    };
    ContentListModel m_model;
    QTimer m_rebuild;
    QVariantMap m_pack;
    QVariantList m_packs, m_content, m_files, m_folderFiles, m_rows;
    QList<Entry> m_entries;
    QHash<QString, int> m_positions;
    Navigate m_navigate;
    QString m_query, m_selectedKey;
    bool m_visible = false, m_dirty = true, m_resetSelection = true;
    void rebuild();
    void filter();
    static QVariantList commands();
};
} // namespace studio
