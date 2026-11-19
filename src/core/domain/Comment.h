#pragma once

#include "core/domain/Annotation.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace studylab {

/// Type d'un commentaire (colonne comments.kind). « Synthèse » existe dans le schéma mais n'est pas
/// proposée dans le lecteur (elle servira aux fiches de révision).
enum class CommentKind { Reflection, Question, Explanation, Reference, Summary };

/// Ce à quoi le commentaire se rapporte (colonne comments.anchor_kind).
///   Free        : le document entier
///   Annotation  : une annotation (le commentaire est la note de l'annotation : un seul par annotation)
///   Passage     : une sélection de texte, sans annotation visible
///   Page        : une page (PDF) ou une position dans un chapitre (EPUB)
enum class CommentAnchor { Free, Annotation, Passage, Page };

QString commentKindKey(CommentKind k);  ///< "reflection", "question"…
std::optional<CommentKind> commentKindFromKey(const QString& key);
QString commentKindLabel(CommentKind k);  ///< « Réflexion », « Question »…
/// Types proposés dans l'éditeur, dans l'ordre d'affichage.
QList<CommentKind> editableCommentKinds();

QString commentAnchorKey(CommentAnchor a);
std::optional<CommentAnchor> commentAnchorFromKey(const QString& key);

/// Commentaire tel que stocké, avec ce qu'il faut pour l'afficher sans autre requête.
struct CommentRecord {
    qint64 id = 0;
    QString uuid;
    qint64 documentId = 0;
    qint64 annotationId = 0;     ///< ancre Annotation
    CommentAnchor anchor = CommentAnchor::Free;
    QJsonObject locator;          ///< ancre Passage ou Page (PdfLocator / EpubLocator + « pageLabel »)
    int unitIndex = -1;           ///< page (PDF) ou section (EPUB) ; -1 pour un commentaire libre
    CommentKind kind = CommentKind::Reflection;
    QString body;                 ///< Markdown
    bool resolved = false;        ///< question résolue
    QString createdAt;
    QString updatedAt;

    // Lu avec le commentaire (jointure sur l'annotation)
    QString quote;                ///< passage cité (annotation ou sélection)
    AnnotationColor annotationColor = AnnotationColor::Yellow;
    AnnotationType annotationType = AnnotationType::Highlight;
    QString pageLabel;
    double sortKey = -1.0;        ///< ordre de lecture ; -1 : commentaire libre (en tête)
};

/// Emplacement lisible : « p. 12 », « 42 % » ou vide pour un commentaire libre.
QString commentLocation(const CommentRecord& c);
/// Clé d'ordre de lecture déduite de l'ancre (voir CommentRecord::sortKey).
double commentSortKey(const CommentRecord& c);

/// Cibles des liens « [[cible]] » ou « [[cible|alias]] » d'un texte Markdown, sans doublon, dans l'ordre.
/// Les liens sont conservés tels quels dans le texte ; ils seront résolus avec l'espace de notes (V1).
QStringList wikiLinkTargets(const QString& markdown);

}  // namespace studylab

#include <QMetaType>
Q_DECLARE_METATYPE(studylab::CommentRecord)
