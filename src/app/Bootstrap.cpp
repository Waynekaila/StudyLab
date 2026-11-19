#include "Bootstrap.h"

#include "core/Log.h"
#include "data/migrations/Migrator.h"
#include "data/sqlite/Database.h"

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
        const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"));
        const QString file = paths.backupsDir() + QStringLiteral("/studylab-avant-migration-v%1-v%2-%3.db").arg(from).arg(to).arg(stamp);
        auto r = database->backupTo(file);
        if (r)
            report.preMigrationBackup = file;
        return r;
    });
    if (!version)
        return version.error();
    report.schemaVersion = version.value();
    return report;
}

void markRunning(const AppPaths& paths)
{
    QFile f(paths.runningMarkerFile());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toUtf8());
}

void markStopped(const AppPaths& paths) { QFile::remove(paths.runningMarkerFile()); }

}  // namespace studylab
