#pragma once
#include "PackRepository.h"
#include "domain/Catalogue.h"
#include "platform/VerifiedDownload.h"
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQueue>
#include <QSet>
#include <QTemporaryDir>
#include <functional>
#include <memory>

namespace studio {
class ModInstaller : public QObject {
    Q_OBJECT
  public:
    explicit ModInstaller(QObject *parent = nullptr, QUrl api = QUrl("https://api.modrinth.com/v2/"));
    ~ModInstaller() override;
    bool busy() const {
        return m_busy;
    }
    bool cancellable() const {
        return m_busy && !m_applying;
    }
    void start(const QString &path, const Release &release, const QString &title,
               const QJsonObject &projectIds);
    void startBatch(const QString &path, const QVector<ContentInstallRequest> &requests,
                    bool allowFabricBridge = false, bool referenceOnly = false);
    void cancel();
  signals:
    void progress(QString status);
    void finished(studio::RepositoryResult result);

  private:
    struct Task {
        QString projectId, versionId;
    };
    QNetworkAccessManager m_network;
    VerifiedDownload m_download;
    bool m_referenceOnly = false;
    QMap<QString, QString> m_rootPaths;
    QUrl m_api;
    QNetworkReply *m_reply = nullptr;
    int m_generation = 0, m_downloadIndex = 0;
    bool m_busy = false, m_applying = false;
    QString m_path, m_game, m_loader;
    QStringList m_allowedLoaders, m_allowedChannels;
    QSet<QString> m_shallowRoots;
    QJsonObject m_expected;
    QQueue<Task> m_tasks;
    QMap<QString, Release> m_resolved;
    QMap<QString, QString> m_titles;
    QMap<QString, QJsonObject> m_existing;
    QMap<QString, QJsonObject> m_disabledReferences;
    QJsonArray m_references;
    QMap<QString, QJsonObject> m_rootIdentities;
    std::shared_ptr<QTemporaryDir> m_staging;
    void fail(const QString &message);
    void get(const QUrl &url, std::function<void(QJsonDocument)> callback);
    bool addResolved(const Release &release);
    void resolveNext();
    void downloadNext();
    void apply();
    bool safeDownloadUrl(const QUrl &url) const;
};
} // namespace studio
