#include "PackReleasePolicy.h"
#include "domain/ReleaseStages.h"
#include <QCryptographicHash>
#include <QDir>
namespace studio {
PackReleasePolicy::PackReleasePolicy(std::shared_ptr<IBrowseSettingsStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store)) {}
QString PackReleasePolicy::instanceKey(const QString &path) {
    auto normalized = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return normalized;
}
QStringList PackReleasePolicy::channels(const QString &path) const {
    if (path.isEmpty())
        return {"release"};
    const auto key = instanceKey(path);
    if (!m_cache.contains(key)) {
        const auto storageKey =
            "content/release-stages/" +
            QString::fromLatin1(QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex());
        const auto saved = m_store->value(storageKey).toStringList();
        auto stages = normalizedReleaseStages(saved);
        // Corrupt/obsolete settings must not silently opt stable content into prereleases.
        if (stages.isEmpty())
            stages = {"release"};
        m_cache.insert(key, stages);
    }
    return m_cache.value(key);
}
void PackReleasePolicy::setChannels(const QString &path, const QStringList &input) {
    const auto stages = effectiveReleaseStages(input);
    if (path.isEmpty() || (!input.isEmpty() && stages.isEmpty()) || channels(path) == stages)
        return;
    const auto storageKey =
        "content/release-stages/" +
        QString::fromLatin1(
            QCryptographicHash::hash(instanceKey(path).toUtf8(), QCryptographicHash::Sha256).toHex());
    m_store->save(storageKey, stages);
    m_cache.insert(instanceKey(path), stages);
    ++m_revision;
    emit changed();
    emit policyChanged(path);
}
bool PackReleasePolicy::allows(const QString &path, const QString &channel) const {
    return releaseStageAllowed(channel, channels(path));
}
} // namespace studio
