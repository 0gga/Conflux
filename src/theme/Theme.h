#pragma once
#include <QObject>
#include <QVariantMap>
#include <QSettings>
#include <memory>
#include <QtQml/qqmlregistration.h>

namespace studio {
class Theme : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString file READ file NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString folder READ folder CONSTANT)
    Q_PROPERTY(QStringList available READ available NOTIFY changed)
public:
    explicit Theme(QObject* parent = nullptr, QString storageOverride = {});
    QVariantMap colors() const { return m_colors; }
    QString name() const { return m_name; }
    QString file() const { return m_file; }
    QString error() const { return m_error; }
    QString folder() const;
    QStringList available() const;
    Q_INVOKABLE bool select(const QString& file);
    Q_INVOKABLE void reload();
    Q_INVOKABLE void openFolder();
signals:
    void changed();
private:
    QVariantMap m_colors;
    QString m_name, m_error, m_file, m_storageOverride;
    std::unique_ptr<QSettings> m_settings;
    bool m_initializing = true;
};
}
