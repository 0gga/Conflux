#include "ConfigCodec.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <functional>
namespace studio {
static QString pointerToken(QString value) {
    return value.replace('~', "~0").replace('/', "~1");
}
static QJsonValue replacement(const QJsonValue &old, const QVariant &value, QString &error) {
    if (old.isBool()) {
        if (value.metaType().id() != QMetaType::Bool)
            error = "Expected a toggle value.";
        return value.toBool();
    }
    if (old.isString())
        return value.toString();
    if (old.isArray()) {
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(value.toString().toUtf8(), &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isArray()) {
            error = "Expected a valid list.";
            return {};
        }
        for (const auto &entry : doc.array())
            if (entry.isObject() || entry.isArray()) {
                error = "Lists of objects are not editable in this editor.";
                return {};
            }
        const auto previous = old.toArray();
        const auto next = doc.array();
        for (qsizetype index = 0; index < next.size() && !previous.isEmpty(); ++index)
            if (next[index].type() != previous[std::min(index, previous.size() - 1)].type()) {
                error = "List items must keep their original type.";
                return {};
            }
        return next;
    }
    if (old.isDouble()) {
        bool valid = false;
        if (old.toVariant().metaType().id() == QMetaType::LongLong) {
            const auto number = value.toString().toLongLong(&valid);
            if (!valid)
                error = "Expected a whole number in the 64-bit range.";
            return QJsonValue(number);
        }
        const auto number = value.toString().toDouble(&valid);
        if (!valid || !std::isfinite(number))
            error = "Expected a finite number.";
        return number;
    }
    error = "Unsupported value type.";
    return {};
}
ConfigDecodeResult JsonConfigCodec::decode(const QByteArray &bytes) const {
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError)
        return {{}, "Invalid JSON: " + parse.errorString()};
    ConfigDecodeResult result;
    std::function<void(const QJsonValue &, QString, QString, QString)> visit =
        [&](const QJsonValue &value, QString id, QString label, QString section) {
            if (result.fields.size() >= 1000) {
                ++result.unsupported;
                return;
            }
            if (value.isObject()) {
                const auto object = value.toObject();
                for (auto it = object.begin(); it != object.end(); ++it)
                    visit(it.value(), id + "/" + pointerToken(it.key()), it.key(),
                          id.isEmpty() ? "General" : id.mid(1));
                return;
            }
            if (value.isNull()) {
                ++result.unsupported;
                return;
            }
            QString type = value.isBool()                                             ? "boolean"
                           : value.isString()                                         ? "string"
                           : value.isArray()                                          ? "list"
                           : value.toVariant().metaType().id() == QMetaType::LongLong ? "integer"
                                                                                      : "number";
            if (value.isArray())
                for (const auto &entry : value.toArray())
                    if (entry.isObject() || entry.isArray() ||
                        (entry.isDouble() && std::abs(entry.toDouble()) > 9007199254740991.0)) {
                        ++result.unsupported;
                        return;
                    }
            QVariant editable = value.toVariant();
            if (type == "integer")
                editable = QString::number(value.toInteger());
            else if (type == "number")
                editable = QString::number(value.toDouble(), 'g', 17);
            else if (type == "list")
                editable = QString::fromUtf8(QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact));
            result.fields.append(QVariantMap{{"id", id},
                                             {"label", label},
                                             {"section", section},
                                             {"type", type},
                                             {"value", editable},
                                             {"description", ""}});
        };
    if (document.isObject())
        visit(document.object(), {}, {}, {});
    else {
        int index = 0;
        for (const auto &value : document.array())
            visit(value, "/" + QString::number(index), QString::number(index++), "General");
    }
    return result;
}
ConfigEncodeResult JsonConfigCodec::encode(const QByteArray &original, const QVariantMap &changes) const {
    const auto decoded = decode(original);
    if (!decoded.error.isEmpty())
        return {{}, decoded.error};
    QSet<QString> allowed;
    for (const auto &field : decoded.fields)
        allowed.insert(field.toMap().value("id").toString());
    for (auto it = changes.begin(); it != changes.end(); ++it)
        if (!allowed.contains(it.key()))
            return {{}, "Unknown config field."};
    auto document = QJsonDocument::fromJson(original);
    QString error;
    std::function<QJsonValue(QJsonValue, QString)> update = [&](QJsonValue node, QString id) -> QJsonValue {
        if (changes.contains(id))
            return replacement(node, changes.value(id), error);
        if (node.isObject()) {
            auto object = node.toObject();
            for (auto it = object.begin(); it != object.end(); ++it)
                it.value() = update(it.value(), id + "/" + pointerToken(it.key()));
            return object;
        }
        if (node.isArray()) {
            auto array = node.toArray();
            for (qsizetype index = 0; index < array.size(); ++index)
                array[index] = update(array[index], id + "/" + QString::number(index));
            return array;
        }
        return node;
    };
    const auto root =
        update(document.isObject() ? QJsonValue(document.object()) : QJsonValue(document.array()), {});
    if (!error.isEmpty())
        return {{}, error};
    QByteArray output =
        root.isObject() ? QJsonDocument(root.toObject()).toJson() : QJsonDocument(root.toArray()).toJson();
    if (original.contains("\r\n"))
        output.replace("\n", "\r\n");
    return {output, {}};
}
ConfigCodecs defaultConfigCodecs() {
    return {std::make_shared<JsonConfigCodec>(), std::make_shared<TomlConfigCodec>(),
            std::make_shared<PropertiesConfigCodec>()};
}
} // namespace studio
