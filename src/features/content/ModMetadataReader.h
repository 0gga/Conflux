#pragma once
#include "ArchiveReader.h"
#include <QVariantMap>
#include <memory>
namespace studio {
class IModMetadataReader {
  public:
    virtual ~IModMetadataReader() = default;
    virtual QVariantMap read(const QString &archive) const = 0;
};
class ModMetadataReader final : public IModMetadataReader {
  public:
    explicit ModMetadataReader(std::shared_ptr<const IArchiveReader> archives)
        : m_archives(std::move(archives)) {}
    QVariantMap read(const QString &) const override;

  private:
    std::shared_ptr<const IArchiveReader> m_archives;
};
} // namespace studio
