#include "data/repositories/ReadingRepository.h"

#include "core/Clock.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"

#include <QJsonDocument>

namespace studylab::db {

namespace {
QString toJson(const QJsonObject& o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}
QJsonObject fromJson(const QString& s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}
}  // namespace

ReadingRepository::ReadingRepository(Database& db) : m_db(db) {}

Result<std::optional<ReadingPosition>> ReadingRepository::position(qint64 documentId)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "SELECT locator_json, progress, view_json, updated_at FROM reading_progress WHERE document_id = ?1"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, documentId);
    auto row = s.step();
    if (!row)
        return row.error();
    if (!row.value())
        return std::optional<ReadingPosition>{};
    ReadingPosition p;
    p.locator = fromJson(s.columnText(0));
    p.progress = s.columnDouble(1);
    p.view = fromJson(s.columnText(2));
    p.updatedAt = s.columnText(3);
    return std::optional<ReadingPosition>{p};
}

Result<void> ReadingRepository::savePosition(qint64 documentId, const ReadingPosition& p)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO reading_progress(document_id, locator_json, progress, view_json, updated_at) "
        "VALUES (?1, ?2, ?3, ?4, ?5) ON CONFLICT(document_id) DO UPDATE SET locator_json = excluded.locator_json, "
        "progress = excluded.progress, view_json = excluded.view_json, updated_at = excluded.updated_at"));
    if (!stmt)
        return stmt.error();
    return stmt.value()
        .bind(1, documentId)
        .bind(2, toJson(p.locator))
        .bind(3, qBound(0.0, p.progress, 1.0))
        .bind(4, toJson(p.view))
        .bind(5, nowIsoUtc())
        .run();
}

}  // namespace studylab::db
