#include "Theme.h"
#include <QColor>
#include <QDesktopServices>
#include <QUrl>
namespace studio {
Theme::Theme(QObject *parent, QString root)
    : Theme(std::make_shared<NativeThemeStore>(std::move(root)), parent) {}
Theme::Theme(std::shared_ptr<IThemeStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store)) {
    const auto saved = m_store->selected();
    select(builtInOled);
    if (!saved.isEmpty())
        select(saved);
    m_initializing = false;
}
bool Theme::select(const QString &file) {
    const auto input = m_store->read(file);
    const auto palette = input.error.isEmpty() ? ThemePalette::decode(input.bytes) : std::nullopt;
    if (!palette) {
        m_error = input.error.isEmpty() ? "Invalid theme: use schemaVersion 1 and valid colors for every "
                                          "token. The current theme was kept."
                                        : input.error;
        emit changed();
        return false;
    }
    m_palette = m_original = *palette;
    m_file = file;
    m_error.clear();
    if (!m_initializing)
        m_store->select(file);
    emit changed();
    return true;
}
bool Theme::setColor(const QString &token, const QString &value) {
    if (!m_palette.colors.contains(token) || !QColor::isValidColorName(value)) {
        m_error = "Choose a valid color for an existing theme role.";
        emit changed();
        return false;
    }
    const QColor color(value);
    m_palette.colors[token] = color.name(color.alpha() == 255 ? QColor::HexRgb : QColor::HexArgb).toUpper();
    m_error.clear();
    emit changed();
    return true;
}
bool Theme::saveCustom(const QString &name) {
    const ThemePalette draft{name.trimmed(), m_palette.colors};
    const auto result = m_store->save(draft.name, draft.encode());
    if (!result.error.isEmpty()) {
        m_error = result.error;
        emit changed();
        return false;
    }
    m_palette = m_original = draft;
    m_file = result.file;
    m_store->select(m_file);
    m_error.clear();
    emit changed();
    return true;
}
void Theme::discardChanges() {
    m_palette = m_original;
    m_error.clear();
    emit changed();
}
void Theme::reload() {
    select(m_file);
}
void Theme::openFolder() {
    QDesktopServices::openUrl(QUrl::fromLocalFile(folder()));
}
} // namespace studio
