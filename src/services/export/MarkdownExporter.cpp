#include "services/export/MarkdownExporter.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>

#include <algorithm>

namespace studylab {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("MarkdownExporter", text);
}

/// Préfixe chaque ligne pour l'inclure dans un callout (« > »).
QString quoteBlock(const QString& text)
{
    QStringList lines = text.split(QLatin1Char('\n'));
    for (QString& l : lines)
        l = l.isEmpty() ? QStringLiteral(">") : QStringLiteral("> ") + l;
    return lines.join(QLatin1Char('\n'));
}

QString shortId(const QString& uuid)
{
    return uuid.left(8);
}

Result<void> writeText(const QString& path, const QString& text)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return Error::make(Error::Code::Io, tr("Impossible d'écrire %1 : %2").arg(path, f.errorString()));
    f.write(text.toUtf8());
    if (!f.commit())
        return Error::make(Error::Code::Io, tr("Impossible d'écrire %1 : %2").arg(path, f.errorString()));
    return {};
}

QString surname(const QString& author)
{
    const QString sort = makeAuthorSortName(author);
    const int comma = sort.indexOf(QLatin1Char(','));
    return (comma > 0 ? sort.left(comma) : sort).trimmed();
}

QString isoDate(const QString& isoDateTime)
{
    return isoDateTime.left(10);
}

}  // namespace

QString MarkdownExporter::safeFileName(const QString& name)
{
    QString s = name;
    static const QRegularExpression reserved(QStringLiteral("[<>:\"/\\\\|?*\\x00-\\x1F#^\\[\\]]"));
    s.replace(reserved, QStringLiteral(" "));
    s = s.simplified();
    while (s.endsWith(QLatin1Char('.')) || s.endsWith(QLatin1Char(' ')))
        s.chop(1);
    if (s.size() > 120)
        s = s.left(120).trimmed();
    static const QRegularExpression windowsReserved(QStringLiteral("^(CON|PRN|AUX|NUL|COM\\d|LPT\\d)$"),
                                                    QRegularExpression::CaseInsensitiveOption);
    if (s.isEmpty() || windowsReserved.match(s).hasMatch())
        s = QStringLiteral("Sans titre") + (s.isEmpty() ? QString() : QStringLiteral(" ") + s);
    return s;
}

QString MarkdownExporter::documentBaseName(const DocumentRecord& doc)
{
    QString base = doc.title.trimmed();
    if (!doc.authors.isEmpty())
        base = surname(doc.authors.first()) + QStringLiteral(" - ") + base;
    if (doc.year > 0)
        base += QStringLiteral(" (%1)").arg(doc.year);
    return safeFileName(base);
}

QString MarkdownExporter::yamlString(const QString& s)
{
    QString e = s;
    e.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
    e.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    e.replace(QLatin1Char('\n'), QStringLiteral("\\n"));
    return QLatin1Char('"') + e + QLatin1Char('"');
}

QString MarkdownExporter::pageRef(const AnnotationRecord& a)
{
    return annotationLocation(a);
}

QString MarkdownExporter::documentNote(const DocumentRecord& doc, const QList<AnnotationRecord>& annotations,
                                       const Options& options)
{
    QStringList y;
    y << QStringLiteral("---");
    y << QStringLiteral("type: document");
    y << QStringLiteral("title: ") + yamlString(doc.title);
    if (!doc.subtitle.isEmpty())
        y << QStringLiteral("subtitle: ") + yamlString(doc.subtitle);
    if (!doc.authors.isEmpty()) {
        y << QStringLiteral("authors:");
        for (const QString& a : doc.authors)
            y << QStringLiteral("  - ") + yamlString(a);
    }
    if (doc.year > 0)
        y << QStringLiteral("year: %1").arg(doc.year);
    if (!doc.publisher.isEmpty())
        y << QStringLiteral("publisher: ") + yamlString(doc.publisher);
    if (!doc.isbn.isEmpty())
        y << QStringLiteral("isbn: ") + yamlString(doc.isbn);
    if (!doc.doi.isEmpty())
        y << QStringLiteral("doi: ") + yamlString(doc.doi);
    if (!doc.language.isEmpty())
        y << QStringLiteral("language: ") + yamlString(doc.language);
    y << QStringLiteral("format: ") + documentFormatKey(doc.format);
    y << QStringLiteral("status: ") + readingStatusKey(doc.status);
    if (!doc.tags.isEmpty()) {
        // Les tags Obsidian n'acceptent pas les espaces : ils sont remplacés par des tirets.
        y << QStringLiteral("tags:");
        for (QString t : doc.tags)
            y << QStringLiteral("  - ") + yamlString(t.replace(QLatin1Char(' '), QLatin1Char('-')));
    }
    y << QStringLiteral("uuid: ") + doc.uuid;
    y << QStringLiteral("added: ") + isoDate(doc.addedAt);
    y << QStringLiteral("exported: ") + options.date.toString(Qt::ISODate);
    y << QStringLiteral("---");

    QStringList md = y;
    md << QString() << QStringLiteral("# ") + doc.title;
    if (!doc.subtitle.isEmpty())
        md << QString() << QStringLiteral("*") + doc.subtitle + QStringLiteral("*");
    md << QString();
    QStringList facts;
    if (!doc.authors.isEmpty())
        facts << QStringLiteral("**%1** : %2").arg(tr("Auteurs"), doc.authors.join(QStringLiteral(", ")));
    if (!doc.publisher.isEmpty() || doc.year > 0) {
        QStringList ed;
        if (!doc.publisher.isEmpty())
            ed << doc.publisher;
        if (doc.year > 0)
            ed << QString::number(doc.year);
        facts << QStringLiteral("**%1** : %2").arg(tr("Édition"), ed.join(QStringLiteral(", ")));
    }
    facts << QStringLiteral("**%1** : %2").arg(tr("Statut"), readingStatusLabel(doc.status));
    if (options.appLinks)
        facts << QStringLiteral("[%1](%2)").arg(tr("Ouvrir dans StudyLab"), appLink(doc.uuid));
    for (const QString& f : facts)
        md << QStringLiteral("- ") + f;
    if (!doc.description.isEmpty())
        md << QString() << doc.description;

    int bookmarks = 0;
    for (const auto& a : annotations)
        bookmarks += a.type == AnnotationType::Bookmark;
    const int count = int(annotations.size()) - bookmarks;
    md << QString() << QStringLiteral("## ") + tr("Annotations") << QString();
    if (count > 0)
        md << tr("%1 — voir [[%2/Annotations|Annotations]].")
                  .arg(count == 1 ? tr("1 annotation") : tr("%1 annotations").arg(count), baseName(doc, options));
    else
        md << tr("Aucune annotation.");
    const int comments = int(options.comments.size());
    if (comments > 0) {
        md << QString() << QStringLiteral("## ") + tr("Commentaires") << QString();
        md << tr("%1 — voir [[%2/Commentaires|Commentaires]].")
                  .arg(comments == 1 ? tr("1 commentaire") : tr("%1 commentaires").arg(comments), baseName(doc, options));
    }
    return md.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString MarkdownExporter::annotationsNote(const DocumentRecord& doc, const QList<AnnotationRecord>& annotations,
                                          const Options& options)
{
    const QString base = baseName(doc, options);
    QStringList md;
    md << QStringLiteral("---") << QStringLiteral("type: annotations")
       << QStringLiteral("document: ") + yamlString(QStringLiteral("[[%1]]").arg(base))
       << QStringLiteral("document_uuid: ") + doc.uuid
       << QStringLiteral("exported: ") + options.date.toString(Qt::ISODate) << QStringLiteral("---") << QString();
    md << QStringLiteral("# ") + tr("Annotations — %1").arg(doc.title) << QString();

    QList<AnnotationRecord> bookmarks;
    QList<AnnotationRecord> items;
    for (const auto& a : annotations)
        (a.type == AnnotationType::Bookmark ? bookmarks : items) << a;
    std::stable_sort(items.begin(), items.end(),
                     [](const AnnotationRecord& x, const AnnotationRecord& y) { return x.sortKey < y.sortKey; });
    std::stable_sort(bookmarks.begin(), bookmarks.end(),
                     [](const AnnotationRecord& x, const AnnotationRecord& y) { return x.unitIndex < y.unitIndex; });

    md << tr("Source : [[%1]] · %2 · exporté le %3")
              .arg(base,
                   items.size() == 1 ? tr("1 annotation") : tr("%1 annotations").arg(items.size()),
                   options.date.toString(QStringLiteral("dd/MM/yyyy")));

    if (!bookmarks.isEmpty()) {
        md << QString() << QStringLiteral("## ") + tr("Signets") << QString();
        for (const auto& b : bookmarks)
            md << QStringLiteral("- ") + pageRef(b) + (b.title.isEmpty() ? QString() : QStringLiteral(" — ") + b.title);
    }

    QString currentGroup;
    bool firstGroup = true;
    for (const auto& a : items) {
        QString group = options.chapterOf ? options.chapterOf(a.unitIndex) : QString();
        if (group.isEmpty())
            group = annotationGroupFallback(a);
        if (firstGroup || group != currentGroup) {
            md << QString() << QStringLiteral("## ") + group;
            currentGroup = group;
            firstGroup = false;
        }
        const QString calloutType = a.type == AnnotationType::Note ? QStringLiteral("note") : QStringLiteral("quote");
        const QString where = options.appLinks
                                  ? QStringLiteral("[%1](%2)").arg(pageRef(a), appLink(doc.uuid, QStringLiteral("annotation"), a.uuid))
                                  : pageRef(a);
        QString header = QStringLiteral("> [!%1] %2 · %3").arg(calloutType, where, annotationTypeLabel(a.type));
        if (a.type != AnnotationType::Note)
            header += QStringLiteral(" · ") + annotationColorLabel(a.color).toLower();
        md << QString() << header;
        if (!a.title.isEmpty())
            md << QStringLiteral("> **%1**").arg(a.title);
        if (!a.selectedText.isEmpty())
            md << quoteBlock(normalizeQuote(a.selectedText));
        else if (a.type == AnnotationType::Marker)
            md << QStringLiteral("> *%1*").arg(tr("Zone sans texte"));
        if (a.type == AnnotationType::Note && !a.note.isEmpty())
            md << quoteBlock(a.note.trimmed());
        md << QStringLiteral("^ann-") + shortId(a.uuid);
        if (a.type != AnnotationType::Note && !a.note.isEmpty())
            md << QString() << a.note.trimmed();
    }
    if (items.isEmpty() && bookmarks.isEmpty())
        md << QString() << tr("Aucune annotation.");
    return md.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

namespace {
QString calloutFor(CommentKind kind)
{
    switch (kind) {
    case CommentKind::Question: return QStringLiteral("question");
    case CommentKind::Explanation: return QStringLiteral("info");
    case CommentKind::Reference: return QStringLiteral("cite");
    case CommentKind::Summary: return QStringLiteral("abstract");
    case CommentKind::Reflection: break;
    }
    return QStringLiteral("note");
}
}  // namespace

QString MarkdownExporter::commentsNote(const DocumentRecord& doc, const QList<CommentRecord>& comments,
                                       const Options& options)
{
    const QString base = baseName(doc, options);
    QStringList md;
    md << QStringLiteral("---") << QStringLiteral("type: comments")
       << QStringLiteral("document: ") + yamlString(QStringLiteral("[[%1]]").arg(base))
       << QStringLiteral("document_uuid: ") + doc.uuid
       << QStringLiteral("exported: ") + options.date.toString(Qt::ISODate) << QStringLiteral("---") << QString();
    md << QStringLiteral("# ") + tr("Commentaires — %1").arg(doc.title) << QString();
    md << tr("Source : [[%1]] · %2 · exporté le %3")
              .arg(base, comments.size() == 1 ? tr("1 commentaire") : tr("%1 commentaires").arg(comments.size()),
                   options.date.toString(QStringLiteral("dd/MM/yyyy")));

    QList<CommentRecord> sorted = comments;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const CommentRecord& x, const CommentRecord& y) { return x.sortKey < y.sortKey; });
    QString currentGroup;
    bool firstGroup = true;
    for (const CommentRecord& c : sorted) {
        QString group;
        if (c.anchor == CommentAnchor::Free || c.unitIndex < 0) {
            group = tr("Commentaires généraux");
        } else {
            group = options.chapterOf ? options.chapterOf(c.unitIndex) : QString();
            if (group.isEmpty()) {
                AnnotationRecord probe;
                probe.unitIndex = c.unitIndex;
                probe.pageLabel = c.pageLabel;
                probe.locator = c.locator;
                group = annotationGroupFallback(probe);
            }
        }
        if (firstGroup || group != currentGroup) {
            md << QString() << QStringLiteral("## ") + group;
            currentGroup = group;
            firstGroup = false;
        }
        QStringList head{commentKindLabel(c.kind)};
        const QString where = commentLocation(c);
        if (!where.isEmpty())
            head << (options.appLinks ? QStringLiteral("[%1](%2)").arg(where, appLink(doc.uuid, QStringLiteral("comment"), c.uuid))
                                      : where);
        else if (options.appLinks)
            head << QStringLiteral("[%1](%2)").arg(tr("ouvrir"), appLink(doc.uuid, QStringLiteral("comment"), c.uuid));
        if (c.kind == CommentKind::Question)
            head << (c.resolved ? tr("résolue") : tr("ouverte"));
        md << QString() << QStringLiteral("> [!%1] %2").arg(calloutFor(c.kind), head.join(QStringLiteral(" · ")));
        if (!c.quote.isEmpty()) {
            QString line = QStringLiteral("> *« %1 »*").arg(normalizeQuote(c.quote));
            md << line;
            md << QStringLiteral(">");
        }
        md << quoteBlock(c.body.trimmed());
        md << QStringLiteral("^com-") + shortId(c.uuid);
    }
    if (sorted.isEmpty())
        md << QString() << tr("Aucun commentaire.");
    return md.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QJsonObject MarkdownExporter::metadata(const DocumentRecord& doc, const QList<AnnotationRecord>& annotations,
                                       const Options& options)
{
    QJsonObject d{
        {QStringLiteral("uuid"), doc.uuid},
        {QStringLiteral("title"), doc.title},
        {QStringLiteral("subtitle"), doc.subtitle},
        {QStringLiteral("authors"), QJsonArray::fromStringList(doc.authors)},
        {QStringLiteral("year"), doc.year > 0 ? QJsonValue(doc.year) : QJsonValue()},
        {QStringLiteral("publisher"), doc.publisher},
        {QStringLiteral("isbn"), doc.isbn},
        {QStringLiteral("doi"), doc.doi},
        {QStringLiteral("language"), doc.language},
        {QStringLiteral("format"), documentFormatKey(doc.format)},
        {QStringLiteral("status"), readingStatusKey(doc.status)},
        {QStringLiteral("favorite"), doc.favorite},
        {QStringLiteral("tags"), QJsonArray::fromStringList(doc.tags)},
        {QStringLiteral("pageCount"), doc.pageCount},
        {QStringLiteral("fileHash"), doc.fileHash},
        {QStringLiteral("addedAt"), doc.addedAt},
    };
    QJsonArray anns;
    for (const auto& a : annotations) {
        anns.append(QJsonObject{
            {QStringLiteral("uuid"), a.uuid},
            {QStringLiteral("type"), annotationTypeKey(a.type)},
            {QStringLiteral("color"), annotationColorKey(a.color)},
            {QStringLiteral("page"), a.unitIndex},
            {QStringLiteral("pageLabel"), a.pageLabel},
            {QStringLiteral("text"), a.selectedText},
            {QStringLiteral("title"), a.title},
            {QStringLiteral("note"), a.note},
            {QStringLiteral("locator"), a.locator},
            {QStringLiteral("anchorStatus"), a.anchorStatus},
            {QStringLiteral("createdAt"), a.createdAt},
            {QStringLiteral("updatedAt"), a.updatedAt},
        });
    }
    QJsonArray coms;
    for (const auto& c : options.comments) {
        coms.append(QJsonObject{
            {QStringLiteral("uuid"), c.uuid},
            {QStringLiteral("kind"), commentKindKey(c.kind)},
            {QStringLiteral("anchor"), commentAnchorKey(c.anchor)},
            {QStringLiteral("annotation"), c.annotationId > 0 ? QJsonValue(c.annotationId) : QJsonValue()},
            {QStringLiteral("unit"), c.unitIndex >= 0 ? QJsonValue(c.unitIndex) : QJsonValue()},
            {QStringLiteral("locator"), c.locator.isEmpty() ? QJsonValue() : QJsonValue(c.locator)},
            {QStringLiteral("quote"), c.quote},
            {QStringLiteral("body"), c.body},
            {QStringLiteral("resolved"), c.resolved},
            {QStringLiteral("links"), QJsonArray::fromStringList(wikiLinkTargets(c.body))},
            {QStringLiteral("createdAt"), c.createdAt},
            {QStringLiteral("updatedAt"), c.updatedAt},
        });
    }
    return QJsonObject{{QStringLiteral("studylabExport"), 1},
                       {QStringLiteral("exportedAt"), options.date.toString(Qt::ISODate)},
                       {QStringLiteral("document"), d},
                       {QStringLiteral("annotations"), anns},
                       {QStringLiteral("comments"), coms}};
}

QString MarkdownExporter::baseName(const DocumentRecord& doc, const Options& options)
{
    return options.baseName.isEmpty() ? documentBaseName(doc) : options.baseName;
}

QString MarkdownExporter::appLink(const QString& documentUuid, const QString& itemKey, const QString& itemUuid)
{
    QString link = QStringLiteral("studylab://open?doc=") + documentUuid;
    if (!itemKey.isEmpty() && !itemUuid.isEmpty())
        link += QLatin1Char('&') + itemKey + QLatin1Char('=') + itemUuid;
    return link;
}

Result<QString> MarkdownExporter::exportDocument(const QString& targetDir, const DocumentRecord& doc,
                                                 const QList<AnnotationRecord>& annotations, const Options& options)
{
    const QString base = baseName(doc, options);
    QDir root(targetDir);
    if (!root.exists() || !root.mkpath(base))
        return Error::make(Error::Code::Io, tr("Impossible de créer le dossier %1.").arg(root.filePath(base)));
    const QString folder = root.filePath(base);
    if (auto r = writeText(folder + QLatin1Char('/') + base + QStringLiteral(".md"), documentNote(doc, annotations, options)); !r)
        return r.error();
    if (auto r = writeText(folder + QStringLiteral("/Annotations.md"), annotationsNote(doc, annotations, options)); !r)
        return r.error();
    const QString commentsFile = folder + QStringLiteral("/Commentaires.md");
    if (!options.comments.isEmpty()) {
        if (auto r = writeText(commentsFile, commentsNote(doc, options.comments, options)); !r)
            return r.error();
    } else {
        QFile::remove(commentsFile);  // export précédent
    }
    const QByteArray json = QJsonDocument(metadata(doc, annotations, options)).toJson(QJsonDocument::Indented);
    if (auto r = writeText(folder + QStringLiteral("/metadata.json"), QString::fromUtf8(json)); !r)
        return r.error();
    return folder;
}

}  // namespace studylab
