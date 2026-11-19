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
};

Result<StartupReport> openAndMigrate(const AppPaths& paths);

/// Marqueur de session : présent pendant l'exécution, supprimé à la fermeture normale.
void markRunning(const AppPaths& paths);
void markStopped(const AppPaths& paths);

}  // namespace studylab
