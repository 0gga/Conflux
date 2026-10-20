#pragma once
#include "PackRepository.h"
#include "domain/Catalogue.h"
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QQueue>
#include <QSaveFile>
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
                    bool allowFabricBridge = false);
    void cancel();
  signals:
    void progress(QString status);
    void finished(studio::RepositoryResult result);

  private:
    struct Task {
        QString projectId, versionId;
    };
    QNetworkAccessManager m_network;
    QUrl m_api;
    QNetworkReply *m_reply = nullptr;
    int m_generation = 0, m_downloadIndex = 0;
    bool m_busy = false, m_applying = false;
    QString m_path, m_game, m_loader;
    QStringList m_allowedLoaders;
    QJsonObject m_expected;
    QQueue<Task> m_tasks;
    QMap<QString, Release> m_resolved;
    QMap<QString, QString> m_titles;
    QMap<QString, QJsonObject> m_existing;
    QMap<QString, QJsonObject> m_disabledReferences;
    QJsonArray m_references;
    QMap<QString, QJsonObject> m_rootIdentities;
    std::shared_ptr<QTemporaryDir> m_staging;
    std::shared_ptr<QSaveFile> m_downloadFile;
    void fail(const QString &message);
    void get(const QUrl &url, std::function<void(QJsonDocument)> callback);
    bool addResolved(const Release &release);
    void resolveNext();
    void downloadNext();
    void apply();
    bool safeDownloadUrl(const QUrl &url) const;
};
} // namespace studio
