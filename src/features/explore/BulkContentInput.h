#pragma once
#include "domain/Catalogue.h"
namespace studio {
struct BulkContentReference {
    QString raw, query, provider, type, slug, error;
    int line = 0;
    bool filename = false;
};
struct BulkContentInput {
    QVector<BulkContentReference> references;
    QString error;
};
BulkContentInput parseBulkContentInput(const QString &, const QString &defaultType = "mod");
// Names suggest candidates; only provider IDs establish content identity.
int bulkContentMatchRank(const BulkContentReference &, const Project &);
} // namespace studio
