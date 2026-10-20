#pragma once
#include <QStringList>
namespace studio {
inline bool isKnownOpenSourceLicense(const QString &license) {
    static const QStringList known{"MIT",           "Apache-2.0",        "BSD-2-Clause",  "BSD-3-Clause",
                                   "ISC",           "MPL-2.0",           "LGPL-2.1-only", "LGPL-2.1-or-later",
                                   "LGPL-3.0-only", "LGPL-3.0-or-later", "GPL-2.0-only",  "GPL-2.0-or-later",
                                   "GPL-3.0-only",  "GPL-3.0-or-later",  "AGPL-3.0-only", "AGPL-3.0-or-later",
                                   "CC0-1.0",       "Unlicense"};
    return known.contains(license);
}
} // namespace studio
