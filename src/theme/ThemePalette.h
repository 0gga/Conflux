#pragma once
#include <QVariantList>
#include <QVariantMap>
#include <optional>
namespace studio {
struct ThemePalette {
    QString name;
    QVariantMap colors;
    static QVariantList groups();
    static std::optional<ThemePalette> decode(const QByteArray &);
    QByteArray encode() const;
};
} // namespace studio
