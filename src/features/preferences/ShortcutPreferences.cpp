#include "ShortcutPreferences.h"
#include <QKeySequence>
#include <QPointer>
#include <QSet>
namespace studio {
QVariantList ShortcutPreferences::definitions() {
    return {QVariantMap{{"key", "back"}, {"title", "Back"}, {"iconName", "back"}, {"default", "Alt+Left"}},
            QVariantMap{
                {"key", "forward"}, {"title", "Forward"}, {"iconName", "forward"}, {"default", "Alt+Right"}},
            QVariantMap{{"key", "discover"},
                        {"title", "Explore content"},
                        {"iconName", "compass"},
                        {"default", "Ctrl+K"}},
            QVariantMap{
                {"key", "newPack"}, {"title", "New modpack"}, {"iconName", "plus"}, {"default", "Ctrl+N"}},
            QVariantMap{{"key", "palette"},
                        {"title", "Search Conflux"},
                        {"iconName", "search"},
                        {"default", "Ctrl+Shift+P"}}};
}
QVariantMap ShortcutPreferences::defaults() {
    QVariantMap result;
    for (const auto &value : definitions()) {
        const auto row = value.toMap();
        result[row.value("key").toString()] = row.value("default");
    }
    return result;
}
QString ShortcutPreferences::validate(const QString &text, QString *canonical) {
    canonical->clear();
    if (text.isEmpty())
        return {};
    if (text.size() > 128)
        return "Choose a single shortcut.";
    const auto sequence = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (sequence.count() != 1 || sequence[0].key() == Qt::Key_unknown || sequence[0].key() == 0)
        return "Choose a single shortcut.";
    const auto key = sequence[0].key();
    const auto mods = sequence[0].keyboardModifiers();
    if (key == Qt::Key_Escape || key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt ||
        key == Qt::Key_Meta)
        return "Choose another shortcut.";
    if (!(mods & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) &&
        !(key >= Qt::Key_F1 && key <= Qt::Key_F35))
        return "Use Ctrl, Alt, or a function key.";
#ifdef Q_OS_WIN
    if (mods.testFlag(Qt::MetaModifier))
        return "Windows-key shortcuts are reserved.";
#endif
    *canonical = sequence.toString(QKeySequence::PortableText);
    static const QSet<QString> reserved = [] {
        QSet<QString> result;
        for (const auto &text : QStringList{
                 "Alt+F4",          "Alt+Tab",   "Alt+Shift+Tab", "Ctrl+Alt+Tab",    "Ctrl+Alt+Shift+Tab",
                 "Ctrl+Alt+Delete", "Ctrl+C",    "Ctrl+V",        "Ctrl+X",          "Ctrl+A",
                 "Ctrl+Z",          "Ctrl+Y",    "Ctrl+Shift+Z",  "Ctrl+S",          "Ctrl+Backspace",
                 "Ctrl+Delete",     "Ctrl+Left", "Ctrl+Right",    "Ctrl+Shift+Left", "Ctrl+Shift+Right",
                 "Ctrl+Home",       "Ctrl+End"})
            result.insert(QKeySequence::fromString(text, QKeySequence::PortableText)
                              .toString(QKeySequence::PortableText));
        return result;
    }();
    if (reserved.contains(*canonical))
        return "This shortcut is reserved for the system or editing.";
    return {};
}
ShortcutPreferences::ShortcutPreferences(std::shared_ptr<IBrowseSettingsStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store)), m_model(this), m_bindings(defaults()) {
    try {
        m_overrides = m_store->value("keyboard/shortcuts", QVariantMap{}).toMap();
    } catch (...) {
    }
    auto restored = m_bindings;
    bool valid = true;
    for (auto it = restored.begin(); it != restored.end(); ++it)
        if (m_overrides.contains(it.key())) {
            QString canonical;
            if (m_overrides.value(it.key()).metaType().id() != QMetaType::QString ||
                !validate(m_overrides.value(it.key()).toString(), &canonical).isEmpty())
                valid = false;
            else
                it.value() = canonical;
        }
    QSet<QString> used;
    for (auto it = restored.cbegin(); it != restored.cend(); ++it) {
        const auto binding = it.value().toString();
        if (!binding.isEmpty() && used.contains(binding))
            valid = false;
        used.insert(binding);
    }
    if (valid)
        m_bindings = restored;
    else
        for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it)
            m_overrides.remove(it.key());
    connect(this, &ShortcutPreferences::changed, this, [this] { m_model.setRows(rows()); });
    m_model.setRows(rows());
}
QVariantList ShortcutPreferences::rows() const {
    QVariantList result;
    for (const auto &value : definitions()) {
        auto row = value.toMap();
        const auto id = row.value("key").toString(), binding = m_bindings.value(id).toString();
        row["contentKey"] = id;
        row["sequence"] = binding;
        row["display"] = binding.isEmpty() ? QString("Unassigned")
                                           : QKeySequence::fromString(binding, QKeySequence::PortableText)
                                                 .toString(QKeySequence::NativeText);
        row["custom"] = binding != row.value("default").toString();
        result << row;
    }
    return result;
}
QString ShortcutPreferences::recordingTitle() const {
    for (const auto &value : definitions())
        if (value.toMap().value("key") == m_recording)
            return value.toMap().value("title").toString();
    return {};
}
QString ShortcutPreferences::candidate() const {
    return m_candidate.isEmpty() ? QString{}
                                 : QKeySequence::fromString(m_candidate, QKeySequence::PortableText)
                                       .toString(QKeySequence::NativeText);
}
bool ShortcutPreferences::persist(const QVariantMap &bindings, const QVariantMap &overrides) {
    const QPointer<ShortcutPreferences> guard(this);
    const auto store = m_store;
    try {
        store->save("keyboard/shortcuts", overrides);
    } catch (...) {
        if (guard) {
            m_error = "Could not save shortcuts.";
            emit captureChanged();
        }
        return false;
    }
    if (!guard)
        return false;
    m_bindings = bindings;
    m_overrides = overrides;
    m_error.clear();
    emit changed();
    if (guard)
        emit captureChanged();
    return true;
}
bool ShortcutPreferences::setBinding(const QString &action, const QString &text) {
    if (!m_bindings.contains(action))
        return false;
    QString canonical;
    m_error = validate(text, &canonical);
    if (m_error.isEmpty() && !canonical.isEmpty())
        for (const auto &value : definitions()) {
            const auto row = value.toMap();
            const auto id = row.value("key").toString();
            if (id != action && m_bindings.value(id) == canonical) {
                m_error = "Already used by " + row.value("title").toString() + ".";
                break;
            }
        }
    if (!m_error.isEmpty()) {
        emit captureChanged();
        return false;
    }
    if (m_bindings.value(action) == canonical) {
        emit captureChanged();
        return true;
    }
    auto bindings = m_bindings, overrides = m_overrides;
    bindings[action] = canonical;
    if (defaults().value(action) == canonical)
        overrides.remove(action);
    else
        overrides[action] = canonical;
    return persist(bindings, overrides);
}
bool ShortcutPreferences::reset(const QString &action) {
    return defaults().contains(action) && setBinding(action, defaults().value(action).toString());
}
bool ShortcutPreferences::resetAll() {
    auto overrides = m_overrides;
    for (auto it = m_bindings.cbegin(); it != m_bindings.cend(); ++it)
        overrides.remove(it.key());
    const QPointer<ShortcutPreferences> guard(this);
    const auto generation = m_captureGeneration;
    const bool saved = persist(defaults(), overrides);
    if (guard && saved && generation == m_captureGeneration)
        cancelCapture();
    return saved;
}
void ShortcutPreferences::beginCapture(const QString &action) {
    if (!m_bindings.contains(action))
        return;
    ++m_captureGeneration;
    m_recording = action;
    m_candidate.clear();
    m_candidateKey = 0;
    m_error.clear();
    emit captureChanged();
}
void ShortcutPreferences::cancelCapture() {
    if (!capturing())
        return;
    ++m_captureGeneration;
    m_recording.clear();
    m_candidate.clear();
    m_candidateKey = 0;
    m_error.clear();
    emit captureChanged();
}
void ShortcutPreferences::capturePress(int key, int modifiers) {
    if (!capturing())
        return;
    if (key == Qt::Key_Escape) {
        cancelCapture();
        return;
    }
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt || key == Qt::Key_Meta ||
        key == Qt::Key_AltGr)
        return;
    const QKeySequence sequence{QKeyCombination{Qt::KeyboardModifiers::fromInt(modifiers), Qt::Key(key)}};
    m_error = validate(sequence.toString(QKeySequence::PortableText), &m_candidate);
    m_candidateKey = m_error.isEmpty() ? key : 0;
    if (!m_error.isEmpty())
        m_candidate.clear();
    emit captureChanged();
}
bool ShortcutPreferences::captureRelease(int key) {
    if (!capturing() || !m_candidateKey || key != m_candidateKey)
        return false;
    const auto action = m_recording, sequence = m_candidate;
    m_candidateKey = 0;
    m_candidate.clear();
    const QPointer<ShortcutPreferences> guard(this);
    const auto generation = m_captureGeneration;
    const bool saved = setBinding(action, sequence);
    if (guard && saved && generation == m_captureGeneration)
        cancelCapture();
    return saved;
}
bool ShortcutPreferences::clearCaptured() {
    if (!capturing())
        return false;
    const auto action = m_recording;
    const QPointer<ShortcutPreferences> guard(this);
    const auto generation = m_captureGeneration;
    const bool saved = setBinding(action, {});
    if (guard && saved && generation == m_captureGeneration)
        cancelCapture();
    return saved;
}
} // namespace studio
