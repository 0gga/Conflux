#pragma once
#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <vector>
namespace studio {
struct ConfigDecodeResult {
    QVariantList fields;
    QString error;
    int unsupported = 0;
};
struct ConfigEncodeResult {
    QByteArray bytes;
    QString error;
};
class IConfigCodec {
  public:
    virtual ~IConfigCodec() = default;
    virtual QStringList extensions() const = 0;
    virtual ConfigDecodeResult decode(const QByteArray &) const = 0;
    virtual ConfigEncodeResult encode(const QByteArray &, const QVariantMap &changes) const = 0;
};
class JsonConfigCodec final : public IConfigCodec {
  public:
    QStringList extensions() const override {
        return {"json"};
    }
    ConfigDecodeResult decode(const QByteArray &) const override;
    ConfigEncodeResult encode(const QByteArray &, const QVariantMap &) const override;
};
class TomlConfigCodec final : public IConfigCodec {
  public:
    QStringList extensions() const override {
        return {"toml"};
    }
    ConfigDecodeResult decode(const QByteArray &) const override;
    ConfigEncodeResult encode(const QByteArray &, const QVariantMap &) const override;
};
class PropertiesConfigCodec final : public IConfigCodec {
  public:
    QStringList extensions() const override {
        return {"properties"};
    }
    ConfigDecodeResult decode(const QByteArray &) const override;
    ConfigEncodeResult encode(const QByteArray &, const QVariantMap &) const override;
};
// The repository receives a codec registry; a new format does not change the editor.
using ConfigCodecs = std::vector<std::shared_ptr<const IConfigCodec>>;
ConfigCodecs defaultConfigCodecs();
} // namespace studio
