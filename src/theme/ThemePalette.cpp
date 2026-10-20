#include "ThemePalette.h"
#include <QColor>
#include <QJsonDocument>
#include <QJsonObject>
namespace studio {
QVariantList ThemePalette::groups() {
    QVariantList result;
    const QList<QPair<QString, QStringList>> groups{
        {"surfaces", {"background", "panel", "card", "input", "hover", "selection", "border"}},
        {"text", {"text", "muted"}},
        {"actions", {"accent", "accentText"}},
        {"status", {"warning", "warningSurface", "danger"}}};
    for (const auto &group : groups)
        result << QVariantMap{{"key", group.first}, {"tokens", group.second}};
    return result;
}
std::optional<ThemePalette> ThemePalette::decode(const QByteArray &bytes) {
    const auto doc = QJsonDocument::fromJson(bytes).object();
    ThemePalette result{doc.value("name").toString().trimmed(), {}};
    if (doc.value("schemaVersion").toInt() != 1 || result.name.isEmpty())
        return std::nullopt;
    for (const auto &entry : groups()) {
        const auto group = entry.toMap();
        const auto values = doc.value(group.value("key").toString()).toObject();
        for (const auto &key : group.value("tokens").toStringList()) {
            const auto value = values.value(key).toString();
            if (!QColor::isValidColorName(value))
                return std::nullopt;
            result.colors[key] = value;
        }
    }
    return result;
}
QByteArray ThemePalette::encode() const {
    QJsonObject doc{{"schemaVersion", 1}, {"name", name}};
    for (const auto &entry : groups()) {
        const auto group = entry.toMap();
        QJsonObject values;
        for (const auto &key : group.value("tokens").toStringList())
            values[key] = colors.value(key).toString();
        doc[group.value("key").toString()] = values;
    }
    return QJsonDocument(doc).toJson(QJsonDocument::Indented);
}
} // namespace studio
