#include "services/notes/NoteDrafts.h"

#include "core/AppLink.h"

#include <QCoreApplication>
#include <QLocale>
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

QString revisionFolder() { return tr("Fiches de révision"); }

namespace {

/// Première phrase (ou début) du texte d'une note, sans front matter ni titres.
QString firstSentence(const QString& markdown, int max = 180)
{
    QString text;
    for (const QString& line : stripFrontMatter(markdown).split(u'\n')) {
        const QString l = line.trimmed();
        if (l.isEmpty()) {
            if (!text.isEmpty())
                break;
            continue;
        }
        if (l.startsWith(u'#') || l.startsWith(QLatin1String("$$")) || l.startsWith(QLatin1String("---")))
            continue;
        text += (text.isEmpty() ? QString() : QStringLiteral(" ")) + l;
    }
    text = text.simplified();
    static const QRegularExpression sentence(QStringLiteral("^(.{20,}?[.!?])(\\s|$)"));
    if (const auto m = sentence.match(text); m.hasMatch() && m.captured(1).size() <= max)
        return m.captured(1);
    if (text.size() <= max)
        return text;
    text = text.left(max);
    const qsizetype space = text.lastIndexOf(u' ');
    return (space > max / 2 ? text.left(space) : text).trimmed() + QStringLiteral("…");
}

QString noteLink(const QString& title)
{
    static const QRegularExpression unsafe(QStringLiteral("[\\[\\]|#^\\n]"));
    return unsafe.match(title).hasMatch() ? QStringLiteral("*%1*").arg(title) : QStringLiteral("[[%1]]").arg(title);
}

/// Formules affichées ($$…$$) d'un texte Markdown.
QStringList displayFormulas(const QString& markdown)
{
    static const QRegularExpression re(QStringLiteral("\\$\\$([\\s\\S]+?)\\$\\$"));
    QStringList out;
    for (auto it = re.globalMatch(markdown); it.hasNext();) {
        const QString f = it.next().captured(1).trimmed();
        if (!f.isEmpty())
            out << f;
    }
    return out;
}

QString indent(const QString& text)
{
    QStringList lines;
    for (const QString& l : text.trimmed().split(u'\n'))
        lines << (l.trimmed().isEmpty() ? QString() : QStringLiteral("  ") + l);
    return lines.join(u'\n');
}

}  // namespace

NoteDraft revisionSheet(const SheetMaterial& m, const SheetOptions& o)
{
    NoteDraft d;
    d.type = NoteType::Revision;
    d.folder = revisionFolder();
    d.title = tr("Fiche de révision · %1").arg(m.subject);
    const QLocale fr(QLocale::French, QLocale::France);

    int passageCount = 0;
    QString passages;
    for (const SheetDocument& sd : m.documents) {
        QList<AnnotationRecord> list = inReadingOrder(sd.annotations);
        list.erase(std::remove_if(list.begin(), list.end(),
                                  [](const AnnotationRecord& a) {
                                      return normalizeQuote(a.selectedText).isEmpty() && a.note.trimmed().isEmpty();
                                  }),
                   list.end());
        if (list.isEmpty())
            continue;
        // Trop de passages : les commentés d'abord, puis les autres, chacun dans l'ordre de lecture.
        if (list.size() > o.maxPassagesPerDocument) {
            std::stable_partition(list.begin(), list.end(),
                                  [](const AnnotationRecord& a) { return !a.note.trimmed().isEmpty(); });
            list = list.mid(0, o.maxPassagesPerDocument);
            std::stable_sort(list.begin(), list.end(),
                             [](const AnnotationRecord& a, const AnnotationRecord& b) { return a.sortKey < b.sortKey; });
        }
        passages += QStringLiteral("### %1\n\n").arg(documentRef(sd.doc).isEmpty() ? tr("Document sans titre")
                                                                                   : documentRef(sd.doc));
        for (const AnnotationRecord& a : list) {
            const QString quote = normalizeQuote(a.selectedText).simplified();
            const QString where = annotationLocation(a);
            const QString link = AppLink{sd.doc.uuid, a.uuid, {}}.toString();
            const QString ref = QStringLiteral("[%1](%2)").arg(where.isEmpty() ? tr("passage") : where, link);
            passages += quote.isEmpty() ? QStringLiteral("- %1\n").arg(ref)
                                        : QStringLiteral("- « %1 » (%2)\n").arg(quote, ref);
            if (!a.note.trimmed().isEmpty())
                passages += indent(a.note) + u'\n';
            ++passageCount;
        }
        passages += u'\n';
    }

    QStringList concepts;
    QStringList formulas;
    QStringList others;
    for (const NoteRecord& n : m.notes) {
        if (n.type == NoteType::Revision)
            continue;
        if (n.type == NoteType::Concept) {
            const QString first = firstSentence(n.body);
            concepts << (first.isEmpty() ? QStringLiteral("- **%1**").arg(noteLink(n.title))
                                         : QStringLiteral("- **%1** : %2").arg(noteLink(n.title), first));
        } else {
            others << QStringLiteral("- %1 · %2").arg(noteLink(n.title), noteTypeLabel(n.type));
        }
        for (const QString& f : displayFormulas(n.body))
            formulas << QStringLiteral("$$\n%1\n$$\n*%2*").arg(f, noteLink(n.title));
    }
    for (const SheetDocument& sd : m.documents) {
        for (const AnnotationRecord& a : sd.annotations)
            for (const QString& f : displayFormulas(a.note))
                formulas << QStringLiteral("$$\n%1\n$$\n*[%2](%3)*")
                                .arg(f, QStringLiteral("%1, %2").arg(sd.doc.title, annotationLocation(a)),
                                     AppLink{sd.doc.uuid, a.uuid, {}}.toString());
    }
    formulas.removeDuplicates();

    QStringList questions;
    for (const SheetDocument& sd : m.documents) {
        for (const CommentRecord& c : sd.comments) {
            if (c.kind != CommentKind::Question || c.resolved)
                continue;
            const QString where = commentWhere(c);
            questions << QStringLiteral("- [ ] %1 (%2, [%3](%4))")
                             .arg(c.body.simplified(), sd.doc.title, where.isEmpty() ? tr("commentaire") : where,
                                  AppLink{sd.doc.uuid, {}, c.uuid}.toString());
        }
    }
    for (const QString& q : m.cardQuestions)
        questions << QStringLiteral("- [ ] %1").arg(q.simplified());

    QString body = QStringLiteral("---\ntags: [révision, brouillon]\nmatière: %1\nstatut: brouillon\ngénéré: %2\n---\n\n")
                       .arg(yamlString(m.subject), m.date.toString(Qt::ISODate));
    QStringList counts;
    if (!m.documents.isEmpty())
        counts << (m.documents.size() == 1 ? tr("1 document") : tr("%1 documents").arg(m.documents.size()));
    if (passageCount > 0)
        counts << (passageCount == 1 ? tr("1 passage annoté") : tr("%1 passages annotés").arg(passageCount));
    const int noteCount = int(std::count_if(m.notes.begin(), m.notes.end(),
                                            [](const NoteRecord& n) { return n.type != NoteType::Revision; }));
    if (noteCount > 0)
        counts << (noteCount == 1 ? tr("1 note") : tr("%1 notes").arg(noteCount));
    body += tr("> **Brouillon** généré le %1 depuis %2 de « %3 ». Relisez-le, reformulez avec vos mots, gardez "
               "l'essentiel, puis marquez la fiche comme relue.\n\n")
                .arg(fr.toString(m.date, QStringLiteral("d MMMM yyyy")),
                     counts.isEmpty() ? tr("aucun contenu") : counts.join(QStringLiteral(", ")), m.subject);
    body += tr("## L'essentiel\n\n*Les trois ou quatre idées à retenir, avec vos mots.*\n\n");
    if (o.concepts && !concepts.isEmpty())
        body += tr("## Définitions\n\n") + concepts.join(u'\n') + QStringLiteral("\n\n");
    if (o.formulas && !formulas.isEmpty())
        body += tr("## Formules\n\n") + formulas.join(QStringLiteral("\n\n")) + QStringLiteral("\n\n");
    if (o.passages && !passages.isEmpty())
        body += tr("## Passages clés\n\n") + passages;
    if (o.questions && !questions.isEmpty())
        body += tr("## Questions pour s'entraîner\n\n") + questions.join(u'\n') + QStringLiteral("\n\n");
    if (!others.isEmpty())
        body += tr("## Notes de la matière\n\n") + others.join(u'\n') + u'\n';
    d.body = body;
    return d;
}

bool isDraftSheet(const QString& markdown)
{
    const FrontMatter fm = parseFrontMatter(markdown);
    return fm.present && fm.values.value(QStringLiteral("statut")).toString() == QLatin1String("brouillon");
}

QString markSheetReviewed(const QString& markdown)
{
    const FrontMatter fm = parseFrontMatter(markdown);
    if (!fm.present)
        return markdown;
    QStringList lines = markdown.left(fm.bodyStart).split(u'\n');
    static const QRegularExpression status(QStringLiteral("^statut\\s*:\\s*[\"']?brouillon[\"']?\\s*$"));
    static const QRegularExpression tagList(QStringLiteral("^tags\\s*:\\s*\\[(.*)\\]\\s*$"));
    for (int i = int(lines.size()) - 1; i >= 0; --i) {
        if (status.match(lines[i].trimmed()).hasMatch()) {
            lines.removeAt(i);
            continue;
        }
        if (const auto m = tagList.match(lines[i]); m.hasMatch()) {
            QStringList tags;
            for (const QString& t : m.captured(1).split(u','))
                if (const QString v = t.trimmed(); !v.isEmpty() && v != QLatin1String("brouillon"))
                    tags << v;
            lines[i] = QStringLiteral("tags: [%1]").arg(tags.join(QStringLiteral(", ")));
        }
    }
    QString out = lines.join(u'\n') + markdown.mid(fm.bodyStart);
    // Le paragraphe d'avertissement du brouillon n'a plus lieu d'être.
    static const QRegularExpression notice(QStringLiteral("^> \\*\\*Brouillon\\*\\* [^\\n]*\\n\\n?"),
                                           QRegularExpression::MultilineOption);
    out.remove(notice);
    return out;
}

}  // namespace studylab::NoteDrafts
