#pragma once
#include "AppearanceStore.h"
#include <QObject>
#include <QtQml/qqmlregistration.h>
namespace studio {
class Appearance final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(QVariantList controls READ controls CONSTANT)
  public:
    explicit Appearance(QObject *parent = nullptr, QString storageOverride = {});
    Appearance(std::shared_ptr<IAppearanceStore>, QObject *parent = nullptr);
    QVariantMap values() const {
        return m_values;
    }
    QVariantList controls() const;
    Q_INVOKABLE bool setValue(const QString &, double);
    Q_INVOKABLE void reset();
    Q_INVOKABLE void resetValue(const QString &key);
    Q_INVOKABLE void resetGroup(const QString &group);
    Q_INVOKABLE qreal filterWidthFor(qreal available) const {
        return qMin(m_values.value("filterWidth").toDouble(), qMax(144.0, available * 0.4));
    }
  signals:
    void changed();

  private:
    std::shared_ptr<IAppearanceStore> m_store;
    QVariantMap m_values;
};
} // namespace studio
