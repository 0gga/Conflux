#include "VersionChangelogs.h"
#include "DescriptionRenderer.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QtConcurrent>
#include <algorithm>
namespace studio {
static QString contentVersion(const Release &release) {
    auto label = (release.version.isEmpty() ? release.title : release.version).trimmed();
    const auto original = label;
    QStringList qualifiers = release.loaders;
    for (const auto &game : release.gameVersions) {
        qualifiers << game << "mc" + game << "minecraft" + game;
        // Publishers may label a snapshot build with its declared game's base version.
        const auto base = game.section('-', 0, 0);
        if (base != game && QRegularExpression("^\\d+(?:\\.\\d+)+$").match(base).hasMatch())
            qualifiers << base << "mc" + base << "minecraft" + base;
    }
    for (const auto &qualifier : qualifiers) {
        if (qualifier.isEmpty())
            continue;
        const auto token = QRegularExpression::escape(qualifier);
        auto candidate = label;
        candidate.remove(QRegularExpression("(?:\\(\\s*" + token + "\\s*\\)|\\[\\s*" + token + "\\s*\\])",
                                            QRegularExpression::CaseInsensitiveOption));
        candidate.remove(QRegularExpression("(^|[\\s+_\\-])" + token + "(?=$|[\\s+_\\-])",
                                            QRegularExpression::CaseInsensitiveOption));
        candidate.remove(QRegularExpression("^[\\s+_\\-]+|[\\s+_\\-]+$"));
        if (!candidate.isEmpty())
            label = candidate;
    }
    label.remove(QRegularExpression("^[\\s+_\\-]+|[\\s+_\\-]+$"));
    if (label.isEmpty())
        return original.isEmpty() ? release.id : original;
    label.remove(QRegularExpression("^v(?=\\d)", QRegularExpression::CaseInsensitiveOption));
    return label.simplified();
}
VersionChangelogs::VersionChangelogs(std::shared_ptr<IProjectShowcaseSource> source, QObject *parent)
    : QAbstractListModel(parent), m_source(std::move(source)) {}
void VersionChangelogs::clear() {
    beginResetModel();
    ++m_generation;
    m_entries.clear();
    m_versions.clear();
    m_aliases.clear();
    m_pending.clear();
    m_selected = -1;
    m_active = 0;
    endResetModel();
    emit selectionChanged();
}
void VersionChangelogs::setReleases(const QVector<Release> &releases) {
    setGroups(groupReleases(releases));
}
QVector<VersionChangelogs::Group> VersionChangelogs::groupReleases(const QVector<Release> &releases) {
    QVector<Group> entries;
    QSet<QString> aliases;
    auto sorted = releases;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Release &a, const Release &b) { return a.published > b.published; });
    QHash<QString, int> groups;
    for (const auto &release : sorted) {
        const auto alias = release.provider + ':' + release.id;
        if (release.id.isEmpty() || aliases.contains(alias))
            continue;
        const auto label = contentVersion(release);
        const auto key = label.isEmpty() ? alias : label.toCaseFolded();
        auto position = groups.value(key, -1);
        if (position < 0) {
            position = entries.size();
            groups.insert(key, position);
            Group entry;
            entry.release = release;
            entry.label = label;
            entries << entry;
        }
        auto &entry = entries[position];
        entry.aliases << alias;
        aliases.insert(alias);
        if ((entry.release.provider != "modrinth" && release.provider == "modrinth") ||
            (entry.release.provider == release.provider && entry.release.changelog.isEmpty() &&
             !release.changelog.isEmpty()))
            entry.release = release;
    }
    return entries;
}
void VersionChangelogs::setGroups(const QVector<Group> &groups) {
    clear();
    beginResetModel();
    m_entries.reserve(groups.size());
    m_versions.reserve(groups.size());
    for (const auto &group : groups) {
        Entry entry;
        entry.release = group.release;
        entry.label = group.label;
        entry.aliases = group.aliases;
        for (const auto &alias : group.aliases)
            m_aliases.insert(alias, m_entries.size());
        m_entries << entry;
        auto row = entry.release.toVariant();
        row["version"] = entry.label;
        m_versions << row;
    }
    endResetModel();
    emit selectionChanged();
}
QVariantList VersionChangelogs::versions() const {
    return m_versions;
}
QVariant VersionChangelogs::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size() || role != Qt::UserRole + 1)
        return {};
    const auto &entry = m_entries[index.row()];
    return QVariantMap{
        {"version", entry.label}, {"blocks", entry.blocks},
        {"error", entry.error},   {"busy", entry.requested && !entry.loaded && entry.error.isEmpty()},
        {"loaded", entry.loaded}, {"published", entry.release.published}};
}
QVariantMap VersionChangelogs::selectedRelease() const {
    if (m_selected < 0)
        return {};
    auto row = m_entries[m_selected].release.toVariant();
    row["version"] = m_entries[m_selected].label;
    return row;
}
QVariantList VersionChangelogs::selectedBlocks() const {
    return m_selected < 0 ? QVariantList{} : m_entries[m_selected].blocks;
}
QString VersionChangelogs::selectedNotice() const {
    return m_selected < 0 ? QString{} : m_entries[m_selected].error;
}
bool VersionChangelogs::selectedBusy() const {
    return m_selected >= 0 && m_entries[m_selected].requested && !m_entries[m_selected].loaded &&
           m_entries[m_selected].error.isEmpty();
}
void VersionChangelogs::select(const QVariantMap &release) {
    const auto index =
        m_aliases.value(release.value("provider").toString() + ':' + release.value("id").toString(), -1);
    if (index < 0)
        return;
    const bool changed = m_selected != index;
    m_selected = index;
    request(index);
    if (changed)
        emit selectionChanged();
}
void VersionChangelogs::request(int index, bool retry) {
    if (index < 0 || index >= m_entries.size())
        return;
    auto &entry = m_entries[index];
    if (entry.loaded || (entry.requested && (!retry || entry.error.isEmpty())))
        return;
    entry.requested = true;
    entry.error.clear();
    m_pending.enqueue(index);
    notify(index);
    pump();
}
void VersionChangelogs::notify(int index) {
    emit dataChanged(this->index(index), this->index(index), {Qt::UserRole + 1});
    if (index == m_selected)
        emit selectionChanged();
}
void VersionChangelogs::pump() {
    while (m_active < 2 && !m_pending.isEmpty()) {
        const auto index = m_pending.dequeue(), generation = m_generation;
        ++m_active;
        const auto release = m_entries[index].release;
        const QPointer<VersionChangelogs> guard(this);
        const auto delivered = std::make_shared<bool>(false);
        m_source->loadChangelog(
            release, [guard, index, generation, delivered,
                      markdown = release.provider == "modrinth"](QString raw, QString error) {
                if (!guard || generation != guard->m_generation || *delivered)
                    return;
                *delivered = true;
                if (!error.isEmpty()) {
                    guard->finish(index, generation, {}, error);
                    return;
                }
                auto *watcher = new QFutureWatcher<QVariantList>(guard);
                connect(watcher, &QFutureWatcher<QVariantList>::finished, guard,
                        [guard, watcher, index, generation] {
                            const auto blocks = watcher->result();
                            watcher->deleteLater();
                            if (guard)
                                guard->finish(index, generation, blocks);
                        });
                watcher->setFuture(
                    QtConcurrent::run([raw, markdown] { return describeContent(raw, markdown).blocks; }));
            });
    }
}
void VersionChangelogs::finish(int index, int generation, QVariantList blocks, QString error) {
    if (generation != m_generation)
        return;
    auto &entry = m_entries[index];
    entry.blocks = std::move(blocks);
    entry.error = std::move(error);
    entry.loaded = entry.error.isEmpty();
    --m_active;
    notify(index);
    QTimer::singleShot(0, this, &VersionChangelogs::pump);
}
} // namespace studio
