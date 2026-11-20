#pragma once

#include "core/Result.h"
#include "core/domain/Reference.h"

#include <QList>

namespace studylab::db {

class Database;

/// Références bibliographiques (table references_, CSL-JSON).
class ReferenceRepository {
public:
    explicit ReferenceRepository(Database& db);

    Result<QList<Reference>> all();
    Result<Reference> get(qint64 id);
    /// Recherche sans casse ; NotFound si la clé n'existe pas.
    Result<Reference> byCitekey(const QString& citekey);
    Result<QList<Reference>> byDocument(qint64 documentId);
    Result<Reference> insert(Reference ref);
    Result<void> update(const Reference& ref);
    Result<void> remove(qint64 id);

private:
    Database& m_db;
};

}  // namespace studylab::db
