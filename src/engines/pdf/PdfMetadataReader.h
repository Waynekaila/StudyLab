#pragma once

#include "core/Result.h"
#include "engines/document/DocumentMetadata.h"

namespace studylab {

/// Lecture des métadonnées d'un PDF (dictionnaire Info) et rendu de la première page en couverture.
/// Utilisable depuis un thread de travail. Un PDF protégé par mot de passe n'est pas une erreur :
/// passwordProtected = true et le titre vient du nom de fichier (le mot de passe sera demandé à l'ouverture).
class PdfMetadataReader {
public:
    static Result<DocumentMetadata> read(const QString& path, int coverWidth = kCoverWidth);
};

}  // namespace studylab
