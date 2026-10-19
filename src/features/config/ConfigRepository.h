#pragma once
#include "ConfigCodec.h"
#include "platform/PackFileStore.h"
namespace studio {
struct ConfigTarget {
    QString root, relativePath, modId, modTitle, icon, association;
};
struct ConfigDocument {
    ConfigTarget target;
    QByteArray original;
    ConfigDecodeResult decoded;
};
struct ConfigSaveResult {
    bool ok = false;
    QString error, backupPath;
    QByteArray bytes;
};
class IConfigRepository {
  public:
    virtual ~IConfigRepository() = default;
    virtual ConfigDocument load(const ConfigTarget &) const = 0;
    virtual ConfigSaveResult save(const ConfigDocument &, const QVariantMap &) const = 0;
};
class ConfigRepository final : public IConfigRepository {
  public:
    ConfigRepository(std::shared_ptr<const IPackFileReader> files,
                     std::shared_ptr<const IConfigFileWriter> writer, ConfigCodecs codecs)
        : m_files(std::move(files)), m_writer(std::move(writer)), m_codecs(std::move(codecs)) {}
    ConfigDocument load(const ConfigTarget &) const override;
    ConfigSaveResult save(const ConfigDocument &, const QVariantMap &) const override;

  private:
    std::shared_ptr<const IPackFileReader> m_files;
    std::shared_ptr<const IConfigFileWriter> m_writer;
    ConfigCodecs m_codecs;
    const IConfigCodec *codec(const QString &) const;
};
} // namespace studio
