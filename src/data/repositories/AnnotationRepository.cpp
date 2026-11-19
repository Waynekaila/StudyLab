#include "data/repositories/AnnotationRepository.h"

#include "core/Clock.h"
#include "core/Uuid.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "data/sqlite/Transaction.h"

#include <QJsonDocument>

namespace studylab::db {

namespace {

QString toJson(const QJsonObject& o)
{
    return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString orNull(const QString& s)
{
    return s.isEmpty() ? QString() : s;
}

// Colonnes lues par readRow(), dans l'ordre. La note est le commentaire lié le plus récent.
const QString kSelect = QStringLiteral(
    "SELECT a.id, a.uuid, a.document_id, a.type, a.color, a.unit_index, a.page_label, a.locator_json, a.sort_key, "
    "a.selected_text, a.title, a.anchor_status, a.doc_hash, a.created_at, a.updated_at, "
    "(SELECT c.body_md FROM comments c WHERE c.annotation_id = a.id AND c.anchor_kind = 'annotation' "
    "   AND c.deleted_at IS NULL ORDER BY c.updated_at DESC, c.id DESC LIMIT 1) "
    "FROM annotations a ");

AnnotationRecord readRow(const Statement& s)
{
    AnnotationRecord r;
    r.id = s.columnInt64(0);
    r.uuid = s.columnText(1);
    r.documentId = s.columnInt64(2);
    r.type = annotationTypeFromKey(s.columnText(3)).value_or(AnnotationType::Highlight);
    r.color = annotationColorFromKey(s.columnText(4)).value_or(AnnotationColor::Yellow);
    r.unitIndex = int(s.columnInt64(5));
    r.pageLabel = s.columnText(6);
    r.locator = QJsonDocument::fromJson(s.columnText(7).toUtf8()).object();
    r.sortKey = s.columnDouble(8);
    r.selectedText = s.columnText(9);
    r.title = s.columnText(10);
    r.anchorStatus = s.columnText(11);
    r.docHash = s.columnText(12);
    r.createdAt = s.columnText(13);
    r.updatedAt = s.columnText(14);
    r.note = s.columnText(15);
    return r;
}

Result<QList<AnnotationRecord>> collect(Statement& stmt)
{
    QList<AnnotationRecord> out;
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

Result<void> validate(const AnnotationRecord& r)
{
    if (r.documentId <= 0 || r.docHash.isEmpty() || r.locator.isEmpty())
        return Error::make(Error::Code::InvalidArgument,
                           QStringLiteral("Une annotation exige un document, l'empreinte du fichier et une ancre."));
    return {};
}

}  // namespace

AnnotationRepository::AnnotationRepository(Database& db) : m_db(db) {}

Result<void> AnnotationRepository::insert(AnnotationRecord& r)
{
    if (auto v = validate(r); !v)
        return v;
    if (r.uuid.isEmpty())
        r.uuid = newUuid();
    r.createdAt = r.createdAt.isEmpty() ? nowIsoUtc() : r.createdAt;
    r.updatedAt = r.createdAt;

    Transaction tx(m_db);
    if (!tx.isActive())
        return tx.beginError();
    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO annotations(uuid, document_id, type, color, unit_index, page_label, locator_json, sort_key, "
        "selected_text, title, anchor_status, doc_hash, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, "
        "?8, ?9, ?10, ?11, ?12, ?13, ?13)"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, r.uuid)
        .bind(2, r.documentId)
        .bind(3, annotationTypeKey(r.type))
        .bind(4, annotationColorKey(r.color))
        .bind(5, r.unitIndex)
        .bind(6, orNull(r.pageLabel))
        .bind(7, toJson(r.locator))
        .bind(8, r.sortKey)
        .bind(9, orNull(r.selectedText))
        .bind(10, orNull(r.title))
        .bind(11, r.anchorStatus.isEmpty() ? QStringLiteral("ok") : r.anchorStatus)
        .bind(12, r.docHash)
        .bind(13, r.createdAt);
    if (auto res = s.run(); !res)
        return res;
    r.id = m_db.lastInsertRowId();
    if (!r.note.trimmed().isEmpty()) {
        if (auto res = setNote(r.id, r.note); !res)
            return res;
    }
    return tx.commit();
}

Result<void> AnnotationRepository::update(AnnotationRecord& r)
{
    if (r.id <= 0)
        return Error::make(Error::Code::InvalidArgument, QStringLiteral("Annotation inconnue."));
    if (auto v = validate(r); !v)
        return v;
    r.updatedAt = nowIsoUtc();
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE annotations SET type = ?2, color = ?3, unit_index = ?4, page_label = ?5, locator_json = ?6, "
        "sort_key = ?7, selected_text = ?8, title = ?9, anchor_status = ?10, updated_at = ?11 "
        "WHERE id = ?1 AND deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    if (auto res = stmt.value()
                       .bind(1, r.id)
                       .bind(2, annotationTypeKey(r.type))
                       .bind(3, annotationColorKey(r.color))
                       .bind(4, r.unitIndex)
                       .bind(5, orNull(r.pageLabel))
                       .bind(6, toJson(r.locator))
                       .bind(7, r.sortKey)
                       .bind(8, orNull(r.selectedText))
                       .bind(9, orNull(r.title))
                       .bind(10, r.anchorStatus)
                       .bind(11, r.updatedAt)
                       .run();
        !res)
        return res;
    if (m_db.changes() == 0)
        return Error::make(Error::Code::NotFound, QStringLiteral("Annotation introuvable."));
    return {};
}

Result<void> AnnotationRepository::setNote(qint64 annotationId, const QString& body)
{
    const QString text = body.trimmed();
    const QString now = nowIsoUtc();
    auto existing = m_db.prepare(QStringLiteral(
        "SELECT c.id FROM comments c WHERE c.annotation_id = ?1 AND c.anchor_kind = 'annotation' "
        "AND c.deleted_at IS NULL ORDER BY c.updated_at DESC, c.id DESC LIMIT 1"));
    if (!existing)
        return existing.error();
    existing.value().bind(1, annotationId);
    auto row = existing.value().step();
    if (!row)
        return row.error();
    const qint64 commentId = row.value() ? existing.value().columnInt64(0) : 0;

    if (text.isEmpty()) {
        if (commentId == 0)
            return {};
        auto del = m_db.prepare(QStringLiteral("UPDATE comments SET deleted_at = ?2, updated_at = ?2 WHERE id = ?1"));
        if (!del)
            return del.error();
        return del.value().bind(1, commentId).bind(2, now).run();
    }
    if (commentId != 0) {
        auto upd = m_db.prepare(QStringLiteral("UPDATE comments SET body_md = ?2, updated_at = ?3 WHERE id = ?1"));
        if (!upd)
            return upd.error();
        return upd.value().bind(1, commentId).bind(2, text).bind(3, now).run();
    }
    auto ins = m_db.prepare(QStringLiteral(
        "INSERT INTO comments(uuid, document_id, annotation_id, anchor_kind, unit_index, kind, body_md, created_at, "
        "updated_at) SELECT ?1, a.document_id, a.id, 'annotation', a.unit_index, 'reflection', ?2, ?3, ?3 "
        "FROM annotations a WHERE a.id = ?4"));
    if (!ins)
        return ins.error();
    if (auto res = ins.value().bind(1, newUuid()).bind(2, text).bind(3, now).bind(4, annotationId).run(); !res)
        return res;
    if (m_db.changes() == 0)
        return Error::make(Error::Code::NotFound, QStringLiteral("Annotation introuvable."));
    return {};
}

Result<AnnotationRecord> AnnotationRepository::findById(qint64 id)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral("WHERE a.id = ?1"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, id);
    auto rows = collect(stmt.value());
    if (!rows)
        return rows.error();
    if (rows.value().isEmpty())
        return Error::make(Error::Code::NotFound, QStringLiteral("Annotation introuvable."));
    return rows.value().first();
}

Result<QList<AnnotationRecord>> AnnotationRepository::listForDocument(qint64 documentId)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral(
        "WHERE a.document_id = ?1 AND a.deleted_at IS NULL ORDER BY a.sort_key, a.id"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, documentId);
    return collect(stmt.value());
}

Result<int> AnnotationRepository::countForDocument(qint64 documentId)
{
    auto stmt = m_db.prepare(
        QStringLiteral("SELECT count(*) FROM annotations WHERE document_id = ?1 AND deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, documentId);
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    return int(stmt.value().columnInt64(0));
}

Result<std::optional<AnnotationRecord>> AnnotationRepository::bookmarkOnPage(qint64 documentId, int unitIndex)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral(
        "WHERE a.document_id = ?1 AND a.unit_index = ?2 AND a.type = 'bookmark' AND a.deleted_at IS NULL "
        "ORDER BY a.id LIMIT 1"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, documentId).bind(2, unitIndex);
    auto rows = collect(stmt.value());
    if (!rows)
        return rows.error();
    if (rows.value().isEmpty())
        return std::optional<AnnotationRecord>{};
    return std::optional<AnnotationRecord>{rows.value().first()};
}

Result<void> AnnotationRepository::remove(qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE annotations SET deleted_at = ?2, updated_at = ?2 WHERE id = ?1 AND deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    if (auto res = stmt.value().bind(1, id).bind(2, nowIsoUtc()).run(); !res)
        return res;
    if (m_db.changes() == 0)
        return Error::make(Error::Code::NotFound, QStringLiteral("Annotation introuvable."));
    return {};
}

Result<void> AnnotationRepository::restore(qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE annotations SET deleted_at = NULL, updated_at = ?2 WHERE id = ?1 AND deleted_at IS NOT NULL"));
    if (!stmt)
        return stmt.error();
    if (auto res = stmt.value().bind(1, id).bind(2, nowIsoUtc()).run(); !res)
        return res;
    if (m_db.changes() == 0)
        return Error::make(Error::Code::NotFound, QStringLiteral("Annotation introuvable ou non supprimée."));
    return {};
}

}  // namespace studylab::db
