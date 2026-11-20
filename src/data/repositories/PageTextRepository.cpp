#include "data/repositories/PageTextRepository.h"

#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "data/sqlite/Transaction.h"

namespace studylab::db {

namespace {
// Une page dont le texte natif fait moins de 16 caractères (rien, un numéro de page, un tampon) est lue par l'OCR.
const QString kHasText = QStringLiteral("(p.source = 'ocr' OR length(trim(p.content)) >= 16)");

Result<QList<PendingTextDocument>> readPending(Database& db, const QString& sql)
{
    auto stmt = db.prepare(sql);
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

QString ocrPendingWhere()
{
    return QStringLiteral("d.deleted_at IS NULL AND d.format = 'pdf' AND d.text_indexed = 1 AND (d.page_count IS NULL "
                          "OR d.page_count > (SELECT COUNT(*) FROM page_text p WHERE p.document_id = d.id AND %1))")
        .arg(kHasText);
}
}  // namespace

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
    return readPending(m_db, QStringLiteral(
        "SELECT id, format, file_path, storage_mode FROM documents WHERE deleted_at IS NULL AND text_indexed = 0 "
        "AND format IN ('pdf', 'epub', 'txt', 'md') ORDER BY added_at DESC, id DESC"));
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

Result<QList<PendingTextDocument>> PageTextRepository::ocrPending()
{
    return readPending(m_db, QStringLiteral("SELECT d.id, d.format, d.file_path, d.storage_mode FROM documents d WHERE %1 "
                                            "ORDER BY d.added_at DESC, d.id DESC").arg(ocrPendingWhere()));
}

Result<QList<int>> PageTextRepository::unitsWithText(qint64 documentId)
{
    auto stmt = m_db.prepare(QStringLiteral("SELECT p.unit_index FROM page_text p WHERE p.document_id = ?1 AND %1 "
                                            "ORDER BY p.unit_index").arg(kHasText));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, documentId);
    QList<int> out;
    while (true) {
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out << int(stmt.value().columnInt64(0));
    }
    return out;
}

Result<void> PageTextRepository::storeOcr(qint64 documentId, int unit, const QString& content, const QString& wordsJson)
{
    if (content.trimmed().isEmpty())
        return {};
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO page_text(document_id, unit_index, source, content, words_json) VALUES (?1, ?2, 'ocr', ?3, ?4) "
        "ON CONFLICT(document_id, unit_index) DO UPDATE SET source = 'ocr', content = excluded.content, "
        "words_json = excluded.words_json WHERE page_text.source = 'ocr' OR length(trim(page_text.content)) < 16"));
    if (!stmt)
        return stmt.error();
    return stmt.value().bind(1, documentId).bind(2, unit).bind(3, content).bind(4, wordsJson).run();
}

Result<void> PageTextRepository::markOcrDone(qint64 documentId, bool failed)
{
    auto stmt = m_db.prepare(QStringLiteral("UPDATE documents SET text_indexed = ?2 WHERE id = ?1 AND text_indexed = 1"));
    if (!stmt)
        return stmt.error();
    return stmt.value().bind(1, documentId).bind(2, failed ? -2 : 2).run();
}

Result<QList<QPair<int, QString>>> PageTextRepository::ocrWords(qint64 documentId)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT unit_index, words_json FROM page_text WHERE document_id = ?1 AND source = 'ocr' AND words_json IS NOT NULL "
        "ORDER BY unit_index"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, documentId);
    QList<QPair<int, QString>> out;
    while (true) {
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        out << qMakePair(int(stmt.value().columnInt64(0)), stmt.value().columnText(1));
    }
    return out;
}

Result<void> PageTextRepository::resetOcr()
{
    Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    if (auto r = m_db.exec(QStringLiteral("DELETE FROM page_text WHERE source = 'ocr'")); !r)
        return r;
    if (auto r = m_db.exec(QStringLiteral("UPDATE documents SET text_indexed = 1 WHERE text_indexed IN (2, -2)")); !r)
        return r;
    return tx.commit();
}

Result<QPair<int, int>> PageTextRepository::ocrProgress()
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT (SELECT COUNT(*) FROM documents WHERE deleted_at IS NULL AND format = 'pdf' AND text_indexed IN (2, -2)), "
        "(SELECT COUNT(*) FROM documents d WHERE %1)").arg(ocrPendingWhere()));
    if (!stmt)
        return stmt.error();
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    return qMakePair(int(stmt.value().columnInt64(0)), int(stmt.value().columnInt64(1)));
}

}  // namespace studylab::db
