#pragma once

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
    enum class Kind { Document, Annotation, Comment };
    static constexpr char16_t kMarkStart = u'';
    static constexpr char16_t kMarkEnd = u'';

    Kind kind = Kind::Document;
    qint64 documentId = 0;
    qint64 itemId = 0;              ///< document, annotation ou commentaire
    QString documentTitle;
    QString snippet;                ///< auteurs (document), passage (annotation), texte (commentaire)
    QString location;               ///< « p. 12 », « 42 % » ; vide pour un document
    DocumentFormat format = DocumentFormat::Pdf;
    AnnotationColor color = AnnotationColor::Yellow;  ///< annotation
    QString commentKind;            ///< libellé du type de commentaire
};

struct SearchResults {
    QString query;
    QList<SearchHit> documents;
    QList<SearchHit> annotations;
    QList<SearchHit> comments;
    bool isEmpty() const { return documents.isEmpty() && annotations.isEmpty() && comments.isEmpty(); }
    int total() const { return int(documents.size() + annotations.size() + comments.size()); }
};

/// Recherche minimale du MVP (index FTS5) : métadonnées des documents, annotations (passage cité et
/// titre) et commentaires. Le texte intégral des documents arrive en V1. Chaque mot est cherché comme
/// préfixe, sans tenir compte des accents ni de la casse ; tous les mots doivent être présents.
class SearchService {
public:
    explicit SearchService(db::Database& db);

    /// Au plus `perGroup` résultats par groupe, les plus pertinents d'abord. Requête vide : aucun résultat.
    Result<SearchResults> search(const QString& text, int perGroup = 8);

    /// Retire les marqueurs d'un extrait.
    static QString plain(const QString& snippet);

private:
    db::Database& m_db;
};

}  // namespace studylab
