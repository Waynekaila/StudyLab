#pragma once

#include "core/Result.h"
#include "core/domain/Annotation.h"

#include <QList>

#include <optional>

namespace studylab::db {

class Database;

/// Accès aux annotations (table annotations) et à leur note (commentaire lié, anchor_kind = 'annotation').
/// La suppression est logique (deleted_at) : elle s'annule avec restore().
class AnnotationRepository {
public:
    explicit AnnotationRepository(Database& db);

    /// Insère l'annotation (et sa note si non vide). Renseigne id, uuid, createdAt, updatedAt.
    Result<void> insert(AnnotationRecord& record);
    /// Met à jour type, couleur, titre, texte cité, ancre et clé de tri.
    Result<void> update(AnnotationRecord& record);
    /// Remplace la note de l'annotation ; une note vide supprime la note existante.
    Result<void> setNote(qint64 annotationId, const QString& body);

    Result<AnnotationRecord> findById(qint64 id);
    /// Annotations actives d'un document, dans l'ordre de lecture.
    Result<QList<AnnotationRecord>> listForDocument(qint64 documentId);
    /// Dernières annotations de la bibliothèque (hors signets et documents retirés), les plus récentes d'abord.
    Result<QList<AnnotationRecord>> listRecent(int limit);
    Result<int> countForDocument(qint64 documentId);
    /// Signet de la page, s'il existe.
    Result<std::optional<AnnotationRecord>> bookmarkOnPage(qint64 documentId, int unitIndex);

    Result<void> remove(qint64 id);
    Result<void> restore(qint64 id);

private:
    Database& m_db;
};

}  // namespace studylab::db
