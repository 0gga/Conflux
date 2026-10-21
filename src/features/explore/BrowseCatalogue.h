#pragma once
#include "domain/CatalogueSearch.h"
#include "features/preferences/BrowseFilterPreferences.h"
#include "features/preferences/ContentVisibility.h"
#include <QObject>
#include <QSet>
#include <memory>
namespace studio {
// Owns query/page state; HTTP, installed identity and persistence are separate ports.
class BrowseCatalogue final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList projects READ projects NOTIFY changed)
    Q_PROPERTY(QVariantMap filters READ filters NOTIFY changed)
    Q_PROPERTY(QVariantMap author READ author NOTIFY changed)
    Q_PROPERTY(QVariantMap options READ options NOTIFY changed)
    Q_PROPERTY(int page READ page NOTIFY changed)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY changed)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(qreal scrollPosition READ scrollPosition WRITE setScrollPosition NOTIFY scrollChanged)
  public:
    BrowseCatalogue(std::shared_ptr<ICatalogueSearch>, std::shared_ptr<IContentVisibility>,
                    QHash<QString, QString> identities = {},
                    std::shared_ptr<IBrowseFilterPreferences> preferences = {}, QObject *parent = nullptr);
    QVariantList projects() const;
    QVariantMap filters() const {
        return m_filters;
    }
    QVariantMap author() const {
        return m_author;
    }
    QVariantMap options() const {
        return m_options;
    }
    int page() const {
        return m_page + 1;
    }
    bool busy() const {
        return m_busy;
    }
    bool hasNext() const;
    int pageCount() const;
    QString notice() const {
        return m_notice;
    }
    qreal scrollPosition() const {
        return m_scroll;
    }
    void setScrollPosition(qreal);
    void setContext(const QString &, const QStringList &versions, const QStringList &loaders,
                    const QVariantMap &author = {});
    void setInstalled(const QVariantList &inventory);
    void setKey(const QByteArray &);
    void setDefaults(const QVariantMap &);
    Q_INVOKABLE void configure(const QVariantMap &);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void goToPage(int);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void hide(const QVariantMap &, bool hidden = true);
    Q_INVOKABLE void loadOptions();
  signals:
    void changed();
    void scrollChanged();

  private:
    struct Stream {
        QVector<Project> rows;
        int offset = 0;
        bool exhausted = false;
        qint64 total = 0, nextDownloadBound = 0;
    };
    struct Snapshot {
        QVariantMap filters;
        QVector<QVariantList> pages;
        QMap<QString, Stream> streams;
        QSet<QString> seen, installed;
        QHash<QString, Project> known;
        QVariantList building;
        int page = 0;
        qreal scroll = 0;
        bool defaultLoaders = true;
    };
    std::shared_ptr<ICatalogueSearch> m_source;
    std::shared_ptr<IContentVisibility> m_visibility;
    std::shared_ptr<IBrowseFilterPreferences> m_preferences;
    QHash<QString, QString> m_identities;
    QSet<QString> m_installed, m_seen, m_downloadLookups;
    QHash<QString, Project> m_known;
    QVariantMap m_filters, m_options, m_defaults, m_author;
    QMap<QString, QString> m_contextTypes;
    QMap<QString, Stream> m_streams;
    QVector<QVariantList> m_pages;
    QMap<QString, Snapshot> m_contexts;
    QString m_context, m_notice;
    QStringList m_defaultVersions, m_defaultLoaders;
    int m_page = 0, m_targetPage = 0, m_generation = 0, m_reads = 0;
    bool m_busy = false, m_cf = false, m_optionsLoaded = false;
    bool m_defaultLoaderSelection = true;
    qreal m_scroll = 0;
    QVariantList m_building;
    CatalogueQuery request(const QString &) const;
    QString identity(const Project &) const;
    bool accepted(const Project &) const;
    void reset();
    void pump();
    void fetchMore(const QString &);
    bool hydrateDownloads();
    void publish(Project);
    bool before(const Project &, const Project &) const;
    QString viewKey() const;
    void saveSnapshot();
    void activateView(bool sameInstance = false);
    void saveCompatibility();
    bool applyDefaults();
};
} // namespace studio
