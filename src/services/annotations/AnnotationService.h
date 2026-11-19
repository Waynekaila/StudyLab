#pragma once

#include "core/Result.h"
#include "core/domain/Annotation.h"
#include "data/repositories/AnnotationRepository.h"

#include <QObject>

#include <functional>

#include <optional>

namespace studylab {

namespace db {
class Database;
}

/// Création, modification et suppression (annulable) des annotations, avec notification des vues
/// (lecteur, volet Annotations) pour une mise à jour immédiate.
class AnnotationService : public QObject {
    Q_OBJECT
public:
    explicit AnnotationService(db::Database& db, QObject* parent = nullptr);

    /// Crée l'annotation : l'empreinte du fichier et la clé de tri sont renseignées ici.
    Result<AnnotationRecord> create(AnnotationRecord record);
    Result<AnnotationRecord> setColor(qint64 id, AnnotationColor color);
    Result<AnnotationRecord> setType(qint64 id, AnnotationType type);
    Result<AnnotationRecord> setNote(qint64 id, const QString& note);
    /// Retrait logique ; restore() l'annule.
    Result<void> remove(qint64 id);
    Result<AnnotationRecord> restore(qint64 id);

    Result<AnnotationRecord> find(qint64 id);
    Result<QList<AnnotationRecord>> list(qint64 documentId);
    /// Dernières annotations de toute la bibliothèque (accueil).
    Result<QList<AnnotationRecord>> recent(int limit);
    Result<int> count(qint64 documentId);

    /// Ajoute un signet sur la page, ou retire celui qui existe. Renvoie le signet s'il a été créé.
    Result<std::optional<AnnotationRecord>> toggleBookmark(qint64 documentId, int page, const QString& pageLabel);
    Result<bool> hasBookmark(qint64 documentId, int page);

signals:
    void annotationAdded(const studylab::AnnotationRecord& record);
    void annotationChanged(const studylab::AnnotationRecord& record);
    void annotationRemoved(qint64 id, qint64 documentId);

private:
    Result<AnnotationRecord> modify(qint64 id, const std::function<void(AnnotationRecord&)>& change);

    db::Database& m_db;
    db::AnnotationRepository m_repo;
};

}  // namespace studylab
