#include "DescriptionRenderer.h"
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextImageFormat>
#include <QTextList>
#include <QTextTable>
#include <QUrl>
namespace studio {
namespace {
bool remote(const QString &value) {
    const QUrl url(value);
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty();
}
QString attribute(const QString &tag, const QString &key) {
    const QRegularExpression expression("\\b" + key + R"re(\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))re",
                                        QRegularExpression::CaseInsensitiveOption);
    const auto match = expression.match(tag);
    auto value = !match.captured(1).isNull()   ? match.captured(1)
                 : !match.captured(2).isNull() ? match.captured(2)
                                               : match.captured(3);
    return value.replace("&amp;", "&").replace("&quot;", "\"").replace("&#39;", "'");
}
QMap<QString, QString> imageLinks(QString source) {
    source.remove(QRegularExpression(R"((```|~~~)[^\n]*\n[\s\S]*?\1[^\n]*)"));
    QMap<QString, QString> links;
    const auto add = [&links](QString image, QString link) {
        if (!remote(image) || !remote(link))
            return;
        if (links.contains(image) && links.value(image) != link)
            links[image] = {};
        else
            links[image] = link;
    };
    auto matches =
        QRegularExpression(R"(<a\b([^>]*)>([\s\S]*?)</a\s*>)", QRegularExpression::CaseInsensitiveOption)
            .globalMatch(source);
    while (matches.hasNext()) {
        const auto anchor = matches.next();
        const auto link = attribute(anchor.captured(1), "href");
        auto images = QRegularExpression("<img\\b[^>]*>", QRegularExpression::CaseInsensitiveOption)
                          .globalMatch(anchor.captured(2));
        while (images.hasNext())
            add(attribute(images.next().captured(), "src"), link);
    }
    matches = QRegularExpression(R"(\[!\[[^\]]*\]\(([^\s)]+)\)\]\(([^\s)]+)\))").globalMatch(source);
    while (matches.hasNext()) {
        const auto match = matches.next();
        add(match.captured(1), match.captured(2));
    }
    return links;
}
QMap<QString, QVariantMap> imageRows(const QString &source) {
    QMap<QString, QVariantMap> rows;
    auto matches = QRegularExpression(R"(<(p|div|center)\b([^>]*)>([\s\S]*?)</\1\s*>)",
                                      QRegularExpression::CaseInsensitiveOption)
                       .globalMatch(source);
    int group = 0;
    while (matches.hasNext()) {
        const auto match = matches.next();
        auto content = match.captured(3);
        content.remove(QRegularExpression("<[^>]*>"));
        if (!content.trimmed().isEmpty())
            continue;
        const auto identity = QString::number(group++);
        const auto alignment =
            match.captured(1).toLower() == "center" || attribute(match.captured(2), "align") == "center"
                ? "center"
                : "left";
        auto images = QRegularExpression("<img\\b[^>]*>", QRegularExpression::CaseInsensitiveOption)
                          .globalMatch(match.captured(3));
        while (images.hasNext()) {
            const auto url = attribute(images.next().captured(), "src");
            if (!remote(url))
                continue;
            if (rows.contains(url))
                rows[url] = {};
            else
                rows[url] = {{"group", identity}, {"alignment", alignment}};
        }
    }
    return rows;
}
QVariantList restoreImageRows(const QVariantList &blocks, const QMap<QString, QVariantMap> &groups) {
    QVariantList result;
    for (const auto &value : blocks) {
        auto block = value.toMap();
        const auto items = block.value("items").toList();
        if (block.value("kind") == "images" && !items.isEmpty()) {
            const auto group = groups.value(items.first().toMap().value("source").toString());
            bool same = !group.isEmpty();
            for (const auto &item : items)
                same &= groups.value(item.toMap().value("source").toString()) == group;
            if (same) {
                block["imageGroup"] = group.value("group");
                block["alignment"] = group.value("alignment");
                if (!result.isEmpty() && result.last().toMap().value("kind") == "images" &&
                    result.last().toMap().value("imageGroup") == group.value("group")) {
                    auto previous = result.last().toMap();
                    auto merged = previous.value("items").toList();
                    merged += items;
                    previous["items"] = merged;
                    result.last() = previous;
                    continue;
                }
            }
        }
        auto rows = block.value("rows").toList();
        for (auto &rowValue : rows) {
            auto row = rowValue.toMap();
            auto cells = row.value("cells").toList();
            for (auto &cellValue : cells) {
                auto cell = cellValue.toMap();
                cell["blocks"] = restoreImageRows(cell.value("blocks").toList(), groups);
                cellValue = cell;
            }
            row["cells"] = cells;
            rowValue = row;
        }
        if (!rows.isEmpty())
            block["rows"] = rows;
        result << block;
    }
    return result;
}
void restoreImageLinks(QVariantList &blocks, const QMap<QString, QString> &links) {
    for (auto &value : blocks) {
        auto block = value.toMap();
        auto items = block.value("items").toList();
        for (auto &item : items) {
            auto image = item.toMap();
            if (image.value("link").toString().isEmpty())
                image["link"] = links.value(image.value("source").toString());
            item = image;
        }
        if (!items.isEmpty())
            block["items"] = items;
        auto rows = block.value("rows").toList();
        for (auto &rowValue : rows) {
            auto row = rowValue.toMap();
            auto cells = row.value("cells").toList();
            for (auto &cellValue : cells) {
                auto cell = cellValue.toMap();
                auto content = cell.value("blocks").toList();
                restoreImageLinks(content, links);
                cell["blocks"] = content;
                cellValue = cell;
            }
            row["cells"] = cells;
            rowValue = row;
        }
        if (!rows.isEmpty())
            block["rows"] = rows;
        value = block;
    }
}
QString htmlSubset(QString input) {
    input.remove(QRegularExpression("<(script|style|iframe|object)\\b[^>]*>.*?</\\1\\s*>",
                                    QRegularExpression::CaseInsensitiveOption |
                                        QRegularExpression::DotMatchesEverythingOption));
    const QRegularExpression tags("<(/?)([a-zA-Z][a-zA-Z0-9]*)([^>]*)>");
    const QRegularExpression attrs(R"re(([\w-]+)\s*=\s*(?:"([^"]*)"|'([^']*)'|([^\s>]+)))re");
    const QStringList allowed{"p",     "div",   "center", "span", "a",   "img",        "br",     "hr", "h1",
                              "h2",    "h3",    "h4",     "h5",   "h6",  "b",          "strong", "i",  "em",
                              "u",     "s",     "del",    "code", "pre", "blockquote", "ul",     "ol", "li",
                              "table", "thead", "tbody",  "tr",   "td",  "th"};
    QString output;
    qsizetype cursor = 0;
    auto matches = tags.globalMatch(input);
    while (matches.hasNext()) {
        const auto tag = matches.next();
        output += input.mid(cursor, tag.capturedStart() - cursor);
        cursor = tag.capturedEnd();
        const auto name = tag.captured(2).toLower();
        if (!allowed.contains(name))
            continue;
        if (tag.captured(1) == "/") {
            output += "</" + name + ">";
            continue;
        }
        QString properties;
        bool imageAllowed = false;
        auto attributes = attrs.globalMatch(tag.captured(3));
        while (attributes.hasNext()) {
            const auto attr = attributes.next();
            const auto key = attr.captured(1).toLower();
            const auto value = !attr.captured(2).isNull()   ? attr.captured(2)
                               : !attr.captured(3).isNull() ? attr.captured(3)
                                                            : attr.captured(4);
            bool keep = false;
            if ((key == "src" && name == "img") || (key == "href" && name == "a")) {
                keep = remote(value);
                if (key == "src")
                    imageAllowed = keep;
            } else if (key == "alt" && name == "img")
                keep = true;
            else if (key == "align")
                keep = QStringList{"left", "center", "right", "justify"}.contains(value.toLower());
            else if (QStringList{"width", "height", "colspan", "rowspan"}.contains(key)) {
                bool ok;
                const auto n = value.toInt(&ok);
                keep = ok && n > 0 && n <= 4096;
            }
            if (keep)
                properties += ' ' + key + "=\"" + value.toHtmlEscaped() + "\"";
        }
        if (name != "img" || imageAllowed)
            output += '<' + name + properties + '>';
    }
    return output + input.mid(cursor);
}
QString htmlMarkdown(const QString &source) {
    QTextDocument fragment;
    fragment.setHtml(source);
    return fragment.toMarkdown();
}
QString normalizedMarkdown(QString text) {
    // Protect fenced examples before processing HTML outside code blocks.
    QStringList code;
    const QRegularExpression fences(R"((```|~~~)[^\n]*\n[\s\S]*?\1[^\n]*)");
    auto matches = fences.globalMatch(text);
    QList<QRegularExpressionMatch> found;
    while (matches.hasNext())
        found.prepend(matches.next());
    for (const auto &match : found) {
        const auto key = "CONFLUXCODE" + QString::number(code.size()) + "TOKEN";
        code << match.captured();
        text.replace(match.capturedStart(), match.capturedLength(), key);
    }
    text = htmlSubset(text);
    // Qt's mixed raw-HTML/Markdown parser drops text after image-only HTML blocks.
    // Convert those blocks to Markdown first; retain images and links rather than deleting them.
    const QRegularExpression blocks(
        R"(<(p|div|center|table|ul|ol|blockquote|pre|h[1-6])\b[^>]*>[\s\S]*?</\1\s*>)",
        QRegularExpression::CaseInsensitiveOption);
    matches = blocks.globalMatch(text);
    found.clear();
    while (matches.hasNext())
        found.prepend(matches.next());
    for (const auto &match : found)
        text.replace(match.capturedStart(), match.capturedLength(),
                     "\n\n" + htmlMarkdown(match.captured()) + "\n\n");
    const QRegularExpression inlineHtml(R"(<a\b[^>]*>[\s\S]*?</a\s*>|<img\b[^>]*>)",
                                        QRegularExpression::CaseInsensitiveOption);
    matches = inlineHtml.globalMatch(text);
    found.clear();
    while (matches.hasNext())
        found.prepend(matches.next());
    for (const auto &match : found)
        text.replace(match.capturedStart(), match.capturedLength(), htmlMarkdown(match.captured()).trimmed());
    text.replace(QRegularExpression("<br\\s*/?>", QRegularExpression::CaseInsensitiveOption), "\n\n");
    text.replace(QRegularExpression("</?(strong|b)\\b[^>]*>", QRegularExpression::CaseInsensitiveOption),
                 "**");
    text.replace(QRegularExpression("</?(em|i)\\b[^>]*>", QRegularExpression::CaseInsensitiveOption), "*");
    text.replace(QRegularExpression("</?code\\b[^>]*>", QRegularExpression::CaseInsensitiveOption), "`");
    text.replace(QRegularExpression("<hr\\s*/?>", QRegularExpression::CaseInsensitiveOption), "\n\n---\n\n");
    text.remove(QRegularExpression("<[^>]*>"));
    for (int i = 0; i < code.size(); ++i)
        text.replace("CONFLUXCODE" + QString::number(i) + "TOKEN", code[i]);
    return text;
}
QString fragmentHtml(const QTextFragment &fragment) {
    const auto format = fragment.charFormat();
    auto text = fragment.text().toHtmlEscaped();
    text.replace(QChar::LineSeparator, "<br/>");
    if (format.fontWeight() >= QFont::Bold)
        text = "<strong>" + text + "</strong>";
    if (format.fontItalic())
        text = "<em>" + text + "</em>";
    if (format.fontStrikeOut())
        text = "<s>" + text + "</s>";
    if (format.fontUnderline())
        text = "<u>" + text + "</u>";
    if (format.fontFixedPitch())
        text = "<code>" + text + "</code>";
    if (format.isAnchor() && remote(format.anchorHref()))
        text = "<a href=\"" + format.anchorHref().toHtmlEscaped() + "\">" + text + "</a>";
    return text;
}
QVariantList frameBlocks(QTextFrame::iterator begin, QTextFrame::iterator end, int depth);
QVariantList blockContent(const QTextBlock &block) {
    QVariantList result;
    QString text;
    QVariantList images;
    const auto format = block.blockFormat();
    const auto level = qBound(0, format.headingLevel(), 6);
    const bool code =
        format.hasProperty(QTextFormat::BlockCodeFence) || format.hasProperty(QTextFormat::BlockCodeLanguage);
    const auto flushText = [&] {
        if (text.trimmed().isEmpty()) {
            text.clear();
            return;
        }
        if (const auto *list = block.textList()) {
            const auto style = list->format().style();
            const bool numbered =
                style == QTextListFormat::ListDecimal || style == QTextListFormat::ListLowerAlpha ||
                style == QTextListFormat::ListUpperAlpha || style == QTextListFormat::ListLowerRoman ||
                style == QTextListFormat::ListUpperRoman;
            text.prepend(numbered ? QString::number(list->itemNumber(block) + 1) + ". "
                                  : QString::fromUtf8("\xe2\x80\xa2 "));
        }
        const auto tag = code ? QString("pre") : level ? "h" + QString::number(level) : QString("p");
        result << QVariantMap{{"kind", "text"},
                              {"html", '<' + tag + '>' + text + "</" + tag + '>'},
                              {"code", code},
                              {"quote", format.intProperty(QTextFormat::BlockQuoteLevel) > 0},
                              {"indent", qMin(6, block.textList() ? block.textList()->format().indent() - 1
                                                                  : format.indent())}};
        text.clear();
    };
    const auto flushImages = [&] {
        if (!images.isEmpty()) {
            result << QVariantMap{{"kind", "images"}, {"items", images}};
            images.clear();
        }
    };
    for (auto it = block.begin(); !it.atEnd(); ++it) {
        const auto fragment = it.fragment();
        if (!fragment.isValid())
            continue;
        if (fragment.charFormat().isImageFormat()) {
            flushText();
            const auto image = fragment.charFormat().toImageFormat();
            if (!remote(image.name()))
                continue;
            images << QVariantMap{{"source", image.name()},
                                  {"width", image.width()},
                                  {"height", image.height()},
                                  {"alt", image.stringProperty(QTextFormat::ImageAltText)},
                                  {"link", remote(image.anchorHref()) ? image.anchorHref() : QString{}}};
        } else {
            if (!fragment.text().trimmed().isEmpty())
                flushImages();
            text += fragmentHtml(fragment);
        }
    }
    flushText();
    flushImages();
    return result;
}
QVariantList frameBlocks(QTextFrame::iterator begin, QTextFrame::iterator end, int depth) {
    QVariantList result;
    if (depth > 12)
        return result;
    for (auto it = begin; it != end; ++it) {
        if (auto *frame = it.currentFrame()) {
            if (auto *table = qobject_cast<QTextTable *>(frame)) {
                QVariantList rows;
                for (int row = 0; row < table->rows(); ++row) {
                    QVariantList cells;
                    for (int col = 0; col < table->columns(); ++col) {
                        const auto cell = table->cellAt(row, col);
                        if (cell.row() != row || cell.column() != col)
                            continue;
                        cells << QVariantMap{{"blocks", frameBlocks(cell.begin(), cell.end(), depth + 1)}};
                    }
                    rows << QVariantMap{{"cells", cells}};
                }
                result << QVariantMap{{"kind", "table"}, {"rows", rows}};
            } else
                result += frameBlocks(frame->begin(), frame->end(), depth + 1);
        } else if (it.currentBlock().isValid())
            result += blockContent(it.currentBlock());
    }
    return result;
}
QString readableHtml(const QVariantList &blocks) {
    QString html;
    for (const auto &value : blocks) {
        const auto block = value.toMap();
        if (block.value("kind") == "text")
            html += block.value("html").toString();
        for (const auto &row : block.value("rows").toList())
            for (const auto &cell : row.toMap().value("cells").toList())
                html += readableHtml(cell.toMap().value("blocks").toList());
    }
    return html;
}
} // namespace
RenderedDescription describeContent(const QString &source, bool markdown) {
    QTextDocument document;
    if (markdown)
        document.setMarkdown(normalizedMarkdown(source.left(2 * 1024 * 1024)),
                             QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) |
                                 QTextDocument::MarkdownNoHTML);
    else
        document.setHtml(htmlSubset(source.left(2 * 1024 * 1024)));
    auto blocks = frameBlocks(document.rootFrame()->begin(), document.rootFrame()->end(), 0);
    restoreImageLinks(blocks, imageLinks(source.left(2 * 1024 * 1024)));
    blocks = restoreImageRows(blocks, imageRows(source.left(2 * 1024 * 1024)));
    return {readableHtml(blocks), blocks};
}
QString renderDescription(const QString &source, bool markdown) {
    return describeContent(source, markdown).html;
}
} // namespace studio
