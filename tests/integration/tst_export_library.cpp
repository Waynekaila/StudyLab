#include "core/AppPaths.h"
#include "data/migrations/Migrator.h"
#include "data/sqlite/Database.h"
#include "engines/epub/EpubPackage.h"
#include "services/annotations/AnnotationService.h"
#include "services/comments/CommentService.h"
#include "services/export/ChapterIndex.h"
#include "services/export/LibraryExporter.h"
#include "services/export/MarkdownExporter.h"
#include "services/library/LibraryService.h"
#include "support/Fixtures.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::test;

namespace {

QByteArray utf16Hex(const QString& s)
{
    QByteArray out("<FEFF");
    for (QChar c : s)
        out += QByteArray::number(c.unicode(), 16).rightJustified(4, '0').toUpper();
    return out + '>';
}

/// PDF de trois pages écrit à la main, avec un sommaire (QPdfWriter ne sait pas en produire).
bool writePdfWithOutline(const QString& path, const QStringList& outlineTitles, const QList<int>& outlinePages)
{
    const int pages = 3;
    QList<QByteArray> objs;
    const int firstOutline = 4;
    const int firstPage = firstOutline + int(outlineTitles.size());
    QByteArray kids;
    for (int p = 0; p < pages; ++p)
        kids += QByteArray::number(firstPage + p) + " 0 R ";
    objs << "<< /Type /Catalog /Pages 2 0 R /Outlines 3 0 R >>";
    objs << "<< /Type /Pages /Kids [" + kids + "] /Count " + QByteArray::number(pages) + " >>";
    objs << "<< /Type /Outlines /First " + QByteArray::number(firstOutline) + " 0 R /Last "
                + QByteArray::number(firstOutline + outlineTitles.size() - 1) + " 0 R /Count "
                + QByteArray::number(outlineTitles.size()) + " >>";
    for (int i = 0; i < outlineTitles.size(); ++i) {
        QByteArray o = "<< /Title " + utf16Hex(outlineTitles.at(i)) + " /Parent 3 0 R";
        if (i > 0)
            o += " /Prev " + QByteArray::number(firstOutline + i - 1) + " 0 R";
        if (i + 1 < outlineTitles.size())
            o += " /Next " + QByteArray::number(firstOutline + i + 1) + " 0 R";
        o += " /Dest [" + QByteArray::number(firstPage + outlinePages.at(i)) + " 0 R /XYZ 0 200 0] >>";
        objs << o;
    }
    for (int p = 0; p < pages; ++p)
        objs << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] >>";

    QByteArray pdf("%PDF-1.4\n");
    QList<qsizetype> offsets;
    for (int i = 0; i < objs.size(); ++i) {
        offsets << pdf.size();
        pdf += QByteArray::number(i + 1) + " 0 obj\n" + objs.at(i) + "\nendobj\n";
    }
    const qsizetype xref = pdf.size();
    pdf += "xref\n0 " + QByteArray::number(objs.size() + 1) + "\n0000000000 65535 f \n";
    for (qsizetype off : offsets)
        pdf += QByteArray::number(off).rightJustified(10, '0') + " 00000 n \n";
    pdf += "trailer\n<< /Size " + QByteArray::number(objs.size() + 1) + " /Root 1 0 R >>\nstartxref\n"
           + QByteArray::number(xref) + "\n%%EOF\n";
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(pdf) == pdf.size();
}

QString readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()) : QString();
}

}  // namespace

/// Campagne MVP : import → annotations et commentaires → export de la bibliothèque (coffre Obsidian + CSV).
class TstExportLibrary : public QObject {
    Q_OBJECT

    QTemporaryDir m_tmp;

private slots:
    void initTestCase()
    {
        qRegisterMetaType<ImportReport>();
        QVERIFY(m_tmp.isValid());
    }

    void csvAndNames()
    {
        QCOMPARE(LibraryExporter::csvField(QStringLiteral("simple")), QStringLiteral("simple"));
        QCOMPARE(LibraryExporter::csvField(QStringLiteral("a, b")), QStringLiteral("\"a, b\""));
        QCOMPARE(LibraryExporter::csvField(QStringLiteral("dit \"oui\"")), QStringLiteral("\"dit \"\"oui\"\"\""));
        QCOMPARE(LibraryExporter::csvField(QStringLiteral("deux\r\nlignes")), QStringLiteral("\"deux\nlignes\""));
        QCOMPARE(LibraryExporter::folderName(QDate(2026, 11, 19)), QStringLiteral("StudyLab Export 2026-11-19"));

        DocumentExportData a;
        a.doc.title = QStringLiteral("Cours");
        a.doc.authors = {QStringLiteral("Jean Dupont")};
        a.doc.year = 2020;
        DocumentExportData b = a;
        DocumentExportData c = a;
        c.doc.title = QStringLiteral("COURS");  // même nom sous Windows
        QCOMPARE(LibraryExporter::uniqueBaseNames({a, b, c}),
                 QStringList({QStringLiteral("Dupont - Cours (2020)"), QStringLiteral("Dupont - Cours (2020) (2)"),
                              QStringLiteral("Dupont - COURS (2020) (3)")}));

        QCOMPARE(MarkdownExporter::appLink(QStringLiteral("u1")), QStringLiteral("studylab://open?doc=u1"));
        QCOMPARE(MarkdownExporter::appLink(QStringLiteral("u1"), QStringLiteral("annotation"), QStringLiteral("a2")),
                 QStringLiteral("studylab://open?doc=u1&annotation=a2"));
    }

    void notesInTheExport()
    {
        NoteRecord note;
        note.title = QStringLiteral("Cycle de Carnot");
        note.slug = QStringLiteral("Cycle de Carnot");
        note.uuid = QStringLiteral("n-1");
        note.type = NoteType::Concept;
        note.folder = QStringLiteral("Thermo/Cours");
        note.body = QStringLiteral("---\ntags: [cours]\n---\n\nVoir [[Cours]].\n\n![](attachments/a1.png)\n");
        QCOMPARE(LibraryExporter::notePath(note), QStringLiteral("Notes/Thermo/Cours/Cycle de Carnot.md"));
        QCOMPARE(LibraryExporter::noteAttachments(note.body), QStringList{QStringLiteral("attachments/a1.png")});
        const QString md = LibraryExporter::noteMarkdown(note);
        QVERIFY2(md.startsWith(QStringLiteral("---\ntags: [cours]\ntype: concept\nuuid: n-1\n---\n")), qPrintable(md));
        QVERIFY(md.contains(QStringLiteral("![](../../../_attachments/a1.png)")));
        QVERIFY(md.contains(QStringLiteral("[[Cours]]")));

        NoteRecord root = note;
        root.folder.clear();
        root.body = QStringLiteral("Sans front matter ![x](attachments/b.png)");
        QCOMPARE(LibraryExporter::notePath(root), QStringLiteral("Notes/Cycle de Carnot.md"));
        const QString rootMd = LibraryExporter::noteMarkdown(root);
        QVERIFY(rootMd.startsWith(QStringLiteral("---\ntype: concept\nuuid: n-1\n---\n\n")));
        QVERIFY(rootMd.contains(QStringLiteral("![x](../_attachments/b.png)")));

        // Écriture : notes, images copiées, README qui les compte.
        QTemporaryDir attachments;
        QFile img(attachments.filePath(QStringLiteral("a1.png")));
        QVERIFY(img.open(QIODevice::WriteOnly));
        img.write("PNG");
        img.close();
        LibraryExporter::Options opt;
        opt.date = QDate(2026, 11, 20);
        opt.notes = {note};
        opt.attachmentsDir = attachments.path();
        opt.bibtex = QStringLiteral("@book{cengel2019,\n  title = {Thermodynamique}\n}\n");
        opt.references = 1;
        QTemporaryDir out;
        auto result = LibraryExporter::write(out.path(), {}, opt);
        QVERIFY(result);
        QCOMPARE(result.value().notes, 1);
        QCOMPARE(result.value().attachments, 1);
        QVERIFY(result.value().errors.isEmpty());
        const QString folder = result.value().folder;
        QCOMPARE(readAll(folder + QStringLiteral("/Notes/Thermo/Cours/Cycle de Carnot.md")), md);
        QVERIFY(QFile::exists(folder + QStringLiteral("/_attachments/a1.png")));
        const QString readme = readAll(folder + QStringLiteral("/README.md"));
        QVERIFY2(readme.contains(QStringLiteral("1 note.")), qPrintable(readme));
        QVERIFY(readme.contains(QStringLiteral("`Notes/`")));
        // Références citées par [[@clé]] : fichier BibTeX à la racine.
        QCOMPARE(readAll(folder + QStringLiteral("/Références.bib")), opt.bibtex);
        QVERIFY(readme.contains(QStringLiteral("`Références.bib` contient 1 référence")));
    }

    void chapterIndexReadsPdfOutlineAndEpubToc()
    {
        const QString pdf = m_tmp.filePath(QStringLiteral("sommaire.pdf"));
        QVERIFY(writePdfWithOutline(pdf, {QStringLiteral("Chapitre 1 · Énergie"), QStringLiteral("Chapitre 2 · L'entropie")}, {0, 2}));
        const ChapterIndex p = ChapterIndex::forPdf(pdf);
        QCOMPARE(p.entries().size(), 2);
        QCOMPARE(p.chapterOf(0), QStringLiteral("Chapitre 1 · Énergie"));
        QCOMPARE(p.chapterOf(1), QStringLiteral("Chapitre 1 · Énergie"));
        QCOMPARE(p.chapterOf(2), QStringLiteral("Chapitre 2 · L'entropie"));

        for (bool epub3 : {true, false}) {
            EpubSpec spec;
            spec.title = QStringLiteral("Livre");
            spec.epub3 = epub3;
            spec.chapters = {QStringLiteral("<p>Un.</p>"), QStringLiteral("<p>Deux.</p>"), QStringLiteral("<p>Trois.</p>")};
            spec.tocTitles = {QStringLiteral("Premier"), QStringLiteral("Deuxième & suite"), QStringLiteral("Troisième")};
            const QString path = m_tmp.filePath(epub3 ? QStringLiteral("toc3.epub") : QStringLiteral("toc2.epub"));
            QVERIFY(writeEpub(path, spec));
            auto pkg = EpubPackage::open(path);
            QVERIFY(pkg.ok());
            const auto toc = pkg.value()->toc();
            QCOMPARE(toc.size(), 3);
            QCOMPARE(toc.at(1).label, QStringLiteral("Deuxième & suite"));
            QCOMPARE(toc.at(1).section, 1);
            const ChapterIndex e = ChapterIndex::forEpub(path);
            QCOMPARE(e.chapterOf(2), QStringLiteral("Troisième"));
        }
        QVERIFY(ChapterIndex::forPdf(m_tmp.filePath(QStringLiteral("absent.pdf"))).isEmpty());
    }

    void importAnnotateCommentExport()
    {
        // Sources : un PDF avec sommaire, deux PDF de même titre, un EPUB 3 avec table des matières
        const QString src = m_tmp.filePath(QStringLiteral("src"));
        QVERIFY(QDir().mkpath(src));
        QVERIFY(writePdfWithOutline(src + QStringLiteral("/thermo.pdf"),
                                    {QStringLiteral("Chapitre 1 · Énergie"), QStringLiteral("Chapitre 2 · L'entropie")}, {0, 2}));
        QVERIFY(writePdf(src + QStringLiteral("/cours-a.pdf"), {QStringLiteral("Cours de mécanique"), 2, QStringLiteral("Version A")}));
        QVERIFY(writePdf(src + QStringLiteral("/cours-b.pdf"), {QStringLiteral("Cours de mécanique"), 2, QStringLiteral("Version B")}));
        EpubSpec e;
        e.title = QStringLiteral("L'Écume des jours");
        e.authors = {QStringLiteral("Boris Vian")};
        e.chapters = {QStringLiteral("<p>Colin.</p>"), QStringLiteral("<p>Chloé.</p>")};
        e.tocTitles = {QStringLiteral("I"), QStringLiteral("II")};
        QVERIFY(writeEpub(src + QStringLiteral("/vian.epub"), e));

        QTemporaryDir data;
        AppPaths paths(data.path());
        QVERIFY(paths.ensureCreated());
        auto db = db::Database::openInMemory();
        QVERIFY(db::Migrator(db::Migrator::loadFrom().value()).migrate(*db.value()).ok());
        LibraryService lib(*db.value(), paths);
        AnnotationService annotations(*db.value());
        CommentService comments(*db.value());

        QSignalSpy finished(&lib, &LibraryService::importFinished);
        lib.importPaths({src});
        QVERIFY(finished.wait(30000));
        QCOMPARE(lib.count(), 4);

        qint64 thermo = 0;
        qint64 vian = 0;
        QList<qint64> ids;
        const auto all = lib.documents();
        QVERIFY(all.ok());
        for (const auto& d : all.value()) {
            ids << d.id;
            if (d.title == QLatin1String("thermo"))
                thermo = d.id;
            if (d.format == DocumentFormat::Epub)
                vian = d.id;
        }
        QVERIFY(thermo > 0 && vian > 0);

        // Annotations : une par chapitre du PDF, une citation avec virgule et guillemets, une dans l'EPUB
        auto pdfAnnotation = [&](int page, const QString& text) {
            PdfLocator l;
            l.page = page;
            l.rects = {QRectF(0.1, 0.2, 0.5, 0.02)};
            l.exact = text;
            AnnotationRecord r;
            r.documentId = thermo;
            r.locator = l.toJson();
            r.selectedText = text;
            auto created = annotations.create(r);
            if (!created)
                qFatal("%s", qPrintable(created.error().message));
            return created.value();
        };
        const AnnotationRecord a1 = pdfAnnotation(0, QStringLiteral("L'énergie se conserve"));
        const AnnotationRecord a2 = pdfAnnotation(2, QStringLiteral("L'entropie, dit-on, « croît »"));
        EpubLocator el;
        el.cfi = QStringLiteral("epubcfi(/6/4!/4/2,/1:0,/1:5)");
        el.section = 1;
        el.exact = QStringLiteral("Chloé");
        AnnotationRecord ea;
        ea.documentId = vian;
        ea.color = AnnotationColor::Green;
        ea.locator = el.toJson();
        ea.selectedText = QStringLiteral("Chloé");
        QVERIFY(annotations.create(ea).ok());

        CommentRecord free;
        free.documentId = thermo;
        free.anchor = CommentAnchor::Free;
        free.kind = CommentKind::Question;
        free.body = QStringLiteral("Pourquoi $\\Delta S \\geq 0$ ?");
        QVERIFY(comments.create(free).ok());
        CommentRecord onAnnotation;
        onAnnotation.documentId = thermo;
        onAnnotation.anchor = CommentAnchor::Annotation;
        onAnnotation.annotationId = a2.id;
        onAnnotation.body = QStringLiteral("Voir [[Cycle de Carnot]]");
        QVERIFY(comments.create(onAnnotation).ok());

        auto collected = LibraryExporter::collect(lib, annotations, comments, ids);
        QVERIFY2(collected.ok(), collected.ok() ? "" : qPrintable(collected.error().message));
        QCOMPARE(collected.value().size(), 4);

        QTemporaryDir out;
        LibraryExporter::Options opt;
        opt.date = QDate(2026, 11, 19);
        opt.appVersion = QStringLiteral("0.1.0");
        int calls = 0;
        auto result = LibraryExporter::write(out.path(), collected.value(), opt, [&](int done, int total) {
            ++calls;
            return done <= total;
        });
        QVERIFY2(result.ok(), result.ok() ? "" : qPrintable(result.error().message));
        QCOMPARE(calls, 4);
        const LibraryExportResult r = result.value();
        QCOMPARE(r.documents, 4);
        QCOMPARE(r.annotations, 3);
        QCOMPARE(r.comments, 2);
        QVERIFY(r.errors.isEmpty());
        QVERIFY(!r.cancelled);
        QCOMPARE(QDir(r.folder).dirName(), QStringLiteral("StudyLab Export 2026-11-19"));

        const QDir libDir(r.folder + QStringLiteral("/Bibliothèque"));
        QStringList folders = libDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        folders.sort();
        QCOMPARE(folders.size(), 4);
        QVERIFY2(folders.contains(QStringLiteral("Cours de mécanique")), qPrintable(folders.join(QLatin1Char('|'))));
        QVERIFY(folders.contains(QStringLiteral("Cours de mécanique (2)")));
        QVERIFY(QFileInfo::exists(libDir.filePath(QStringLiteral("Cours de mécanique (2)/Cours de mécanique (2).md"))));

        // Chapitres tirés du sommaire PDF, liens vers StudyLab, commentaires
        const QString thermoDir = libDir.filePath(QStringLiteral("thermo"));
        const QString anns = readAll(thermoDir + QStringLiteral("/Annotations.md"));
        QVERIFY2(anns.contains(QStringLiteral("## Chapitre 1 · Énergie")), qPrintable(anns));
        QVERIFY(anns.contains(QStringLiteral("## Chapitre 2 · L'entropie")));
        QVERIFY(anns.contains(QStringLiteral("[p. 3](studylab://open?doc=%1&annotation=%2)")
                                  .arg(collected.value().at(ids.indexOf(thermo)).doc.uuid, a2.uuid)));
        const QString coms = readAll(thermoDir + QStringLiteral("/Commentaires.md"));
        QVERIFY2(coms.contains(QStringLiteral("> [!question]")), qPrintable(coms));
        QVERIFY(coms.contains(QStringLiteral("Voir [[Cycle de Carnot]]")));
        QVERIFY(readAll(thermoDir + QStringLiteral("/thermo.md")).contains(QStringLiteral("(studylab://open?doc=")));
        const QString epubAnns = readAll(libDir.filePath(QStringLiteral("Vian - L'Écume des jours (2019)/Annotations.md")));
        QVERIFY2(epubAnns.contains(QStringLiteral("## II")), qPrintable(epubAnns));

        // README : index des documents en liens Obsidian
        const QString readme = readAll(r.folder + QStringLiteral("/README.md"));
        QVERIFY2(readme.contains(QStringLiteral("Exporté par StudyLab 0.1.0 : 4 documents, 3 annotations, 2 commentaires.")),
                 qPrintable(readme));
        QVERIFY(readme.contains(QStringLiteral("- [[Cours de mécanique (2)]] — 0 annotation")) ||
                readme.contains(QStringLiteral("- [[Cours de mécanique (2)]] — 0 annotations")));
        QVERIFY(readme.contains(QStringLiteral("- [[thermo]] — 2 annotations · 2 commentaires")));

        // CSV : BOM UTF-8, en-tête, une ligne par annotation, champs protégés
        QFile csvFile(r.folder + QStringLiteral("/annotations.csv"));
        QVERIFY(csvFile.open(QIODevice::ReadOnly));
        const QByteArray raw = csvFile.readAll();
        QVERIFY(raw.startsWith("\xEF\xBB\xBF"));
        const QString csv = QString::fromUtf8(raw.mid(3));
        const QStringList lines = csv.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 4);
        QVERIFY(lines.first().startsWith(QStringLiteral("Document,Auteurs,Année,Type,Couleur,Page,Chapitre,Citation")));
        QVERIFY2(csv.contains(QStringLiteral("\"L'entropie, dit-on, « croît »\"")), qPrintable(csv));
        QVERIFY(csv.contains(QStringLiteral(",Chapitre 2 · L'entropie,")));
        QVERIFY(csv.contains(QStringLiteral("Voir [[Cycle de Carnot]]")));  // note de l'annotation
        QVERIFY(csv.contains(QStringLiteral("L'Écume des jours,Boris Vian,2019,Surlignage,Vert,")));
        QVERIFY(csv.contains(a1.uuid));
        csvFile.close();

        // Un second export du même jour remplace le premier
        auto again = LibraryExporter::write(out.path(), collected.value(), opt);
        QVERIFY(again.ok());
        QCOMPARE(again.value().documents, 4);

        // Annulation après le premier document : l'index ne liste que ce qui est écrit
        QTemporaryDir out2;
        auto cancelled = LibraryExporter::write(out2.path(), collected.value(), opt, [](int, int) { return false; });
        QVERIFY(cancelled.ok());
        QVERIFY(cancelled.value().cancelled);
        QCOMPARE(cancelled.value().documents, 1);
        QVERIFY(readAll(cancelled.value().folder + QStringLiteral("/README.md")).contains(QStringLiteral("1 document,")));
    }
};

QTEST_MAIN(TstExportLibrary)
#include "tst_export_library.moc"
