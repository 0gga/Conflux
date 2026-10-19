#pragma once
#include <QVariantMap>
#include <QVariantList>
#include <QStringList>
namespace studio {
struct PackContentResult {
    QVariantList files;
    QStringList diagnostics;
    QString coverage;
};
class PackContent {
public:
    static PackContentResult scan(const QVariantMap& pack);
};
}
