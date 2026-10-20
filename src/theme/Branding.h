#pragma once
#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>
namespace studio {
class Branding final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
  public:
    explicit Branding(QObject *parent = nullptr);
    Q_INVOKABLE QString label(const QString &) const;
    Q_INVOKABLE QVariantMap info(const QString &) const;

  private:
    QVariantMap m_brands;
};
} // namespace studio
