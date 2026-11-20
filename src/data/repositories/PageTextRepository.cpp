#include "data/repositories/PageTextRepository.h"

#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "data/sqlite/Transaction.h"

namespace studylab::db {

PageTextRepository::PageTextRepository(Database& db) : m_db(db) {}

Result<void> PageTextRepository::replace(qint64 documentId, const QStringList& units)
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    auto del = m_db.prepare(QStringLiteral("DELETE FROM page_text WHERE document_id = ?1 AND source = 'native'"));
    if (!del)
        return del.error();
    if (auto r = del.value().bind(1, documentId).run(); !r)
        return r.error();
    auto ins = m_db.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO page_text(document_id, unit_index, source, content) VALUES (?1, ?2, 'native', ?3)"));
    if (!ins)
        return ins.error();
    for (int i = 0; i < units.size(); ++i) {
        if (units.at(i).trimmed().isEmpty())
            continue;
        ins.value().reset();
        if (auto r = ins.value().bind(1, documentId).bind(2, i).bind(3, units.at(i)).run(); !r)
            return r.error();
    }
    auto mark = m_db.prepare(QStringLiteral("UPDATE documents SET text_indexed = 1 WHERE id = ?1"));
    if (!mark)
        return mark.error();
    if (auto r = mark.value().bind(1, documentId).run(); !r)
        return r.error();
    return tx.commit();
}

Result<void> PageTextRepository::markFailed(qint64 documentId)
{
    auto stmt = m_db.prepare(QStringLiteral("UPDATE documents SET text_indexed = -1 WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    return stmt.value().bind(1, documentId).run();
}

Result<QList<PendingTextDocument>> PageTextRepository::pending()
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT id, format, file_path, storage_mode FROM documents WHERE deleted_at IS NULL AND text_indexed = 0 "
        "AND format IN ('pdf', 'epub', 'txt', 'md') ORDER BY added_at DESC, id DESC"));
    if (!stmt)
        return stmt.error();
    QList<PendingTextDocument> out;
    while (true) {
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        const Statement& s = stmt.value();
        out << PendingTextDocument{s.columnInt64(0), s.columnText(1), s.columnText(2),
                                   s.columnText(3) == QLatin1String("linked")};
    }
    return out;
}

Result<void> PageTextRepository::resetAll()
{
    return m_db.exec(QStringLiteral("UPDATE documents SET text_indexed = 0 WHERE text_indexed <> 2"));
}

Result<QPair<int, int>> PageTextRepository::progress()
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT COUNT(*) FILTER (WHERE text_indexed <> 0), COUNT(*) FROM documents "
        "WHERE deleted_at IS NULL AND format IN ('pdf', 'epub', 'txt', 'md')"));
    if (!stmt)
        return stmt.error();
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    return qMakePair(int(stmt.value().columnInt64(0)), int(stmt.value().columnInt64(1)));
}

Result<QString> PageTextRepository::text(qint64 documentId, int unit)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT content FROM page_text WHERE document_id = ?1 AND unit_index = ?2"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, documentId).bind(2, unit);
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    if (!row.value())
        return Error::make(Error::Code::NotFound, QStringLiteral("Pas de texte pour cette page"));
    return stmt.value().columnText(0);
}

}  // namespace studylab::db
