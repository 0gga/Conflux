#include "ContentAccess.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QLockFile>
#include <QStandardPaths>
namespace studio {
struct ContentAccessGate {
    QRecursiveMutex mutex;
    std::unique_ptr<QLockFile> file;
    int depth = 0;
};
static std::shared_ptr<ContentAccessGate> instanceGate(const QString &root) {
    static QMutex registryMutex;
    static QHash<QString, std::weak_ptr<ContentAccessGate>> registry;
    auto path = QFileInfo(root).canonicalFilePath();
    if (path.isEmpty())
        path = QDir(root).absolutePath();
    path = QDir::cleanPath(path);
#ifdef Q_OS_WIN
    path = path.toCaseFolded();
#endif
    QMutexLocker guard(&registryMutex);
    auto gate = registry.value(path).lock();
    if (!gate) {
        gate = std::make_shared<ContentAccessGate>();
        const auto directory = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) +
                               "/Conflux/content-transactions";
        if (QDir().mkpath(directory)) {
            const auto key = QString::fromLatin1(
                QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
            gate->file = std::make_unique<QLockFile>(directory + '/' + key + ".lock");
        }
        registry[path] = gate;
    }
    return gate;
}
ContentAccess::ContentAccess(const QString &root, Mode mode)
    : m_gate(instanceGate(root)), m_guard(&m_gate->mutex) {
    if (!m_gate->file) {
        m_error = "Could not create the content transaction lock. Nothing was changed.";
        return;
    }
    if (m_gate->depth == 0 && !m_gate->file->tryLock(mode == Write ? 0 : 5000)) {
        m_error = "Another Conflux operation is using this instance. Wait for it to finish, then retry.";
        return;
    }
    ++m_gate->depth;
    m_held = true;
}
ContentAccess::~ContentAccess() {
    if (m_held && --m_gate->depth == 0)
        m_gate->file->unlock();
}
} // namespace studio
