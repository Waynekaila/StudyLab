#pragma once

#include "core/Result.h"
#include "core/domain/Organize.h"

#include <QHash>
#include <QList>
#include <QString>

namespace studylab::db {

class Database;

/// Accès aux matières (subjects, subject_items) et aux collections (collections, collection_items).
/// Les documents et notes supprimés (corbeille) restent rattachés mais ne sont jamais comptés ni listés.
class OrganizeRepository {
public:
    explicit OrganizeRepository(Database& db);

    // ── Matières ──
    Result<void> insertSubject(SubjectRecord& s);
    /// Nom, description, couleur, parent, position, archivage.
    Result<void> updateSubject(const SubjectRecord& s);
    /// Supprime la matière ; ses sous-matières remontent à son parent, ses rattachements disparaissent.
    Result<void> removeSubject(qint64 id);
    /// Toutes les matières, par position puis par nom.
    Result<QList<SubjectRecord>> subjects();

    Result<void> attach(qint64 subjectId, const QString& entityType, qint64 entityId);
    Result<void> detach(qint64 subjectId, const QString& entityType, qint64 entityId);
    /// Matières directement rattachées à un document ou une note.
    Result<QList<qint64>> subjectsOf(const QString& entityType, qint64 entityId);
    /// Documents ou notes actifs rattachés à l'une des matières (sans doublon).
    Result<QList<qint64>> itemsOf(const QList<qint64>& subjectIds, const QString& entityType);
    /// Chiffres clés des documents et notes rattachés à l'une des matières.
    Result<SubjectStats> stats(const QList<qint64>& subjectIds);
    /// Nombre d'éléments actifs (documents + notes) directement rattachés, par matière.
    Result<QHash<qint64, int>> directCounts();

    // ── Collections ──
    Result<void> insertCollection(CollectionRecord& c);
    Result<void> updateCollection(CollectionRecord& c);
    Result<void> removeCollection(qint64 id);
    Result<QList<CollectionRecord>> collections();

    /// Éléments actifs d'une collection, dans l'ordre (position puis ajout).
    Result<QList<CollectionItem>> items(qint64 collectionId);
    /// Ajoute à la fin ; sans effet si l'élément y est déjà.
    Result<void> addItem(qint64 collectionId, CollectionItem::Type type, qint64 id);
    Result<void> removeItem(qint64 collectionId, CollectionItem::Type type, qint64 id);
    /// Nouvel ordre d'une liste de lecture (éléments absents de la liste : inchangés).
    Result<void> reorder(qint64 collectionId, const QList<CollectionItem>& order);
    /// Collections manuelles qui contiennent l'élément.
    Result<QList<qint64>> collectionsOf(CollectionItem::Type type, qint64 id);

    /// Documents actifs qui respectent les règles, par titre. subjectScope : uuid de matière →
    /// identifiants de la matière et de ses sous-matières (fourni par le service).
    Result<QList<qint64>> smartDocuments(const SmartQuery& query, const QHash<QString, QList<qint64>>& subjectScope,
                                         const QString& now);

private:
    Database& m_db;
};

QString collectionItemTypeKey(CollectionItem::Type t);  ///< « document », « note »

}  // namespace studylab::db
