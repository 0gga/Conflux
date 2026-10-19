#pragma once
#include "ConfigRepository.h"
#include <QObject>
namespace studio {
class ConfigEditor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList fields READ fields NOTIFY fieldsChanged)
    Q_PROPERTY(QString title READ title NOTIFY fieldsChanged)
    Q_PROPERTY(QString path READ path NOTIFY fieldsChanged)
    Q_PROPERTY(QString icon READ icon NOTIFY fieldsChanged)
    Q_PROPERTY(QString association READ association NOTIFY fieldsChanged)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(int unsupported READ unsupported NOTIFY fieldsChanged)
  public:
    explicit ConfigEditor(std::shared_ptr<const IConfigRepository>, QObject *parent = nullptr);
    QVariantList fields() const {
        return m_document.decoded.fields;
    }
    QString title() const {
        return m_document.target.modTitle;
    }
    QString path() const {
        return m_document.target.relativePath;
    }
    QString icon() const {
        return m_document.target.icon;
    }
    QString association() const {
        return m_document.target.association;
    }
    QString error() const {
        return m_error;
    }
    QString status() const {
        return m_status;
    }
    bool busy() const {
        return m_busy;
    }
    bool dirty() const {
        return !m_changes.isEmpty();
    }
    int unsupported() const {
        return m_document.decoded.unsupported;
    }
    void open(const ConfigTarget &);
    void close();
    Q_INVOKABLE QVariant fieldValue(const QString &) const;
    Q_INVOKABLE void setField(const QString &, const QVariant &);
    Q_INVOKABLE void save();
    Q_INVOKABLE void discard();
    Q_INVOKABLE void reload();
  signals:
    void fieldsChanged();
    void changed();
    void saved();

  private:
    std::shared_ptr<const IConfigRepository> m_repository;
    ConfigDocument m_document;
    QVariantMap m_changes;
    QString m_error, m_status;
    int m_generation = 0;
    bool m_busy = false;
};
} // namespace studio
