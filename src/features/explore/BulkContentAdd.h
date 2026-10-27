#pragma once
#include "BulkContentInput.h"
#include "domain/CatalogueSearch.h"
#include "features/content/ContentListModel.h"
#include "features/content/ContentPorts.h"
#include "features/preferences/PackReleasePolicy.h"
#include <QQueue>
#include <QTimer>
namespace studio {
class BulkContentAdd final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool operationsBusy READ operationsBusy NOTIFY changed)
    Q_PROPERTY(QVariantMap pack READ pack NOTIFY changed)
    Q_PROPERTY(QString input READ input WRITE setInput NOTIFY inputChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
  public:
    using Install = std::function<bool(QVariantMap, QVariantMap)>;
    BulkContentAdd(std::shared_ptr<ICatalogueSearch>, std::shared_ptr<IContentCatalogue>,
                   std::shared_ptr<PackReleasePolicy>, QHash<QString, QString> identities = {},
                   QObject *parent = nullptr);
    bool visible() const {
        return m_visible;
    }
    bool busy() const {
        return m_activeSearches > 0 || !m_searches.isEmpty();
    }
    bool operationsBusy() const {
        return m_installing || !m_installs.isEmpty();
    }
    QVariantMap pack() const {
        return m_pack;
    }
    QString input() const {
        return m_input;
    }
    QString notice() const {
        return m_notice;
    }
    int selectedCount() const;
    QVariantList rows() const;
    QAbstractItemModel *model() {
        return &m_model;
    }
    void setInput(const QString &);
    void setInstaller(Install install) {
        m_install = std::move(install);
    }
    void setCurseforgeKey(const QByteArray &);
    void open(const QVariantMap &, const QVariantList &, const QString &type = "mod");
    void installationFinished(const QString &, const QVariantList &, const QVariantList &, const QString &);
    Q_INVOKABLE void resolveInput();
    Q_INVOKABLE void setSelected(const QString &, bool);
    Q_INVOKABLE void selectAll(bool);
    Q_INVOKABLE void nextCandidate(const QString &);
    Q_INVOKABLE void installSelected();
    Q_INVOKABLE void retry(const QString &);
    Q_INVOKABLE void dismiss();
  signals:
    void changed();
    void inputChanged();
    void operationFailed(QString message);

  private:
    struct Row {
        QString key, state = "waiting", error;
        BulkContentReference reference;
        QVector<Project> candidates;
        int candidate = 0, pending = 0;
        bool selected = true, accepted = false;
        QVariantMap release;
    };
    struct Search {
        quint64 generation;
        QString key, provider;
    };
    struct PendingInstall {
        quint64 id, generation;
        QStringList keys;
        QVariantMap pack, project;
        QVariantList files;
        QStringList loaders;
    };
    std::shared_ptr<ICatalogueSearch> m_search;
    std::shared_ptr<IContentCatalogue> m_versions;
    std::shared_ptr<PackReleasePolicy> m_policy;
    QHash<QString, QString> m_identities;
    ContentListModel m_model;
    QTimer m_publish;
    Install m_install;
    QVariantMap m_pack;
    QVariantList m_files;
    QString m_input, m_notice, m_type;
    bool m_visible = false, m_curseforge = false, m_installing = false;
    int m_activeSearches = 0;
    quint64 m_generation = 0, m_nextInstall = 0, m_activeInstall = 0;
    QList<Row> m_rows;
    QQueue<Search> m_searches;
    QQueue<PendingInstall> m_installs;
    qsizetype find(const QString &) const;
    void publish();
    void pumpSearches();
    void queueSearch(Row &);
    void pumpInstalls();
    void enqueueInstall(Row &);
    bool installed(const Project &) const;
    bool locked(const Project &) const;
    void updateState(Row &);
};
} // namespace studio
