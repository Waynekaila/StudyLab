#include "data/migrations/Migrator.h"
#include "data/repositories/AnnotationRepository.h"
#include "data/repositories/CommentRepository.h"
#include "data/repositories/DocumentRepository.h"
#include "data/sqlite/Database.h"
#include "services/annotations/AnnotationService.h"
#include "services/comments/CommentService.h"
#include "services/export/MarkdownExporter.h"
#include "services/search/SearchService.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;
using namespace studylab::db;

/// Commentaires (dépôt, service, export) et recherche FTS minimale.
class TstComments : public QObject {
    Q_OBJECT

    std::unique_ptr<Database> m_db;
    DocumentRecord m_doc;

    AnnotationRecord annotate(int page, double y, const QString& text, AnnotationColor color = AnnotationColor::Yellow)
    {
        PdfLocator l;
        l.page = page;
        l.rects = {QRectF(0.1, y, 0.8, 0.02)};
        l.exact = text;
        AnnotationRecord r;
        r.documentId = m_doc.id;
        r.color = color;
        r.locator = l.toJson();
        r.selectedText = text;
        AnnotationService service(*m_db);
        return service.create(r).value();
    }

    static CommentRecord pageComment(qint64 doc, int page, const QString& body, CommentKind kind = CommentKind::Reflection)
    {
        PdfLocator l;
        l.page = page;
        CommentRecord c;
        c.documentId = doc;
        c.anchor = CommentAnchor::Page;
        c.unitIndex = page;
        c.locator = l.toJson();
        c.kind = kind;
        c.body = body;
        return c;
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
        m_doc.authors = {QStringLiteral("Yunus A. Çengel")};
        m_doc.year = 2019;
        m_doc.tags = {QStringLiteral("thermo")};
        QVERIFY(docs.insert(m_doc).ok());
    }

    void kindsAnchorsAndLinks()
    {
        for (CommentKind k : {CommentKind::Reflection, CommentKind::Question, CommentKind::Explanation,
                              CommentKind::Reference, CommentKind::Summary}) {
            QCOMPARE(commentKindFromKey(commentKindKey(k)), std::optional(k));
            QVERIFY(!commentKindLabel(k).isEmpty());
        }
        QCOMPARE(commentKindLabel(CommentKind::Question), QStringLiteral("Question"));
        QCOMPARE(editableCommentKinds().size(), 4);
        QVERIFY(!editableCommentKinds().contains(CommentKind::Summary));
        for (CommentAnchor a : {CommentAnchor::Free, CommentAnchor::Annotation, CommentAnchor::Passage, CommentAnchor::Page})
            QCOMPARE(commentAnchorFromKey(commentAnchorKey(a)), std::optional(a));
        QVERIFY(!commentKindFromKey(QStringLiteral("inconnu")));

        QCOMPARE(wikiLinkTargets(QStringLiteral("Voir [[Réponse fréquentielle]] et [[Série de Fourier|Fourier]], "
                                                "encore [[Réponse fréquentielle]] ; pas [lien](x) ni [[]].")),
                 QStringList({QStringLiteral("Réponse fréquentielle"), QStringLiteral("Série de Fourier")}));
        QVERIFY(wikiLinkTargets(QStringLiteral("$[[a]]$ [[x\ny]]")).contains(QStringLiteral("a")));

        CommentRecord c;
        QCOMPARE(commentLocation(c), QString());
        QCOMPARE(commentSortKey(c), -1.0);
        c = pageComment(1, 11, QStringLiteral("x"));
        QCOMPARE(commentLocation(c), QStringLiteral("p. 12"));
        c.pageLabel = QStringLiteral("xii");
        QCOMPARE(commentLocation(c), QStringLiteral("p. xii"));
        QCOMPARE(commentSortKey(c), 11.0);
        EpubLocator e;
        e.cfi = QStringLiteral("epubcfi(/6/4!/4/2/1:0)");
        e.section = 3;
        e.fraction = 0.5;
        e.progress = 0.42;
        c.locator = e.toJson();
        c.unitIndex = 3;
        QCOMPARE(commentLocation(c), QStringLiteral("42 %"));
        QCOMPARE(commentSortKey(c), 3.5);
    }

    void createListOrderAndValidation()
    {
        CommentService service(*m_db);
        QSignalSpy added(&service, &CommentService::commentAdded);

        CommentRecord free;
        free.documentId = m_doc.id;
        free.body = QStringLiteral("  Plan du livre : chapitres 6 et 7 d'abord.  ");
        auto f = service.create(free);
        QVERIFY2(f.ok(), f.ok() ? "" : qPrintable(f.error().message));
        QCOMPARE(f.value().body, QStringLiteral("Plan du livre : chapitres 6 et 7 d'abord."));
        QCOMPARE(f.value().unitIndex, -1);
        QVERIFY(!f.value().uuid.isEmpty());

        auto p20 = service.create(pageComment(m_doc.id, 20, QStringLiteral("Page 21"), CommentKind::Question));
        QVERIFY(p20.ok());
        auto p3 = service.create(pageComment(m_doc.id, 3, QStringLiteral("Page 4")));
        QVERIFY(p3.ok());

        PdfLocator passage;
        passage.page = 3;
        passage.rects = {QRectF(0.1, 0.8, 0.5, 0.02)};
        passage.exact = QStringLiteral("l'entropie d'un système isolé");
        CommentRecord p;
        p.documentId = m_doc.id;
        p.anchor = CommentAnchor::Passage;
        p.unitIndex = 3;
        p.locator = passage.toJson();
        p.kind = CommentKind::Explanation;
        p.body = QStringLiteral("Car $\\delta Q = 0$.");
        auto pass = service.create(p);
        QVERIFY(pass.ok());
        QCOMPARE(pass.value().quote, passage.exact);
        QCOMPARE(added.count(), 4);

        const auto list = service.list(m_doc.id).value();
        QCOMPARE(list.size(), 4);
        QCOMPARE(list[0].id, f.value().id);     // libre en tête
        QCOMPARE(list[1].id, p3.value().id);    // page 4, haut de page
        QCOMPARE(list[2].id, pass.value().id);  // page 4, bas de page
        QCOMPARE(list[3].id, p20.value().id);
        QCOMPARE(service.count(m_doc.id).value(), 4);

        // Validation
        CommentRecord empty;
        empty.documentId = m_doc.id;
        empty.body = QStringLiteral("   ");
        QCOMPARE(service.create(empty).error().code, Error::Code::InvalidArgument);
        CommentRecord noAnchor = pageComment(m_doc.id, 1, QStringLiteral("x"));
        noAnchor.locator = {};
        QCOMPARE(service.create(noAnchor).error().code, Error::Code::InvalidArgument);
        CommentRecord noDoc;
        noDoc.body = QStringLiteral("x");
        QVERIFY(!service.create(noDoc).ok());
    }

    void updateResolveRemoveRestore()
    {
        CommentService service(*m_db);
        QSignalSpy changed(&service, &CommentService::commentChanged);
        QSignalSpy removed(&service, &CommentService::commentRemoved);
        auto q = service.create(pageComment(m_doc.id, 1, QStringLiteral("Pourquoi ?"), CommentKind::Question)).value();
        QVERIFY(!q.resolved);
        auto r = service.setResolved(q.id, true);
        QVERIFY(r.ok());
        QVERIFY(r.value().resolved);
        auto u = service.update(q.id, CommentKind::Question, QStringLiteral("Pourquoi **donc** ?"));
        QCOMPARE(u.value().body, QStringLiteral("Pourquoi **donc** ?"));
        QVERIFY(u.value().resolved);  // une question reste résolue si elle est seulement reformulée
        u = service.update(q.id, CommentKind::Reflection, QStringLiteral("Réflexion"));
        QVERIFY(!u.value().resolved);
        QCOMPARE(u.value().kind, CommentKind::Reflection);
        QCOMPARE(changed.count(), 3);
        QVERIFY(!service.update(q.id, CommentKind::Reflection, QStringLiteral(" ")).ok());

        QVERIFY(service.remove(q.id).ok());
        QCOMPARE(removed.count(), 1);
        QCOMPARE(removed.first().at(1).toLongLong(), m_doc.id);
        QCOMPARE(service.list(m_doc.id).value().size(), 0);
        QVERIFY(!service.remove(q.id).ok());
        QVERIFY(service.restore(q.id).ok());
        QCOMPARE(service.list(m_doc.id).value().size(), 1);
        QVERIFY(!service.restore(q.id).ok());
    }

    void annotationCommentIsItsNote()
    {
        CommentService comments(*m_db);
        AnnotationService annotations(*m_db);
        const AnnotationRecord a = annotate(4, 0.3, QStringLiteral("production d'entropie"), AnnotationColor::Blue);

        CommentRecord c;
        c.documentId = m_doc.id;
        c.anchor = CommentAnchor::Annotation;
        c.annotationId = a.id;
        c.body = QStringLiteral("Même idée qu'en vibrations : voir [[Réponse fréquentielle]].");
        auto created = comments.create(c);
        QVERIFY2(created.ok(), created.ok() ? "" : qPrintable(created.error().message));
        QCOMPARE(created.value().unitIndex, 4);
        QCOMPARE(created.value().quote, a.selectedText);
        QCOMPARE(created.value().annotationColor, AnnotationColor::Blue);
        QCOMPARE(created.value().sortKey, a.sortKey);
        QCOMPARE(commentLocation(created.value()), QStringLiteral("p. 5"));

        // La note de l'annotation est ce commentaire, dans les deux sens.
        QCOMPARE(annotations.find(a.id).value().note, c.body);
        QVERIFY(annotations.setNote(a.id, QStringLiteral("Modifié depuis la barre flottante")).ok());
        QCOMPARE(comments.find(created.value().id).value().body, QStringLiteral("Modifié depuis la barre flottante"));
        QCOMPARE(comments.forAnnotation(a.id).value()->id, created.value().id);

        // Un seul commentaire par annotation
        QCOMPARE(comments.create(c).error().code, Error::Code::InvalidArgument);

        // Annotation supprimée : son commentaire disparaît du volet, et revient avec elle.
        QVERIFY(annotations.remove(a.id).ok());
        QCOMPARE(comments.list(m_doc.id).value().size(), 0);
        QCOMPARE(comments.count(m_doc.id).value(), 0);
        QVERIFY(annotations.restore(a.id).ok());
        QCOMPARE(comments.list(m_doc.id).value().size(), 1);

        // Annotation d'un autre document ou supprimée : refusée
        CommentRecord other = c;
        other.annotationId = 9999;
        QVERIFY(!comments.create(other).ok());

        // Rétablir un ancien commentaire remplacé entre-temps est refusé (un seul par annotation)
        QVERIFY(comments.remove(created.value().id).ok());
        CommentRecord again = c;
        again.body = QStringLiteral("Nouveau");
        QVERIFY(comments.create(again).ok());
        QVERIFY(!comments.restore(created.value().id).ok());
    }

    void exportWritesCommentsNote()
    {
        CommentService service(*m_db);
        AnnotationService annotations(*m_db);
        const AnnotationRecord a = annotate(4, 0.3, QStringLiteral("production d'entropie"), AnnotationColor::Blue);
        CommentRecord linked;
        linked.documentId = m_doc.id;
        linked.anchor = CommentAnchor::Annotation;
        linked.annotationId = a.id;
        linked.kind = CommentKind::Explanation;
        linked.body = QStringLiteral("Mesure des irréversibilités.\n\n$$S_{gen} \\geq 0$$");
        QVERIFY(service.create(linked).ok());
        CommentRecord free;
        free.documentId = m_doc.id;
        free.kind = CommentKind::Question;
        free.body = QStringLiteral("Lire [[Clausius]] avant l'examen ?");
        QVERIFY(service.create(free).ok());

        MarkdownExporter::Options opt;
        opt.date = QDate(2026, 11, 19);
        opt.chapterOf = [](int page) { return page >= 3 ? QStringLiteral("Chapitre 7 · Entropie") : QString(); };
        opt.comments = service.list(m_doc.id).value();
        const QString md = MarkdownExporter::commentsNote(m_doc, opt.comments, opt);
        QVERIFY(md.startsWith(QStringLiteral("---\ntype: comments\n")));
        QVERIFY(md.contains(QStringLiteral("document: \"[[Çengel - Thermodynamique une approche pragmatique (2019)]]\"")));
        QVERIFY(md.contains(QStringLiteral("# Commentaires — Thermodynamique : une approche pragmatique")));
        QVERIFY(md.contains(QStringLiteral("2 commentaires")));
        const int general = int(md.indexOf(QStringLiteral("## Commentaires généraux")));
        const int chapter = int(md.indexOf(QStringLiteral("## Chapitre 7 · Entropie")));
        QVERIFY(general > 0 && chapter > general);
        QVERIFY(md.contains(QStringLiteral("> [!question] Question · ouverte\n> Lire [[Clausius]] avant l'examen ?\n^com-")));
        QVERIFY(md.contains(QStringLiteral("> [!info] Explication · p. 5\n> *« production d'entropie »*\n>\n"
                                           "> Mesure des irréversibilités.\n>\n> $$S_{gen} \\geq 0$$\n^com-")));

        // Le document renvoie vers Commentaires.md ; metadata.json liste les commentaires et leurs liens.
        const QString note = MarkdownExporter::documentNote(m_doc, annotations.list(m_doc.id).value(), opt);
        QVERIFY(note.contains(QStringLiteral("2 commentaires — voir [[Çengel - Thermodynamique une approche pragmatique (2019)/Commentaires|Commentaires]].")));
        const QJsonObject meta = MarkdownExporter::metadata(m_doc, annotations.list(m_doc.id).value(), opt);
        const QJsonArray coms = meta.value(QStringLiteral("comments")).toArray();
        QCOMPARE(coms.size(), 2);
        QCOMPARE(coms.at(0).toObject().value(QStringLiteral("links")).toArray().at(0).toString(), QStringLiteral("Clausius"));

        QTemporaryDir out;
        auto folder = MarkdownExporter::exportDocument(out.path(), m_doc, annotations.list(m_doc.id).value(), opt);
        QVERIFY(folder.ok());
        QVERIFY(QFile::exists(folder.value() + QStringLiteral("/Commentaires.md")));
        // Sans commentaire, un ancien Commentaires.md est retiré.
        opt.comments.clear();
        QVERIFY(MarkdownExporter::exportDocument(out.path(), m_doc, annotations.list(m_doc.id).value(), opt).ok());
        QVERIFY(!QFile::exists(folder.value() + QStringLiteral("/Commentaires.md")));
    }

    void searchFindsMetadataAnnotationsAndComments()
    {
        CommentService comments(*m_db);
        AnnotationService annotations(*m_db);
        SearchService search(*m_db);
        QVERIFY(search.search(QStringLiteral("   ")).value().isEmpty());

        const AnnotationRecord a = annotate(4, 0.3, QStringLiteral("La production d'entropie mesure les irréversibilités"));
        annotate(5, 0.3, QStringLiteral("Autre passage sans rapport"));
        CommentRecord c = pageComment(m_doc.id, 6, QStringLiteral("Pourquoi l'**entropie** croît-elle ?"), CommentKind::Question);
        c.locator.insert(QStringLiteral("pageLabel"), QStringLiteral("312"));
        QVERIFY(comments.create(c).ok());

        // Accents, casse et préfixes ignorés ; tous les mots requis
        auto r = search.search(QStringLiteral("ENTRO"));
        QVERIFY(r.ok());
        QCOMPARE(r.value().annotations.size(), 1);
        QCOMPARE(r.value().comments.size(), 1);
        QCOMPARE(r.value().documents.size(), 0);
        const SearchHit& ann = r.value().annotations.first();
        QCOMPARE(ann.itemId, a.id);
        QCOMPARE(ann.documentId, m_doc.id);
        QCOMPARE(ann.location, QStringLiteral("p. 5"));
        QCOMPARE(ann.documentTitle, m_doc.title);
        QVERIFY(ann.snippet.contains(QChar(SearchHit::kMarkStart)));
        QCOMPARE(SearchService::plain(ann.snippet), QStringLiteral("La production d'entropie mesure les irréversibilités"));
        const SearchHit& com = r.value().comments.first();
        QCOMPARE(com.location, QStringLiteral("p. 312"));
        QCOMPARE(com.commentKind, QStringLiteral("Question"));

        QCOMPARE(search.search(QStringLiteral("irreversibilites")).value().annotations.size(), 1);
        QCOMPARE(search.search(QStringLiteral("entropie rapport")).value().annotations.size(), 0);

        // Métadonnées : titre, auteur, tags
        r = search.search(QStringLiteral("cengel"));
        QCOMPARE(r.value().documents.size(), 1);
        QCOMPARE(r.value().documents.first().snippet, QStringLiteral("Yunus A. Çengel"));
        QCOMPARE(search.search(QStringLiteral("thermo pragma")).value().documents.size(), 1);

        // Caractères spéciaux de FTS5 sans effet
        QVERIFY(search.search(QStringLiteral("\"entropie* AND (OR -")).ok());

        // Supprimés : exclus
        QVERIFY(annotations.remove(a.id).ok());
        QCOMPARE(search.search(QStringLiteral("entropie")).value().annotations.size(), 0);
        QVERIFY(comments.remove(comments.list(m_doc.id).value().first().id).ok());
        QCOMPARE(search.search(QStringLiteral("entropie")).value().comments.size(), 0);

        // Limite par groupe
        for (int i = 0; i < 12; ++i)
            QVERIFY(comments.create(pageComment(m_doc.id, i, QStringLiteral("Carnot %1").arg(i))).ok());
        QCOMPARE(search.search(QStringLiteral("carnot"), 5).value().comments.size(), 5);
    }
};

QTEST_GUILESS_MAIN(TstComments)
#include "tst_comments.moc"
