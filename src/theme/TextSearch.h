#pragma once
#include "domain/TextSearch.h"
#include <QObject>
#include <QtQml/qqmlregistration.h>
namespace studio {
class TextSearch final : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
  public:
    using QObject::QObject;
    Q_INVOKABLE QStringList filter(const QStringList &values, const QString &query) const {
        TextQuery matcher(query);
        QStringList groups[4];
        for (const auto &value : values) {
            const auto rank = matcher.rank(value);
            if (rank >= 0)
                groups[rank] << value;
        }
        return groups[0] + groups[1] + groups[2] + groups[3];
    }
    Q_INVOKABLE bool matches(const QString &text, const QString &query) const {
        return textMatches(text, query);
    }
};
} // namespace studio
