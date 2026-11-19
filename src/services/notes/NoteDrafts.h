#pragma once

#include "core/domain/Annotation.h"
#include "core/domain/Comment.h"
#include "core/domain/Document.h"
#include "core/domain/Note.h"

#include <QList>
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

}  // namespace NoteDrafts

}  // namespace studylab
