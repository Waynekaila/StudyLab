#pragma once

#include "core/AppPaths.h"
#include "core/domain/Document.h"

#include <QSet>
#include <QString>
#include <QStringList>

#include <optional>

namespace studylab {

/// Résultat de la préparation d'un fichier (thread de travail) : un document prêt à être inséré,
/// un doublon ou un échec motivé.
struct ImportCandidate {
    QString sourcePath;
    std::optional<DocumentRecord> record;  ///< prêt à insérer (fichier copié, couverture écrite)
    QString duplicateHash;                 ///< non vide : déjà dans la bibliothèque (ou deux fois dans la sélection)
    bool duplicateInSelection = false;
    QString error;                         ///< non vide : échec, message lisible
};

/// Étapes d'import exécutées hors du thread de l'interface. Aucune écriture en base ici.
namespace ImportWorker {

/// Développe les dossiers (récursivement) en fichiers PDF/EPUB, dans un ordre stable.
/// Les fichiers explicitement choisis sont conservés même si leur extension est inattendue
/// (le contenu est vérifié ensuite).
QStringList expandPaths(const QStringList& inputs);

/// Vérifie, calcule l'empreinte, lit les métadonnées, copie le fichier (mode géré) et écrit la couverture.
/// `libraryHashes` : empreintes déjà en bibliothèque ; `batchHashes` : celles de la sélection en cours
/// (complété au fur et à mesure).
ImportCandidate prepare(const QString& path, const AppPaths& paths, StorageMode mode,
                        const QSet<QString>& libraryHashes, QSet<QString>& batchHashes);

/// Chemin relatif (au dossier library/) du fichier géré : « ab/abcdef….pdf ».
QString managedRelativePath(const QString& sha256, DocumentFormat format);

}  // namespace ImportWorker

}  // namespace studylab
