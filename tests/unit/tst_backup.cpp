#include "Bootstrap.h"

#include "core/AppPaths.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "services/backup/BackupService.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

using namespace studylab;

namespace {

/// Mode « écrivain » : écrit sans fin dans la base donnée, jusqu'à être tué (test de robustesse).
int runWriter(const QString& file)
{
    auto opened = db::Database::open(file);
    if (!opened)
        return 2;
    db::Database& database = *opened.value();
    QFile ready(file + QStringLiteral(".pret"));
    for (int i = 0;; ++i) {
        if (!database.exec(QStringLiteral("BEGIN IMMEDIATE")))
            return 3;
        for (int j = 0; j < 50; ++j) {
            auto s = database.prepare(QStringLiteral("INSERT INTO settings(key, value) VALUES (?1, ?2) "
                                                     "ON CONFLICT(key) DO UPDATE SET value = excluded.value"));
            if (!s)
                return 4;
            s.value().bind(1, QStringLiteral("charge/%1").arg(j)).bind(2, QStringLiteral("\"%1\"").arg(QString(400, QChar('x' + (i % 3)))));
            if (!s.value().run())
                return 5;
        }
        if (!database.exec(QStringLiteral("COMMIT")))
            return 6;
        if (i == 20 && ready.open(QIODevice::WriteOnly))
            ready.close();
    }
}

qint64 countSettings(db::Database& database)
{
    return database.queryInt(QStringLiteral("SELECT count(*) FROM settings")).value();
}

}  // namespace

class TstBackup : public QObject {
    Q_OBJECT

    static void touch(const QString& dir, BackupKind kind, const QDateTime& when, const QString& detail = {})
    {
        QFile f(QDir(dir).filePath(BackupService::fileNameFor(kind, when, detail)));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("x");
    }

private slots:
    void namesParseAndList()
    {
        const QDateTime t(QDate(2026, 11, 19), QTime(20, 15, 3));
        QCOMPARE(BackupService::fileNameFor(BackupKind::Automatic, t), QStringLiteral("studylab-auto-20261119-201503.db"));
        QCOMPARE(BackupService::fileNameFor(BackupKind::BeforeMigration, t, QStringLiteral("v2-v3")),
                 QStringLiteral("studylab-avant-migration-v2-v3-20261119-201503.db"));
        const auto info = BackupService::parse(QStringLiteral("/x/studylab-avant-migration-v2-v3-20261119-201503.db"));
        QVERIFY(info);
        QCOMPARE(info->kind, BackupKind::BeforeMigration);
        QCOMPARE(info->createdAt, t);
        QVERIFY(!BackupService::parse(QStringLiteral("/x/autre-20261119-201503.db")));
        QVERIFY(!BackupService::parse(QStringLiteral("/x/studylab-auto-2026.db")));
        QCOMPARE(backupKindLabel(BackupKind::Manual), QStringLiteral("Manuelle"));

        QTemporaryDir dir;
        touch(dir.path(), BackupKind::Manual, t.addDays(-3));
        touch(dir.path(), BackupKind::Automatic, t);
        touch(dir.path(), BackupKind::Automatic, t.addDays(-1));
        QFile(dir.filePath(QStringLiteral("notes.txt"))).open(QIODevice::WriteOnly);
        const auto list = BackupService::list(dir.path());
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[0].createdAt, t);
        QCOMPARE(list[2].kind, BackupKind::Manual);
    }

    void dailyBackupAndRotation()
    {
        QTemporaryDir dir;
        AppPaths paths(dir.path());
        QVERIFY(paths.ensureCreated());
        auto startup = openAndMigrate(paths);
        QVERIFY(startup.ok());
        // Premier démarrage : sauvegarde quotidienne créée
        QVERIFY(!startup.value().automaticBackup.isEmpty());
        QVERIFY(QFile::exists(startup.value().automaticBackup));
        BackupService service(paths, *startup.value().db);
        QCOMPARE(service.list().size(), 1);
        QVERIFY(BackupService::validate(service.latest()->path).ok());

        // Moins de 24 h : rien de plus ; au-delà : nouvelle sauvegarde
        const QDateTime now = service.latest()->createdAt;
        QVERIFY(!service.backupIfDue(now.addSecs(3600)).value());
        auto next = service.backupIfDue(now.addSecs(25 * 3600));
        QVERIFY(next.ok() && next.value());
        QCOMPARE(next.value()->kind, BackupKind::Automatic);
        QCOMPARE(service.list().size(), 2);

        // Rotation : 7 jours complets, puis une par semaine (4), puis une par mois (6) ; manuelles gardées
        QTemporaryDir rot;
        AppPaths rp(rot.path());
        QVERIFY(rp.ensureCreated());
        const QDateTime ref(QDate(2026, 11, 19), QTime(9, 0));
        for (int d = 0; d < 300; ++d)
            touch(rp.backupsDir(), BackupKind::Automatic, ref.addDays(-d));
        touch(rp.backupsDir(), BackupKind::Manual, ref.addDays(-400));
        for (int i = 0; i < 8; ++i)
            touch(rp.backupsDir(), BackupKind::BeforeMigration, ref.addDays(-i), QStringLiteral("v%1-v%2").arg(i).arg(i + 1));
        auto opened = db::Database::openInMemory();
        BackupService rs(rp, *opened.value());
        const QStringList removed = rs.rotate(ref);
        QVERIFY(!removed.isEmpty());
        const auto kept = rs.list();
        int autos = 0, manual = 0, migrations = 0;
        for (const BackupInfo& b : kept) {
            autos += b.kind == BackupKind::Automatic;
            manual += b.kind == BackupKind::Manual;
            migrations += b.kind == BackupKind::BeforeMigration;
            if (b.kind == BackupKind::Automatic)
                QVERIFY2(b.createdAt.daysTo(ref) < 7 + 28 + 186, qPrintable(b.path));
        }
        QCOMPARE(manual, 1);
        QCOMPARE(migrations, 5);
        QVERIFY2(autos >= 7 + 4 && autos <= 7 + 4 + 6, qPrintable(QString::number(autos)));
        for (int d = 0; d < 7; ++d) {
            const QString daily = QDir(rp.backupsDir()).filePath(BackupService::fileNameFor(BackupKind::Automatic, ref.addDays(-d)));
            QVERIFY2(QFile::exists(daily), qPrintable(daily));
        }
        QVERIFY(rs.rotate(ref).isEmpty());  // stable
    }

    void validateRejectsDamagedFiles()
    {
        QTemporaryDir dir;
        QFile junk(dir.filePath(QStringLiteral("studylab-manuelle-20261119-101010.db")));
        QVERIFY(junk.open(QIODevice::WriteOnly));
        junk.write(QByteArray(8192, 'z'));
        junk.close();
        QVERIFY(!BackupService::validate(junk.fileName()).ok());
        QVERIFY(!BackupService::validate(dir.filePath(QStringLiteral("absent.db"))).ok());
        // Base SQLite qui n'est pas une base StudyLab
        {
            auto other = db::Database::open(dir.filePath(QStringLiteral("autre.db")));
            QVERIFY(other.value()->exec(QStringLiteral("CREATE TABLE t(x)")).ok());
        }
        QVERIFY(!BackupService::validate(dir.filePath(QStringLiteral("autre.db"))).ok());
    }

    void scheduledRestoreAppliesAtNextStartup()
    {
        QTemporaryDir dir;
        AppPaths paths(dir.path());
        QString backupFile;
        {
            auto startup = openAndMigrate(paths);
            db::Database& database = *startup.value().db;
            QVERIFY(database.exec(QStringLiteral("INSERT INTO settings(key, value) VALUES ('etat', '\"avant\"')")).ok());
            BackupService service(paths, database);
            auto backup = service.create(BackupKind::Manual);
            QVERIFY2(backup.ok(), backup.ok() ? "" : qPrintable(backup.error().message));
            backupFile = backup.value().path;
            QVERIFY(database.exec(QStringLiteral("UPDATE settings SET value = '\"après\"' WHERE key = 'etat'")).ok());

            QVERIFY(!service.scheduleRestore(dir.filePath(QStringLiteral("absent.db"))).ok());
            QVERIFY(!service.pendingRestore());
            QVERIFY(service.scheduleRestore(backupFile).ok());
            QCOMPARE(*service.pendingRestore(), QFileInfo(backupFile).absoluteFilePath());
        }
        auto startup = openAndMigrate(paths);
        QVERIFY(startup.ok());
        QCOMPARE(startup.value().restoredFrom, QFileInfo(backupFile).absoluteFilePath());
        QVERIFY(startup.value().restoreError.isEmpty());
        QCOMPARE(startup.value().db->queryString(QStringLiteral("SELECT value FROM settings WHERE key = 'etat'")).value(),
                 QStringLiteral("\"avant\""));
        // La base remplacée a été conservée
        bool safety = false;
        for (const BackupInfo& b : BackupService::list(paths.backupsDir()))
            safety |= b.kind == BackupKind::BeforeRestore;
        QVERIFY(safety);
        BackupService service(paths, *startup.value().db);
        QVERIFY(!service.pendingRestore());

        // Restauration annulée : rien ne change au démarrage suivant
        QVERIFY(service.scheduleRestore(backupFile).ok());
        service.cancelPendingRestore();
        QVERIFY(!service.pendingRestore());
    }

    void killDuringWritesKeepsDatabaseIntact()
    {
        // 02-architecture §4 : « kill -9 pendant une écriture → base intègre au redémarrage ».
        QTemporaryDir dir;
        AppPaths paths(dir.path());
        {
            auto startup = openAndMigrate(paths);
            QVERIFY(startup.ok());
        }
        for (int round = 0; round < 3; ++round) {
            QFile::remove(paths.databaseFile() + QStringLiteral(".pret"));
            QProcess writer;
            QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
            env.insert(QStringLiteral("STUDYLAB_TEST_WRITER"), paths.databaseFile());
            writer.setProcessEnvironment(env);
            writer.start(QCoreApplication::applicationFilePath(), {});
            QVERIFY(writer.waitForStarted());
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(paths.databaseFile() + QStringLiteral(".pret")), 30000);
            QTest::qWait(30 + round * 40);
            writer.kill();  // SIGKILL sous Linux, TerminateProcess sous Windows
            QVERIFY(writer.waitForFinished());
            QCOMPARE(writer.exitStatus(), QProcess::CrashExit);

            markRunning(paths);  // l'arrêt brutal a laissé le marqueur
            auto startup = openAndMigrate(paths);
            QVERIFY2(startup.ok(), startup.ok() ? "" : qPrintable(startup.error().message));
            QVERIFY(startup.value().previousRunCrashed);
            QVERIFY2(startup.value().integrityProblems.isEmpty(),
                     qPrintable(startup.value().integrityProblems.join(QLatin1Char('\n'))));
            auto full = startup.value().db->checkIntegrity(true);
            QVERIFY(full.ok() && full.value().isEmpty());
            // Transactions entières : 50 lignes de charge, jamais une partie seulement
            QCOMPARE(startup.value().db->queryInt(QStringLiteral("SELECT count(*) FROM settings WHERE key LIKE 'charge/%'")).value(),
                     qint64(50));
            QVERIFY(countSettings(*startup.value().db) >= 50);
            markStopped(paths);
        }
    }

    void restoreLatestAfterCorruption()
    {
        QTemporaryDir dir;
        AppPaths paths(dir.path());
        {
            auto startup = openAndMigrate(paths);
            QVERIFY(startup.ok());
            QVERIFY(startup.value().db->exec(QStringLiteral("INSERT INTO settings(key, value) VALUES ('ok', '1')")).ok());
            QVERIFY(BackupService(paths, *startup.value().db).create(BackupKind::Manual, QDateTime::currentDateTime().addSecs(5)).ok());
        }
        // Base endommagée (en-tête écrasé)
        QFile f(paths.databaseFile());
        QVERIFY(f.open(QIODevice::ReadWrite));
        f.seek(0);
        f.write(QByteArray(4096, '\0'));
        f.close();
        QFile::remove(paths.databaseFile() + QStringLiteral("-wal"));
        QFile::remove(paths.databaseFile() + QStringLiteral("-shm"));

        auto restored = restoreLatestBackup(paths);
        QVERIFY2(restored.ok(), restored.ok() ? "" : qPrintable(restored.error().message));
        auto startup = openAndMigrate(paths);
        QVERIFY(startup.ok());
        QCOMPARE(startup.value().db->queryString(QStringLiteral("SELECT value FROM settings WHERE key = 'ok'")).value(),
                 QStringLiteral("1"));
    }
};

int main(int argc, char* argv[])
{
    const QString writer = qEnvironmentVariable("STUDYLAB_TEST_WRITER");
    if (!writer.isEmpty())
        return runWriter(writer);
    QCoreApplication app(argc, argv);
    TstBackup test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_backup.moc"
