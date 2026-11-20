#pragma once

#include "core/Result.h"
#include "engines/document/DocumentMetadata.h"

#include <QImage>
#include <QString>
#include <QStringList>

namespace studylab {

/// Images importées comme documents (scan d'une page, photo d'un tableau, schéma, TIFF de plusieurs pages).
///
/// Une image est lue par le lecteur PDF à travers un PDF construit à la demande : une page par image, à la taille de
/// l'image (sa résolution si elle est connue, sinon 150 dpi). Zones, notes, signets, zoom, miniatures et OCR
/// fonctionnent ainsi comme pour un scan en PDF ; les ancres (rectangles normalisés) ne dépendent que de l'image.
namespace ImagePages {

/// Extensions reconnues et lisibles par les modules d'image installés (png, jpg, jpeg, gif, bmp, webp, tif, tiff).
QStringList extensions();
bool isImageFile(const QString& path);
/// Nombre d'images (pages) du fichier : plusieurs pour un TIFF multipage, 0 s'il est illisible.
int pageCount(const QString& path);
/// Image n° `index` (0 pour la première).
QImage page(const QString& path, int index);
/// Titre (nom du fichier), nombre de pages, couverture (première image réduite).
Result<DocumentMetadata> metadata(const QString& path, int coverWidth = kCoverWidth);
/// Écrit le PDF d'affichage (remplacé de façon atomique).
Result<void> writePdf(const QString& imagePath, const QString& pdfPath);

}  // namespace ImagePages

}  // namespace studylab
