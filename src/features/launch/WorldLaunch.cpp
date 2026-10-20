#include "WorldLaunch.h"
namespace studio {
WorldLaunch::WorldLaunch(std::vector<std::shared_ptr<const IWorldLaunchAdapter>> adapters,
                         std::shared_ptr<ILaunchUrlOpener> opener,
                         std::shared_ptr<const IPackFileReader> files, QObject *parent)
    : QObject(parent), m_adapters(std::move(adapters)), m_opener(std::move(opener)),
      m_files(std::move(files)) {}
void WorldLaunch::setPack(const QVariantMap &pack) {
    if (pack == m_pack)
        return;
    m_pack = pack;
    m_status.clear();
    emit changed();
}
const IWorldLaunchAdapter *WorldLaunch::adapter() const {
    for (const auto &adapter : m_adapters)
        if (adapter->supports(m_pack))
            return adapter.get();
    return nullptr;
}
QString WorldLaunch::unavailableReason() const {
    const auto *launch = adapter();
    if (!launch)
        return "Direct world launch is currently available for Modrinth installations. Conflux and "
               "CurseForge game launch support is still being built.";
    const auto reason = launch->unavailableReason(m_pack);
    if (!reason.isEmpty())
        return reason;
    if (!m_opener->available(launch->worldUrl(m_pack, "world").scheme()))
        return "Install or repair Modrinth App to register its launch links.";
    return {};
}
bool WorldLaunch::packAvailable() const {
    const auto *launch = adapter();
    return launch && !m_pack.value("launcherInstanceId").toString().isEmpty() &&
           m_opener->available(launch->worldUrl(m_pack, {}).scheme());
}
void WorldLaunch::playPack() {
    if (!packAvailable())
        m_status = "This installation has no supported launcher handoff yet.";
    else
        m_status = m_opener->open(adapter()->worldUrl(m_pack, {})) ? "Launch request sent to Modrinth App."
                                                                   : "Could not open Modrinth App.";
    emit changed();
}
void WorldLaunch::play(const QString &relative) {
    m_status = unavailableReason();
    if (m_status.isEmpty()) {
        bool found = false;
        const auto root = m_pack.value("path").toString();
        for (const auto &entry : m_files->list(root, "saves")) {
            const auto world = entry.toMap();
            if (world.value("relativePath") == relative && world.value("directory").toBool()) {
                found = true;
                break;
            }
        }
        if (!found || !relative.startsWith("saves/") || relative.mid(6).contains('/') ||
            relative.contains('\\') || relative.mid(6) == "..")
            m_status = "This world is no longer available in the selected pack. Refresh Worlds.";
        else {
            const auto level = m_files->read(root, relative + "/level.dat", 16);
            if (!level.error.isEmpty() || level.bytes.isEmpty())
                m_status = "This folder has no readable level.dat and cannot be launched as a world.";
            else if (!m_opener->open(adapter()->worldUrl(m_pack, relative.mid(6))))
                m_status = "Could not send the world launch request to Modrinth App.";
            else
                m_status =
                    "Launch request sent to Modrinth. It will use your selected Minecraft account and open " +
                    relative.mid(6) + ".";
        }
    }
    emit changed();
}
} // namespace studio
