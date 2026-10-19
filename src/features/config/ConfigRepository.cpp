#include "ConfigRepository.h"
#include <QFileInfo>
namespace studio {
const IConfigCodec *ConfigRepository::codec(const QString &path) const {
    const auto extension = QFileInfo(path).suffix().toLower();
    for (const auto &codec : m_codecs)
        if (codec->extensions().contains(extension))
            return codec.get();
    return nullptr;
}
ConfigDocument ConfigRepository::load(const ConfigTarget &target) const {
    ConfigDocument result;
    result.target = target;
    if (target.modId.isEmpty() ||
        (!target.relativePath.startsWith("config/") && !target.relativePath.startsWith("defaultconfigs/"))) {
        result.decoded.error = "Only configs with an identified related mod can be edited.";
        return result;
    }
    const auto *parser = codec(target.relativePath);
    if (!parser) {
        result.decoded.error = "This config format is not supported by the visual editor yet.";
        return result;
    }
    const auto file = m_files->read(target.root, target.relativePath, 2 * 1024 * 1024);
    if (!file.error.isEmpty()) {
        result.decoded.error = file.error;
        return result;
    }
    result.original = file.bytes;
    result.decoded = parser->decode(file.bytes);
    return result;
}
ConfigSaveResult ConfigRepository::save(const ConfigDocument &document, const QVariantMap &changes) const {
    if (!document.decoded.error.isEmpty() || document.target.modId.isEmpty())
        return {false, "Reload a supported identified config before saving."};
    const auto *parser = codec(document.target.relativePath);
    if (!parser)
        return {false, "Unsupported config format."};
    const auto encoded = parser->encode(document.original, changes);
    if (!encoded.error.isEmpty())
        return {false, encoded.error};
    const auto written = m_writer->writeConfig(document.target.root, document.target.relativePath,
                                               document.original, encoded.bytes);
    return {written.ok, written.error, written.backupPath, encoded.bytes};
}
} // namespace studio
