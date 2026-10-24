#include "InstallationPreferences.h"
namespace studio {
InstallationPreferences::InstallationPreferences(std::shared_ptr<IBrowseSettingsStore> store, QObject *parent)
    : QObject(parent), m_store(std::move(store)),
      m_optional(m_store->value("content/promptOptionalDependencies", true).toBool()) {}
void InstallationPreferences::setPromptOptionalDependencies(bool value) {
    if (m_optional == value)
        return;
    m_optional = value;
    m_store->save("content/promptOptionalDependencies", value);
    emit changed();
}
} // namespace studio
