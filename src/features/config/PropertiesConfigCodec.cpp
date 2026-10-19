#include "ConfigCodec.h"
#include <QRegularExpression>
#include <QSet>
#include <cmath>
namespace studio {
static QString unescape(const QString &text, bool &valid) {
    QString result;
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text[i] != '\\') {
            result += text[i];
            continue;
        }
        if (++i >= text.size()) {
            valid = false;
            break;
        }
        const auto c = text[i];
        if (c == 'n')
            result += '\n';
        else if (c == 'r')
            result += '\r';
        else if (c == 't')
            result += '\t';
        else if (c == 'f')
            result += '\f';
        else if (c == 'u') {
            if (i + 4 >= text.size()) {
                valid = false;
                break;
            }
            bool ok = false;
            const auto code = text.mid(i + 1, 4).toUShort(&ok, 16);
            valid = valid && ok;
            result += QChar(code);
            i += 4;
        } else
            result += c;
    }
    return result;
}
static QString escape(const QString &text) {
    QString result;
    for (const auto c : text) {
        if (c == '\\')
            result += "\\\\";
        else if (c == '\n')
            result += "\\n";
        else if (c == '\r')
            result += "\\r";
        else if (c == '\t')
            result += "\\t";
        else if (c.unicode() < 32 || c.unicode() > 126)
            result += "\\u" + QString::number(c.unicode(), 16).rightJustified(4, '0');
        else
            result += c;
    }
    if (result.startsWith(' '))
        result.prepend('\\');
    return result;
}
ConfigDecodeResult PropertiesConfigCodec::decode(const QByteArray &bytes) const {
    ConfigDecodeResult result;
    const auto text = QString::fromUtf8(bytes);
    if (text.toUtf8() != bytes)
        return {{}, "This properties file uses an unsupported text encoding."};
    const auto lines = text.split('\n');
    QStringList comments;
    bool continuing = false;
    QSet<QString> keys;
    for (qsizetype index = 0; index < lines.size(); ++index) {
        const auto line = lines[index];
        const auto trimmed = line.trimmed();
        const bool continued =
            QRegularExpression("(?<!\\\\)(?:\\\\\\\\)*\\\\$").match(line.trimmed()).hasMatch();
        if (continuing || continued) {
            continuing = continued;
            ++result.unsupported;
            comments.clear();
            continue;
        }
        if (trimmed.startsWith('#') || trimmed.startsWith('!')) {
            comments << trimmed.mid(1).trimmed();
            continue;
        }
        const auto match =
            QRegularExpression("^(\\s*([^#!\\s:=][^:=]*?)\\s*[:=]\\s*)([^\\r]*)(\\r?)$").match(line);
        if (!match.hasMatch()) {
            if (!trimmed.isEmpty())
                ++result.unsupported;
            comments.clear();
            continue;
        }
        const auto key = match.captured(2).trimmed();
        if (key.contains('\\') || keys.contains(key)) {
            for (qsizetype item = result.fields.size(); item > 0; --item)
                if (result.fields[item - 1].toMap().value("id") == key)
                    result.fields.removeAt(item - 1);
            ++result.unsupported;
            comments.clear();
            continue;
        }
        keys.insert(key);
        bool valid = true;
        const auto value = unescape(match.captured(3), valid);
        if (!valid) {
            ++result.unsupported;
            continue;
        }
        const auto type = value == "true" || value == "false"                           ? "boolean"
                          : QRegularExpression("^[+-]?[0-9]+$").match(value).hasMatch() ? "integer"
                                                                                        : "string";
        result.fields.append(
            QVariantMap{{"id", key},
                        {"label", key},
                        {"section", key.contains('.') ? key.section('.', 0, -2) : "General"},
                        {"type", type},
                        {"value", type == QString("boolean") ? QVariant(value == "true") : QVariant(value)},
                        {"description", comments.join('\n')},
                        {"line", index},
                        {"prefix", match.captured(1)},
                        {"suffix", match.captured(4)}});
        comments.clear();
        if (result.fields.size() >= 1000) {
            ++result.unsupported;
            break;
        }
    }
    return result;
}
ConfigEncodeResult PropertiesConfigCodec::encode(const QByteArray &original,
                                                 const QVariantMap &changes) const {
    const auto document = decode(original);
    if (!document.error.isEmpty())
        return {{}, document.error};
    auto lines = QString::fromUtf8(original).split('\n');
    QSet<QString> seen;
    for (const auto &item : document.fields) {
        const auto field = item.toMap();
        const auto id = field.value("id").toString();
        if (!changes.contains(id))
            continue;
        seen.insert(id);
        const auto value = changes.value(id);
        const auto type = field.value("type").toString();
        QString encoded;
        if (type == "boolean") {
            if (value.metaType().id() != QMetaType::Bool)
                return {{}, "Expected a toggle value."};
            encoded = value.toBool() ? "true" : "false";
        } else if (type == "integer") {
            bool valid = false;
            const auto integer = value.toString().toLongLong(&valid);
            if (!valid)
                return {{}, "Expected a 64-bit whole number."};
            encoded = QString::number(integer);
        } else
            encoded = escape(value.toString());
        lines[field.value("line").toInt()] =
            field.value("prefix").toString() + encoded + field.value("suffix").toString();
    }
    if (seen.size() != changes.size())
        return {{}, "Unknown or unsupported properties field."};
    return {lines.join('\n').toUtf8(), {}};
}
} // namespace studio
