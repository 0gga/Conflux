#pragma once
#include <QMap>
#include <QString>
#include <QStringList>
namespace studio {
inline QString contentLabel(const QString &key) {
    static const QMap<QString, QString> names{{"fabric", "Fabric"},
                                              {"forge", "Forge"},
                                              {"neoforge", "NeoForge"},
                                              {"quilt", "Quilt"},
                                              {"vanilla", "Vanilla"},
                                              {"forge-family", "Forge / NeoForge"},
                                              {"iris", "Iris"},
                                              {"optifine", "OptiFine"},
                                              {"modrinth", "Modrinth"},
                                              {"curseforge", "CurseForge"},
                                              {"conflux", "Conflux"},
                                              {"client_only", "Client"},
                                              {"client", "Client"},
                                              {"server", "Server"},
                                              {"client_and_server", "Client or server"},
                                              {"client_or_server", "Client or server"},
                                              {"both", "Client or server"},
                                              {"server_only", "Server"},
                                              {"dedicated_server_only", "Server"},
                                              {"singleplayer_only", "Singleplayer"},
                                              {"client_only_server_optional", "Client"},
                                              {"server_only_client_optional", "Server"},
                                              {"client_or_server_prefers_both", "Client or server"},
                                              {"mod", "Mods"},
                                              {"resourcepack", "Resource Packs"},
                                              {"datapack", "Data Packs"},
                                              {"shader", "Shaders"}};
    const auto lower = key.toLower();
    static const QStringList loaders{"neoforge", "fabric", "forge", "quilt"};
    for (const auto &loader : loaders)
        if (lower.size() > loader.size() && lower.at(loader.size()) == QLatin1Char('-') &&
            lower.startsWith(loader))
            return names.value(loader) + QLatin1Char(' ') + key.mid(loader.size() + 1);
    return names.value(lower, key);
}
inline QString contentCategory(const QString &type) {
    static const QMap<QString, QString> categories{{"mod", "mods"},
                                                   {"resourcepack", "resourcepacks"},
                                                   {"shader", "shaderpacks"},
                                                   {"datapack", "datapacks"}};
    return categories.value(type);
}
inline QString categoryType(const QString &category) {
    static const QMap<QString, QString> types{{"mods", "mod"},
                                              {"resourcepacks", "resourcepack"},
                                              {"shaderpacks", "shader"},
                                              {"datapacks", "datapack"}};
    return types.value(category, "mod");
}
} // namespace studio
