#pragma once
#include "ContentPorts.h"
#include <QObject>
#include <QQueue>
#include <QSet>
#include <memory>
namespace studio {
struct ContentDependency {
    QString provider, projectId, versionId;
};
class IContentDependencySource {
  public:
    using Callback = std::function<void(ContentInstallRequest, QString)>;
    virtual ~IContentDependencySource() = default;
    virtual void resolve(const QVariantMap &, const QVariantList &, const ContentDependency &,
                         const QStringList &channels, Callback) = 0;
    virtual void cancel() = 0;
};
// Selected files commit first. Each dependency has its own verified transaction and failure boundary.
class ContentDependencyInstaller final : public QObject, public IContentInstaller {
    Q_OBJECT
  public:
    ContentDependencyInstaller(std::shared_ptr<IContentInstaller>, std::shared_ptr<IContentDependencySource>,
                               std::shared_ptr<const IContentFileStore>, QObject *parent = nullptr);
    void install(const QVariantMap &, const QVariantList &, const QVector<ContentInstallRequest> &,
                 Callback) override;
    void cancel() override;
    bool cancellable() const override;
  signals:
    void progress(QString);

  private:
    struct PendingDependency {
        ContentDependency reference;
        QStringList channels;
    };
    std::shared_ptr<IContentInstaller> m_leaf;
    std::shared_ptr<IContentDependencySource> m_source;
    std::shared_ptr<const IContentFileStore> m_files;
    QVariantMap m_pack;
    QVariantList m_rows, m_predictions;
    QQueue<ContentInstallRequest> m_roots;
    QQueue<PendingDependency> m_dependencies;
    QSet<QString> m_seen;
    QHash<QString, QString> m_selected;
    Callback m_callback;
    QStringList m_warnings, m_errors;
    bool m_cancelled = false, m_changed = false, m_leafActive = false, m_resolving = false;
    bool m_reloading = false, m_passthrough = false;
    quint64 m_generation = 0;
    void next();
    void later();
    void apply(ContentInstallRequest, bool root);
    void expand(const ContentInstallRequest &);
    bool retained(const ContentDependency &) const;
    void finish();
};
} // namespace studio
