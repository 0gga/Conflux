#pragma once
#include <QString>
#include <QVariantList>
namespace studio {
struct RenderedDescription {
    QString html;
    QVariantList blocks;
};
// Parse provider markup once into themed native text, image rows and table cells.
RenderedDescription describeContent(const QString &source, bool markdown);
QString renderDescription(const QString &source, bool markdown);
} // namespace studio
