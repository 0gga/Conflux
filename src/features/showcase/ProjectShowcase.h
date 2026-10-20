#pragma once
#include "ProjectShowcaseSource.h"
#include <QObject>
#include <QVariantList>
#include <memory>
namespace studio {
class ProjectShowcase final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap project READ project NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    Q_PROPERTY(QVariantList descriptionBlocks READ descriptionBlocks NOTIFY changed)
    Q_PROPERTY(QStringList gallery READ gallery NOTIFY changed)
    Q_PROPERTY(QVariantList versions READ versions NOTIFY changed)
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString filePath READ filePath NOTIFY changed)
    Q_PROPERTY(QString requestKey READ requestKey NOTIFY changed)
  public:
    ProjectShowcase(std::shared_ptr<IProjectShowcaseSource> source,
                    std::shared_ptr<const IInstalledModIdentity> identity, QObject *parent = nullptr);
    void open(const QVariantMap &pack, const QVariantMap &file);
    void openProject(const QVariantMap &pack, const QVariantMap &project);
    void close();
    void setCurseforgeKey(const QByteArray &key) {
        m_source->setCurseforgeKey(key);
    }
    QVariantMap project() const {
        return m_project;
    }
    QString description() const {
        return m_description;
    }
    QVariantList descriptionBlocks() const {
        return m_descriptionBlocks;
    }
    QStringList gallery() const {
        return m_gallery;
    }
    QVariantList versions() const {
        return m_versions;
    }
    QString notice() const {
        return m_notice;
    }
    bool busy() const {
        return m_busy;
    }
    QString filePath() const {
        return m_filePath;
    }
    QString requestKey() const {
        return m_requestKey;
    }
  signals:
    void changed();

  private:
    std::shared_ptr<IProjectShowcaseSource> m_source;
    std::shared_ptr<const IInstalledModIdentity> m_identity;
    QVariantMap m_project;
    QString m_description, m_notice, m_filePath, m_requestKey;
    void resolve(const InstalledModIdentity &, const QVariantMap &, int generation);
    QStringList m_gallery;
    QVariantList m_versions, m_descriptionBlocks;
    bool m_busy = false;
    int m_generation = 0;
};
} // namespace studio
