#pragma once
#include "ContentOperationState.h"
namespace studio {
struct ContentLockIntent {
    enum Route { Ignore, Source, Local, Reject } route = Ignore;
    QString error;
    ContentOperation operation;
};
// Plans routing only. The caller owns state, preferences, queueing and error publication.
ContentLockIntent planContentLockIntent(const QVariantMap &pack, const QVariantList &rows, bool locked,
                                        bool sourceOnly = false);
} // namespace studio
