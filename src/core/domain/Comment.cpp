#include "core/domain/Comment.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>

namespace studylab {

namespace {
struct KindInfo {
    CommentKind kind;
    const char* key;
    const char* label;
};
const KindInfo kKinds[] = {
    {CommentKind::Reflection, "reflection", QT_TRANSLATE_NOOP("Comment", "Réflexion")},
    {CommentKind::Question, "question", QT_TRANSLATE_NOOP("Comment", "Question")},
    {CommentKind::Explanation, "explanation", QT_TRANSLATE_NOOP("Comment", "Explication")},
    {CommentKind::Reference, "reference", QT_TRANSLATE_NOOP("Comment", "Référence")},
    {CommentKind::Summary, "summary", QT_TRANSLATE_NOOP("Comment", "Synthèse")},
};

struct AnchorInfo {
    CommentAnchor anchor;
    const char* key;
};
const AnchorInfo kAnchors[] = {
    {CommentAnchor::Free, "free"},
    {CommentAnchor::Annotation, "annotation"},
    {CommentAnchor::Passage, "passage"},
    {CommentAnchor::Page, "page"},
};
}  // namespace

QString commentKindKey(CommentKind k)
{
    for (const auto& i : kKinds) {
        if (i.kind == k)
            return QString::fromLatin1(i.key);
    }
    return QStringLiteral("reflection");
}

std::optional<CommentKind> commentKindFromKey(const QString& key)
{
    for (const auto& i : kKinds) {
        if (key == QLatin1String(i.key))
            return i.kind;
    }
    return std::nullopt;
}

QString commentKindLabel(CommentKind k)
{
    for (const auto& i : kKinds) {
        if (i.kind == k)
            return QCoreApplication::translate("Comment", i.label);
    }
    return {};
}

QList<CommentKind> editableCommentKinds()
{
    return {CommentKind::Question, CommentKind::Explanation, CommentKind::Reflection, CommentKind::Reference};
}

QString commentAnchorKey(CommentAnchor a)
{
    for (const auto& i : kAnchors) {
        if (i.anchor == a)
            return QString::fromLatin1(i.key);
    }
    return QStringLiteral("free");
}

std::optional<CommentAnchor> commentAnchorFromKey(const QString& key)
{
    for (const auto& i : kAnchors) {
        if (key == QLatin1String(i.key))
            return i.anchor;
    }
    return std::nullopt;
}

QString commentLocation(const CommentRecord& c)
{
    if (c.anchor == CommentAnchor::Free || c.unitIndex < 0)
        return {};
    if (const auto epub = EpubLocator::fromJson(c.locator))
        return QStringLiteral("%1 %").arg(qRound(epub->progress * 100));
    return QStringLiteral("p. %1").arg(c.pageLabel.isEmpty() ? QString::number(c.unitIndex + 1) : c.pageLabel);
}

double commentSortKey(const CommentRecord& c)
{
    if (c.anchor == CommentAnchor::Free || c.unitIndex < 0)
        return -1.0;
    if (const auto pdf = PdfLocator::fromJson(c.locator))
        return annotationSortKey(pdf->page, pdf->rects.isEmpty() ? 0.0 : pdf->relativeY());
    if (const auto epub = EpubLocator::fromJson(c.locator))
        return annotationSortKey(epub->section, epub->fraction);
    return annotationSortKey(c.unitIndex, 0.0);
}

QStringList wikiLinkTargets(const QString& markdown)
{
    static const QRegularExpression link(QStringLiteral("\\[\\[([^\\[\\]|\\n]+)(?:\\|[^\\[\\]\\n]*)?\\]\\]"));
    QStringList out;
    auto it = link.globalMatch(markdown);
    while (it.hasNext()) {
        const QString target = it.next().captured(1).trimmed();
        if (!target.isEmpty() && !out.contains(target))
            out << target;
    }
    return out;
}

}  // namespace studylab
