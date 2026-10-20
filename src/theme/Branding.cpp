#include "Branding.h"
#include "domain/ContentTypes.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
namespace studio {
Branding::Branding(QObject *parent) : QObject(parent) {
    QFile file(":/qt/qml/Conflux/Style/themes/brands.json");
    if (file.open(QIODevice::ReadOnly))
        m_brands = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}
QString Branding::label(const QString &key) const {
    return contentLabel(key);
}
QVariantMap Branding::info(const QString &key) const {
    auto canonical = key.toLower();
    for (const auto &loader : QStringList{"neoforge", "fabric", "forge", "quilt"})
        if (canonical.startsWith(loader + '-')) {
            canonical = loader;
            break;
        }
    auto data = m_brands.value(canonical).toMap();
    data["label"] = label(key);
    return data;
}
} // namespace studio
