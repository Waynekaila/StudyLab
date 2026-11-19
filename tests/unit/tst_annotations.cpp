#include "data/migrations/Migrator.h"
#include "data/repositories/AnnotationRepository.h"
#include "data/repositories/DocumentRepository.h"
#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/export/MarkdownExporter.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

class TstAnnotations : public QObject {
    Q_OBJECT

    std::unique_ptr<Database> m_db;
    DocumentRecord m_doc;

    static PdfLocator locator(int page, double y, const QString& text = {})
    {
        PdfLocator l;
        l.page = page;
        l.rects = {QRectF(0.1, y, 0.8, 0.02), QRectF(0.1, y + 0.025, 0.5, 0.02)};
        l.exact = text;
        l.prefix = QStringLiteral("avant ");
        l.suffix = QStringLiteral(" après");
        l.start = 10;
        l.end = 10 + int(text.size());
        return l;
    }

    static AnnotationRecord make(qint64 docId, int page, double y, const QString& text,
                                 AnnotationType type = AnnotationType::Highlight,
                                 AnnotationColor color = AnnotationColor::Yellow)
    {
        AnnotationRecord r;
        r.documentId = docId;
        r.type = type;
        r.color = color;
        r.locator = locator(page, y, text).toJson();
        r.selectedText = text;
        return r;
    }

private slots:
    void init()
    {
        m_db = std::move(Database::openInMemory().value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*m_db).ok());
        DocumentRepository docs(*m_db);
        m_doc = {};
        m_doc.title = QStringLiteral("Thermodynamique : une approche pragmatique");
        m_doc.fileHash = QStringLiteral("abc123");
        m_doc.filePath = QStringLiteral("abc123.pdf");
        m_doc.pageCount = 980;
        m_doc.authors = {QStringLiteral("Yunus A. Çengel"), QStringLiteral("Michael A. Boles")};
        m_doc.year = 2019;
        m_doc.tags = {QStringLiteral("thermo"), QStringLiteral("examen S5")};
        QVERIFY(docs.insert(m_doc).ok());
    }

    void locatorRoundTrip()
    {
        const PdfLocator l = locator(41, 0.304, QStringLiteral("L'entropie d'un système isolé"));
        const QJsonObject json = l.toJson();
        QCOMPARE(json.value(QStringLiteral("kind")).toString(), QStringLiteral("pdf"));
        QCOMPARE(json.value(QStringLiteral("rects")).toArray().size(), 2);
        const auto back = PdfLocator::fromJson(json);
        QVERIFY(back.has_value());
        QCOMPARE(back->page, 41);
        QCOMPARE(back->rects.size(), 2);
        QVERIFY(qAbs(back->rects[0].top() - 0.304) < 1e-4);
        QCOMPARE(back->exact, l.exact);
        QCOMPARE(back->prefix, l.prefix);
        QCOMPARE(back->start, 10);
        QVERIFY(qAbs(back->relativeY() - 0.304) < 1e-4);
        QVERIFY(!PdfLocator::fromJson(QJsonObject{{QStringLiteral("kind"), QStringLiteral("epub")}}).has_value());
        QCOMPARE(normalizeQuote(QStringLiteral("irréversi-\nbilités  du\nsystème")), QStringLiteral("irréversibilités du système"));
        QCOMPARE(annotationTypeFromKey(QStringLiteral("strikeout")), std::optional(AnnotationType::Strikeout));
        QCOMPARE(annotationColorKey(AnnotationColor::Purple), QStringLiteral("purple"));
    }

    void repositoryCrudAndNotes()
    {
        AnnotationRepository repo(*m_db);
        auto a = make(m_doc.id, 3, 0.5, QStringLiteral("deuxième"));
        a.docHash = m_doc.fileHash;
        a.sortKey = 3.5;
        a.unitIndex = 3;
        a.note = QStringLiteral("À revoir");
        QVERIFY(repo.insert(a).ok());
        auto b = make(m_doc.id, 1, 0.2, QStringLiteral("premier"));
        b.docHash = m_doc.fileHash;
        b.sortKey = 1.2;
        b.unitIndex = 1;
        QVERIFY(repo.insert(b).ok());

        auto list = repo.listForDocument(m_doc.id);
        QVERIFY(list.ok());
        QCOMPARE(list.value().size(), 2);
        QCOMPARE(list.value()[0].selectedText, QStringLiteral("premier"));  // ordre de lecture
        QCOMPARE(list.value()[1].note, QStringLiteral("À revoir"));
        QVERIFY(!list.value()[1].uuid.isEmpty());

        // Note : modification, puis suppression
        QVERIFY(repo.setNote(b.id, QStringLiteral("  Pourquoi ?  ")).ok());
        QCOMPARE(repo.findById(b.id).value().note, QStringLiteral("Pourquoi ?"));
        QVERIFY(repo.setNote(b.id, QStringLiteral("Pourquoi donc ?")).ok());
        QCOMPARE(repo.findById(b.id).value().note, QStringLiteral("Pourquoi donc ?"));
        QCOMPARE(m_db->queryInt(QStringLiteral("SELECT count(*) FROM comments WHERE annotation_id = %1").arg(b.id)).value(), 1);
        QVERIFY(repo.setNote(b.id, QString()).ok());
        QVERIFY(repo.findById(b.id).value().note.isEmpty());

        // Modification de la couleur et du type
        AnnotationRecord changed = repo.findById(a.id).value();
        changed.color = AnnotationColor::Green;
        changed.type = AnnotationType::Underline;
        QVERIFY(repo.update(changed).ok());
        QCOMPARE(repo.findById(a.id).value().color, AnnotationColor::Green);
        QCOMPARE(repo.findById(a.id).value().type, AnnotationType::Underline);

        // Retrait logique puis restauration
        QVERIFY(repo.remove(a.id).ok());
        QCOMPARE(repo.countForDocument(m_doc.id).value(), 1);
        QVERIFY(!repo.remove(a.id).ok());
        QVERIFY(repo.restore(a.id).ok());
        QCOMPARE(repo.countForDocument(m_doc.id).value(), 2);

        // Index plein texte alimenté par les déclencheurs
        QCOMPARE(m_db->queryInt(QStringLiteral(
                     "SELECT count(*) FROM fts_annotations WHERE fts_annotations MATCH 'deuxieme'")).value(), 1);

        // Contrôles
        AnnotationRecord invalid;
        QVERIFY(!repo.insert(invalid).ok());
    }

    void serviceSignalsAndBookmarks()
    {
        AnnotationService service(*m_db);
        QSignalSpy added(&service, &AnnotationService::annotationAdded);
        QSignalSpy changed(&service, &AnnotationService::annotationChanged);
        QSignalSpy removed(&service, &AnnotationService::annotationRemoved);

        auto created = service.create(make(m_doc.id, 4, 0.25, QStringLiteral("irréversi-\nbilités")));
        QVERIFY(created.ok());
        QCOMPARE(added.count(), 1);
        const AnnotationRecord a = created.value();
        QCOMPARE(a.docHash, m_doc.fileHash);    // empreinte renseignée par le service
        QCOMPARE(a.unitIndex, 4);
        QVERIFY(qAbs(a.sortKey - 4.25) < 1e-6);
        QCOMPARE(a.selectedText, QStringLiteral("irréversibilités"));

        QVERIFY(service.setColor(a.id, AnnotationColor::Pink).ok());
        QVERIFY(service.setNote(a.id, QStringLiteral("Lister les sources")).ok());
        QCOMPARE(changed.count(), 2);
        QCOMPARE(changed.last().at(0).value<AnnotationRecord>().note, QStringLiteral("Lister les sources"));

        QVERIFY(service.remove(a.id).ok());
        QCOMPARE(removed.count(), 1);
        QCOMPARE(removed.first().at(1).toLongLong(), m_doc.id);
        QVERIFY(service.restore(a.id).ok());
        QCOMPARE(added.count(), 2);
        QCOMPARE(service.find(a.id).value().color, AnnotationColor::Pink);

        // Signet : ajout puis retrait par la même commande
        auto first = service.toggleBookmark(m_doc.id, 7, QStringLiteral("viii"));
        QVERIFY(first.ok() && first.value().has_value());
        QVERIFY(service.hasBookmark(m_doc.id, 7).value());
        auto second = service.toggleBookmark(m_doc.id, 7, QString());
        QVERIFY(second.ok() && !second.value().has_value());
        QVERIFY(!service.hasBookmark(m_doc.id, 7).value());

        AnnotationRecord orphan = make(999, 1, 0.1, QStringLiteral("x"));
        QVERIFY(!service.create(orphan).ok());
    }

    void markdownExport()
    {
        AnnotationService service(*m_db);
        auto hl = make(m_doc.id, 411, 0.3, QStringLiteral("L'entropie d'un système isolé ne peut que croître"));
        hl.pageLabel = QStringLiteral("389");
        hl.note = QStringLiteral("Second principe.\nVoir [[Cycle de Carnot]] #examen");
        QVERIFY(service.create(hl).ok());
        QVERIFY(service.create(make(m_doc.id, 411, 0.6, QStringLiteral("Une transformation \"impossible\""),
                                    AnnotationType::Underline, AnnotationColor::Green)).ok());
        QVERIFY(service.create(make(m_doc.id, 20, 0.1, QString(), AnnotationType::Marker, AnnotationColor::Blue)).ok());
        auto note = make(m_doc.id, 30, 0.5, QString(), AnnotationType::Note);
        note.note = QStringLiteral("Question pour le TD");
        QVERIFY(service.create(note).ok());
        QVERIFY(service.toggleBookmark(m_doc.id, 5, QString()).ok());
        const auto anns = service.list(m_doc.id).value();
        QCOMPARE(anns.size(), 5);

        MarkdownExporter::Options opt;
        opt.date = QDate(2026, 11, 19);
        opt.chapterOf = [](int page) {
            return page >= 400 ? QStringLiteral("Chapitre 7 · L'entropie") : QString();
        };
        QCOMPARE(MarkdownExporter::documentBaseName(m_doc),
                 QStringLiteral("Çengel - Thermodynamique une approche pragmatique (2019)"));
        QCOMPARE(MarkdownExporter::safeFileName(QStringLiteral("a/b:c*?.  ")), QStringLiteral("a b c"));
        QCOMPARE(MarkdownExporter::safeFileName(QStringLiteral("CON")), QStringLiteral("Sans titre CON"));
        QCOMPARE(MarkdownExporter::yamlString(QStringLiteral("dit \"oui\"")), QStringLiteral("\"dit \\\"oui\\\"\""));

        const QString md = MarkdownExporter::annotationsNote(m_doc, anns, opt);
        QVERIFY2(md.startsWith(QStringLiteral("---\ntype: annotations\n")), qPrintable(md));
        QVERIFY(md.contains(QStringLiteral("## Signets\n\n- p. 6")));
        QVERIFY(md.contains(QStringLiteral("## Chapitre 7 · L'entropie")));
        QVERIFY(md.contains(QStringLiteral("## Page 21")));
        QVERIFY(md.contains(QStringLiteral("> [!quote] p. 389 · Surlignage · jaune\n> L'entropie d'un système isolé ne peut que croître\n^ann-")));
        QVERIFY(md.contains(QStringLiteral("Second principe.\nVoir [[Cycle de Carnot]] #examen")));
        QVERIFY(md.contains(QStringLiteral("> [!quote] p. 412 · Soulignement · vert")));
        QVERIFY(md.contains(QStringLiteral("> *Zone sans texte*")));
        QVERIFY(md.contains(QStringLiteral("> [!note] p. 31 · Note\n> Question pour le TD")));
        QVERIFY(md.contains(QStringLiteral("4 annotations")));
        // Ordre de lecture : page 21 avant le chapitre 7
        QVERIFY(md.indexOf(QStringLiteral("## Page 21")) < md.indexOf(QStringLiteral("## Chapitre 7")));
        // Un seul titre pour les deux annotations du chapitre 7
        QCOMPARE(md.count(QStringLiteral("## Chapitre 7")), 1);

        const QString fiche = MarkdownExporter::documentNote(m_doc, anns, opt);
        QVERIFY(fiche.contains(QStringLiteral("title: \"Thermodynamique : une approche pragmatique\"")));
        QVERIFY(fiche.contains(QStringLiteral("authors:\n  - \"Yunus A. Çengel\"\n  - \"Michael A. Boles\"")));
        QVERIFY(fiche.contains(QStringLiteral("  - \"examen-S5\"")));
        QVERIFY(fiche.contains(QStringLiteral("uuid: ") + m_doc.uuid));
        QVERIFY(fiche.contains(QStringLiteral("[[Çengel - Thermodynamique une approche pragmatique (2019)/Annotations|Annotations]]")));

        QTemporaryDir out;
        auto folder = MarkdownExporter::exportDocument(out.path(), m_doc, anns, opt);
        QVERIFY2(folder.ok(), folder.ok() ? "" : qPrintable(folder.error().message));
        const QDir dir(folder.value());
        QVERIFY(dir.exists(QStringLiteral("Annotations.md")));
        QVERIFY(dir.exists(MarkdownExporter::documentBaseName(m_doc) + QStringLiteral(".md")));
        QFile json(dir.filePath(QStringLiteral("metadata.json")));
        QVERIFY(json.open(QIODevice::ReadOnly));
        const QJsonObject meta = QJsonDocument::fromJson(json.readAll()).object();
        QCOMPARE(meta.value(QStringLiteral("annotations")).toArray().size(), 5);
        QCOMPARE(meta.value(QStringLiteral("document")).toObject().value(QStringLiteral("uuid")).toString(), m_doc.uuid);
        // Un second export remplace le premier sans erreur
        QVERIFY(MarkdownExporter::exportDocument(out.path(), m_doc, anns, opt).ok());
    }
};

QTEST_GUILESS_MAIN(TstAnnotations)
#include "tst_annotations.moc"
