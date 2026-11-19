#pragma once

#include "core/Result.h"
#include "core/domain/Graph.h"
#include "data/repositories/GraphRepository.h"

namespace studylab {

namespace db {
class Database;
}

/// Graphe de connaissances (02-architecture, services/knowledge) : construction depuis la table links, filtres et
/// voisinage (core/domain/Graph), disposition force-dirigée (ForceLayout) calculée hors du thread de l'interface.
class KnowledgeService {
public:
    explicit KnowledgeService(db::Database& db);

    /// Graphe complet : documents, notes, concepts et liens non résolus.
    Result<KnowledgeGraph> graph();

private:
    db::GraphRepository m_repo;
};

}  // namespace studylab
