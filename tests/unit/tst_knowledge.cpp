#include "core/domain/Graph.h"
#include "data/migrations/Migrator.h"
#include "data/repositories/DocumentRepository.h"
#include "data/repositories/NoteRepository.h"
#include "data/sqlite/Database.h"
#include "services/knowledge/ForceLayout.h"
#include "services/knowledge/KnowledgeService.h"
#include "services/notes/NoteService.h"
#include "services/organize/OrganizeService.h"

#include <QLineF>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace studylab;
using namespace studylab::db;

/// V1-4 : graphe de connaissances (construction depuis la table links, filtres, voisinage, disposition).
class TstKnowledge : public QObject {
    Q_OBJECT

    std::unique_ptr<Database> m_db;
    QTemporaryDir m_dir;

    qint64 addDocument(const QString& title, const QStringList& authors = {})
    {
        DocumentRepository docs(*m_db);
        DocumentRecord d;
        d.title = title;
        d.authors = authors;
        d.fileHash = title;
        d.filePath = title + QStringLiteral(".pdf");
        if (!docs.insert(d))
            return 0;
        return d.id;
    }

    void addAnnotation(qint64 documentId, const QString& type = QStringLiteral("highlight"))
    {
        static int n = 0;
        QVERIFY(m_db->exec(QStringLiteral(
            "INSERT INTO annotations(uuid, document_id, type, unit_index, locator_json, sort_key, doc_hash, created_at, "
            "updated_at) VALUES ('a%1', %2, '%3', 0, '{}', 0, 'h', '2026-01-01', '2026-01-01')")
            .arg(++n).arg(documentId).arg(type)).ok());
    }

    static KnowledgeGraph chain(int n)
    {
        KnowledgeGraph g;
        for (int i = 0; i < n; ++i) {
            GraphNode node;
            node.key = QStringLiteral("n:%1").arg(i);
            node.title = QString::number(i);
            g.nodes << node;
        }
        for (int i = 0; i + 1 < n; ++i)
            g.edges << GraphEdge{i, i + 1, false};
        return g;
    }

private slots:
    void init()
    {
        m_db = std::move(Database::openInMemory().value());
        QVERIFY(Migrator(Migrator::loadFrom().value()).migrate(*m_db).ok());
    }

    void excerpt()
    {
        const QString md = QStringLiteral(
            "---\ntags: [thermo]\n---\n# Second principe\n\nL'**entropie** est une [[Fonction d'état|fonction d'état]]."
            "\n\n- Voir [Çengel](https://example.org) et `dS`.\n\n```\ncode\n```\n> Citation");
        QCOMPARE(graphExcerpt(md), QStringLiteral(
            "L'entropie est une fonction d'état. Voir Çengel et dS. Citation"));
        const QString longText = QString(QStringLiteral("mot ")).repeated(100);
        const QString cut = graphExcerpt(longText, 40);
        QVERIFY(cut.endsWith(QStringLiteral("…")));
        QVERIFY(cut.size() <= 41);
    }

    void buildFromDatabase()
    {
        NoteService notes(*m_db, m_dir.path());
        OrganizeService organize(*m_db);
        const qint64 cengel = addDocument(QStringLiteral("Thermodynamique"), {QStringLiteral("Yunus A. Çengel")});
        const qint64 trashed = addDocument(QStringLiteral("Supprimé"));
        addAnnotation(cengel);
        addAnnotation(cengel);
        addAnnotation(cengel, QStringLiteral("bookmark"));
        QVERIFY(DocumentRepository(*m_db).remove(trashed).ok());

        const qint64 entropie = notes.create(QStringLiteral("Entropie"), NoteType::Concept).value().id;
        const qint64 principe = notes.create(QStringLiteral("Second principe"), NoteType::Course, {},
                                             QStringLiteral("Voir [[Entropie]], [[Thermodynamique]] et [[Exergie]].\n"
                                                            "Encore [[entropie]]."))
                                    .value().id;
        QVERIFY(NoteRepository(*m_db).addSources(principe, {{QStringLiteral("document"), cengel}}).ok());

        const qint64 thermo = organize.createSubject(QStringLiteral("Thermodynamique")).value().id;
        const qint64 archived = organize.createSubject(QStringLiteral("Ancienne")).value().id;
        QVERIFY(organize.setSubjectArchived(archived, true).ok());
        QVERIFY(organize.attach(archived, OrganizeService::Entity::Note, principe).ok());
        QVERIFY(organize.attach(thermo, OrganizeService::Entity::Note, principe).ok());
        QVERIFY(organize.attach(thermo, OrganizeService::Entity::Document, cengel).ok());

        KnowledgeService knowledge(*m_db);
        const KnowledgeGraph g = knowledge.graph().value();
        QCOMPARE(g.nodes.size(), 4);  // document, 2 notes, lien non résolu « Exergie »
        QCOMPARE(g.indexOf(QStringLiteral("d:%1").arg(trashed)), -1);

        const GraphNode& doc = g.nodes.at(g.indexOf(QStringLiteral("d:%1").arg(cengel)));
        QCOMPARE(doc.kind, GraphNodeKind::Document);
        QCOMPARE(doc.detail, QStringLiteral("Yunus A. Çengel"));
        QCOMPARE(doc.annotations, 2);
        QCOMPARE(doc.backlinks, 1);
        QCOMPARE(doc.sources, 1);
        QCOMPARE(doc.subjects, QList<qint64>{thermo});

        const GraphNode& conceptNode = g.nodes.at(g.indexOf(QStringLiteral("n:%1").arg(entropie)));
        QCOMPARE(conceptNode.kind, GraphNodeKind::Concept);
        QCOMPARE(conceptNode.backlinks, 1);  // deux liens, une seule arête
        QVERIFY(conceptNode.color.isEmpty());

        const GraphNode& note = g.nodes.at(g.indexOf(QStringLiteral("n:%1").arg(principe)));
        QCOMPARE(note.kind, GraphNodeKind::Note);
        QCOMPARE(note.outLinks, 3);  // cibles distinctes
        QCOMPARE(note.sources, 1);
        QCOMPARE(note.subjects, QList<qint64>{thermo});  // matière archivée ignorée
        QVERIFY(!note.color.isEmpty());
        QCOMPARE(note.excerpt, QStringLiteral("Voir Entropie, Thermodynamique et Exergie. Encore entropie."));

        const int unresolved = g.indexOf(QStringLiteral("u:exergie"));
        QVERIFY(unresolved >= 0);
        QCOMPARE(g.nodes.at(unresolved).kind, GraphNodeKind::Unresolved);
        QCOMPARE(g.nodes.at(unresolved).title, QStringLiteral("Exergie"));

        // Arêtes : note → conceptNode, note → document ([[…]] et provenance confondues), note → non résolu
        QCOMPARE(g.edges.size(), 3);

        // Note supprimée : ses liens disparaissent avec elle.
        QVERIFY(notes.remove(principe).ok());
        const KnowledgeGraph after = knowledge.graph().value();
        QCOMPARE(after.nodes.size(), 2);
        QCOMPARE(after.edges.size(), 0);
        QCOMPARE(after.nodes.at(after.indexOf(QStringLiteral("d:%1").arg(cengel))).backlinks, 0);
    }

    void filterAndNeighborhood()
    {
        KnowledgeGraph g;
        const auto node = [&g](GraphNodeKind kind, const QString& key, QList<qint64> subjects = {}) {
            GraphNode n;
            n.kind = kind;
            n.key = key;
            n.subjects = subjects;
            g.nodes << n;
            return int(g.nodes.size()) - 1;
        };
        const int doc = node(GraphNodeKind::Document, QStringLiteral("d:1"), {10});
        const int a = node(GraphNodeKind::Note, QStringLiteral("n:1"), {10});
        const int b = node(GraphNodeKind::Note, QStringLiteral("n:2"), {20});
        const int c = node(GraphNodeKind::Concept, QStringLiteral("n:3"));
        const int u = node(GraphNodeKind::Unresolved, QStringLiteral("u:x"));
        const int lone = node(GraphNodeKind::Unresolved, QStringLiteral("u:y"));
        g.edges = {{a, doc, true}, {a, b, false}, {b, c, false}, {c, u, false}, {b, lone, false}};

        GraphFilter f;
        KnowledgeGraph shown = filterGraph(g, f);
        QCOMPARE(shown.nodes.size(), 4);  // liens non résolus masqués par défaut
        QCOMPARE(shown.edges.size(), 3);

        f.unresolved = true;
        shown = filterGraph(g, f);
        QVERIFY(shown.indexOf(QStringLiteral("u:x")) >= 0);
        QVERIFY(shown.indexOf(QStringLiteral("u:y")) >= 0);

        f.concepts = false;
        shown = filterGraph(g, f);
        QCOMPARE(shown.indexOf(QStringLiteral("u:x")), -1);  // le concept qui le cite n'est plus affiché
        QVERIFY(shown.indexOf(QStringLiteral("u:y")) >= 0);

        GraphFilter bySubject;
        bySubject.filterSubjects = true;
        bySubject.subjects = {10};
        shown = filterGraph(g, bySubject);
        QCOMPARE(shown.nodes.size(), 3);  // d:1, n:1 et le conceptNode sans matière
        bySubject.unfiled = false;
        QCOMPARE(filterGraph(g, bySubject).nodes.size(), 2);

        QCOMPARE(graphNeighborhood(g, doc, 1), (QSet<int>{doc, a}));
        QCOMPARE(graphNeighborhood(g, doc, 2), (QSet<int>{doc, a, b}));
        QCOMPARE(graphNeighborhood(g, doc, -1).size(), 6);
        QCOMPARE(graphNeighborhood(g, 99, 1).size(), 0);

        const KnowledgeGraph sub = subgraph(g, {a, b, c});
        QCOMPARE(sub.nodes.size(), 3);
        QCOMPARE(sub.edges.size(), 2);
        QCOMPARE(sub.nodes.at(sub.edges.at(0).from).key, QStringLiteral("n:1"));
    }

    void layout()
    {
        const KnowledgeGraph g = chain(12);
        LayoutOptions o;
        const QList<QPointF> p1 = forceLayout(g, o);
        const QList<QPointF> p2 = forceLayout(g, o);
        QCOMPARE(p1.size(), 12);
        QCOMPARE(p1, p2);  // reproductible
        double neighbours = 0, all = 0;
        int pairs = 0;
        for (int i = 0; i < p1.size(); ++i) {
            QVERIFY(std::isfinite(p1.at(i).x()) && std::isfinite(p1.at(i).y()));
            for (int j = i + 1; j < p1.size(); ++j) {
                all += QLineF(p1.at(i), p1.at(j)).length();
                ++pairs;
            }
        }
        for (const GraphEdge& e : g.edges)
            neighbours += QLineF(p1.at(e.from), p1.at(e.to)).length();
        QVERIFY(neighbours / g.edges.size() < all / pairs);
        QVERIFY(neighbours / g.edges.size() > 20);  // pas d'effondrement

        // Positions connues : le calcul en part (nœud isolé laissé à peu près en place).
        KnowledgeGraph single;
        single.nodes << GraphNode{};
        single.nodes[0].key = QStringLiteral("n:1");
        o.initial.insert(QStringLiteral("n:1"), QPointF(500, 500));
        QCOMPARE(forceLayout(single, o).at(0), QPointF(0, 0));  // centré

        // Annulation : rend tout de même une position par nœud.
        std::atomic_bool cancel{true};
        QCOMPARE(forceLayout(g, LayoutOptions{}, &cancel).size(), 12);
        QVERIFY(forceLayout(KnowledgeGraph{}, LayoutOptions{}).isEmpty());
    }

    void layoutGroupsBySubject()
    {
        // Deux matières sans lien entre elles : regroupées, chaque matière forme un amas séparé.
        KnowledgeGraph g;
        for (int i = 0; i < 16; ++i) {
            GraphNode n;
            n.key = QStringLiteral("n:%1").arg(i);
            n.subjects = {i % 2 == 0 ? 1 : 2};
            g.nodes << n;
        }
        LayoutOptions o;
        o.groupBySubject = true;
        const QList<QPointF> p = forceLayout(g, o);
        QPointF c1, c2;
        for (int i = 0; i < p.size(); ++i)
            (i % 2 == 0 ? c1 : c2) += p.at(i) / 8.0;
        double spread = 0;
        for (int i = 0; i < p.size(); ++i)
            spread += QLineF(p.at(i), i % 2 == 0 ? c1 : c2).length() / 16.0;
        QVERIFY2(QLineF(c1, c2).length() > spread, qPrintable(QStringLiteral("%1 / %2")
                                                                .arg(QLineF(c1, c2).length()).arg(spread)));
    }
};

QTEST_GUILESS_MAIN(TstKnowledge)
#include "tst_knowledge.moc"
