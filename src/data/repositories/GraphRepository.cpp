#include "data/repositories/GraphRepository.h"

#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"

#include <QRegularExpression>
#include <QSet>

namespace studylab::db {

namespace {

const QChar kSep(31);

/// Parcourt toutes les lignes d'une requête sans paramètre.
template <typename F>
Result<void> forEachRow(Database& db, const QString& sql, F read)
{
    auto stmt = db.prepare(sql);
    if (!stmt)
        return stmt.error();
    while (true) {
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            return {};
        read(stmt.value());
    }
}

QString docKey(qint64 id) { return QStringLiteral("d:%1").arg(id); }
QString noteKey(qint64 id) { return QStringLiteral("n:%1").arg(id); }

}  // namespace

GraphRepository::GraphRepository(Database& db) : m_db(db) {}

Result<KnowledgeGraph> GraphRepository::load()
{
    KnowledgeGraph g;
    QHash<QString, int> index;
    const auto add = [&g, &index](GraphNode n) {
        index.insert(n.key, int(g.nodes.size()));
        g.nodes << std::move(n);
    };

    // Documents
    auto docs = forEachRow(m_db, QStringLiteral(
        "SELECT d.id, d.title, d.description, "
        "  (SELECT group_concat(a.name, char(31)) FROM (SELECT a.name FROM document_authors da "
        "     JOIN authors a ON a.id = da.author_id WHERE da.document_id = d.id ORDER BY da.position) a), "
        "  (SELECT COUNT(*) FROM annotations x WHERE x.document_id = d.id AND x.deleted_at IS NULL "
        "     AND x.type <> 'bookmark') "
        "FROM documents d WHERE d.deleted_at IS NULL ORDER BY d.sort_title, d.id"),
        [&add](const Statement& s) {
            GraphNode n;
            n.kind = GraphNodeKind::Document;
            n.id = s.columnInt64(0);
            n.key = docKey(n.id);
            n.title = s.columnText(1);
            n.excerpt = s.columnText(2).simplified();
            n.detail = s.columnText(3).split(kSep, Qt::SkipEmptyParts).join(QStringLiteral(", "));
            n.annotations = int(s.columnInt64(4));
            add(std::move(n));
        });
    if (!docs)
        return docs.error();

    // Notes (les notes de type « Concept » sont les concepts du graphe)
    auto notes = forEachRow(m_db, QStringLiteral(
        "SELECT id, title, note_type, body_md FROM notes WHERE deleted_at IS NULL ORDER BY title COLLATE NOCASE, id"),
        [&add](const Statement& s) {
            GraphNode n;
            n.id = s.columnInt64(0);
            n.key = noteKey(n.id);
            n.title = s.columnText(1);
            n.noteType = noteTypeFromKey(s.columnText(2)).value_or(NoteType::Personal);
            n.kind = n.noteType == NoteType::Concept ? GraphNodeKind::Concept : GraphNodeKind::Note;
            n.detail = noteTypeLabel(n.noteType);
            n.excerpt = graphExcerpt(s.columnText(3));
            add(std::move(n));
        });
    if (!notes)
        return notes.error();

    // Liens [[…]] des notes actives
    QSet<QPair<int, int>> seen;
    const auto edge = [&g, &seen](int from, int to, bool derived) {
        if (from < 0 || to < 0 || from == to || seen.contains({from, to}))
            return false;
        seen.insert({from, to});
        g.edges << GraphEdge{from, to, derived};
        return true;
    };
    auto wiki = forEachRow(m_db, QStringLiteral(
        "SELECT l.source_id, l.target_type, l.target_id, l.target_text FROM links l "
        "JOIN notes n ON n.id = l.source_id AND n.deleted_at IS NULL "
        "WHERE l.source_type = 'note' AND l.origin = 'wikilink' ORDER BY l.id"),
        [&](const Statement& s) {
            const int from = index.value(noteKey(s.columnInt64(0)), -1);
            if (from < 0)
                return;
            int to = -1;
            if (s.isNull(2)) {
                const QString text = s.columnText(3);
                const QString key = QStringLiteral("u:") + wikiLinkKey(text).toLower();
                if (key.size() <= 2)
                    return;
                to = index.value(key, -1);
                if (to < 0) {
                    GraphNode n;
                    n.kind = GraphNodeKind::Unresolved;
                    n.key = key;
                    n.title = text.section(QRegularExpression(QStringLiteral("[|#^]")), 0, 0).trimmed();
                    n.detail = graphNodeKindLabel(GraphNodeKind::Unresolved);
                    add(std::move(n));
                    to = int(g.nodes.size()) - 1;
                }
            } else {
                const qint64 id = s.columnInt64(2);
                to = index.value(s.columnText(1) == QLatin1String("document") ? docKey(id) : noteKey(id), -1);
            }
            if (to < 0)
                return;
            ++g.nodes[from].outLinks;
            if (edge(from, to, false))
                ++g.nodes[to].backlinks;
        });
    if (!wiki)
        return wiki.error();

    QSet<QPair<int, int>> derivedPairs;
    // Provenance : la note est tirée d'un document (directement, ou par une annotation ou un commentaire).
    auto derived = forEachRow(m_db, QStringLiteral(
        "SELECT l.source_id, CASE l.target_type "
        "  WHEN 'document' THEN l.target_id "
        "  WHEN 'annotation' THEN (SELECT a.document_id FROM annotations a WHERE a.id = l.target_id) "
        "  WHEN 'comment' THEN (SELECT c.document_id FROM comments c WHERE c.id = l.target_id) END "
        "FROM links l JOIN notes n ON n.id = l.source_id AND n.deleted_at IS NULL "
        "WHERE l.source_type = 'note' AND l.kind = 'derived_from' ORDER BY l.id"),
        [&](const Statement& s) {
            const int from = index.value(noteKey(s.columnInt64(0)), -1);
            if (from < 0)
                return;
            ++g.nodes[from].sources;
            if (s.isNull(1))
                return;
            const int to = index.value(docKey(s.columnInt64(1)), -1);
            if (to < 0)
                return;
            // Plusieurs sources d'une note dans le même document : une seule note tirée, une seule arête.
            if (!derivedPairs.contains({from, to})) {
                derivedPairs.insert({from, to});
                ++g.nodes[to].sources;
            }
            edge(from, to, true);
        });
    if (!derived)
        return derived.error();

    // Matières non archivées
    auto subjects = forEachRow(m_db, QStringLiteral(
        "SELECT si.entity_type, si.entity_id, s.id, s.color FROM subject_items si "
        "JOIN subjects s ON s.id = si.subject_id AND s.archived = 0 "
        "WHERE si.entity_type IN ('document', 'note') ORDER BY s.position, s.id"),
        [&](const Statement& s) {
            const qint64 id = s.columnInt64(1);
            const int i = index.value(s.columnText(0) == QLatin1String("document") ? docKey(id) : noteKey(id), -1);
            if (i < 0)
                return;
            GraphNode& n = g.nodes[i];
            if (n.subjects.isEmpty())
                n.color = s.isNull(3) ? QStringLiteral("slate") : s.columnText(3);
            n.subjects << s.columnInt64(2);
        });
    if (!subjects)
        return subjects.error();
    return g;
}

}  // namespace studylab::db
