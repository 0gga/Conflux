#include "ConfigEditor.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent>
namespace studio {
ConfigEditor::ConfigEditor(std::shared_ptr<const IConfigRepository> repository, QObject *parent)
    : QObject(parent), m_repository(std::move(repository)) {}
void ConfigEditor::open(const ConfigTarget &target) {
    if (target.root == m_document.target.root && target.relativePath == path())
        return;
    load(target);
}
void ConfigEditor::load(ConfigTarget target, QString status) {
    const auto generation = ++m_generation;
    const QPointer<ConfigEditor> guard(this);
    const auto repository = m_repository;
    m_document = {};
    m_document.target = target;
    m_changes.clear();
    m_error.clear();
    m_status = std::move(status);
    m_busy = true;
    auto *watcher = new QFutureWatcher<ConfigDocument>(this);
    connect(watcher, &QFutureWatcher<ConfigDocument>::finished, this, [this, guard, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_document = result;
        m_error = result.decoded.error;
        m_busy = false;
        emit fieldsChanged();
        if (guard && generation == m_generation)
            emit changed();
    });
    watcher->setFuture(QtConcurrent::run([repository, target] {
        try {
            return repository->load(target);
        } catch (...) {
            ConfigDocument failed;
            failed.target = target;
            failed.decoded.error = "Could not load this config. Reload to try again.";
            return failed;
        }
    }));
    if (!guard || generation != m_generation || !m_busy)
        return;
    emit changed();
    if (guard && generation == m_generation)
        emit fieldsChanged();
}
void ConfigEditor::close() {
    if (m_document.target.relativePath.isEmpty() && !m_busy && !dirty())
        return;
    const auto generation = ++m_generation;
    const QPointer<ConfigEditor> guard(this);
    m_document = {};
    m_changes.clear();
    m_error.clear();
    m_status.clear();
    m_busy = false;
    emit fieldsChanged();
    if (guard && generation == m_generation)
        emit changed();
}
QVariant ConfigEditor::fieldValue(const QString &id) const {
    if (m_changes.contains(id))
        return m_changes.value(id);
    for (const auto &entry : fields())
        if (entry.toMap().value("id") == id)
            return entry.toMap().value("value");
    return {};
}
void ConfigEditor::setField(const QString &id, const QVariant &value) {
    if (m_busy)
        return;
    for (const auto &entry : fields()) {
        const auto field = entry.toMap();
        if (field.value("id") == id) {
            if (field.value("value") == value)
                m_changes.remove(id);
            else
                m_changes[id] = value;
            m_error.clear();
            m_status.clear();
            emit changed();
            return;
        }
    }
}
void ConfigEditor::save() {
    if (m_busy || !dirty())
        return;
    const auto generation = m_generation;
    const QPointer<ConfigEditor> guard(this);
    // An accepted Save belongs to this immutable document, even if a notification
    // navigates elsewhere or disposes the editor while its worker is running.
    const auto repository = m_repository;
    const auto document = m_document;
    const auto changes = m_changes;
    m_busy = true;
    m_error.clear();
    auto *watcher = new QFutureWatcher<ConfigSaveResult>(this);
    connect(watcher, &QFutureWatcher<ConfigSaveResult>::finished, this,
            [this, guard, watcher, generation, target = document.target] {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (generation != m_generation)
                    return;
                m_busy = false;
                if (!result.ok) {
                    m_error = result.error;
                    emit changed();
                    return;
                }
                // Reload through the repository to rebuild field metadata from the saved file.
                const auto reloadGeneration = m_generation + 1;
                load(target, result.backupPath.isEmpty()
                                 ? "Saved."
                                 : "Saved. A backup of the previous file was created.");
                if (guard && reloadGeneration == m_generation)
                    emit saved();
            });
    watcher->setFuture(QtConcurrent::run([repository, document, changes] {
        try {
            return repository->save(document, changes);
        } catch (...) {
            return ConfigSaveResult{false, "Could not save this config. Your edits were kept; try again."};
        }
    }));
    if (guard && generation == m_generation && m_busy)
        emit changed();
}
void ConfigEditor::discard() {
    if (m_busy)
        return;
    const auto generation = m_generation;
    const QPointer<ConfigEditor> guard(this);
    m_changes.clear();
    m_error.clear();
    emit fieldsChanged();
    if (guard && generation == m_generation)
        emit changed();
}
void ConfigEditor::reload() {
    if (m_busy || dirty())
        return;
    const auto target = m_document.target;
    m_document = {};
    open(target);
}
} // namespace studio
