#include "Appearance.h"
#include <cmath>
namespace studio {
QVariantList Appearance::controls() const {
    QVariantList result;
    const auto add = [&result](QString key, QString label, QString group, int low, int high, int step,
                               int initial, QString suffix) {
        result << QVariantMap{{"key", key},         {"label", label},   {"group", group},
                              {"min", low},         {"max", high},      {"step", step},
                              {"initial", initial}, {"suffix", suffix}, {"kind", "number"}};
    };
    add("uiScale", "Overall UI size", "Interface", 75, 150, 5, 100, "%");
    add("contentScale", "Content size", "Content cards", 75, 150, 5, 100, "%");
    add("artworkSize", "Artwork size", "Content cards", 64, 144, 4, 88, " px");
    add("cardPadding", "Card padding", "Content cards", 4, 24, 1, 10, " px");
    add("installedOpacity", "Installed content opacity", "Content cards", 25, 100, 5, 50, "%");
    add("cardGap", "Space between cards", "Content cards", 4, 24, 1, 8, " px");
    add("filterScale", "Filter size", "Filters", 75, 150, 5, 100, "%");
    add("filterWidth", "Filter panel width", "Filters", 200, 360, 8, 232, " px");
    add("filterSpacing", "Space between filter sections", "Filters", 8, 28, 1, 16, " px");
    add("versionListHeight", "Version list height", "Filters", 120, 400, 20, 240, " px");
    add("libraryRowHeight", "Library row height", "Interface", 76, 140, 4, 96, " px");
    add("contentRowHeight", "Instance content row height", "Interface", 64, 112, 2, 82, " px");
    add("scrollbarWidth", "Scrollbar width", "Interface", 6, 12, 1, 8, " px");
    add("galleryFitPercent", "Gallery image fit", "Interface", 60, 100, 1, 83, "%");
    add("tooltipDelay", "Tooltip delay", "Interface", 0, 1500, 100, 600, " ms");
    const auto toggle = [&result](QString key, QString label, QString group, int initial) {
        result << QVariantMap{{"key", key}, {"label", label}, {"group", group},    {"kind", "toggle"},
                              {"min", 0},   {"max", 1},       {"initial", initial}};
    };
    result << QVariantMap{{"key", "navigationMode"},
                          {"label", "Navigation sidebar"},
                          {"group", "Interface"},
                          {"kind", "choice"},
                          {"min", 0},
                          {"max", 2},
                          {"initial", 0},
                          {"choices", QStringList{"Automatic", "Icons only", "Expanded when space allows"}}};
    toggle("previewVisible", "Show live settings preview", "Interface", 1);
    toggle("tooltips", "Show tooltips", "Interface", 1);
    toggle("reduceMotion", "Reduce interface motion", "Interface", 0);
    toggle("showAuthor", "Show author", "Content cards", 1);
    toggle("showSummary", "Show description summary", "Content cards", 1);
    toggle("showDownloads", "Show provider download counts", "Content cards", 1);
    toggle("showFollowers", "Show follower counts", "Content cards", 1);
    toggle("showUpdated", "Show update age", "Content cards", 1);
    toggle("showEnvironment", "Show environment icon", "Content cards", 1);
    toggle("showCategories", "Show category tags", "Content cards", 1);
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
        // Preserve the previously configured opacity while correcting its presentation meaning.
        const auto number =
            saved.value(key, key == "installedOpacity" ? saved.value("unavailableOpacity") : QVariant{})
                .toDouble(&valid);
        m_values[key] = valid && std::isfinite(number) && number >= field.value("min").toDouble() &&
                                number <= field.value("max").toDouble() &&
                                (field.value("kind") == "number" || std::floor(number) == number)
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
            value > field.value("max").toDouble() ||
            (field.value("kind") != "number" && std::floor(value) != value))
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
void Appearance::resetValue(const QString &key) {
    for (const auto &item : controls()) {
        const auto field = item.toMap();
        if (field.value("key") == key) {
            setValue(key, field.value("initial").toDouble());
            return;
        }
    }
}
void Appearance::resetGroup(const QString &group) {
    bool dirty = false;
    for (const auto &item : controls()) {
        const auto field = item.toMap();
        const auto key = field.value("key").toString();
        if (field.value("group") == group && m_values.value(key) != field.value("initial")) {
            m_values[key] = field.value("initial");
            dirty = true;
        }
    }
    if (dirty) {
        m_store->save(m_values);
        emit changed();
    }
}
} // namespace studio
