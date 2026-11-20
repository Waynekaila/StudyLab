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
    QString format;       ///< « pdf », « epub », « txt », « md »
    QString filePath;
    bool linked = false;  ///< filePath absolu (document lié) ; sinon relatif au dossier library/
};

/// Texte intégral des documents (table page_text, index fts_page_text tenu par triggers) et état d'indexation
/// (documents.text_indexed : 0 à faire, 1 texte natif, 2 pages sans texte passées à l'OCR, -1 échec ou rien à
/// indexer, -2 OCR impossible).
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

    // OCR (V1-7b) : les pages d'un PDF sans texte natif reçoivent le texte reconnu (source « ocr »).
    /// PDF indexés dont des pages n'ont pas de texte, les plus récemment ajoutés d'abord.
    Result<QList<PendingTextDocument>> ocrPending();
    /// Pages du document qui ont déjà un texte (natif ou OCR).
    Result<QList<int>> unitsWithText(qint64 documentId);
    /// Texte reconnu d'une page et ses mots (JSON d'OcrPage) ; une page qui a déjà un texte natif est laissée.
    Result<void> storeOcr(qint64 documentId, int unit, const QString& content, const QString& wordsJson);
    /// Fin de l'OCR du document : text_indexed = 2 (ou -2 si `failed`).
    Result<void> markOcrDone(qint64 documentId, bool failed = false);
    /// Mots reconnus de chaque page OCR du document (page → JSON).
    Result<QList<QPair<int, QString>>> ocrWords(qint64 documentId);
    /// Oublie tout le texte reconnu : les PDF concernés repassent dans la file d'OCR.
    Result<void> resetOcr();
    /// PDF passés à l'OCR, PDF en attente d'OCR.
    Result<QPair<int, int>> ocrProgress();

private:
    Database& m_db;
};

}  // namespace studylab::db
