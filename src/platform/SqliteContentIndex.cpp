#include "SqliteContentIndex.h"
#include "domain/Catalogue.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
namespace studio {
namespace {
const QStringList fileFields{"sourceIds",
                             "metadataSha1",
                             "identitySha1",
                             "metadataAttemptedAt",
                             "sourceUrls",
                             "provider",
                             "projectId",
                             "projectUrl",
                             "installedRelease",
                             "version",
                             "title",
                             "icon",
                             "author",
                             "authors",
                             "authorIcon",
                             "authorIds",
                             "environment",
                             "openSource",
                             "categories",
                             "tags",
                             "metadataResolved",
                             "identityResolved",
                             "curseforgeFingerprint"};
const QStringList projectFields{"projectUrl", "title",      "icon",      "author",
                                "authors",    "authorIcon", "authorIds", "environment",
                                "openSource", "categories", "tags"};
class Connection final {
  public:
    QSqlDatabase db;
    QString error;
    explicit Connection(const QString &path) : m_name(QUuid::createUuid().toString()) {
        if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
            error = "Could not create the local content index folder.";
            return;
        }
        db = QSqlDatabase::addDatabase("QSQLITE", m_name);
        db.setDatabaseName(path);
        db.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
        if (!db.open()) {
            error = "Could not open the local content index: " + db.lastError().text();
            return;
        }
        QSqlQuery query(db);
        if (!query.exec("PRAGMA user_version") || !query.next()) {
            error = query.lastError().text();
            return;
        }
        const int version = query.value(0).toInt();
        if (version != 0 && version != 1) {
            error = "This local content index uses an unsupported schema.";
            return;
        }
        if (version == 0) {
            if (!query.exec(
                    "SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'") ||
                query.next()) {
                error = "The local content index contains an unrecognized schema.";
                return;
            }
            if (!db.transaction()) {
                error = db.lastError().text();
                return;
            }
            const QStringList schema{
                "CREATE TABLE content_file(sha1 TEXT NOT NULL, category TEXT NOT NULL, metadata BLOB NOT "
                "NULL, PRIMARY KEY(sha1,category))",
                "CREATE TABLE content_project(provider TEXT NOT NULL, project_id TEXT NOT NULL, metadata "
                "BLOB NOT NULL, resolved_at INTEGER NOT NULL, PRIMARY KEY(provider,project_id))",
                "CREATE TABLE instance_file(instance TEXT NOT NULL, relative_path TEXT NOT NULL, sha1 TEXT "
                "NOT NULL, category TEXT NOT NULL, PRIMARY KEY(instance,relative_path))",
                "CREATE INDEX instance_content_hash ON instance_file(sha1,category)",
                "CREATE TABLE project_tag(provider TEXT NOT NULL, project_id TEXT NOT NULL, tag TEXT NOT "
                "NULL, PRIMARY KEY(provider,project_id,tag))",
                "CREATE INDEX content_tags ON project_tag(tag,provider,project_id)",
                "CREATE TABLE project_author(provider TEXT NOT NULL, project_id TEXT NOT NULL, position "
                "INTEGER NOT NULL, metadata BLOB NOT NULL, PRIMARY KEY(provider,project_id,position))",
                "PRAGMA user_version=1"};
            for (const auto &sql : schema)
                if (!query.exec(sql)) {
                    error = query.lastError().text();
                    db.rollback();
                    return;
                }
            if (!db.commit()) {
                error = db.lastError().text();
                db.rollback();
                return;
            }
        }
    }
    ~Connection() {
        db.close();
        db = {};
        QSqlDatabase::removeDatabase(m_name);
    }

  private:
    QString m_name;
};
bool validHash(const QString &hash) {
    static const QRegularExpression pattern("^[a-fA-F0-9]{40}$");
    return pattern.match(hash).hasMatch();
}
bool hasFingerprint(const QVariantMap &row) {
    bool valid = false;
    const auto fingerprint = row.value("curseforgeFingerprint").toULongLong(&valid);
    return valid && fingerprint <= 0xffffffffULL &&
           row.value("curseforgeFingerprint").toDouble() == double(fingerprint);
}
QVariantMap selected(const QVariantMap &row, const QStringList &fields) {
    QVariantMap result;
    for (const auto &field : fields)
        if (row.contains(field)) {
            if (field == "curseforgeFingerprint" && !hasFingerprint(row))
                continue;
            if ((field == "icon" || field == "authorIcon") && row.value(field).toString().startsWith("data:"))
                continue;
            if (field == "installedRelease")
                result[field] = releaseFromVariant(row.value(field).toMap()).toVariant();
            else if (field == "tags" || field == "categories")
                result[field] = row.value(field).toStringList();
            else
                result[field] = row.value(field);
        }
    return result;
}
QVariantMap decode(const QByteArray &bytes) {
    if (bytes.size() > 1024 * 1024)
        return {};
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    return error.error == QJsonParseError::NoError && doc.isObject() ? doc.object().toVariantMap()
                                                                     : QVariantMap{};
}
QString primaryProvider(const QVariantMap &row) {
    const auto ids = row.value("sourceIds").toMap();
    const auto provider = row.value("provider").toString();
    if ((provider == "modrinth" || provider == "curseforge") && !ids.value(provider).toString().isEmpty())
        return provider;
    return !ids.value("modrinth").toString().isEmpty()     ? "modrinth"
           : !ids.value("curseforge").toString().isEmpty() ? "curseforge"
                                                           : "";
}
bool identitiesAgree(const QVariantMap &actual, const QVariantMap &cached) {
    const auto left = actual.value("sourceIds").toMap(), right = cached.value("sourceIds").toMap();
    for (auto it = left.cbegin(); it != left.cend(); ++it)
        if (!it.value().toString().isEmpty() && !right.value(it.key()).toString().isEmpty() &&
            it.value() != right.value(it.key()))
            return false;
    return true;
}
QString replaceMembership(QSqlDatabase &db, const QVariantMap &pack, const QVariantList &actual) {
    const auto instance = pack.value("path").toString();
    if (instance.isEmpty())
        return {};
    QSqlQuery clear(db), member(db);
    clear.prepare("DELETE FROM instance_file WHERE instance=?");
    clear.addBindValue(instance);
    if (!clear.exec())
        return clear.lastError().text();
    member.prepare("INSERT INTO instance_file VALUES(?,?,?,?)");
    for (const auto &value : actual) {
        const auto row = value.toMap();
        const auto hash = row.value("sha1").toString();
        if (!validHash(hash) || row.value("directory").toBool())
            continue;
        member.bindValue(0, instance);
        member.bindValue(1, row.value("relativePath"));
        member.bindValue(2, hash.toLower());
        member.bindValue(3, row.value("category"));
        if (!member.exec())
            return member.lastError().text();
    }
    return {};
}
} // namespace
SqliteContentIndex::SqliteContentIndex(QString path) : m_path(std::move(path)) {}
quint64 SqliteContentIndex::beginSnapshot(const QVariantMap &pack) const {
    if (pack.value("path").toString().isEmpty())
        return 0;
    QMutexLocker lock(&m_snapshotMutex);
    m_snapshots[pack.value("path").toString()] = ++m_snapshot;
    return m_snapshot;
}
ContentIndexResult SqliteContentIndex::lookup(const QVariantMap &pack, const QVariantList &actual,
                                              quint64 snapshot) const {
    QMutexLocker lock(&m_mutex);
    Connection connection(m_path);
    if (!connection.error.isEmpty())
        return {actual, connection.error};
    ContentIndexResult result{actual, {}};
    const auto currentSnapshot = [&] {
        QMutexLocker state(&m_snapshotMutex);
        return !snapshot || m_snapshots.value(pack.value("path").toString()) == snapshot;
    };
    if (!pack.value("path").toString().isEmpty() && currentSnapshot()) {
        if (!connection.db.transaction())
            result.error = connection.db.lastError().text();
        else {
            result.error = replaceMembership(connection.db, pack, actual);
            if (!result.error.isEmpty() || !currentSnapshot())
                connection.db.rollback();
            else if (!connection.db.commit()) {
                result.error = connection.db.lastError().text();
                connection.db.rollback();
            }
        }
    }
    QSqlQuery file(connection.db), project(connection.db);
    file.prepare("SELECT metadata FROM content_file WHERE sha1=? AND category=?");
    project.prepare("SELECT metadata,resolved_at FROM content_project WHERE provider=? AND project_id=?");
    for (auto &value : result.rows) {
        auto row = value.toMap();
        const auto hash = row.value("sha1").toString();
        if (!validHash(hash) || row.value("directory").toBool())
            continue;
        file.bindValue(0, hash.toLower());
        file.bindValue(1, row.value("category"));
        if (!file.exec()) {
            result.error = file.lastError().text();
            continue;
        }
        if (!file.next())
            continue;
        const auto metadata = selected(decode(file.value(0).toByteArray()), fileFields);
        if (hasFingerprint(metadata)) {
            row["curseforgeFingerprint"] = metadata.value("curseforgeFingerprint");
            value = row;
        }
        if ((!metadata.value("metadataResolved").toBool() && !metadata.value("identityResolved").toBool()) ||
            primaryProvider(metadata).isEmpty() || !identitiesAgree(row, metadata))
            continue;
        const auto currentIds = row.value("sourceIds").toMap();
        const auto currentRelease = row.value("installedRelease").toMap();
        const auto installed = releaseFromVariant(currentRelease);
        const bool verifiedRelease =
            (installed.provider == "modrinth" || installed.provider == "curseforge") &&
            !installed.id.isEmpty() && !installed.projectId.isEmpty() &&
            installed.sha1.compare(hash, Qt::CaseInsensitive) == 0 &&
            currentIds.value(installed.provider).toString() == installed.projectId;
        const auto currentVersion = row.value("version");
        for (auto it = metadata.cbegin(); it != metadata.cend(); ++it)
            row[it.key()] = it.value();
        auto ids = row.value("sourceIds").toMap();
        for (auto it = currentIds.cbegin(); it != currentIds.cend(); ++it)
            if (!it.value().toString().isEmpty())
                ids[it.key()] = it.value();
        row["sourceIds"] = ids;
        if (verifiedRelease) {
            row["installedRelease"] = currentRelease;
            row["version"] = currentVersion.toString().isEmpty() ? installed.version : currentVersion;
        }
        const auto provider = primaryProvider(row), id = ids.value(provider).toString();
        project.bindValue(0, provider);
        project.bindValue(1, id);
        if (project.exec() && project.next()) {
            const auto fresh = selected(decode(project.value(0).toByteArray()), projectFields);
            const bool complete =
                row.value("metadataResolved").toBool() ||
                project.value(1).toLongLong() >= metadata.value("metadataAttemptedAt").toLongLong();
            if (!fresh.isEmpty() && complete) {
                row["metadataResolved"] = true;
                row["metadataSha1"] = hash;
            }
            for (auto it = fresh.cbegin(); it != fresh.cend(); ++it)
                if (complete || !row.contains(it.key()) || row.value(it.key()).toString().isEmpty())
                    row[it.key()] = it.value();
        }
        value = row;
    }
    return result;
}
QString SqliteContentIndex::save(const QVariantMap &pack, const QVariantList &actual) const {
    QMutexLocker lock(&m_mutex);
    Connection connection(m_path);
    if (!connection.error.isEmpty())
        return connection.error;
    auto &db = connection.db;
    if (!db.transaction())
        return db.lastError().text();
    auto failed = [&](const QString &message) {
        db.rollback();
        return message;
    };
    QSqlQuery file(db), project(db), membership(db), tags(db), authors(db), clear(db), previous(db);
    previous.prepare("SELECT metadata FROM content_file WHERE sha1=? AND category=?");
    file.prepare("INSERT INTO content_file VALUES(?,?,?) ON CONFLICT(sha1,category) DO UPDATE SET "
                 "metadata=excluded.metadata");
    project.prepare("INSERT INTO content_project VALUES(?,?,?,?) ON CONFLICT(provider,project_id) DO UPDATE "
                    "SET metadata=excluded.metadata,resolved_at=excluded.resolved_at");
    membership.prepare("INSERT INTO instance_file VALUES(?,?,?,?)");
    tags.prepare("INSERT OR IGNORE INTO project_tag VALUES(?,?,?)");
    authors.prepare("INSERT INTO project_author VALUES(?,?,?,?)");
    const auto instance = pack.value("path").toString();
    if (!instance.isEmpty()) {
        clear.prepare("DELETE FROM instance_file WHERE instance=?");
        clear.addBindValue(instance);
        if (!clear.exec())
            return failed(clear.lastError().text());
    }
    QSet<QPair<QString, QString>> writtenProjects;
    for (const auto &value : actual) {
        const auto row = value.toMap(), ids = row.value("sourceIds").toMap();
        const auto hash = row.value("sha1").toString(), category = row.value("category").toString();
        if (!validHash(hash) || row.value("directory").toBool())
            continue;
        if (!instance.isEmpty()) {
            membership.bindValue(0, instance);
            membership.bindValue(1, row.value("relativePath"));
            membership.bindValue(2, hash.toLower());
            membership.bindValue(3, category);
            if (!membership.exec())
                return failed(membership.lastError().text());
        }
        const auto provider = primaryProvider(row), id = ids.value(provider).toString();
        if (!row.value("metadataResolved").toBool() && !row.value("identityResolved").toBool() &&
            !hasFingerprint(row))
            continue;
        const auto release = row.value("installedRelease").toMap();
        if (!release.isEmpty() && !release.value("sha1").toString().isEmpty() &&
            release.value("sha1").toString().compare(hash, Qt::CaseInsensitive))
            return failed("Verified content metadata differs from its file hash.");
        auto metadata = selected(row, fileFields);
        if (row.value("metadataResolved").toBool()) {
            const auto proof = row.value("metadataSha1").toString();
            if (!proof.isEmpty() && proof.compare(hash, Qt::CaseInsensitive))
                return failed("Resolved metadata belongs to a different file hash.");
            metadata["metadataSha1"] = hash;
        }
        if (row.value("identityResolved").toBool() || row.value("metadataResolved").toBool())
            metadata["identitySha1"] = hash;
        if (!row.value("metadataResolved").toBool()) {
            previous.bindValue(0, hash.toLower());
            previous.bindValue(1, category);
            if (!previous.exec())
                return failed(previous.lastError().text());
            if (previous.next()) {
                const auto known = selected(decode(previous.value(0).toByteArray()), fileFields);
                if (known.value("metadataResolved").toBool() ||
                    (known.value("identityResolved").toBool() && !row.value("identityResolved").toBool())) {
                    metadata = known;
                    if (hasFingerprint(row))
                        metadata["curseforgeFingerprint"] = row.value("curseforgeFingerprint");
                }
            }
        }
        const auto bytes =
            QJsonDocument(QJsonObject::fromVariantMap(metadata)).toJson(QJsonDocument::Compact);
        if (bytes.size() > 1024 * 1024)
            return failed("Content metadata exceeds the local index limit.");
        file.bindValue(0, hash.toLower());
        file.bindValue(1, category);
        file.bindValue(2, bytes);
        if (!file.exec())
            return failed(file.lastError().text());
        const auto identity = qMakePair(provider, id);
        if (!row.value("metadataResolved").toBool() || provider.isEmpty() ||
            writtenProjects.contains(identity))
            continue;
        writtenProjects.insert(identity);
        project.bindValue(0, provider);
        project.bindValue(1, id);
        project.bindValue(2, QJsonDocument(QJsonObject::fromVariantMap(selected(row, projectFields)))
                                 .toJson(QJsonDocument::Compact));
        project.bindValue(3, QDateTime::currentMSecsSinceEpoch());
        if (!project.exec())
            return failed(project.lastError().text());
        for (const auto &table : {QString("project_tag"), QString("project_author")}) {
            clear.prepare("DELETE FROM " + table + " WHERE provider=? AND project_id=?");
            clear.addBindValue(provider);
            clear.addBindValue(id);
            if (!clear.exec())
                return failed(clear.lastError().text());
        }
        for (const auto &tag : row.value("tags", row.value("categories")).toStringList()) {
            tags.bindValue(0, provider);
            tags.bindValue(1, id);
            tags.bindValue(2, tag);
            if (!tags.exec())
                return failed(tags.lastError().text());
        }
        const auto members = row.value("authors").toList();
        for (int i = 0; i < members.size(); ++i) {
            authors.bindValue(0, provider);
            authors.bindValue(1, id);
            authors.bindValue(2, i);
            authors.bindValue(3, QJsonDocument::fromVariant(members[i]).toJson(QJsonDocument::Compact));
            if (!authors.exec())
                return failed(authors.lastError().text());
        }
    }
    if (!db.commit())
        return failed(db.lastError().text());
    return {};
}
} // namespace studio
