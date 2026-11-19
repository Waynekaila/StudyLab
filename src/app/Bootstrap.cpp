#include "Bootstrap.h"

#include "core/Log.h"
#include "data/migrations/Migrator.h"
#include "data/sqlite/Database.h"
#include "services/backup/BackupService.h"

#include <QDateTime>
#include <QDir>
#include <QFile>

namespace studylab {

Result<StartupReport> openAndMigrate(const AppPaths& paths)
{
    StartupReport report;
    if (!paths.ensureCreated())
        return Error::make(Error::Code::Io, QStringLiteral("Impossible de créer le dossier de données : %1").arg(paths.dataDir()));

    report.previousRunCrashed = QFile::exists(paths.runningMarkerFile());

    if (auto restored = BackupService::applyPendingRestore(paths); restored)
        report.restoredFrom = restored.value();
    else
        report.restoreError = restored.error().message;

    auto opened = db::Database::open(paths.databaseFile());
    if (!opened)
        return opened.error();
    report.db = std::move(opened).value();

    if (report.previousRunCrashed) {
        qCWarning(lcApp) << "Fermeture anormale détectée : vérification de la base";
        auto check = report.db->checkIntegrity(false);
        if (check)
            report.integrityProblems = check.value();
        else
            report.integrityProblems << check.error().message;
    }

    auto migrations = db::Migrator::loadFrom();
    if (!migrations)
        return migrations.error();
    db::Migrator migrator(migrations.value());

    db::Database* database = report.db.get();
    auto version = migrator.migrate(*database, [&](int from, int to) -> Result<void> {
        const QString file = paths.backupsDir() + QLatin1Char('/')
                             + BackupService::fileNameFor(BackupKind::BeforeMigration, QDateTime::currentDateTime(),
                                                          QStringLiteral("v%1-v%2").arg(from).arg(to));
        auto r = database->backupTo(file);
        if (r)
            report.preMigrationBackup = file;
        return r;
    });
    if (!version)
        return version.error();
    report.schemaVersion = version.value();

    // Sauvegarde quotidienne, jamais d'une base signalée endommagée (la rotation pourrait évincer une bonne copie).
    if (report.integrityProblems.isEmpty()) {
        BackupService backups(paths, *database);
        auto due = backups.backupIfDue();
        if (!due)
            qCWarning(lcApp) << "Sauvegarde automatique impossible :" << due.error().message;
        else if (due.value())
            report.automaticBackup = due.value()->path;
    }
    return report;
}

Result<QString> restoreLatestBackup(const AppPaths& paths)
{
    for (const BackupInfo& b : BackupService::list(paths.backupsDir())) {
        if (b.kind == BackupKind::BeforeRestore || !BackupService::validate(b.path))
            continue;
        if (auto r = BackupService::restoreNow(paths, b.path); !r)
            return r.error();
        return b.path;
    }
    return Error::make(Error::Code::NotFound, QStringLiteral("Aucune sauvegarde saine n'a été trouvée."));
}

void markRunning(const AppPaths& paths)
{
    QFile f(paths.runningMarkerFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
}

void markStopped(const AppPaths& paths) { QFile::remove(paths.runningMarkerFile()); }

}  // namespace studylab
