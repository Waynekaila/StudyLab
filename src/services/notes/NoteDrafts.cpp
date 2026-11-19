#include "services/notes/NoteDrafts.h"

#include "core/AppLink.h"

#include <QCoreApplication>
#include <QRegularExpression>

#include <algorithm>

namespace studylab::NoteDrafts {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("NoteDrafts", text); }

/// [[Titre]] si le titre peut servir de lien, sinon le titre en italique.
QString documentRef(const DocumentRecord& doc)
{
    static const QRegularExpression unsafe(QStringLiteral("[\\[\\]|#^\\n]"));
    if (doc.title.isEmpty())
        return {};
    return unsafe.match(doc.title).hasMatch() ? QStringLiteral("*%1*").arg(doc.title)
                                              : QStringLiteral("[[%1]]").arg(doc.title);
}

QString yamlString(const QString& v)
{
    QString s = v;
    s.replace(u'\\', QStringLiteral("\\\\")).replace(u'"', QStringLiteral("\\\""));
    return QStringLiteral("\"%1\"").arg(s);
}

QString frontMatter(const DocumentRecord& doc, const QString& tag)
{
    QString fm = QStringLiteral("---\n");
    if (!doc.title.isEmpty())
        fm += QStringLiteral("source: %1\n").arg(yamlString(documentRef(doc).startsWith(u'[') ? documentRef(doc) : doc.title));
    if (!doc.authors.isEmpty()) {
        QStringList quoted;
        for (const QString& a : doc.authors)
            quoted << yamlString(a);
        fm += QStringLiteral("auteurs: [%1]\n").arg(quoted.join(QStringLiteral(", ")));
    }
    if (doc.year > 0)
        fm += QStringLiteral("année: %1\n").arg(doc.year);
    fm += QStringLiteral("tags: [%1]\n---\n\n").arg(tag);
    return fm;
}

/// « Çengel, [[Thermodynamique]] » : premier auteur et document.
QString attribution(const DocumentRecord& doc)
{
    QStringList parts;
    if (!doc.authors.isEmpty())
        parts << QStringLiteral("*%1*").arg(doc.authors.first());
    if (const QString ref = documentRef(doc); !ref.isEmpty())
        parts << ref;
    return parts.join(QStringLiteral(", "));
}

QString blockquote(const QString& text)
{
    QStringList lines;
    for (const QString& l : text.split(u'\n'))
        lines << (l.trimmed().isEmpty() ? QStringLiteral(">") : QStringLiteral("> ") + l.trimmed());
    return lines.join(u'\n');
}

/// Début d'un texte, coupé à un mot, pour un titre.
QString headline(const QString& text, int max = 60)
{
    static const QRegularExpression md(QStringLiteral("[*_`>#$]|\\[\\[|\\]\\]"));
    QString s = text.section(u'\n', 0, 0).trimmed();
    if (s.isEmpty())
        s = text.simplified();
    s.remove(md);
    s = s.simplified();
    if (s.size() <= max)
        return s;
    s = s.left(max);
    const qsizetype space = s.lastIndexOf(u' ');
    if (space > max / 2)
        s = s.left(space);
    return s.trimmed() + QStringLiteral("…");
}

QString commentWhere(const CommentRecord& c)
{
    const QString where = commentLocation(c);
    return where;
}

QList<AnnotationRecord> inReadingOrder(QList<AnnotationRecord> list)
{
    std::stable_sort(list.begin(), list.end(),
                     [](const AnnotationRecord& a, const AnnotationRecord& b) { return a.sortKey < b.sortKey; });
    list.erase(std::remove_if(list.begin(), list.end(),
                              [](const AnnotationRecord& a) { return a.type == AnnotationType::Bookmark; }),
               list.end());
    return list;
}

}  // namespace

QString readingFolder() { return tr("Notes de lecture"); }

QString quoteBlock(const DocumentRecord& doc, const AnnotationRecord& a)
{
    QString text = normalizeQuote(a.selectedText);
    if (text.isEmpty())
        text = a.type == AnnotationType::Marker ? tr("*Zone annotée*") : tr("*Note sans passage*");
    QString out = blockquote(text);
    const QString where = annotationLocation(a);
    const QString link = AppLink{doc.uuid, a.uuid, {}}.toString();
    QStringList cite;
    if (const QString who = attribution(doc); !who.isEmpty())
        cite << who;
    if (!where.isEmpty())
        cite << QStringLiteral("[%1](%2)").arg(where, link);
    else
        cite << QStringLiteral("[%1](%2)").arg(tr("ouvrir le passage"), link);
    out += QStringLiteral("\n> — ") + cite.join(QStringLiteral(", "));
    if (!a.note.trimmed().isEmpty())
        out += QStringLiteral("\n\n") + a.note.trimmed();
    return out + u'\n';
}

NoteDraft fromAnnotation(const DocumentRecord& doc, const AnnotationRecord& a)
{
    NoteDraft d;
    d.type = NoteType::Reading;
    d.folder = readingFolder();
    const QString quote = normalizeQuote(a.selectedText);
    d.title = quote.isEmpty() ? QStringLiteral("%1 — %2").arg(doc.title, annotationLocation(a)) : headline(quote);
    d.body = frontMatter(doc, QStringLiteral("lecture")) + quoteBlock(doc, a) + u'\n';
    return d;
}

NoteDraft fromAnnotations(const DocumentRecord& doc, const QList<AnnotationRecord>& annotations)
{
    const QList<AnnotationRecord> list = inReadingOrder(annotations);
    if (list.size() == 1)
        return fromAnnotation(doc, list.first());
    NoteDraft d;
    d.type = NoteType::Reading;
    d.folder = readingFolder();
    d.title = tr("%1 — passages").arg(doc.title);
    QString body = frontMatter(doc, QStringLiteral("lecture"));
    for (const AnnotationRecord& a : list)
        body += quoteBlock(doc, a) + u'\n';
    d.body = body;
    return d;
}

NoteDraft fromComment(const DocumentRecord& doc, const CommentRecord& c)
{
    NoteDraft d;
    d.folder = readingFolder();
    d.type = c.kind == CommentKind::Question      ? NoteType::Research
             : c.kind == CommentKind::Explanation ? NoteType::Concept
                                                  : NoteType::Reading;
    d.title = headline(c.body);
    if (d.title.isEmpty())
        d.title = tr("Commentaire sur %1").arg(doc.title);
    QString body = frontMatter(doc, QStringLiteral("lecture"));
    if (!c.quote.trimmed().isEmpty())
        body += blockquote(normalizeQuote(c.quote)) + QStringLiteral("\n\n");
    body += c.body.trimmed() + QStringLiteral("\n\n");
    QStringList cite;
    if (const QString who = attribution(doc); !who.isEmpty())
        cite << who;
    const QString link = AppLink{doc.uuid, {}, c.uuid}.toString();
    const QString where = commentWhere(c);
    cite << QStringLiteral("[%1](%2)").arg(where.isEmpty() ? tr("ouvrir le commentaire") : where, link);
    body += QStringLiteral("— ") + cite.join(QStringLiteral(", ")) + u'\n';
    d.body = body;
    return d;
}

NoteDraft fromDocument(const DocumentRecord& doc, const QList<AnnotationRecord>& annotations,
                       const QList<CommentRecord>& comments)
{
    NoteDraft d;
    d.type = NoteType::Reading;
    d.folder = readingFolder();
    // « Lecture · Titre » : un titre identique à celui du document capterait ses [[liens]].
    d.title = doc.title.isEmpty() ? tr("Note de lecture") : tr("Lecture · %1").arg(doc.title);
    QString body = frontMatter(doc, QStringLiteral("lecture"));
    body += tr("## Idées principales\n\n\n");
    const QList<AnnotationRecord> list = inReadingOrder(annotations);
    if (!list.isEmpty()) {
        body += tr("## Passages annotés\n\n");
        for (const AnnotationRecord& a : list)
            body += quoteBlock(doc, a) + u'\n';
    }
    QStringList questions;
    for (const CommentRecord& c : comments) {
        if (c.kind != CommentKind::Question || c.resolved)
            continue;
        const QString where = commentWhere(c);
        const QString link = AppLink{doc.uuid, {}, c.uuid}.toString();
        questions << QStringLiteral("- [ ] %1 ([%2](%3))")
                         .arg(c.body.simplified(), where.isEmpty() ? tr("commentaire") : where, link);
    }
    if (!questions.isEmpty())
        body += tr("## Questions en suspens\n\n") + questions.join(u'\n') + QStringLiteral("\n\n");
    body += tr("## À retenir\n\n");
    d.body = body;
    return d;
}

}  // namespace studylab::NoteDrafts
