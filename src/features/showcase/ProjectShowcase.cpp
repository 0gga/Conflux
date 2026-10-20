#include "ProjectShowcase.h"
#include <QFont>
#include <QFutureWatcher>
#include <QPointer>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QUrl>
#include <QtConcurrent>
#include <algorithm>
namespace studio {
static QString nativeDescription(const QString &source, bool markdown) {
    if (source.trimmed().isEmpty())
        return {};
    QTextDocument document;
    QFont font;
    font.setPixelSize(14);
    document.setDefaultFont(font);
    if (markdown)
        document.setMarkdown(source);
    else
        document.setHtml(source);
    QList<QPair<int, int>> images;
    for (auto block = document.begin(); block.isValid(); block = block.next())
        for (auto item = block.begin(); !item.atEnd(); ++item) {
            const auto fragment = item.fragment();
            if (fragment.isValid() && fragment.charFormat().isImageFormat())
                images << qMakePair(fragment.position(), fragment.length());
        }
    // Gallery retrieval has its own bounds and identity checks. Description markup
    // never loads arbitrary inline images or imports the author's light palette.
    for (auto it = images.crbegin(); it != images.crend(); ++it) {
        QTextCursor cursor(&document);
        cursor.setPosition(it->first);
        cursor.setPosition(it->first + it->second, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    struct Format {
        int position, length;
        QTextCharFormat value;
    };
    QList<Format> formats;
    for (auto block = document.begin(); block.isValid(); block = block.next())
        for (auto item = block.begin(); !item.atEnd(); ++item) {
            const auto fragment = item.fragment();
            if (!fragment.isValid())
                continue;
            auto format = fragment.charFormat();
            format.clearProperty(QTextFormat::ForegroundBrush);
            format.clearProperty(QTextFormat::BackgroundBrush);
            if (format.isAnchor() && QUrl(format.anchorHref()).scheme() != "https") {
                format.setAnchor(false);
                format.setAnchorHref({});
            }
            formats << Format{fragment.position(), fragment.length(), format};
        }
    for (const auto &format : formats) {
        QTextCursor cursor(&document);
        cursor.setPosition(format.position);
        cursor.setPosition(format.position + format.length, QTextCursor::KeepAnchor);
        cursor.setCharFormat(format.value);
    }
    return document.toHtml();
}
ProjectShowcase::ProjectShowcase(std::shared_ptr<IProjectShowcaseSource> source,
                                 std::shared_ptr<const IInstalledModIdentity> identity, QObject *parent)
    : QObject(parent), m_source(std::move(source)), m_identity(std::move(identity)) {}
void ProjectShowcase::close() {
    ++m_generation;
    m_project.clear();
    m_versions.clear();
    m_description.clear();
    m_gallery.clear();
    m_notice.clear();
    m_filePath.clear();
    m_busy = false;
    m_requestKey.clear();
    emit changed();
}
void ProjectShowcase::open(const QVariantMap &pack, const QVariantMap &file) {
    close();
    m_filePath = file.value("relativePath").toString();
    m_requestKey = m_filePath;
    m_project = {{"title", file.value("title")}, {"icon", file.value("icon")}};
    m_busy = true;
    const int generation = m_generation;
    emit changed();
    auto *watcher = new QFutureWatcher<InstalledModIdentity>(this);
    connect(watcher, &QFutureWatcher<InstalledModIdentity>::finished, this,
            [this, watcher, generation, pack] {
                const auto identity = watcher->result();
                watcher->deleteLater();
                if (generation != m_generation)
                    return;
                if (!identity.error.isEmpty()) {
                    m_busy = false;
                    m_notice = identity.error;
                    emit changed();
                    return;
                }
                resolve(identity, pack, generation);
            });
    const auto identity = m_identity;
    watcher->setFuture(QtConcurrent::run([identity, pack, file] { return identity->inspect(pack, file); }));
}
void ProjectShowcase::openProject(const QVariantMap &pack, const QVariantMap &project) {
    close();
    m_project = project;
    m_requestKey = project.value("provider").toString() + ':' + project.value("id").toString();
    m_busy = true;
    emit changed();
    InstalledModIdentity identity;
    const auto ids = project.value("sourceIds").toMap();
    for (auto it = ids.begin(); it != ids.end(); ++it)
        if (it.key() == "modrinth" || it.key() == "curseforge")
            identity.sourceIds[it.key()] = it.value().toString();
    if (identity.sourceIds.isEmpty() &&
        (project.value("provider") == "modrinth" || project.value("provider") == "curseforge"))
        identity.sourceIds[project.value("provider").toString()] = project.value("id").toString();
    resolve(identity, pack, m_generation);
}
void ProjectShowcase::resolve(const InstalledModIdentity &identity, const QVariantMap &pack, int generation) {
    const QPointer<ProjectShowcase> guard(this);
    m_source->resolve(identity, [guard, generation, pack](Project project, QString error) {
        if (!guard || guard->m_generation != generation)
            return;
        auto *self = guard.data();
        self->m_notice = error;
        if (project.id.isEmpty()) {
            self->m_busy = false;
            if (self->m_notice.isEmpty())
                self->m_notice = "No verified online project was found for this file. Local or unpublished "
                                 "mods may not have a showcase.";
            emit self->changed();
            return;
        }
        self->m_project = project.toVariant();
        emit self->changed();
        self->m_source->load(
            project, pack.value("minecraft").toString(), pack.value("loader").toString(),
            [guard, generation, project](ProjectDetail detail) {
                if (!guard || guard->m_generation != generation)
                    return;
                auto *self = guard.data();
                self->m_description = nativeDescription(detail.description, project.provider == "modrinth");
                self->m_gallery = detail.images;
                if (!detail.errors.isEmpty()) {
                    if (!self->m_notice.isEmpty())
                        self->m_notice += '\n';
                    self->m_notice += detail.errors.join('\n');
                }
                self->m_versions = groupReleaseVariants(detail.releases);
                for (int index = 0; index < self->m_versions.size(); ++index) {
                    auto group = self->m_versions.at(index).toMap();
                    auto variants = group.value("variants").toList();
                    for (int variantIndex = 0; variantIndex < variants.size(); ++variantIndex) {
                        auto variant = variants.at(variantIndex).toMap();
                        variant["pageUrl"] = variant.value("provider") == "modrinth"
                                                 ? "https://modrinth.com/mod/" +
                                                       variant.value("projectId").toString() + "/version/" +
                                                       variant.value("id").toString()
                                                 : project.sourceUrls.value("curseforge") + "/files/" +
                                                       variant.value("id").toString();
                        variants[variantIndex] = variant;
                    }
                    group["variants"] = variants;
                    self->m_versions[index] = group;
                }
                self->m_busy = false;
                emit self->changed();
            });
    });
}
} // namespace studio
