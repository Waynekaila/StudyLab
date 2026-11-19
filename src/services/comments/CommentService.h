#pragma once

#include "core/Result.h"
#include "core/domain/Comment.h"
#include "data/repositories/CommentRepository.h"

#include <QObject>

#include <functional>
#include <optional>

namespace studylab {

namespace db {
class Database;
}

/// Commentaires d'un document : création, modification, suppression annulable, avec notification
/// des vues (volet Commentaires, volet Annotations pour la note d'une annotation).
class CommentService : public QObject {
    Q_OBJECT
public:
    explicit CommentService(db::Database& db, QObject* parent = nullptr);

    Result<CommentRecord> create(CommentRecord record);
    Result<CommentRecord> update(qint64 id, CommentKind kind, const QString& body);
    Result<CommentRecord> setResolved(qint64 id, bool resolved);
    /// Retrait logique ; restore() l'annule.
    Result<void> remove(qint64 id);
    Result<CommentRecord> restore(qint64 id);

    Result<CommentRecord> find(qint64 id);
    Result<std::optional<CommentRecord>> forAnnotation(qint64 annotationId);
    Result<QList<CommentRecord>> list(qint64 documentId);
    Result<int> count(qint64 documentId);

signals:
    void commentAdded(const studylab::CommentRecord& record);
    void commentChanged(const studylab::CommentRecord& record);
    void commentRemoved(qint64 id, qint64 documentId, qint64 annotationId);

private:
    Result<CommentRecord> modify(qint64 id, const std::function<void(CommentRecord&)>& change);

    db::CommentRepository m_repo;
};

}  // namespace studylab
