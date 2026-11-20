#include "data/repositories/ReferenceRepository.h"

#include "core/Clock.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"

#include <QCoreApplication>
#include <QJsonDocument>

namespace studylab::db {

namespace {

const QString kSelect = QStringLiteral(
    "SELECT r.id, r.document_id, r.citekey, r.csl_json, r.created_at FROM references_ r ");

Reference readRow(const Statement& s)
{
    Reference r = fromCslJson(QJsonDocument::fromJson(s.columnText(3).toUtf8()).object());
    r.id = s.columnInt64(0);
    r.documentId = s.isNull(1) ? 0 : s.columnInt64(1);
    r.citekey = s.columnText(2);
    r.createdAt = s.columnText(4);
    return r;
}

Result<QList<Reference>> collect(Statement& stmt)
{
    QList<Reference> out;
    while (true) {
        auto row = stmt.step();
        if (!row)
            return row.error();
        if (!row.value())
            return out;
        Reference r = readRow(stmt);
        out << r;
    }
}

Error notFound()
{
    return Error::make(Error::Code::NotFound, QCoreApplication::translate("References", "Référence introuvable."));
}

QString cslText(const Reference& r)
{
    return QString::fromUtf8(QJsonDocument(toCslJson(r)).toJson(QJsonDocument::Compact));
}

}  // namespace

ReferenceRepository::ReferenceRepository(Database& db) : m_db(db) {}

Result<QList<Reference>> ReferenceRepository::all()
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral("ORDER BY r.citekey COLLATE NOCASE"));
    if (!stmt)
        return stmt.error();
    return collect(stmt.value());
}

Result<Reference> ReferenceRepository::get(qint64 id)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral("WHERE r.id = ?1"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, id);
    auto rows = collect(stmt.value());
    if (!rows)
        return rows.error();
    if (rows.value().isEmpty())
        return notFound();
    return rows.value().first();
}

Result<Reference> ReferenceRepository::byCitekey(const QString& citekey)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral("WHERE r.citekey = ?1 COLLATE NOCASE"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, citekey);
    auto rows = collect(stmt.value());
    if (!rows)
        return rows.error();
    if (rows.value().isEmpty())
        return notFound();
    return rows.value().first();
}

Result<QList<Reference>> ReferenceRepository::byDocument(qint64 documentId)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral("WHERE r.document_id = ?1 ORDER BY r.id"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, documentId);
    return collect(stmt.value());
}

Result<Reference> ReferenceRepository::insert(Reference ref)
{
    ref.createdAt = ref.createdAt.isEmpty() ? nowIsoUtc() : ref.createdAt;
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO references_ (document_id, citekey, csl_json, created_at) VALUES (?1, ?2, ?3, ?4)"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    if (ref.documentId > 0)
        s.bind(1, ref.documentId);
    else
        s.bindNull(1);
    s.bind(2, ref.citekey).bind(3, cslText(ref)).bind(4, ref.createdAt);
    if (auto r = s.run(); !r)
        return r.error();
    ref.id = m_db.lastInsertRowId();
    return ref;
}

Result<void> ReferenceRepository::update(const Reference& ref)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE references_ SET document_id = ?2, citekey = ?3, csl_json = ?4 WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, ref.id);
    if (ref.documentId > 0)
        s.bind(2, ref.documentId);
    else
        s.bindNull(2);
    s.bind(3, ref.citekey).bind(4, cslText(ref));
    if (auto r = s.run(); !r)
        return r;
    if (m_db.changes() == 0)
        return notFound();
    return {};
}

Result<void> ReferenceRepository::remove(qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral("DELETE FROM references_ WHERE id = ?1"));
    if (!stmt)
        return stmt.error();
    if (auto r = stmt.value().bind(1, id).run(); !r)
        return r;
    if (m_db.changes() == 0)
        return notFound();
    return {};
}

}  // namespace studylab::db
