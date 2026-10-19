#include "ConfigEditor.h"
#include <QFutureWatcher>
#include <QtConcurrent>
namespace studio {
ConfigEditor::ConfigEditor(std::shared_ptr<const IConfigRepository> repository, QObject *parent)
    : QObject(parent), m_repository(std::move(repository)) {}
void ConfigEditor::open(const ConfigTarget &target) {
    if (target.root == m_document.target.root && target.relativePath == path())
        return;
    const auto generation = ++m_generation;
    m_document = {};
    m_document.target = target;
    m_changes.clear();
    m_error.clear();
    m_status.clear();
    m_busy = true;
    emit changed();
    emit fieldsChanged();
    auto *watcher = new QFutureWatcher<ConfigDocument>(this);
    connect(watcher, &QFutureWatcher<ConfigDocument>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_document = result;
        m_error = result.decoded.error;
        m_busy = false;
        emit fieldsChanged();
        emit changed();
    });
    const auto repository = m_repository;
    watcher->setFuture(QtConcurrent::run([repository, target] { return repository->load(target); }));
}
void ConfigEditor::close() {
    if (m_document.target.relativePath.isEmpty() && !m_busy && !dirty())
        return;
    ++m_generation;
    m_document = {};
    m_changes.clear();
    m_error.clear();
    m_status.clear();
    m_busy = false;
    emit fieldsChanged();
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
    m_busy = true;
    m_error.clear();
    emit changed();
    auto *watcher = new QFutureWatcher<ConfigSaveResult>(this);
    connect(watcher, &QFutureWatcher<ConfigSaveResult>::finished, this, [this, watcher, generation] {
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
        const auto target = m_document.target;
        m_document = {};
        m_changes.clear();
        open(target);
        m_status = "Saved. A backup of the previous file was created.";
        emit changed();
        emit saved();
    });
    const auto repository = m_repository;
    const auto document = m_document;
    const auto changes = m_changes;
    watcher->setFuture(
        QtConcurrent::run([repository, document, changes] { return repository->save(document, changes); }));
}
void ConfigEditor::discard() {
    if (m_busy)
        return;
    m_changes.clear();
    m_error.clear();
    emit fieldsChanged();
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
