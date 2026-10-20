#pragma once
#include "ThemePalette.h"
#include "ThemeStore.h"
#include <QObject>
#include <QtQml/qqmlregistration.h>
namespace studio {
class Theme : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QVariantList colorGroups READ colorGroups CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString file READ file NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool edited READ edited NOTIFY changed)
    Q_PROPERTY(QString folder READ folder CONSTANT)
    Q_PROPERTY(QStringList available READ available NOTIFY changed)
  public:
    explicit Theme(QObject *parent = nullptr, QString storageOverride = {});
    Theme(std::shared_ptr<IThemeStore>, QObject *parent = nullptr);
    QVariantMap colors() const {
        return m_palette.colors;
    }
    QVariantList colorGroups() const {
        return ThemePalette::groups();
    }
    QString name() const {
        return m_palette.name;
    }
    QString file() const {
        return m_file;
    }
    QString error() const {
        return m_error;
    }
    bool edited() const {
        return m_palette.colors != m_original.colors;
    }
    QString folder() const {
        return m_store->folder();
    }
    QStringList available() const {
        return m_store->available();
    }
    Q_INVOKABLE bool select(const QString &);
    Q_INVOKABLE bool setColor(const QString &token, const QString &color);
    Q_INVOKABLE bool saveCustom(const QString &name);
    Q_INVOKABLE void discardChanges();
    Q_INVOKABLE void reload();
    Q_INVOKABLE void openFolder();
  signals:
    void changed();

  private:
    std::shared_ptr<IThemeStore> m_store;
    ThemePalette m_palette, m_original;
    QString m_error, m_file;
    bool m_initializing = true;
};
} // namespace studio
