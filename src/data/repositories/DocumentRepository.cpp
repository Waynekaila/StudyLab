#include "data/repositories/DocumentRepository.h"

#include "core/Clock.h"
#include "core/Uuid.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "data/sqlite/Transaction.h"

#include <QRegularExpression>

namespace studylab::db {

namespace {

constexpr QChar kSep(0x1F);  // séparateur d'unités (absent des noms)

QStringList cleanList(const QStringList& in)
{
    QStringList out;
    for (const QString& raw : in) {
        const QString s = raw.simplified();
        if (s.isEmpty())
            continue;
        bool dup = false;
        for (const QString& o : out)
            dup = dup || o.compare(s, Qt::CaseInsensitive) == 0;
        if (!dup)
            out << s;
    }
    return out;
}

QString orNull(const QString& s)
{
    return s.isEmpty() ? QString() : s;
}

// Colonnes lues par readRow(), dans l'ordre.
const QString kSelect = QStringLiteral(
    "SELECT d.id, d.uuid, d.title, d.sort_title, d.subtitle, d.publisher, d.year, d.isbn, d.doi, d.language, "
    "d.description, d.format, d.storage_mode, d.file_path, d.file_hash, d.file_size, d.page_count, d.cover_path, "
    "d.status, d.favorite, d.rating, d.added_at, d.updated_at, d.last_opened_at, "
    "COALESCE(rp.progress, 0), "
    "(SELECT group_concat(a.name, char(31) ORDER BY da.position) FROM document_authors da "
    "   JOIN authors a ON a.id = da.author_id WHERE da.document_id = d.id), "
    "(SELECT group_concat(t.name, char(31) ORDER BY t.name COLLATE NOCASE) FROM entity_tags et "
    "   JOIN tags t ON t.id = et.tag_id WHERE et.entity_type = 'document' AND et.entity_id = d.id) "
    "FROM documents d LEFT JOIN reading_progress rp ON rp.document_id = d.id ");

DocumentRecord readRow(const Statement& s)
{
    DocumentRecord r;
    r.id = s.columnInt64(0);
    r.uuid = s.columnText(1);
    r.title = s.columnText(2);
    r.sortTitle = s.columnText(3);
    r.subtitle = s.columnText(4);
    r.publisher = s.columnText(5);
    r.year = int(s.columnInt64(6));
    r.isbn = s.columnText(7);
    r.doi = s.columnText(8);
    r.language = s.columnText(9);
    r.description = s.columnText(10);
    r.format = documentFormatFromKey(s.columnText(11)).value_or(DocumentFormat::Pdf);
    r.storageMode = s.columnText(12) == QLatin1String("linked") ? StorageMode::Linked : StorageMode::Managed;
    r.filePath = s.columnText(13);
    r.fileHash = s.columnText(14);
    r.fileSize = s.columnInt64(15);
    r.pageCount = int(s.columnInt64(16));
    r.coverPath = s.columnText(17);
    r.status = readingStatusFromKey(s.columnText(18)).value_or(ReadingStatus::ToRead);
    r.favorite = s.columnInt64(19) != 0;
    r.rating = int(s.columnInt64(20));
    r.addedAt = s.columnText(21);
    r.updatedAt = s.columnText(22);
    r.lastOpenedAt = s.columnText(23);
    r.progress = s.columnDouble(24);
    if (!s.isNull(25))
        r.authors = s.columnText(25).split(kSep);
    if (!s.isNull(26))
        r.tags = s.columnText(26).split(kSep);
    return r;
}

Result<QList<DocumentRecord>> collect(Statement& stmt)
{
    QList<DocumentRecord> out;
    while (true) {
        auto row = stmt.step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out << readRow(stmt);
    }
    return out;
}

void bindMetadata(Statement& s, const DocumentRecord& r)
{
    s.bind(s.parameterIndex(":title"), r.title)
        .bind(s.parameterIndex(":sort_title"), r.sortTitle)
        .bind(s.parameterIndex(":subtitle"), orNull(r.subtitle))
        .bind(s.parameterIndex(":publisher"), orNull(r.publisher))
        .bind(s.parameterIndex(":isbn"), orNull(r.isbn))
        .bind(s.parameterIndex(":doi"), orNull(r.doi))
        .bind(s.parameterIndex(":language"), orNull(r.language))
        .bind(s.parameterIndex(":description"), orNull(r.description))
        .bind(s.parameterIndex(":status"), readingStatusKey(r.status))
        .bind(s.parameterIndex(":favorite"), r.favorite ? 1 : 0)
        .bind(s.parameterIndex(":rating"), qBound(0, r.rating, 5))
        .bind(s.parameterIndex(":updated_at"), r.updatedAt);
    if (r.year > 0)
        s.bind(s.parameterIndex(":year"), r.year);
    else
        s.bindNull(s.parameterIndex(":year"));
}

}  // namespace

DocumentRepository::DocumentRepository(Database& db) : m_db(db) {}

QString DocumentRepository::toFtsQuery(const QString& text)
{
    static const QRegularExpression splitter(QStringLiteral("[^\\p{L}\\p{N}]+"));
    QStringList terms;
    for (const QString& t : text.split(splitter, Qt::SkipEmptyParts))
        terms << QLatin1Char('"') + t + QStringLiteral("\"*");
    return terms.join(QLatin1Char(' '));
}

Result<void> DocumentRepository::insert(DocumentRecord& r)
{
    r.title = r.title.simplified();
    if (r.title.isEmpty() || r.fileHash.isEmpty() || r.filePath.isEmpty())
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Titre, chemin et empreinte sont obligatoires."));
    if (r.uuid.isEmpty())
        r.uuid = newUuid();
    r.sortTitle = makeSortTitle(r.title);
    r.authors = cleanList(r.authors);
    r.tags = cleanList(r.tags);
    r.addedAt = r.addedAt.isEmpty() ? nowIsoUtc() : r.addedAt;
    r.updatedAt = r.addedAt;

    Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();

    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO documents(uuid, title, sort_title, subtitle, publisher, year, isbn, doi, language, description, "
        "format, storage_mode, file_path, file_hash, file_size, page_count, cover_path, status, favorite, rating, "
        "added_at, updated_at) VALUES (:uuid, :title, :sort_title, :subtitle, :publisher, :year, :isbn, :doi, "
        ":language, :description, :format, :storage_mode, :file_path, :file_hash, :file_size, :page_count, "
        ":cover_path, :status, :favorite, :rating, :added_at, :updated_at)"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    bindMetadata(s, r);
    s.bind(s.parameterIndex(":uuid"), r.uuid)
        .bind(s.parameterIndex(":format"), documentFormatKey(r.format))
        .bind(s.parameterIndex(":storage_mode"),
              r.storageMode == StorageMode::Linked ? QStringLiteral("linked") : QStringLiteral("managed"))
        .bind(s.parameterIndex(":file_path"), r.filePath)
        .bind(s.parameterIndex(":file_hash"), r.fileHash)
        .bind(s.parameterIndex(":file_size"), r.fileSize)
        .bind(s.parameterIndex(":page_count"), r.pageCount)
        .bind(s.parameterIndex(":cover_path"), orNull(r.coverPath))
        .bind(s.parameterIndex(":added_at"), r.addedAt);
    if (auto res = s.run(); !res)
        return res.error();
    r.id = m_db.lastInsertRowId();

    if (auto res = writeAuthors(r.id, r.authors); !res)
        return res;
    if (auto res = writeTags(r.id, r.tags); !res)
        return res;
    if (auto res = writeFts(r); !res)
        return res;
    return tx.commit();
}

Result<void> DocumentRepository::update(DocumentRecord& r)
{
    r.title = r.title.simplified();
    if (r.id <= 0 || r.title.isEmpty())
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Document invalide ou titre vide."));
    r.sortTitle = makeSortTitle(r.title);
    r.authors = cleanList(r.authors);
    r.tags = cleanList(r.tags);
    r.updatedAt = nowIsoUtc();

    Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE documents SET title = :title, sort_title = :sort_title, subtitle = :subtitle, publisher = :publisher, "
        "year = :year, isbn = :isbn, doi = :doi, language = :language, description = :description, status = :status, "
        "favorite = :favorite, rating = :rating, updated_at = :updated_at WHERE id = :id AND deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    bindMetadata(s, r);
    s.bind(s.parameterIndex(":id"), r.id);
    if (auto res = s.run(); !res)
        return res.error();
    if (m_db.changes() == 0)
        return Error::make(Error::Code::NotFound, QStringLiteral("Document introuvable."));

    for (const char* sql : {"DELETE FROM document_authors WHERE document_id = ?1",
                            "DELETE FROM entity_tags WHERE entity_type = 'document' AND entity_id = ?1",
                            "DELETE FROM fts_documents WHERE rowid = ?1"}) {
        auto del = m_db.prepare(QString::fromLatin1(sql));
        if (!del)
            return del.error();
        if (auto res = del.value().bind(1, r.id).run(); !res)
            return res.error();
    }
    if (auto res = writeAuthors(r.id, r.authors); !res)
        return res;
    if (auto res = writeTags(r.id, r.tags); !res)
        return res;
    if (auto res = writeFts(r); !res)
        return res;
    return tx.commit();
}

Result<void> DocumentRepository::writeAuthors(qint64 id, const QStringList& authors)
{
    auto upsert = m_db.prepare(QStringLiteral(
        "INSERT INTO authors(name, sort_name) VALUES (?1, ?2) ON CONFLICT(name) DO UPDATE SET name = name "
        "RETURNING id"));
    auto link = m_db.prepare(
        QStringLiteral("INSERT OR IGNORE INTO document_authors(document_id, author_id, position) VALUES (?1, ?2, ?3)"));
    if (!upsert)
        return upsert.error();
    if (!link)
        return link.error();
    int position = 0;
    for (const QString& name : authors) {
        Statement& u = upsert.value();
        u.reset();
        u.bind(1, name).bind(2, foldForSort(makeAuthorSortName(name)));  // clé de tri sans accents
        auto row = u.step();
        if (!row)
            return row.error();
        const qint64 authorId = u.columnInt64(0);
        u.reset();
        Statement& l = link.value();
        l.reset();
        if (auto res = l.bind(1, id).bind(2, authorId).bind(3, position++).run(); !res)
            return res.error();
    }
    return {};
}

Result<void> DocumentRepository::writeTags(qint64 id, const QStringList& tags)
{
    auto upsert = m_db.prepare(QStringLiteral(
        "INSERT INTO tags(name) VALUES (?1) ON CONFLICT(name) DO UPDATE SET name = name RETURNING id"));
    auto link = m_db.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO entity_tags(tag_id, entity_type, entity_id) VALUES (?1, 'document', ?2)"));
    if (!upsert)
        return upsert.error();
    if (!link)
        return link.error();
    for (const QString& tag : tags) {
        Statement& u = upsert.value();
        u.reset();
        u.bind(1, tag);
        auto row = u.step();
        if (!row)
            return row.error();
        const qint64 tagId = u.columnInt64(0);
        u.reset();
        Statement& l = link.value();
        l.reset();
        if (auto res = l.bind(1, tagId).bind(2, id).run(); !res)
            return res.error();
    }
    return {};
}

Result<void> DocumentRepository::writeFts(const DocumentRecord& r)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO fts_documents(rowid, title, authors, publisher, description, keywords, isbn, doi) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)"));
    if (!stmt)
        return stmt.error();
    QString title = r.title;
    if (!r.subtitle.isEmpty())
        title += QLatin1Char(' ') + r.subtitle;
    QString isbnDigits = r.isbn;
    isbnDigits.remove(QLatin1Char('-'));
    return stmt.value()
        .bind(1, r.id)
        .bind(2, title)
        .bind(3, r.authors.join(QLatin1Char(' ')))
        .bind(4, orNull(r.publisher))
        .bind(5, orNull(r.description))
        .bind(6, r.tags.join(QLatin1Char(' ')))
        .bind(7, orNull(isbnDigits))
        .bind(8, orNull(r.doi))
        .run();
}

Result<DocumentRecord> DocumentRepository::findById(qint64 id)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral("WHERE d.id = ?1"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, id);
    auto rows = collect(stmt.value());
    if (!rows)
        return rows.error();
    if (rows.value().isEmpty())
        return Error::make(Error::Code::NotFound, QStringLiteral("Document introuvable."));
    return rows.value().first();
}

Result<std::optional<DocumentRecord>> DocumentRepository::findByHash(const QString& sha256)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral("WHERE d.file_hash = ?1 AND d.deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, sha256);
    auto rows = collect(stmt.value());
    if (!rows)
        return rows.error();
    if (rows.value().isEmpty())
        return std::optional<DocumentRecord>{};
    return std::optional<DocumentRecord>{rows.value().first()};
}

Result<QList<DocumentRecord>> DocumentRepository::list(const DocumentQuery& q)
{
    QString sql = kSelect + QStringLiteral("WHERE d.deleted_at IS NULL");
    const QString fts = toFtsQuery(q.text);
    if (!fts.isEmpty())
        sql += QStringLiteral(" AND d.id IN (SELECT rowid FROM fts_documents WHERE fts_documents MATCH :fts)");
    if (q.status)
        sql += QStringLiteral(" AND d.status = :status");
    if (q.format)
        sql += QStringLiteral(" AND d.format = :format");
    if (q.favoritesOnly)
        sql += QStringLiteral(" AND d.favorite = 1");
    if (!q.tag.isEmpty())
        sql += QStringLiteral(" AND d.id IN (SELECT et.entity_id FROM entity_tags et JOIN tags t ON t.id = et.tag_id "
                              "WHERE et.entity_type = 'document' AND t.name = :tag)");
    switch (q.sort) {
    case DocumentQuery::Sort::RecentlyAdded:
        sql += QStringLiteral(" ORDER BY d.added_at DESC, d.id DESC");
        break;
    case DocumentQuery::Sort::RecentlyOpened:
        sql += QStringLiteral(" ORDER BY d.last_opened_at IS NULL, d.last_opened_at DESC, d.added_at DESC, d.id DESC");
        break;
    case DocumentQuery::Sort::Title:
        sql += QStringLiteral(" ORDER BY d.sort_title, d.id");
        break;
    case DocumentQuery::Sort::Author:
        sql += QStringLiteral(
            " ORDER BY (SELECT a.sort_name FROM document_authors da JOIN authors a ON a.id = da.author_id "
            "WHERE da.document_id = d.id ORDER BY da.position LIMIT 1) IS NULL, "
            "(SELECT a.sort_name FROM document_authors da JOIN authors a ON a.id = da.author_id "
            "WHERE da.document_id = d.id ORDER BY da.position LIMIT 1), d.sort_title");
        break;
    }
    sql += QStringLiteral(" LIMIT :limit OFFSET :offset");

    auto stmt = m_db.prepare(sql);
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    if (!fts.isEmpty())
        s.bind(s.parameterIndex(":fts"), fts);
    if (q.status)
        s.bind(s.parameterIndex(":status"), readingStatusKey(*q.status));
    if (q.format)
        s.bind(s.parameterIndex(":format"), documentFormatKey(*q.format));
    if (!q.tag.isEmpty())
        s.bind(s.parameterIndex(":tag"), q.tag);
    s.bind(s.parameterIndex(":limit"), q.limit).bind(s.parameterIndex(":offset"), qMax(0, q.offset));
    return collect(s);
}

Result<int> DocumentRepository::count()
{
    auto n = m_db.queryInt(QStringLiteral("SELECT count(*) FROM documents WHERE deleted_at IS NULL"));
    if (!n)
        return n.error();
    return int(n.value());
}

Result<qint64> DocumentRepository::totalFileSize()
{
    return m_db.queryInt(QStringLiteral("SELECT coalesce(sum(file_size), 0) FROM documents WHERE deleted_at IS NULL"));
}

Result<void> DocumentRepository::simpleUpdate(const QString& sql, qint64 id, const QString& value)
{
    auto stmt = m_db.prepare(sql);
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, id).bind(2, nowIsoUtc());
    if (!value.isNull())
        stmt.value().bind(3, value);
    if (auto r = stmt.value().run(); !r)
        return r;
    if (m_db.changes() == 0)
        return Error::make(Error::Code::NotFound, QStringLiteral("Document introuvable."));
    return {};
}

Result<void> DocumentRepository::setStatus(qint64 id, ReadingStatus status)
{
    return simpleUpdate(QStringLiteral("UPDATE documents SET status = ?3, updated_at = ?2 "
                                       "WHERE id = ?1 AND deleted_at IS NULL"),
                        id, readingStatusKey(status));
}

Result<void> DocumentRepository::setFavorite(qint64 id, bool favorite)
{
    return simpleUpdate(QStringLiteral("UPDATE documents SET favorite = ?3, updated_at = ?2 "
                                       "WHERE id = ?1 AND deleted_at IS NULL"),
                        id, favorite ? QStringLiteral("1") : QStringLiteral("0"));
}

Result<void> DocumentRepository::markOpened(qint64 id)
{
    return simpleUpdate(
        QStringLiteral("UPDATE documents SET last_opened_at = ?2 WHERE id = ?1 AND deleted_at IS NULL"), id, {});
}

Result<void> DocumentRepository::remove(qint64 id)
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    if (auto r = simpleUpdate(QStringLiteral("UPDATE documents SET deleted_at = ?2, updated_at = ?2 "
                                             "WHERE id = ?1 AND deleted_at IS NULL"),
                              id, {});
        !r)
        return r;
    auto del = m_db.prepare(QStringLiteral("DELETE FROM fts_documents WHERE rowid = ?1"));
    if (!del)
        return del.error();
    if (auto r = del.value().bind(1, id).run(); !r)
        return r;
    return tx.commit();
}

Result<QStringList> DocumentRepository::documentTags()
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT DISTINCT t.name FROM tags t JOIN entity_tags et ON et.tag_id = t.id AND et.entity_type = 'document' "
        "JOIN documents d ON d.id = et.entity_id AND d.deleted_at IS NULL ORDER BY t.name COLLATE NOCASE"));
    if (!stmt)
        return stmt.error();
    QStringList out;
    while (true) {
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out << stmt.value().columnText(0);
    }
    return out;
}

Result<QSet<QString>> DocumentRepository::activeHashes()
{
    auto stmt = m_db.prepare(QStringLiteral("SELECT file_hash FROM documents WHERE deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    QSet<QString> out;
    while (true) {
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out.insert(stmt.value().columnText(0));
    }
    return out;
}

}  // namespace studylab::db
