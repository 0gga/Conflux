#include "ContentVisibility.h"
#include <QCryptographicHash>
namespace studio {
ContentVisibility::ContentVisibility(std::shared_ptr<IBrowseSettingsStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store)),
      m_hidden(m_store->value("browse/hiddenContent").toStringList()) {}
QStringList ContentVisibility::identities(const QVariantMap &item, const QString &instance) const {
    auto ids = item.value("sourceIds").toMap();
    if (ids.isEmpty() && !item.value("id").toString().isEmpty())
        ids[item.value("provider").toString()] = item.value("id");
    QStringList result;
    for (auto it = ids.cbegin(); it != ids.cend(); ++it)
        if (!it.key().isEmpty() && !it.value().toString().isEmpty())
            result << it.key() + ':' + it.value().toString();
    if (!item.value("sha1").toString().isEmpty())
        result << "sha1:" + item.value("sha1").toString();
    if (!instance.isEmpty() && !item.value("relativePath").toString().isEmpty())
        result << "path:" + QString::fromLatin1(
                                QCryptographicHash::hash(
                                    (instance + '|' + item.value("relativePath").toString()).toUtf8(),
                                    QCryptographicHash::Sha256)
                                    .toHex());
    return result;
}
bool ContentVisibility::hidden(const QVariantMap &item, const QString &instance) const {
    for (const auto &key : identities(item, instance))
        if (m_hidden.contains(key))
            return true;
    return false;
}
void ContentVisibility::setHidden(const QVariantMap &item, bool hidden, const QString &instance) {
    for (const auto &key : identities(item, instance)) {
        if (hidden && !m_hidden.contains(key))
            m_hidden << key;
        if (!hidden)
            m_hidden.removeAll(key);
    }
    m_store->save("browse/hiddenContent", m_hidden);
    emit changed();
}
} // namespace studio
