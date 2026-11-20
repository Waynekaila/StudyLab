#pragma once

#include "core/domain/Annotation.h"
#include "core/domain/Comment.h"
#include "core/domain/Document.h"
#include "core/domain/Note.h"

#include <QDate>
#include <QList>
#include <QStringList>
#include <QString>

namespace studylab {

/// Brouillon d'une note tirée de la lecture : titre, type, dossier proposé et texte Markdown.
struct NoteDraft {
    QString title;
    NoteType type = NoteType::Reading;
    QString folder;
    QString body;
};

/// Textes des notes créées depuis la lecture (05-maquettes §3 : « créer une Note »). Chaque passage est une
/// citation sourcée : auteur, titre, et page reliée au passage par un lien studylab://.
namespace NoteDrafts {

/// Dossier des notes de lecture.
QString readingFolder();
/// Citation d'une annotation, suivie de son commentaire éventuel.
QString quoteBlock(const DocumentRecord& doc, const AnnotationRecord& a);
/// Note d'une seule annotation (titre : début du passage).
NoteDraft fromAnnotation(const DocumentRecord& doc, const AnnotationRecord& a);
/// Note regroupant plusieurs annotations (ordre de lecture), avec leurs commentaires.
NoteDraft fromAnnotations(const DocumentRecord& doc, const QList<AnnotationRecord>& annotations);
/// Note développant un commentaire (titre : première ligne).
NoteDraft fromComment(const DocumentRecord& doc, const CommentRecord& c);
/// Note de lecture du document : fiche, passages annotés et questions en suspens.
NoteDraft fromDocument(const DocumentRecord& doc, const QList<AnnotationRecord>& annotations,
                       const QList<CommentRecord>& comments);

/// Matière d'une fiche de révision (V2-3) : documents de la matière avec leurs passages annotés et leurs questions,
/// notes de la matière, questions des cartes mémoire rattachées.
struct SheetDocument {
    DocumentRecord doc;
    QList<AnnotationRecord> annotations;
    QList<CommentRecord> comments;
};
struct SheetMaterial {
    QString subject;               ///< « Thermodynamique »
    QDate date;                    ///< jour de la génération
    QList<SheetDocument> documents;
    QList<NoteRecord> notes;       ///< hors fiches de révision
    QStringList cardQuestions;     ///< recto des cartes des paquets de la matière
};
struct SheetOptions {
    bool passages = true;
    bool concepts = true;
    bool formulas = true;
    bool questions = true;
    int maxPassagesPerDocument = 25;
};

/// Dossier des fiches de révision.
QString revisionFolder();
/// Brouillon de fiche de révision (type « Fiche de révision », statut « brouillon ») : passages annotés par
/// document (les commentés d'abord), définitions des notes « Concept », formules relevées dans les notes et les
/// commentaires, questions en suspens et questions des cartes, notes de la matière. Rien n'est inventé : chaque
/// élément renvoie à son origine.
NoteDraft revisionSheet(const SheetMaterial& material, const SheetOptions& options = {});
/// Vrai si la note est une fiche générée encore à relire (front matter « statut: brouillon »).
bool isDraftSheet(const QString& markdown);
/// Retire le statut « brouillon » (ligne statut et tag) : la fiche est relue.
QString markSheetReviewed(const QString& markdown);

}  // namespace NoteDrafts

}  // namespace studylab
