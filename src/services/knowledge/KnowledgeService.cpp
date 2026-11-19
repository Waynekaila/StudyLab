#include "services/knowledge/KnowledgeService.h"

namespace studylab {

KnowledgeService::KnowledgeService(db::Database& db) : m_repo(db) {}

Result<KnowledgeGraph> KnowledgeService::graph() { return m_repo.load(); }

}  // namespace studylab
