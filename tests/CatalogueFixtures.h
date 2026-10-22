#pragma once
#include "domain/CatalogueSearch.h"
#include "platform/BrowseSettingsStore.h"
#include <QCoreApplication>
#include <QTimer>
namespace catalogue_test {
class Settings final : public studio::IBrowseSettingsStore {
  public:
    QVariantMap data;
    QVariant value(const QString &key, const QVariant &fallback = {}) const override {
        return data.value(key, fallback);
    }
    void save(const QString &key, const QVariant &value) override {
        data[key] = value;
    }
};
class Search final : public QObject, public studio::ICatalogueSearch {
  public:
    struct Call {
        studio::CatalogueQuery query;
        int offset, limit;
        Callback callback;
        bool delivered = false;
    };
    QVector<studio::Project> projects;
    QList<Call> calls;
    bool delayed = false;
    explicit Search(int count = 80) {
        for (int i = 0; i < count; ++i) {
            studio::Project p;
            p.provider = "modrinth";
            p.id = QString::number(i);
            p.title = "Project " + p.id;
            p.type = "mod";
            p.sourceIds["modrinth"] = p.id;
            p.summary = "A catalogue fixture";
            p.downloads = 10000 - i;
            projects << p;
        }
    }
    void fetch(const studio::CatalogueQuery &query, int offset, int limit, Callback callback) override {
        calls << Call{query, offset, limit, std::move(callback)};
        if (!delayed) {
            const int index = calls.size() - 1;
            QTimer::singleShot(0, this, [this, index] { deliver(index); });
        }
    }
    void deliver(int index, const QString &error = {}) {
        if (calls[index].delivered)
            return;
        calls[index].delivered = true;
        const auto call = calls[index];
        QVector<studio::Project> rows;
        for (const auto &p : projects)
            if ((call.query.projectIds.isEmpty() || call.query.projectIds.contains(p.id)) &&
                (call.query.text.isEmpty() || p.title.contains(call.query.text, Qt::CaseInsensitive)))
                rows << p;
        call.callback({error.isEmpty() ? rows.mid(call.offset, call.limit) : QVector<studio::Project>{},
                       rows.size(), error});
    }
    void facets(std::function<void(QVariantMap, QString)> callback) override {
        callback({{"versions", QStringList{"1.21.1", "1.20.1"}}}, {});
    }
    void setKey(const QByteArray &) override {}
};
} // namespace catalogue_test
