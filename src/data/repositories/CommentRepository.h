#pragma once

#include "core/Result.h"
#include "core/domain/Comment.h"

#include <QList>

#include <optional>

namespace studylab::db {

class Database;

/// Accès aux commentaires (table comments). Une annotation porte au plus un commentaire actif, qui est
/// aussi sa note (AnnotationRepository::setNote écrit la même ligne). Suppression logique (deleted_at).
class CommentRepository {
public:
    explicit CommentRepository(Database& db);

    /// Insère le commentaire. Renseigne id, uuid, createdAt, updatedAt (et unitIndex pour une annotation).
    Result<void> insert(CommentRecord& record);
    /// Met à jour type, texte et état « résolue ». L'ancre ne change pas.
    Result<void> update(CommentRecord& record);

    Result<CommentRecord> findById(qint64 id);
    /// Commentaire actif d'une annotation, s'il existe.
    Result<std::optional<CommentRecord>> findForAnnotation(qint64 annotationId);
    /// Commentaires actifs d'un document : libres d'abord, puis dans l'ordre de lecture.
    /// Ceux d'une annotation supprimée sont masqués (ils reviennent si la suppression est annulée).
    Result<QList<CommentRecord>> listForDocument(qint64 documentId);
    /// Derniers commentaires écrits ou modifiés dans la bibliothèque, les plus récents d'abord.
    Result<QList<CommentRecord>> listRecent(int limit);
    Result<int> countForDocument(qint64 documentId);

    Result<void> remove(qint64 id);
    Result<void> restore(qint64 id);

private:
    Database& m_db;
};

}  // namespace studylab::db
