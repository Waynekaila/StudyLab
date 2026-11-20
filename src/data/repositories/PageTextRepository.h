#pragma once

#include "core/Result.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace studylab::db {

class Database;

/// Document dont le texte reste à extraire.
struct PendingTextDocument {
    qint64 id = 0;
    QString format;       ///< « pdf », « epub »
    QString filePath;
    bool linked = false;  ///< filePath absolu (document lié) ; sinon relatif au dossier library/
};

/// Texte intégral des documents (table page_text, index fts_page_text tenu par triggers) et état d'indexation
/// (documents.text_indexed : 0 à faire, 1 texte natif, -1 échec ou rien à indexer).
class PageTextRepository {
public:
    explicit PageTextRepository(Database& db);

    /// Remplace le texte natif du document (une entrée par page ou section ; les vides sont omises) et le marque
    /// indexé. Une seule transaction.
    Result<void> replace(qint64 documentId, const QStringList& units);
    /// Marque l'échec (fichier illisible, protégé) : le document n'est plus proposé jusqu'à une réindexation.
    Result<void> markFailed(qint64 documentId);
    /// Documents PDF, EPUB, TXT et Markdown actifs à indexer, les plus récemment ajoutés d'abord.
    Result<QList<PendingTextDocument>> pending();
    /// Tout réindexer (texte conservé jusqu'au remplacement).
    Result<void> resetAll();
    /// Documents PDF/EPUB actifs : indexés, total.
    Result<QPair<int, int>> progress();
    Result<QString> text(qint64 documentId, int unit);

private:
    Database& m_db;
};

}  // namespace studylab::db
