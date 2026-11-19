#pragma once

#include "core/Result.h"
#include "core/domain/Organize.h"
#include "data/repositories/OrganizeRepository.h"

#include <QHash>
#include <QObject>

namespace studylab {

namespace db {
class Database;
}

/// Matières hiérarchiques et collections (05-maquettes §6).
///
/// Une matière regroupe des documents et des notes ; ses chiffres et ses listes comprennent ceux de ses
/// sous-matières. Une collection est manuelle (ordre libre : liste de lecture, projet) ou intelligente : des règles
/// lisibles (« Statut n'est pas Terminé ») évaluées à chaque affichage sur les documents de la bibliothèque.
class OrganizeService : public QObject {
    Q_OBJECT
public:
    enum class Entity { Document, Note };

    explicit OrganizeService(db::Database& db, QObject* parent = nullptr);

    // ── Matières ──
    Result<QList<SubjectRecord>> subjects();
    Result<SubjectRecord> subject(qint64 id);
    /// Crée une matière à la fin de son parent (0 = premier niveau). Couleur vide : la suivante du nuancier.
    Result<SubjectRecord> createSubject(const QString& name, qint64 parentId = 0, const QString& color = {});
    Result<SubjectRecord> renameSubject(qint64 id, const QString& name);
    Result<SubjectRecord> setSubjectColor(qint64 id, const QString& color);
    Result<SubjectRecord> setSubjectDescription(qint64 id, const QString& description);
    Result<SubjectRecord> setSubjectArchived(qint64 id, bool archived);
    /// Déplace sous un autre parent, à la position donnée (-1 = à la fin). Refuse de placer une matière sous
    /// l'une de ses propres sous-matières.
    Result<void> moveSubject(qint64 id, qint64 parentId, int position = -1);
    /// Supprime la matière (les documents et notes restent) ; ses sous-matières remontent d'un niveau.
    Result<void> removeSubject(qint64 id);

    /// La matière et toutes ses sous-matières.
    QList<qint64> withDescendants(qint64 id, const QList<SubjectRecord>& all) const;
    /// « Physique › Thermodynamique ».
    QString subjectPath(qint64 id, const QList<SubjectRecord>& all) const;

    Result<void> attach(qint64 subjectId, Entity entity, qint64 entityId);
    Result<void> detach(qint64 subjectId, Entity entity, qint64 entityId);
    /// Remplace les matières d'un document ou d'une note.
    Result<void> setSubjects(Entity entity, qint64 entityId, const QList<qint64>& subjectIds);
    Result<QList<qint64>> subjectsOf(Entity entity, qint64 entityId);

    /// Documents ou notes de la matière et de ses sous-matières.
    Result<QList<qint64>> subjectItems(qint64 subjectId, Entity entity);
    Result<SubjectStats> subjectStats(qint64 subjectId);
    /// Nombre d'éléments par matière, sous-matières comprises (compteurs de la liste).
    Result<QHash<qint64, int>> subjectCounts();

    // ── Collections ──
    Result<QList<CollectionRecord>> collections();
    Result<CollectionRecord> collection(qint64 id);
    Result<CollectionRecord> createCollection(const QString& name, CollectionKind kind, const SmartQuery& query = {});
    Result<CollectionRecord> renameCollection(qint64 id, const QString& name);
    Result<CollectionRecord> setQuery(qint64 id, const SmartQuery& query);
    Result<void> removeCollection(qint64 id);

    /// Éléments d'une collection : règles évaluées pour une collection intelligente (documents seulement).
    Result<QList<CollectionItem>> collectionItems(qint64 id);
    /// Nombre d'éléments par collection.
    Result<QHash<qint64, int>> collectionCounts();
    /// Documents qui respectent des règles (aperçu en direct de l'éditeur).
    Result<QList<qint64>> evaluate(const SmartQuery& query);

    Result<void> addToCollection(qint64 collectionId, Entity entity, qint64 entityId);
    Result<void> removeFromCollection(qint64 collectionId, Entity entity, qint64 entityId);
    Result<void> reorder(qint64 collectionId, const QList<CollectionItem>& order);
    Result<QList<qint64>> collectionsOf(Entity entity, qint64 entityId);

    static QString entityKey(Entity e);

signals:
    void subjectsChanged();
    void collectionsChanged();
    /// Rattachements modifiés (matières ou collections d'un document ou d'une note).
    void membershipChanged();

private:
    Result<SubjectRecord> modifySubject(qint64 id, const std::function<void(SubjectRecord&)>& change);
    Result<CollectionRecord> modifyCollection(qint64 id, const std::function<void(CollectionRecord&)>& change);
    QHash<QString, QList<qint64>> subjectScope(const QList<SubjectRecord>& all) const;

    db::Database& m_db;
    db::OrganizeRepository m_repo;
};

}  // namespace studylab
