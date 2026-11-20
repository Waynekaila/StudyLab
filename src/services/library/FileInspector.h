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
    /// pas d'après l'extension ; TXT et Markdown d'après l'extension (.txt, .text, .md, .markdown), s'ils ne sont
    /// pas binaires. nullopt si le format n'est pas pris en charge.
    static std::optional<DocumentFormat> detectFormat(const QString& path);
    /// Fichier retenu lors du parcours d'un dossier (PDF et EPUB ; un texte s'importe en le choisissant lui-même).
    static bool hasSupportedExtension(const QString& path);
    /// Empreinte SHA-256 en hexadécimal minuscule, lue par blocs (fichiers de plusieurs Go).
    static Result<QString> sha256(const QString& path);
};

}  // namespace studylab
