#pragma once
#include <QString>
namespace studio {
// Provider markup becomes a small native, palette-independent rich-text subset.
QString renderDescription(const QString &source, bool markdown);
} // namespace studio
