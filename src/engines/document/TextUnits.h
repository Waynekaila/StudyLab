#pragma once

#include <QString>

namespace studylab {

/// Texte extrait d'un document, tel qu'il sera indexé : espaces normalisés, césures de fin de ligne recollées
/// (« entro-\npie » → « entropie »), une ligne vide entre paragraphes au plus.
QString normalizeExtractedText(const QString& text);

}  // namespace studylab
