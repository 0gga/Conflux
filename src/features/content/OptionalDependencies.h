#pragma once
#include "ContentDependencyInstaller.h"
#include "ContentListModel.h"
#include "features/preferences/InstallationPreferences.h"
#include "features/preferences/PackReleasePolicy.h"
#include <QObject>
#include <functional>
namespace studio {
class IOptionalDependencySource {
  public:
    using Callback = std::function<void(ContentInstallRequest, QVariantMap, QString)>;
    virtual ~IOptionalDependencySource() = default;
    virtual void resolve(const QVariantMap &, const QVariantList &, const ContentDependency &,
                         const QStringList &, Callback) = 0;
    virtual void cancel() = 0;
    virtual void setCurseforgeKey(const QByteArray &) = 0;
};
// Suggestions are read-only until an individual Install action enters the normal mutation queue.
class OptionalDependencies final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(QVariantMap pack READ pack NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QAbstractItemModel *model READ model CONSTANT)
  public:
    using Install = std::function<bool(QVariantMap, QVariantMap)>;
    OptionalDependencies(std::shared_ptr<IOptionalDependencySource>, std::shared_ptr<InstallationPreferences>,
                         std::shared_ptr<PackReleasePolicy>, QObject *parent = nullptr);
    bool visible() const {
        return !m_rows.isEmpty();
    }
    QVariantMap pack() const {
        return m_pack;
    }
    QVariantList rows() const;
    QAbstractItemModel *model() {
        return &m_model;
    }
    void setInstaller(Install install) {
        m_install = std::move(install);
    }
    void offer(const QVariantMap &, const QVariantList &, const QVariantList &);
    void installationFinished(const QString &, const QVariantList &releases, const QVariantList &files,
                              const QString &error);
    Q_INVOKABLE void install(const QString &key);
    Q_INVOKABLE void retry(const QString &key);
    Q_INVOKABLE void dismiss();
  signals:
    void changed();

  private:
    struct Row {
        QString key;
        ContentDependency reference;
        ContentInstallRequest request;
        QVariantMap project;
        QString state = "waiting", error;
    };
    struct Offer {
        QVariantMap pack;
        QVariantList files, references;
    };
    ContentListModel m_model;
    std::shared_ptr<IOptionalDependencySource> m_source;
    std::shared_ptr<InstallationPreferences> m_preferences;
    std::shared_ptr<PackReleasePolicy> m_policy;
    Install m_install;
    QVariantMap m_pack;
    QVariantList m_files;
    QList<Row> m_rows;
    QList<Offer> m_offers;
    quint64 m_generation = 0;
    bool m_resolving = false;
    qsizetype find(const QString &) const;
    void append(const QVariantList &);
    void later();
    void pump();
    void nextOffer();
};
} // namespace studio
