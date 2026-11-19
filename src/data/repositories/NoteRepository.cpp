#include "data/repositories/NoteRepository.h"

#include "core/Clock.h"
#include "core/Uuid.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"

#include <QJsonDocument>

namespace studylab::db {

namespace {

const QString kSelect = QStringLiteral(
    "SELECT id, uuid, title, slug, note_type, body_md, frontmatter, folder, pinned, created_at, updated_at FROM notes ");

NoteRecord readRow(const Statement& s)
{
    NoteRecord n;
    n.id = s.columnInt64(0);
    n.uuid = s.columnText(1);
    n.title = s.columnText(2);
    n.slug = s.columnText(3);
    n.type = noteTypeFromKey(s.columnText(4)).value_or(NoteType::Personal);
    n.body = s.columnText(5);
    if (!s.isNull(6))
        n.frontMatter = QJsonDocument::fromJson(s.columnText(6).toUtf8()).object();
    n.folder = s.columnText(7);
    n.pinned = s.columnInt64(8) != 0;
    n.createdAt = s.columnText(9);
    n.updatedAt = s.columnText(10);
    return n;
}

template <typename T, typename F>
Result<QList<T>> collect(Statement& stmt, F read)
{
    QList<T> out;
    while (true) {
        auto row = stmt.step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out << read(stmt);
    }
    return out;
}

QString frontMatterJson(const QJsonObject& o)
{
    return o.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

NoteLinkRow readLink(const Statement& s)
{
    NoteLinkRow l;
    l.id = s.columnInt64(0);
    l.sourceNoteId = s.columnInt64(1);
    l.targetText = s.columnText(2);
    l.targetNoteId = s.isNull(3) ? 0 : s.columnInt64(3);
    l.targetType = s.columnText(4);
    return l;
}

Error invalid(const QString& message) { return Error::make(Error::Code::InvalidArgument, message); }
Error notFound() { return Error::make(Error::Code::NotFound, QStringLiteral("Note introuvable.")); }

void bindFolder(Statement& s, int index, const QString& folder)
{
    if (folder.isEmpty())
        s.bindNull(index);
    else
        s.bind(index, folder);
}

}  // namespace

NoteRepository::NoteRepository(Database& db) : m_db(db) {}

Result<void> NoteRepository::insert(NoteRecord& n)
{
    n.title = n.title.trimmed();
    if (n.body.isNull())
        n.body = QStringLiteral("");  // QString nul → NULL, refusé par body_md NOT NULL
    if (n.title.isEmpty())
        return invalid(QStringLiteral("Une note exige un titre."));
    if (n.slug.isEmpty())
        return invalid(QStringLiteral("Nom de fichier manquant."));
    if (n.uuid.isEmpty())
        n.uuid = newUuid();
    n.createdAt = n.createdAt.isEmpty() ? nowIsoUtc() : n.createdAt;
    n.updatedAt = n.updatedAt.isEmpty() ? n.createdAt : n.updatedAt;
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO notes(uuid, title, slug, note_type, body_md, frontmatter, folder, pinned, created_at, updated_at) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10)"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, n.uuid).bind(2, n.title).bind(3, n.slug).bind(4, noteTypeKey(n.type)).bind(5, n.body);
    s.bind(6, frontMatterJson(n.frontMatter));
    bindFolder(s, 7, n.folder);
    s.bind(8, n.pinned ? 1 : 0).bind(9, n.createdAt).bind(10, n.updatedAt);
    if (auto res = s.run(); !res)
        return res;
    n.id = m_db.lastInsertRowId();
    return {};
}

Result<void> NoteRepository::update(NoteRecord& n)
{
    n.title = n.title.trimmed();
    if (n.body.isNull())
        n.body = QStringLiteral("");
    if (n.id <= 0)
        return notFound();
    if (n.title.isEmpty())
        return invalid(QStringLiteral("Une note exige un titre."));
    n.updatedAt = nowIsoUtc();
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE notes SET title = ?2, slug = ?3, note_type = ?4, body_md = ?5, frontmatter = ?6, folder = ?7, pinned = ?8, "
        "updated_at = ?9 WHERE id = ?1 AND deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, n.id).bind(2, n.title).bind(3, n.slug).bind(4, noteTypeKey(n.type)).bind(5, n.body);
    s.bind(6, frontMatterJson(n.frontMatter));
    bindFolder(s, 7, n.folder);
    s.bind(8, n.pinned ? 1 : 0).bind(9, n.updatedAt);
    if (auto res = s.run(); !res)
        return res;
    if (m_db.changes() == 0)
        return notFound();
    return {};
}

Result<NoteRecord> NoteRepository::findById(qint64 id)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral("WHERE id = ?1 AND deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, id);
    auto rows = collect<NoteRecord>(stmt.value(), readRow);
    if (!rows)
        return rows.error();
    if (rows.value().isEmpty())
        return notFound();
    return rows.value().first();
}

Result<NoteRecord> NoteRepository::findByUuid(const QString& uuid)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral("WHERE uuid = ?1 AND deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, uuid);
    auto rows = collect<NoteRecord>(stmt.value(), readRow);
    if (!rows)
        return rows.error();
    if (rows.value().isEmpty())
        return notFound();
    return rows.value().first();
}

Result<QList<NoteRecord>> NoteRepository::list()
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral(
        "WHERE deleted_at IS NULL ORDER BY coalesce(folder, '') COLLATE NOCASE, title COLLATE NOCASE, id"));
    if (!stmt)
        return stmt.error();
    return collect<NoteRecord>(stmt.value(), readRow);
}

Result<int> NoteRepository::count()
{
    auto n = m_db.queryInt(QStringLiteral("SELECT count(*) FROM notes WHERE deleted_at IS NULL"));
    if (!n)
        return n.error();
    return int(n.value());
}

Result<bool> NoteRepository::slugTaken(const QString& slug, qint64 exceptId)
{
    // Comparaison sans casse : deux fichiers « Carnot.md » et « carnot.md » se confondent sous Windows.
    auto stmt = m_db.prepare(QStringLiteral("SELECT count(*) FROM notes WHERE lower(slug) = lower(?1) AND id <> ?2"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, slug).bind(2, exceptId);
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    return stmt.value().columnInt64(0) > 0;
}

Result<void> NoteRepository::remove(qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral("UPDATE notes SET deleted_at = ?2 WHERE id = ?1 AND deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    if (auto res = stmt.value().bind(1, id).bind(2, nowIsoUtc()).run(); !res)
        return res;
    if (m_db.changes() == 0)
        return notFound();
    return {};
}

Result<void> NoteRepository::restore(qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral("UPDATE notes SET deleted_at = NULL WHERE id = ?1 AND deleted_at IS NOT NULL"));
    if (!stmt)
        return stmt.error();
    if (auto res = stmt.value().bind(1, id).run(); !res)
        return res;
    if (m_db.changes() == 0)
        return notFound();
    return {};
}

Result<void> NoteRepository::setAliases(qint64 noteId, const QStringList& aliases)
{
    auto del = m_db.prepare(QStringLiteral("DELETE FROM note_aliases WHERE note_id = ?1"));
    if (!del)
        return del.error();
    if (auto r = del.value().bind(1, noteId).run(); !r)
        return r;
    auto ins = m_db.prepare(QStringLiteral("INSERT OR IGNORE INTO note_aliases(note_id, alias) VALUES (?1, ?2)"));
    if (!ins)
        return ins.error();
    for (const QString& alias : aliases) {
        ins.value().reset();
        if (auto r = ins.value().bind(1, noteId).bind(2, alias).run(); !r)
            return r;
    }
    return {};
}

Result<QList<NoteKeys>> NoteRepository::keys()
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT n.id, n.title, n.slug, a.alias FROM notes n LEFT JOIN note_aliases a ON a.note_id = n.id "
        "WHERE n.deleted_at IS NULL ORDER BY n.id"));
    if (!stmt)
        return stmt.error();
    QList<NoteKeys> out;
    while (true) {
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        const Statement& s = stmt.value();
        const qint64 id = s.columnInt64(0);
        if (out.isEmpty() || out.last().id != id)
            out.append({id, s.columnText(1), s.columnText(2), {}});
        if (!s.isNull(3))
            out.last().aliases << s.columnText(3);
    }
    return out;
}

Result<void> NoteRepository::replaceWikiLinks(qint64 noteId, const QList<NoteLinkRow>& links)
{
    auto del = m_db.prepare(QStringLiteral(
        "DELETE FROM links WHERE source_type = 'note' AND source_id = ?1 AND origin = 'wikilink'"));
    if (!del)
        return del.error();
    if (auto r = del.value().bind(1, noteId).run(); !r)
        return r;
    auto ins = m_db.prepare(QStringLiteral(
        "INSERT INTO links(source_type, source_id, target_type, target_id, target_text, kind, origin, created_at) "
        "VALUES ('note', ?1, ?5, ?2, ?3, 'reference', 'wikilink', ?4)"));
    if (!ins)
        return ins.error();
    const QString now = nowIsoUtc();
    for (const NoteLinkRow& l : links) {
        ins.value().reset();
        ins.value().bind(1, noteId);
        if (l.targetNoteId > 0)
            ins.value().bind(2, l.targetNoteId);
        else
            ins.value().bindNull(2);
        const QString type = l.targetNoteId > 0 ? l.targetType : QStringLiteral("note");
        if (auto r = ins.value().bind(3, l.targetText).bind(4, now).bind(5, type).run(); !r)
            return r;
    }
    return {};
}

Result<QList<NoteLinkRow>> NoteRepository::outgoingLinks(qint64 noteId)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT id, source_id, target_text, target_id, target_type FROM links "
        "WHERE source_type = 'note' AND source_id = ?1 AND origin = 'wikilink' ORDER BY id"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, noteId);
    return collect<NoteLinkRow>(stmt.value(), readLink);
}

Result<QList<NoteLinkRow>> NoteRepository::incomingLinks(qint64 noteId)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT l.id, l.source_id, l.target_text, l.target_id, l.target_type FROM links l JOIN notes s ON s.id = l.source_id "
        "WHERE l.source_type = 'note' AND l.target_type = 'note' AND l.target_id = ?1 AND l.source_id <> ?1 "
        "AND s.deleted_at IS NULL ORDER BY s.updated_at DESC, l.id"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, noteId);
    return collect<NoteLinkRow>(stmt.value(), readLink);
}

Result<QList<NoteLinkRow>> NoteRepository::allWikiLinks()
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT id, source_id, target_text, target_id, target_type FROM links WHERE source_type = 'note' AND origin = 'wikilink'"));
    if (!stmt)
        return stmt.error();
    return collect<NoteLinkRow>(stmt.value(), readLink);
}

Result<void> NoteRepository::setLinkTarget(qint64 linkId, const QString& targetType, qint64 targetId)
{
    auto stmt = m_db.prepare(QStringLiteral("UPDATE links SET target_type = ?2, target_id = ?3 WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, linkId).bind(2, targetId > 0 ? targetType : QStringLiteral("note"));
    if (targetId > 0)
        stmt.value().bind(3, targetId);
    else
        stmt.value().bindNull(3);
    return stmt.value().run();
}

Result<QList<DocumentKey>> NoteRepository::documentKeys()
{
    auto stmt = m_db.prepare(QStringLiteral("SELECT id, title FROM documents WHERE deleted_at IS NULL ORDER BY id"));
    if (!stmt)
        return stmt.error();
    return collect<DocumentKey>(stmt.value(), [](const Statement& s) { return DocumentKey{s.columnInt64(0), s.columnText(1)}; });
}

Result<void> NoteRepository::addSources(qint64 noteId, const QList<QPair<QString, qint64>>& sources)
{
    auto ins = m_db.prepare(QStringLiteral(
        "INSERT INTO links(source_type, source_id, target_type, target_id, kind, origin, created_at) "
        "VALUES ('note', ?1, ?2, ?3, 'derived_from', 'derived', ?4)"));
    if (!ins)
        return ins.error();
    const QString now = nowIsoUtc();
    for (const auto& [kind, id] : sources) {
        if (kind != QLatin1String("annotation") && kind != QLatin1String("comment") && kind != QLatin1String("document"))
            return invalid(QStringLiteral("Source de note inconnue : %1").arg(kind));
        ins.value().reset();
        if (auto r = ins.value().bind(1, noteId).bind(2, kind).bind(3, id).bind(4, now).run(); !r)
            return r;
    }
    return {};
}

Result<QList<NoteSourceRow>> NoteRepository::sources(qint64 noteId)
{
    // Une ligne par source ; les colonnes de l'annotation ou du commentaire sont NULL pour les autres types.
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT l.target_type, l.target_id, d.id, d.uuid, d.title, d.format, "
        "       coalesce(a.uuid, c.uuid), coalesce(a.selected_text, c.body_md), "
        "       coalesce(a.page_label, json_extract(c.locator_json, '$.pageLabel')), coalesce(a.unit_index, c.unit_index), "
        "       (d.id IS NULL OR d.deleted_at IS NOT NULL OR a.deleted_at IS NOT NULL OR c.deleted_at IS NOT NULL "
        "        OR (l.target_type = 'annotation' AND a.id IS NULL) OR (l.target_type = 'comment' AND c.id IS NULL)), "
        "       (SELECT group_concat(name, char(31)) FROM (SELECT au.name FROM document_authors da "
        "          JOIN authors au ON au.id = da.author_id WHERE da.document_id = d.id ORDER BY da.position)) "
        "FROM links l "
        "LEFT JOIN annotations a ON l.target_type = 'annotation' AND a.id = l.target_id "
        "LEFT JOIN comments c ON l.target_type = 'comment' AND c.id = l.target_id "
        "LEFT JOIN documents d ON d.id = CASE l.target_type WHEN 'document' THEN l.target_id "
        "     WHEN 'annotation' THEN a.document_id WHEN 'comment' THEN c.document_id END "
        "WHERE l.source_type = 'note' AND l.source_id = ?1 AND l.kind = 'derived_from' ORDER BY l.id"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, noteId);
    return collect<NoteSourceRow>(stmt.value(), [](const Statement& s) {
        NoteSourceRow r;
        r.kind = s.columnText(0);
        r.id = s.columnInt64(1);
        r.documentId = s.isNull(2) ? 0 : s.columnInt64(2);
        r.documentUuid = s.columnText(3);
        r.documentTitle = s.columnText(4);
        r.format = s.columnText(5);
        r.itemUuid = s.columnText(6);
        r.quote = s.columnText(7);
        r.pageLabel = s.columnText(8);
        r.unitIndex = s.isNull(9) ? -1 : int(s.columnInt64(9));
        r.missing = s.columnInt64(10) != 0;
        if (!s.isNull(11))
            r.authors = s.columnText(11).split(QChar(31));
        return r;
    });
}

Result<QList<qint64>> NoteRepository::notesDerivedFrom(const QList<qint64>& documentIds)
{
    QList<qint64> out;
    if (documentIds.isEmpty())
        return out;
    QStringList ids;
    for (qint64 id : documentIds)
        ids << QString::number(id);
    const QString list = ids.join(u',');
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT DISTINCT n.id FROM notes n JOIN links l ON l.source_type = 'note' AND l.source_id = n.id "
        "AND l.kind = 'derived_from' "
        "LEFT JOIN annotations a ON l.target_type = 'annotation' AND a.id = l.target_id "
        "LEFT JOIN comments c ON l.target_type = 'comment' AND c.id = l.target_id "
        "WHERE n.deleted_at IS NULL AND ((l.target_type = 'document' AND l.target_id IN (%1)) "
        "OR a.document_id IN (%1) OR c.document_id IN (%1)) ORDER BY n.id").arg(list));
    if (!stmt)
        return stmt.error();
    return collect<qint64>(stmt.value(), [](const Statement& s) { return s.columnInt64(0); });
}

Result<void> NoteRepository::addAttachment(qint64 noteId, const QString& uuid, const QString& fileName,
                                           const QString& relPath, const QString& mime)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO attachments(uuid, note_id, file_name, rel_path, mime, created_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6)"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, uuid);
    if (noteId > 0)
        s.bind(2, noteId);
    else
        s.bindNull(2);
    return s.bind(3, fileName).bind(4, relPath).bind(5, mime).bind(6, nowIsoUtc()).run();
}

}  // namespace studylab::db
