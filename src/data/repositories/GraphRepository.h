#pragma once

#include "core/Result.h"
#include "core/domain/Graph.h"

namespace studylab::db {

class Database;

/// Lecture du graphe de connaissances : documents et notes actifs, liens [[…]] des notes (table links,
/// origin = 'wikilink') et provenance des notes tirées de la lecture (kind = 'derived_from'), rattachements aux
/// matières non archivées. Lecture seule.
class GraphRepository {
public:
    explicit GraphRepository(Database& db);

    /// Graphe complet. Les liens non résolus deviennent des nœuds « u:<clé> » (une par cible distincte).
    Result<KnowledgeGraph> load();

private:
    Database& m_db;
};

}  // namespace studylab::db
