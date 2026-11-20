#pragma once

#include "core/Result.h"

#include <QStringList>

#include <atomic>

namespace studylab {

/// Texte natif d'un PDF, une entrée par page (vide pour une page sans texte, ex. numérisée).
/// Utilisable depuis un thread de travail. `cancel` interrompt l'extraction (résultat : erreur).
class PdfTextExtractor {
public:
    static Result<QStringList> extract(const QString& path, const std::atomic_bool* cancel = nullptr);
};

}  // namespace studylab
