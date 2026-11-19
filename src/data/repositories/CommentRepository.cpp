#include "data/repositories/CommentRepository.h"

#include "core/Clock.h"
#include "core/Uuid.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"

#include <QJsonDocument>

#include <algorithm>

namespace studylab::db {

namespace {

const QString kSelect = QStringLiteral(
    "SELECT c.id, c.uuid, c.document_id, c.annotation_id, c.anchor_kind, c.locator_json, c.unit_index, c.kind, "
    "c.body_md, c.resolved, c.created_at, c.updated_at, a.selected_text, a.color, a.type, a.page_label, a.sort_key "
    "FROM comments c LEFT JOIN annotations a ON a.id = c.annotation_id ");

const QString kActive = QStringLiteral("c.deleted_at IS NULL AND (c.annotation_id IS NULL OR a.deleted_at IS NULL)");

CommentRecord readRow(const Statement& s)
{
    CommentRecord r;
    r.id = s.columnInt64(0);
    r.uuid = s.columnText(1);
    r.documentId = s.columnInt64(2);
    r.annotationId = s.isNull(3) ? 0 : s.columnInt64(3);
    r.anchor = commentAnchorFromKey(s.columnText(4)).value_or(CommentAnchor::Free);
    if (!s.isNull(5))
        r.locator = QJsonDocument::fromJson(s.columnText(5).toUtf8()).object();
    r.unitIndex = s.isNull(6) ? -1 : int(s.columnInt64(6));
    r.kind = commentKindFromKey(s.columnText(7)).value_or(CommentKind::Reflection);
    r.body = s.columnText(8);
    r.resolved = s.columnInt64(9) != 0;
    r.createdAt = s.columnText(10);
    r.updatedAt = s.columnText(11);
    if (r.anchor == CommentAnchor::Annotation && !s.isNull(13)) {
        r.quote = s.columnText(12);
        r.annotationColor = annotationColorFromKey(s.columnText(13)).value_or(AnnotationColor::Yellow);
        r.annotationType = annotationTypeFromKey(s.columnText(14)).value_or(AnnotationType::Highlight);
        r.pageLabel = s.columnText(15);
        r.sortKey = s.columnDouble(16);
    } else {
        r.pageLabel = r.locator.value(QStringLiteral("pageLabel")).toString();
        r.quote = r.locator.value(QStringLiteral("textQuote")).toObject().value(QStringLiteral("exact")).toString();
        r.sortKey = commentSortKey(r);
    }
    return r;
}

Result<QList<CommentRecord>> collect(Statement& stmt)
{
    QList<CommentRecord> out;
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

QString toJson(const QJsonObject& o)
{
    return o.isEmpty() ? QString() : QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

Error invalid(const QString& message) { return Error::make(Error::Code::InvalidArgument, message); }

}  // namespace

CommentRepository::CommentRepository(Database& db) : m_db(db) {}

Result<void> CommentRepository::insert(CommentRecord& r)
{
    r.body = r.body.trimmed();
    if (r.documentId <= 0)
        return invalid(QStringLiteral("Un commentaire exige un document."));
    if (r.body.isEmpty())
        return invalid(QStringLiteral("Le commentaire est vide."));
    switch (r.anchor) {
    case CommentAnchor::Free:
        r.annotationId = 0;
        r.locator = {};
        r.unitIndex = -1;
        break;
    case CommentAnchor::Annotation: {
        if (r.annotationId <= 0)
            return invalid(QStringLiteral("Annotation manquante."));
        auto existing = findForAnnotation(r.annotationId);
        if (!existing)
            return existing.error();
        if (existing.value())
            return invalid(QStringLiteral("Cette annotation a déjà un commentaire."));
        auto ann = m_db.prepare(QStringLiteral(
            "SELECT unit_index FROM annotations WHERE id = ?1 AND document_id = ?2 AND deleted_at IS NULL"));
        if (!ann)
            return ann.error();
        ann.value().bind(1, r.annotationId).bind(2, r.documentId);
        auto row = ann.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            return Error::make(Error::Code::NotFound, QStringLiteral("Annotation introuvable."));
        r.unitIndex = int(ann.value().columnInt64(0));
        r.locator = {};
        break;
    }
    case CommentAnchor::Passage:
    case CommentAnchor::Page:
        r.annotationId = 0;
        if (r.locator.isEmpty() || r.unitIndex < 0)
            return invalid(QStringLiteral("Un commentaire sur un passage ou une page exige une ancre."));
        break;
    }
    if (r.uuid.isEmpty())
        r.uuid = newUuid();
    r.createdAt = r.createdAt.isEmpty() ? nowIsoUtc() : r.createdAt;
    r.updatedAt = r.createdAt;

    auto stmt = m_db.prepare(QStringLiteral(
        "INSERT INTO comments(uuid, document_id, annotation_id, anchor_kind, locator_json, unit_index, kind, body_md, "
        "resolved, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?10)"));
    if (!stmt)
        return stmt.error();
    Statement& s = stmt.value();
    s.bind(1, r.uuid).bind(2, r.documentId);
    if (r.annotationId > 0)
        s.bind(3, r.annotationId);
    else
        s.bindNull(3);
    s.bind(4, commentAnchorKey(r.anchor)).bind(5, toJson(r.locator));
    if (r.unitIndex >= 0)
        s.bind(6, r.unitIndex);
    else
        s.bindNull(6);
    s.bind(7, commentKindKey(r.kind)).bind(8, r.body).bind(9, r.resolved ? 1 : 0).bind(10, r.createdAt);
    if (auto res = s.run(); !res)
        return res;
    r.id = m_db.lastInsertRowId();
    return {};
}

Result<void> CommentRepository::update(CommentRecord& r)
{
    r.body = r.body.trimmed();
    if (r.id <= 0)
        return invalid(QStringLiteral("Commentaire inconnu."));
    if (r.body.isEmpty())
        return invalid(QStringLiteral("Le commentaire est vide."));
    r.updatedAt = nowIsoUtc();
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE comments SET kind = ?2, body_md = ?3, resolved = ?4, updated_at = ?5 WHERE id = ?1 AND deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    if (auto res = stmt.value()
                       .bind(1, r.id)
                       .bind(2, commentKindKey(r.kind))
                       .bind(3, r.body)
                       .bind(4, r.resolved ? 1 : 0)
                       .bind(5, r.updatedAt)
                       .run();
        !res)
        return res;
    if (m_db.changes() == 0)
        return Error::make(Error::Code::NotFound, QStringLiteral("Commentaire introuvable."));
    return {};
}

Result<CommentRecord> CommentRepository::findById(qint64 id)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral("WHERE c.id = ?1"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, id);
    auto rows = collect(stmt.value());
    if (!rows)
        return rows.error();
    if (rows.value().isEmpty())
        return Error::make(Error::Code::NotFound, QStringLiteral("Commentaire introuvable."));
    return rows.value().first();
}

Result<std::optional<CommentRecord>> CommentRepository::findForAnnotation(qint64 annotationId)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral(
        "WHERE c.annotation_id = ?1 AND c.anchor_kind = 'annotation' AND c.deleted_at IS NULL "
        "ORDER BY c.updated_at DESC, c.id DESC LIMIT 1"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, annotationId);
    auto rows = collect(stmt.value());
    if (!rows)
        return rows.error();
    if (rows.value().isEmpty())
        return std::optional<CommentRecord>{};
    return std::optional<CommentRecord>{rows.value().first()};
}

Result<QList<CommentRecord>> CommentRepository::listForDocument(qint64 documentId)
{
    auto stmt = m_db.prepare(kSelect + QStringLiteral("WHERE c.document_id = ?1 AND ") + kActive);
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, documentId);
    auto rows = collect(stmt.value());
    if (!rows)
        return rows.error();
    QList<CommentRecord> list = rows.value();
    std::stable_sort(list.begin(), list.end(), [](const CommentRecord& a, const CommentRecord& b) {
        if (a.sortKey != b.sortKey)
            return a.sortKey < b.sortKey;
        return a.createdAt != b.createdAt ? a.createdAt < b.createdAt : a.id < b.id;
    });
    return list;
}

Result<int> CommentRepository::countForDocument(qint64 documentId)
{
    auto stmt = m_db.prepare(QStringLiteral("SELECT count(*) FROM comments c LEFT JOIN annotations a ON a.id = c.annotation_id "
                                            "WHERE c.document_id = ?1 AND ") + kActive);
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, documentId);
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    return int(stmt.value().columnInt64(0));
}

Result<void> CommentRepository::remove(qint64 id)
{
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE comments SET deleted_at = ?2, updated_at = ?2 WHERE id = ?1 AND deleted_at IS NULL"));
    if (!stmt)
        return stmt.error();
    if (auto res = stmt.value().bind(1, id).bind(2, nowIsoUtc()).run(); !res)
        return res;
    if (m_db.changes() == 0)
        return Error::make(Error::Code::NotFound, QStringLiteral("Commentaire introuvable."));
    return {};
}

Result<void> CommentRepository::restore(qint64 id)
{
    // Une annotation ne garde qu'un commentaire : on ne rétablit pas celui-ci si un autre l'a remplacé.
    auto stmt = m_db.prepare(QStringLiteral(
        "UPDATE comments SET deleted_at = NULL, updated_at = ?2 WHERE id = ?1 AND deleted_at IS NOT NULL "
        "AND (annotation_id IS NULL OR NOT EXISTS (SELECT 1 FROM comments o WHERE o.annotation_id = comments.annotation_id "
        "AND o.anchor_kind = 'annotation' AND o.deleted_at IS NULL))"));
    if (!stmt)
        return stmt.error();
    if (auto res = stmt.value().bind(1, id).bind(2, nowIsoUtc()).run(); !res)
        return res;
    if (m_db.changes() == 0)
        return Error::make(Error::Code::NotFound, QStringLiteral("Commentaire introuvable ou non supprimé."));
    return {};
}

}  // namespace studylab::db
