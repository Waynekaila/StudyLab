#pragma once

#include "core/AppPaths.h"
#include "core/Result.h"

#include <QStringList>

#include <memory>

namespace studylab {

namespace db {
class Database;
}

/// Ouvre la base, vérifie son intégrité après un arrêt anormal, sauvegarde puis migre si nécessaire.
struct StartupReport {
    std::unique_ptr<db::Database> db;
    int schemaVersion = 0;
    bool previousRunCrashed = false;
    QStringList integrityProblems;
    QString preMigrationBackup;
    QString restoredFrom;      ///< restauration programmée appliquée à ce démarrage
    QString restoreError;      ///< échec de la restauration programmée (la base actuelle est conservée)
    QString automaticBackup;   ///< sauvegarde quotidienne créée à ce démarrage
};

Result<StartupReport> openAndMigrate(const AppPaths& paths);

/// Après un contrôle d'intégrité en échec, base fermée : restaure la plus récente sauvegarde saine.
/// Renvoie le fichier restauré.
Result<QString> restoreLatestBackup(const AppPaths& paths);

/// Marqueur de session : présent pendant l'exécution, supprimé à la fermeture normale.
void markRunning(const AppPaths& paths);
void markStopped(const AppPaths& paths);

}  // namespace studylab
