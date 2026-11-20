#pragma once

#include "core/Result.h"

#include <QByteArray>
#include <QStringList>

#include <atomic>

namespace studylab {

/// Texte d'un EPUB, une entrée par élément du spine (même indice que la section du lecteur).
/// Balises retirées, blocs séparés par des sauts de ligne, scripts et styles ignorés. Utilisable depuis un thread
/// de travail.
class EpubTextExtractor {
public:
    static Result<QStringList> extract(const QString& path, const std::atomic_bool* cancel = nullptr);
    /// Texte d'un fichier XHTML (exposé pour les tests).
    static QString xhtmlText(const QByteArray& xhtml);
};

}  // namespace studylab
