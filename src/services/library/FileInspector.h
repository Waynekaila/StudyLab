#pragma once

#include "core/Result.h"
#include "core/domain/Document.h"

#include <QString>

#include <optional>

namespace studylab {

/// Vérifications rapides sur un fichier avant import.
class FileInspector {
public:
    /// Format d'après le contenu (signature « %PDF- », archive ZIP dont l'entrée « mimetype » est EPUB),
    /// pas d'après l'extension. nullopt si le format n'est pas pris en charge.
    static std::optional<DocumentFormat> detectFormat(const QString& path);
    /// L'extension correspond-elle à un format pris en charge (tri rapide lors du parcours d'un dossier) ?
    static bool hasSupportedExtension(const QString& path);
    /// Empreinte SHA-256 en hexadécimal minuscule, lue par blocs (fichiers de plusieurs Go).
    static Result<QString> sha256(const QString& path);
};

}  // namespace studylab
