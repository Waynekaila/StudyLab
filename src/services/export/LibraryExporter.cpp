#include "services/export/LibraryExporter.h"

#include "services/annotations/AnnotationService.h"
#include "services/comments/CommentService.h"
#include "services/export/MarkdownExporter.h"
#include "services/library/LibraryService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>

namespace studylab {

namespace {

QString tr(const char* text)
{
    return QCoreApplication::translate("LibraryExporter", text);
}

Result<void> writeUtf8(const QString& path, const QByteArray& data)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(data) != data.size() || !f.commit())
        return Error::make(Error::Code::Io, tr("Impossible d'écrire %1 : %2").arg(path, f.errorString()));
    return {};
}

QString plural(int n, const char* one, const char* many)
{
    return n == 1 ? tr(one) : tr(many).arg(n);
}

int countAnnotations(const QList<AnnotationRecord>& list)
{
    int n = 0;
    for (const auto& a : list)
        n += a.type != AnnotationType::Bookmark;
    return n;
}

}  // namespace

Result<QList<DocumentExportData>> LibraryExporter::collect(LibraryService& library, AnnotationService& annotations,
                                                           CommentService& comments, const QList<qint64>& documentIds)
{
    QList<DocumentExportData> out;
    out.reserve(documentIds.size());
    for (qint64 id : documentIds) {
        auto doc = library.document(id);
        if (!doc)
            return doc.error();
        auto anns = annotations.list(id);
        if (!anns)
            return anns.error();
        auto coms = comments.list(id);
        if (!coms)
            return coms.error();
        out.append({doc.value(), library.documentFile(doc.value()), anns.value(), coms.value()});
    }
    return out;
}

QString LibraryExporter::folderName(const QDate& date)
{
    return QStringLiteral("StudyLab Export ") + date.toString(Qt::ISODate);
}

QStringList LibraryExporter::uniqueBaseNames(const QList<DocumentExportData>& docs)
{
    QStringList names;
    QHash<QString, int> used;  // en minuscules : Windows ne distingue pas la casse
    for (const auto& d : docs) {
        const QString base = MarkdownExporter::documentBaseName(d.doc);
        QString name = base;
        for (int n = 2; used.contains(name.toLower()); ++n)
            name = QStringLiteral("%1 (%2)").arg(base).arg(n);
        used.insert(name.toLower(), 1);
        names << name;
    }
    return names;
}

QString LibraryExporter::csvField(const QString& value)
{
    // RFC 4180 : guillemets doublés, champ entre guillemets s'il contient un séparateur ou un saut de ligne.
    QString v = value;
    v.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    if (v.contains(QLatin1Char(',')) || v.contains(QLatin1Char('"')) || v.contains(QLatin1Char('\n'))
        || v.contains(QLatin1Char('\r')) || v.startsWith(QLatin1Char(' ')) || v.endsWith(QLatin1Char(' '))) {
        v.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + v + QLatin1Char('"');
    }
    return v;
}

QString LibraryExporter::csvHeader()
{
    const QStringList cols{tr("Document"), tr("Auteurs"), tr("Année"), tr("Type"), tr("Couleur"), tr("Page"),
                           tr("Chapitre"), tr("Citation"), tr("Note"), tr("Créée le"), tr("Modifiée le"),
                           tr("UUID document"), tr("UUID annotation")};
    QStringList out;
    for (const QString& c : cols)
        out << csvField(c);
    return out.join(QLatin1Char(','));
}

QStringList LibraryExporter::csvRows(const DocumentExportData& data, const ChapterIndex& chapters)
{
    QList<AnnotationRecord> sorted = data.annotations;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const AnnotationRecord& x, const AnnotationRecord& y) { return x.sortKey < y.sortKey; });
    QStringList rows;
    for (const AnnotationRecord& a : sorted) {
        QString chapter = chapters.chapterOf(a.unitIndex);
        const QStringList fields{
            data.doc.title,
            data.doc.authors.join(QStringLiteral("; ")),
            data.doc.year > 0 ? QString::number(data.doc.year) : QString(),
            annotationTypeLabel(a.type),
            a.type == AnnotationType::Bookmark ? QString() : annotationColorLabel(a.color),
            MarkdownExporter::pageRef(a),
            chapter,
            normalizeQuote(a.selectedText),
            a.note.trimmed(),
            a.createdAt,
            a.updatedAt,
            data.doc.uuid,
            a.uuid,
        };
        QStringList out;
        for (const QString& f : fields)
            out << csvField(f);
        rows << out.join(QLatin1Char(','));
    }
    return rows;
}

QString LibraryExporter::readme(const QList<DocumentExportData>& docs, const QStringList& baseNames,
                                const Options& options)
{
    int anns = 0;
    int coms = 0;
    for (const auto& d : docs) {
        anns += countAnnotations(d.annotations);
        coms += int(d.comments.size());
    }
    QStringList md;
    md << QStringLiteral("# ") + tr("Export StudyLab du %1").arg(options.date.toString(QStringLiteral("dd/MM/yyyy")))
       << QString();
    const QString app = options.appVersion.isEmpty() ? QStringLiteral("StudyLab")
                                                      : QStringLiteral("StudyLab ") + options.appVersion;
    md << tr("Exporté par %1 : %2, %3, %4.")
              .arg(app, plural(int(docs.size()), "1 document", "%1 documents"),
                   plural(anns, "1 annotation", "%1 annotations"), plural(coms, "1 commentaire", "%1 commentaires"));
    md << QString() << QStringLiteral("## ") + tr("Ouvrir dans Obsidian") << QString();
    md << tr("1. Dans Obsidian, choisissez « Ouvrir un dossier comme coffre » et sélectionnez ce dossier.");
    md << tr("2. Chaque document a son dossier dans `Bibliothèque/` : une fiche avec ses métadonnées (YAML), "
             "`Annotations.md`, `Commentaires.md` s'il y en a, et `metadata.json`.");
    md << tr("3. Les citations sont des *callouts* ; chacune a un identifiant de bloc (`^ann-…`) pour y faire "
             "référence depuis vos notes : `[[Annotations#^ann-1a2b3c4d]]`.");
    if (options.appLinks)
        md << tr("4. Les liens de page ouvrent le passage dans StudyLab s'il est installé sur cet ordinateur.");
    md << QString() << tr("`annotations.csv` (UTF-8, séparateur virgule) s'ouvre dans un tableur.");
    md << QString() << QStringLiteral("## ") + tr("Documents") << QString();
    for (int i = 0; i < docs.size(); ++i) {
        const auto& d = docs.at(i);
        QStringList facts;
        if (!d.doc.authors.isEmpty())
            facts << d.doc.authors.join(QStringLiteral(", "));
        facts << plural(countAnnotations(d.annotations), "1 annotation", "%1 annotations");
        if (!d.comments.isEmpty())
            facts << plural(int(d.comments.size()), "1 commentaire", "%1 commentaires");
        md << QStringLiteral("- [[%1]] — %2").arg(baseNames.at(i), facts.join(QStringLiteral(" · ")));
    }
    if (docs.isEmpty())
        md << tr("Aucun document.");
    return md.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

Result<LibraryExportResult> LibraryExporter::write(const QString& parentDir, const QList<DocumentExportData>& docs,
                                                   const Options& options, const Progress& progress)
{
    LibraryExportResult result;
    QDir parent(parentDir);
    const QString name = folderName(options.date);
    if (!parent.exists() || !parent.mkpath(name + QStringLiteral("/Bibliothèque")))
        return Error::make(Error::Code::Io, tr("Impossible de créer le dossier %1.").arg(parent.filePath(name)));
    result.folder = parent.filePath(name);
    const QString libraryDir = result.folder + QStringLiteral("/Bibliothèque");

    const QStringList names = uniqueBaseNames(docs);
    QStringList csv{csvHeader()};
    for (int i = 0; i < docs.size(); ++i) {
        const DocumentExportData& d = docs.at(i);
        const ChapterIndex chapters = ChapterIndex::forDocument(d.file, d.doc.format);
        MarkdownExporter::Options opt;
        opt.date = options.date;
        opt.chapterOf = [&chapters](int unit) { return chapters.chapterOf(unit); };
        opt.comments = d.comments;
        opt.baseName = names.at(i);
        opt.appLinks = options.appLinks;
        if (auto r = MarkdownExporter::exportDocument(libraryDir, d.doc, d.annotations, opt); !r) {
            result.errors << QStringLiteral("%1 : %2").arg(d.doc.title, r.error().message);
        } else {
            ++result.documents;
            result.annotations += countAnnotations(d.annotations);
            result.comments += int(d.comments.size());
            csv << csvRows(d, chapters);
        }
        if (progress && !progress(i + 1, int(docs.size()))) {
            result.cancelled = true;
            break;
        }
    }
    // Index et CSV décrivent ce qui a réellement été écrit
    const QByteArray bom("\xEF\xBB\xBF");  // Excel reconnaît l'UTF-8 grâce à la marque d'ordre
    if (auto r = writeUtf8(result.folder + QStringLiteral("/annotations.csv"),
                           bom + csv.join(QStringLiteral("\r\n")).toUtf8() + "\r\n");
        !r)
        return r.error();
    QList<DocumentExportData> written;
    QStringList writtenNames;
    for (int i = 0; i < docs.size() && i < names.size(); ++i) {
        if (QFileInfo::exists(libraryDir + QLatin1Char('/') + names.at(i) + QLatin1Char('/') + names.at(i) + QStringLiteral(".md"))) {
            written << docs.at(i);
            writtenNames << names.at(i);
        }
    }
    if (auto r = writeUtf8(result.folder + QStringLiteral("/README.md"), readme(written, writtenNames, options).toUtf8()); !r)
        return r.error();
    return result;
}

}  // namespace studylab
