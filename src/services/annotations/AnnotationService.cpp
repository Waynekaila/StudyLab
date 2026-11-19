#include "services/annotations/AnnotationService.h"

#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"

namespace studylab {

AnnotationService::AnnotationService(db::Database& db, QObject* parent) : QObject(parent), m_db(db), m_repo(db)
{
    qRegisterMetaType<AnnotationRecord>();
}

Result<AnnotationRecord> AnnotationService::create(AnnotationRecord record)
{
    if (record.docHash.isEmpty()) {
        auto stmt = m_db.prepare(
            QStringLiteral("SELECT file_hash FROM documents WHERE id = ?1 AND deleted_at IS NULL"));
        if (!stmt)
            return stmt.error();
        stmt.value().bind(1, record.documentId);
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            return Error::make(Error::Code::NotFound, tr("Document introuvable."));
        record.docHash = stmt.value().columnText(0);
    }
    if (const auto loc = PdfLocator::fromJson(record.locator)) {
        record.unitIndex = loc->page;
        record.sortKey = annotationSortKey(loc->page, loc->relativeY());
    } else {
        record.sortKey = annotationSortKey(record.unitIndex, 0.0);
    }
    record.selectedText = normalizeQuote(record.selectedText);
    if (auto r = m_repo.insert(record); !r)
        return r.error();
    auto stored = m_repo.findById(record.id);
    if (stored)
        emit annotationAdded(stored.value());
    return stored;
}

Result<AnnotationRecord> AnnotationService::modify(qint64 id, const std::function<void(AnnotationRecord&)>& change)
{
    auto current = m_repo.findById(id);
    if (!current)
        return current.error();
    AnnotationRecord r = current.value();
    change(r);
    if (auto res = m_repo.update(r); !res)
        return res.error();
    auto stored = m_repo.findById(id);
    if (stored)
        emit annotationChanged(stored.value());
    return stored;
}

Result<AnnotationRecord> AnnotationService::setColor(qint64 id, AnnotationColor color)
{
    return modify(id, [color](AnnotationRecord& r) { r.color = color; });
}

Result<AnnotationRecord> AnnotationService::setType(qint64 id, AnnotationType type)
{
    return modify(id, [type](AnnotationRecord& r) { r.type = type; });
}

Result<AnnotationRecord> AnnotationService::setNote(qint64 id, const QString& note)
{
    if (auto res = m_repo.setNote(id, note); !res)
        return res.error();
    auto stored = m_repo.findById(id);
    if (stored)
        emit annotationChanged(stored.value());
    return stored;
}

Result<void> AnnotationService::remove(qint64 id)
{
    auto current = m_repo.findById(id);
    if (!current)
        return current.error();
    if (auto res = m_repo.remove(id); !res)
        return res;
    emit annotationRemoved(id, current.value().documentId);
    return {};
}

Result<AnnotationRecord> AnnotationService::restore(qint64 id)
{
    if (auto res = m_repo.restore(id); !res)
        return res.error();
    auto stored = m_repo.findById(id);
    if (stored)
        emit annotationAdded(stored.value());
    return stored;
}

Result<AnnotationRecord> AnnotationService::find(qint64 id)
{
    return m_repo.findById(id);
}

Result<QList<AnnotationRecord>> AnnotationService::list(qint64 documentId)
{
    return m_repo.listForDocument(documentId);
}

Result<int> AnnotationService::count(qint64 documentId)
{
    return m_repo.countForDocument(documentId);
}

Result<std::optional<AnnotationRecord>> AnnotationService::toggleBookmark(qint64 documentId, int page,
                                                                          const QString& pageLabel)
{
    auto existing = m_repo.bookmarkOnPage(documentId, page);
    if (!existing)
        return existing.error();
    if (existing.value()) {
        if (auto res = remove(existing.value()->id); !res)
            return res.error();
        return std::optional<AnnotationRecord>{};
    }
    AnnotationRecord r;
    r.documentId = documentId;
    r.type = AnnotationType::Bookmark;
    r.color = AnnotationColor::Blue;
    r.pageLabel = pageLabel;
    PdfLocator loc;
    loc.page = page;
    r.locator = loc.toJson();
    auto created = create(r);
    if (!created)
        return created.error();
    return std::optional<AnnotationRecord>{created.value()};
}

Result<bool> AnnotationService::hasBookmark(qint64 documentId, int page)
{
    auto existing = m_repo.bookmarkOnPage(documentId, page);
    if (!existing)
        return existing.error();
    return existing.value().has_value();
}

}  // namespace studylab
