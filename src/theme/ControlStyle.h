#pragma once
#include <QObject>
#include <QtQml/qqmlregistration.h>
namespace studio {
class ControlStyle final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(int controlRadius READ controlRadius CONSTANT)
    Q_PROPERTY(int tabRadius READ tabRadius CONSTANT)
    Q_PROPERTY(int subtabRadius READ subtabRadius CONSTANT)
    Q_PROPERTY(int panelRadius READ panelRadius CONSTANT)
  public:
    using QObject::QObject;
    int controlRadius() const {
        return 10;
    }
    int tabRadius() const {
        return 22;
    }
    int subtabRadius() const {
        return 18;
    }
    int panelRadius() const {
        return 14;
    }
};
} // namespace studio
