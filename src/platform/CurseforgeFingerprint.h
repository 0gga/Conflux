#pragma once
#include <QString>
namespace studio {
struct CurseforgeFingerprintResult {
    quint32 value = 0;
    QString error;
};
// Read-only, streaming work. Exact SHA1 confirmation is required before an API match is trusted.
CurseforgeFingerprintResult curseforgeFingerprint(const QString &path, const QString &expectedSha1);
} // namespace studio
