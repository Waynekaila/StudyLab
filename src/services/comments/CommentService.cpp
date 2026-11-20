#include "services/comments/CommentService.h"

#include "data/repositories/ActivityRepository.h"

namespace studylab {

CommentService::CommentService(db::Database& db, QObject* parent) : QObject(parent), m_db(db), m_repo(db)
{
    qRegisterMetaType<CommentRecord>();
}

Result<CommentRecord> CommentService::create(CommentRecord record)
{
    if (auto r = m_repo.insert(record); !r)
        return r.error();
    (void)db::ActivityRepository(m_db).log(QStringLiteral("commented"), QStringLiteral("comment"), record.id,
                                           {{QStringLiteral("document"), record.documentId}});
    auto stored = m_repo.findById(record.id);
    if (stored)
        emit commentAdded(stored.value());
    return stored;
}

Result<CommentRecord> CommentService::modify(qint64 id, const std::function<void(CommentRecord&)>& change)
{
    auto current = m_repo.findById(id);
    if (!current)
        return current.error();
    CommentRecord r = current.value();
    change(r);
    if (auto res = m_repo.update(r); !res)
        return res.error();
    auto stored = m_repo.findById(id);
    if (stored)
        emit commentChanged(stored.value());
    return stored;
}

Result<CommentRecord> CommentService::update(qint64 id, CommentKind kind, const QString& body)
{
    return modify(id, [&](CommentRecord& r) {
        r.kind = kind;
        r.body = body;
        if (kind != CommentKind::Question)
            r.resolved = false;
    });
}

Result<CommentRecord> CommentService::setResolved(qint64 id, bool resolved)
{
    return modify(id, [resolved](CommentRecord& r) { r.resolved = resolved; });
}

Result<void> CommentService::remove(qint64 id)
{
    auto current = m_repo.findById(id);
    if (!current)
        return current.error();
    if (auto r = m_repo.remove(id); !r)
        return r;
    emit commentRemoved(id, current.value().documentId, current.value().annotationId);
    return {};
}

Result<CommentRecord> CommentService::restore(qint64 id)
{
    if (auto r = m_repo.restore(id); !r)
        return r.error();
    auto stored = m_repo.findById(id);
    if (stored)
        emit commentAdded(stored.value());
    return stored;
}

Result<CommentRecord> CommentService::find(qint64 id) { return m_repo.findById(id); }
Result<std::optional<CommentRecord>> CommentService::forAnnotation(qint64 annotationId)
{
    return m_repo.findForAnnotation(annotationId);
}
Result<QList<CommentRecord>> CommentService::list(qint64 documentId) { return m_repo.listForDocument(documentId); }

Result<QList<CommentRecord>> CommentService::recent(int limit) { return m_repo.listRecent(limit); }
Result<int> CommentService::count(qint64 documentId) { return m_repo.countForDocument(documentId); }

}  // namespace studylab
