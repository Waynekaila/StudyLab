#pragma once

#include "core/AppLink.h"
#include "core/Result.h"
#include "core/domain/Annotation.h"
#include "core/domain/Document.h"

#include <QList>
#include <QString>

namespace studylab {

namespace db {
class Database;
}

/// Résultat de recherche. Le titre et l'extrait sont du texte brut ; dans l'extrait, les termes
/// trouvés sont encadrés par SearchHit::kMarkStart et SearchHit::kMarkEnd.
struct SearchHit {
    enum class Kind { Document, Annotation, Comment, Note, Passage };
    static constexpr char16_t kMarkStart = u'';
    static constexpr char16_t kMarkEnd = u'';

    Kind kind = Kind::Document;
    qint64 documentId = 0;
    qint64 itemId = 0;              ///< document, annotation, commentaire, note ; passage : ligne de page_text
    QString documentTitle;          ///< note : son titre
    QString snippet;                ///< auteurs (document), passage (annotation), texte (commentaire)
    QString location;               ///< « p. 12 », « 42 % », « section 3 » ; note : son type ; vide pour un document
    int unit = -1;                  ///< passage : page (PDF) ou section (EPUB), à partir de 0
    DocumentFormat format = DocumentFormat::Pdf;
    AnnotationColor color = AnnotationColor::Yellow;  ///< annotation
    QString commentKind;            ///< libellé du type de commentaire
};

struct SearchResults {
    QString query;
    QList<SearchHit> documents;
    QList<SearchHit> annotations;
    QList<SearchHit> comments;
    QList<SearchHit> notes;
    QList<SearchHit> passages;      ///< texte intégral des documents
    bool isEmpty() const { return total() == 0; }
    int total() const
    {
        return int(documents.size() + annotations.size() + comments.size() + notes.size() + passages.size());
    }
};

/// Cible d'un lien studylab:// dans la base.
struct ResolvedLink {
    qint64 documentId = 0;
    bool comment = false;  ///< itemId désigne un commentaire (sinon une annotation)
    qint64 itemId = 0;     ///< 0 : ouvrir le document sans aller à un passage (annotation supprimée…)
};

/// Recherche universelle (index FTS5) : métadonnées des documents, annotations (passage cité et titre),
/// commentaires, notes (titre et texte) et texte intégral des documents (pages PDF, sections EPUB, extrait par
/// TextIndexer). Chaque mot est cherché comme préfixe, sans tenir compte des accents ni de la casse ; tous les mots
/// doivent être présents.
class SearchService {
public:
    explicit SearchService(db::Database& db);

    /// Au plus `perGroup` résultats par groupe, les plus pertinents d'abord (texte intégral : au plus deux passages
    /// par document). Requête vide : aucun résultat.
    Result<SearchResults> search(const QString& text, int perGroup = 8);

    /// Retrouve le document et le passage d'un lien d'export. Document supprimé ou inconnu : NotFound.
    Result<ResolvedLink> resolve(const AppLink& link);

    /// Retire les marqueurs d'un extrait.
    static QString plain(const QString& snippet);

private:
    db::Database& m_db;
};

}  // namespace studylab
