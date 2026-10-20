#include "DescriptionRenderer.h"
#include <QRegularExpression>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>
#include <QUrl>
namespace studio {
QString renderDescription(const QString &source, bool markdown) {
    QTextDocument document;
    if (markdown) {
        auto text = source;
        // Qt's Markdown/HTML interop can discard the entire text following mixed
        // image banners and <br> tags. Normalize raw HTML before Markdown parsing;
        // the output serializer below is the actual formatting/link boundary.
        text.remove(QRegularExpression("<(script|style)\\b[^>]*>.*?</\\1\\s*>",
                                       QRegularExpression::CaseInsensitiveOption |
                                           QRegularExpression::DotMatchesEverythingOption));
        text.replace(QRegularExpression("<\\s*/?\\s*(br|p|div|h[1-6]|tr|td|th|li)\\b[^>]*>",
                                        QRegularExpression::CaseInsensitiveOption),
                     "\n");
        text.remove(QRegularExpression("<[^>]*>"));
        document.setMarkdown(text, QTextDocument::MarkdownFeatures(QTextDocument::MarkdownDialectGitHub) |
                                       QTextDocument::MarkdownNoHTML);
    } else
        document.setHtml(source);
    QString html;
    for (auto block = document.begin(); block.isValid(); block = block.next()) {
        QString text;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const auto fragment = it.fragment();
            if (!fragment.isValid() || fragment.charFormat().isImageFormat())
                continue;
            const auto format = fragment.charFormat();
            auto part = fragment.text().toHtmlEscaped();
            part.replace(QChar::LineSeparator, "<br/>");
            if (format.fontWeight() >= QFont::Bold)
                part = "<strong>" + part + "</strong>";
            if (format.fontItalic())
                part = "<em>" + part + "</em>";
            const QUrl link(format.anchorHref());
            if (format.isAnchor() && link.scheme() == "https" && !link.host().isEmpty())
                part = "<a href=\"" + link.toString().toHtmlEscaped() + "\">" + part + "</a>";
            text += part;
        }
        if (text.trimmed().isEmpty())
            continue;
        const auto level = qBound(0, block.blockFormat().headingLevel(), 6);
        const auto tag = level ? "h" + QString::number(level) : QString("p");
        if (const auto *list = block.textList()) {
            const bool numbered = list->format().style() >= QTextListFormat::ListUpperRoman &&
                                  list->format().style() <= QTextListFormat::ListDecimal;
            text.prepend(numbered ? QString::number(list->itemNumber(block) + 1) + ". " : "• ");
        }
        // Do not serialize provider block/frame/font CSS. Qt's default black block
        // foreground and web tables otherwise override the application's palette.
        html += '<' + tag + '>' + text + "</" + tag + ">";
    }
    return html;
}
} // namespace studio
