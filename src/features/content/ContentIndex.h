#pragma once
#include <QVariantList>
namespace studio {
struct ContentIndexResult {
    QVariantList rows;
    QString error;
};
// App-owned public metadata, never launcher locks or pending/file mutation state.
// lookup/save run on workers. beginSnapshot only registers an in-memory revision.
class IContentIndex {
  public:
    virtual ~IContentIndex() = default;
    virtual quint64 beginSnapshot(const QVariantMap &) const {
        return 0;
    }
    virtual ContentIndexResult lookup(const QVariantMap &pack, const QVariantList &actual,
                                      quint64 snapshot = 0) const = 0;
    virtual QString save(const QVariantMap &pack, const QVariantList &actual) const = 0;
};
} // namespace studio
