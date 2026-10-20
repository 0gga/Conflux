#include "Appearance.h"
#include <cmath>
namespace studio {
QVariantList Appearance::controls() const {
    QVariantList result;
    const auto add = [&result](QString key, QString label, QString group, int low, int high, int step,
                               int initial, QString suffix) {
        result << QVariantMap{{"key", key},  {"label", label}, {"group", group},     {"min", low},
                              {"max", high}, {"step", step},   {"initial", initial}, {"suffix", suffix}};
    };
    add("uiScale", "Overall UI size", "Interface", 75, 150, 5, 100, "%");
    add("contentScale", "Content size", "Content cards", 75, 150, 5, 100, "%");
    add("artworkSize", "Artwork size", "Content cards", 64, 144, 4, 88, " px");
    add("cardPadding", "Card padding", "Content cards", 4, 24, 1, 10, " px");
    add("cardGap", "Space between cards", "Content cards", 4, 24, 1, 8, " px");
    add("filterScale", "Filter size", "Filters", 75, 150, 5, 100, "%");
    add("filterWidth", "Filter panel width", "Filters", 200, 360, 8, 232, " px");
    add("filterSpacing", "Space between filter sections", "Filters", 8, 28, 1, 16, " px");
    add("versionListHeight", "Version list height", "Filters", 120, 400, 20, 240, " px");
    return result;
}
Appearance::Appearance(QObject *parent, QString root)
    : Appearance(std::make_shared<NativeAppearanceStore>(std::move(root)), parent) {}
Appearance::Appearance(std::shared_ptr<IAppearanceStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store)) {
    const auto saved = m_store->load();
    for (const auto &item : controls()) {
        const auto field = item.toMap();
        const auto key = field.value("key").toString();
        bool valid;
        const auto number = saved.value(key).toDouble(&valid);
        m_values[key] = valid && std::isfinite(number) && number >= field.value("min").toDouble() &&
                                number <= field.value("max").toDouble()
                            ? number
                            : field.value("initial").toDouble();
    }
}
bool Appearance::setValue(const QString &key, double value) {
    for (const auto &item : controls()) {
        const auto field = item.toMap();
        if (field.value("key") != key)
            continue;
        if (!std::isfinite(value) || value < field.value("min").toDouble() ||
            value > field.value("max").toDouble())
            return false;
        if (m_values.value(key).toDouble() == value)
            return true;
        m_values[key] = value;
        m_store->save(m_values);
        emit changed();
        return true;
    }
    return false;
}
void Appearance::reset() {
    for (const auto &item : controls()) {
        const auto field = item.toMap();
        m_values[field.value("key").toString()] = field.value("initial");
    }
    m_store->save(m_values);
    emit changed();
}
} // namespace studio
