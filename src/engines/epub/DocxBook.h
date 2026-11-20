#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QString>

namespace studylab {

/// Document Word (.docx, Office Open XML) mis en forme de livre EPUB 3, en mémoire, pour le lecteur EPUB : même
/// principe que TextBook (annotations par CFI, recherche, sommaire, export).
///
/// Lecture seule et déterministe. Repris : titres (styles « Titre 1 » à « Titre 6 » ou niveau hiérarchique), paragraphes,
/// gras, italique, souligné, barré, exposant et indice, listes à puces et numérotées (imbriquées), tableaux, liens,
/// images PNG, JPEG, GIF, SVG, citations, notes de bas de page ; titre, auteur et langue des propriétés du document.
/// Ignorés : mise en page (colonnes, marges, en-têtes et pieds de page), commentaires et suivi des modifications
/// (le texte accepté est montré), objets incorporés, images EMF/WMF (remplacées par leur texte de remplacement).
/// Le livre est découpé aux titres du plus haut niveau qui se répète.
class DocxBook {
public:
    /// Extension .docx (sans tenir compte de la casse).
    static bool isDocxFile(const QString& path);
    /// Archive EPUB (ZIP) construite à partir du document.
    static Result<QByteArray> build(const QString& path);
};

}  // namespace studylab
