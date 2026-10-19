#include "ConfigCodec.h"
#include "toml.hpp"
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
namespace studio {
static qsizetype offset(const QByteArray &bytes, toml::source_position position) {
    qsizetype index = bytes.startsWith("\xef\xbb\xbf") ? 3 : 0;
    unsigned line = 1, column = 1;
    while (index < bytes.size() && (line < position.line || column < position.column)) {
        const auto byte = uchar(bytes[index]);
        if (byte == '\n') {
            ++line;
            column = 1;
            ++index;
        } else {
            ++column;
            index += byte < 0x80 ? 1 : byte < 0xe0 ? 2 : byte < 0xf0 ? 3 : 4;
        }
    }
    return index;
}
static QByteArray quoted(const QString &value) {
    auto bytes = QJsonDocument(QJsonArray{value}).toJson(QJsonDocument::Compact);
    return bytes.mid(1, bytes.size() - 2);
}
static QString description(const QByteArray &bytes, unsigned line) {
    const auto lines = QString::fromUtf8(bytes).split('\n');
    QStringList comments;
    for (int index = int(line) - 2; index >= 0; --index) {
        const auto text = lines[index].trimmed();
        if (text.startsWith('#'))
            comments.prepend(text.mid(1).trimmed());
        else if (text.isEmpty() && comments.isEmpty())
            continue;
        else
            break;
    }
    return comments.join('\n');
}
ConfigDecodeResult TomlConfigCodec::decode(const QByteArray &bytes) const {
    ConfigDecodeResult result;
    try {
        const auto table = toml::parse(std::string_view(bytes.constData(), size_t(bytes.size())));
        std::function<void(const toml::node &, QString, QString, QString)> visit =
            [&](const toml::node &node, QString id, QString label, QString section) {
                if (result.fields.size() >= 1000) {
                    ++result.unsupported;
                    return;
                }
                if (const auto *children = node.as_table()) {
                    for (auto &&[key, value] : *children) {
                        const auto name = QString::fromUtf8(key.str().data(), qsizetype(key.str().size()));
                        auto token = name;
                        token.replace('~', "~0").replace('/', "~1");
                        visit(value, id + "/" + token, name, id.isEmpty() ? "General" : id.mid(1));
                    }
                    return;
                }
                QVariant value;
                QString type;
                if (node.is_boolean()) {
                    type = "boolean";
                    value = node.as_boolean()->get();
                } else if (node.is_integer()) {
                    type = "integer";
                    value = QString::number(node.as_integer()->get());
                } else if (node.is_floating_point()) {
                    type = "number";
                    value = QString::number(node.as_floating_point()->get(), 'g', 17);
                } else if (node.is_string()) {
                    type = "string";
                    value = QString::fromStdString(node.as_string()->get());
                } else if (const auto *array = node.as_array()) {
                    // Until collection comments have their own edit model, preserve such
                    // arrays untouched instead of replacing their inline commentary.
                    if (bytes
                            .mid(offset(bytes, node.source().begin),
                                 offset(bytes, node.source().end) - offset(bytes, node.source().begin))
                            .contains('#')) {
                        ++result.unsupported;
                        return;
                    }

                    for (const auto &item : *array)
                        if ((!item.is_boolean() && !item.is_integer() && !item.is_floating_point() &&
                             !item.is_string()) ||
                            (item.is_integer() && (item.as_integer()->get() > 9007199254740991LL ||
                                                   item.as_integer()->get() < -9007199254740991LL))) {
                            ++result.unsupported;
                            return;
                        }
                    std::ostringstream stream;
                    stream << toml::json_formatter{node};
                    type = "list";
                    value = QString::fromStdString(stream.str());
                } else {
                    ++result.unsupported;
                    return;
                }
                const auto comments = description(bytes, node.source().begin.line);
                QVariantMap field{{"id", id},
                                  {"label", label},
                                  {"section", section},
                                  {"type", type},
                                  {"value", value},
                                  {"description", comments},
                                  {"start", offset(bytes, node.source().begin)},
                                  {"end", offset(bytes, node.source().end)}};
                const auto range =
                    QRegularExpression("Range: ([+-]?[0-9.]+) ~ ([+-]?[0-9.]+)").match(comments);
                if (range.hasMatch()) {
                    field["minimum"] = range.captured(1).toDouble();
                    field["maximum"] = range.captured(2).toDouble();
                }
                const auto choices = QRegularExpression("Allowed Values: ([^\\n]+)").match(comments);
                if (choices.hasMatch() && type == "string") {
                    QStringList values;
                    for (const auto &choice : choices.captured(1).split(','))
                        values << choice.trimmed();
                    field["choices"] = values;
                }
                result.fields.append(field);
            };
        visit(table, {}, {}, {});
    } catch (const toml::parse_error &error) {
        result.error = "Invalid TOML: " +
                       QString::fromUtf8(error.description().data(), qsizetype(error.description().size()));
    }
    return result;
}
ConfigEncodeResult TomlConfigCodec::encode(const QByteArray &original, const QVariantMap &changes) const {
    const auto document = decode(original);
    if (!document.error.isEmpty())
        return {{}, document.error};
    struct Edit {
        qsizetype start, end;
        QByteArray value;
    };
    QList<Edit> edits;
    QSet<QString> seen;
    for (const auto &entry : document.fields) {
        const auto field = entry.toMap();
        const auto id = field.value("id").toString();
        if (!changes.contains(id))
            continue;
        seen.insert(id);
        const auto value = changes.value(id);
        const auto type = field.value("type").toString();
        QByteArray encoded;
        bool valid = true;
        if (type == "boolean") {
            valid = value.metaType().id() == QMetaType::Bool;
            encoded = value.toBool() ? "true" : "false";
        } else if (type == "integer") {
            const auto number = value.toString().toLongLong(&valid);
            encoded = QByteArray::number(number);
        } else if (type == "number") {
            const auto number = value.toString().toDouble(&valid);
            valid = valid && std::isfinite(number);
            encoded = QByteArray::number(number, 'g', 17);
            if (!encoded.contains('.') && !encoded.contains('e'))
                encoded += ".0";
        } else if (type == "string") {
            encoded = quoted(value.toString());
            if (field.contains("choices"))
                valid = field.value("choices").toStringList().contains(value.toString());
        } else {
            QJsonParseError error;
            const auto list = QJsonDocument::fromJson(value.toString().toUtf8(), &error);
            valid = error.error == QJsonParseError::NoError && list.isArray();
            if (valid)
                for (const auto &item : list.array())
                    valid = valid && !item.isNull() && !item.isObject() && !item.isArray();
            encoded = list.toJson(QJsonDocument::Compact);
            const auto before = QJsonDocument::fromJson(field.value("value").toString().toUtf8()).array();
            const auto after = list.array();
            for (qsizetype index = 0; index < after.size() && !before.isEmpty(); ++index)
                valid = valid && after[index].type() == before[std::min(index, before.size() - 1)].type();
        }
        if (field.contains("minimum") && (type == "integer" || type == "number")) {
            const double number = value.toString().toDouble();
            valid = valid && number >= field.value("minimum").toDouble() &&
                    number <= field.value("maximum").toDouble();
        }
        if (!valid)
            return {{},
                    "Invalid value for " + field.value("label").toString() +
                        ". Check its type and allowed range."};
        edits.append({field.value("start").toLongLong(), field.value("end").toLongLong(), encoded});
    }
    if (seen.size() != changes.size())
        return {{}, "Unknown or unsupported TOML field."};
    std::sort(edits.begin(), edits.end(), [](const Edit &a, const Edit &b) { return a.start > b.start; });
    QByteArray output = original;
    for (const auto &edit : edits)
        output.replace(edit.start, edit.end - edit.start, edit.value);
    const auto check = decode(output);
    if (!check.error.isEmpty())
        return {{}, check.error};
    return {output, {}};
}
} // namespace studio
