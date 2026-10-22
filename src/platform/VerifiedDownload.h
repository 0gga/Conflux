#pragma once
#include "domain/Catalogue.h"
#include <QObject>
#include <atomic>
#include <functional>
#include <memory>
namespace studio {
struct VerifiedDownloadResult {
    QString path, error;
};
class VerifiedDownload final : public QObject {
    Q_OBJECT
  public:
    explicit VerifiedDownload(QObject *parent = nullptr, bool local = false)
        : QObject(parent), m_local(local) {}
    ~VerifiedDownload() override {
        cancel();
    }
    void start(const Release &, const QString &directory, const QString &archiveType,
               std::function<void(VerifiedDownloadResult)>, const QString &existingFile = {});
    void cancel() {
        if (m_cancelled)
            *m_cancelled = true;
    }
    bool busy() const {
        return bool(m_cancelled);
    }
  signals:
    void progress(QString);

  private:
    bool m_local = false;
    std::shared_ptr<std::atomic_bool> m_cancelled;
};
} // namespace studio
