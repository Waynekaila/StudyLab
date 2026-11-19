#include "services/backup/BackupService.h"

#include "core/Log.h"
#include "data/sqlite/Database.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace studylab {

namespace {

const char* kStamp = "yyyyMMdd-HHmmss";

struct KindPrefix {
    BackupKind kind;
    const char* prefix;
};
const KindPrefix kPrefixes[] = {
    {BackupKind::Automatic, "studylab-auto-"},
    {BackupKind::Manual, "studylab-manuelle-"},
    {BackupKind::BeforeMigration, "studylab-avant-migration-"},
    {BackupKind::BeforeRestore, "studylab-avant-restauration-"},
};

QString prefixOf(BackupKind kind)
{
    for (const auto& p : kPrefixes) {
        if (p.kind == kind)
            return QString::fromLatin1(p.prefix);
    }
    return QStringLiteral("studylab-");
}

Result<void> copyOver(const QString& from, const QString& to)
{
    for (const QString& suffix : {QString(), QStringLiteral("-wal"), QStringLiteral("-shm"), QStringLiteral("-journal")}) {
        if (QFile::exists(to + suffix) && !QFile::remove(to + suffix))
            return Error::make(Error::Code::Io, QStringLiteral("Impossible de remplacer %1").arg(to + suffix));
    }
    const QString tmp = to + QStringLiteral(".restauration");
    QFile::remove(tmp);
    if (!QFile::copy(from, tmp))
        return Error::make(Error::Code::Io, QStringLiteral("Impossible de copier la sauvegarde %1").arg(from));
    QFile(tmp).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    if (!QFile::rename(tmp, to))
        return Error::make(Error::Code::Io, QStringLiteral("Impossible de mettre en place la base restaurée."));
    return {};
}

}  // namespace

QString backupKindLabel(BackupKind kind)
{
    switch (kind) {
    case BackupKind::Automatic: return QCoreApplication::translate("Backup", "Automatique");
    case BackupKind::Manual: return QCoreApplication::translate("Backup", "Manuelle");
    case BackupKind::BeforeMigration: return QCoreApplication::translate("Backup", "Avant mise à jour");
    case BackupKind::BeforeRestore: return QCoreApplication::translate("Backup", "Avant restauration");
    }
    return {};
}

BackupService::BackupService(AppPaths paths, db::Database& db) : m_paths(std::move(paths)), m_db(db) {}

QString BackupService::fileNameFor(BackupKind kind, const QDateTime& when, const QString& detail)
{
    QString name = prefixOf(kind);
    if (!detail.isEmpty())
        name += detail + QLatin1Char('-');
    return name + when.toString(QString::fromLatin1(kStamp)) + QStringLiteral(".db");
}

std::optional<BackupInfo> BackupService::parse(const QString& path)
{
    static const QRegularExpression stamp(QStringLiteral("(\\d{8}-\\d{6})\\.db$"));
    const QFileInfo fi(path);
    const QString name = fi.fileName();
    const auto m = stamp.match(name);
    if (!m.hasMatch())
        return std::nullopt;
    for (const auto& p : kPrefixes) {
        if (!name.startsWith(QLatin1String(p.prefix)))
            continue;
        BackupInfo info;
        info.path = fi.absoluteFilePath();
        info.kind = p.kind;
        info.createdAt = QDateTime::fromString(m.captured(1), QString::fromLatin1(kStamp));
        info.size = fi.size();
        if (!info.createdAt.isValid())
            return std::nullopt;
        return info;
    }
    return std::nullopt;
}

QList<BackupInfo> BackupService::list(const QString& directory)
{
    QList<BackupInfo> out;
    const QDir dir(directory);
    for (const QString& name : dir.entryList({QStringLiteral("studylab-*.db")}, QDir::Files)) {
        if (auto info = parse(dir.filePath(name)))
            out << *info;
    }
    std::sort(out.begin(), out.end(), [](const BackupInfo& a, const BackupInfo& b) {
        return a.createdAt != b.createdAt ? a.createdAt > b.createdAt : a.path > b.path;
    });
    return out;
}

QList<BackupInfo> BackupService::list() const { return list(m_paths.backupsDir()); }

std::optional<BackupInfo> BackupService::latest() const
{
    const auto all = list();
    if (all.isEmpty())
        return std::nullopt;
    return all.first();
}

Result<BackupInfo> BackupService::create(BackupKind kind, const QDateTime& now)
{
    if (!QDir().mkpath(m_paths.backupsDir()))
        return Error::make(Error::Code::Io, QStringLiteral("Impossible de créer le dossier %1").arg(m_paths.backupsDir()));
    QDateTime when = now;
    QString file = QDir(m_paths.backupsDir()).filePath(fileNameFor(kind, when));
    while (QFile::exists(file)) {  // deux sauvegardes dans la même seconde
        when = when.addSecs(1);
        file = QDir(m_paths.backupsDir()).filePath(fileNameFor(kind, when));
    }
    if (auto r = m_db.backupTo(file); !r)
        return r.error();
    auto info = parse(file);
    if (!info)
        return Error::make(Error::Code::Io, QStringLiteral("Sauvegarde illisible : %1").arg(file));
    return *info;
}

Result<std::optional<BackupInfo>> BackupService::backupIfDue(const QDateTime& now)
{
    for (const BackupInfo& b : list()) {
        if (b.kind != BackupKind::BeforeRestore && b.createdAt.secsTo(now) < 24 * 3600 && b.createdAt <= now) {
            rotate(now);
            return std::optional<BackupInfo>{};
        }
    }
    auto created = create(BackupKind::Automatic, now);
    if (!created)
        return created.error();
    rotate(now);
    return std::optional<BackupInfo>{created.value()};
}

QStringList BackupService::rotate(const QDateTime& now)
{
    QStringList removed;
    QSet<QString> weeks;
    QSet<QString> months;
    int beforeMigration = 0;
    int beforeRestore = 0;
    for (const BackupInfo& b : list()) {  // du plus récent au plus ancien
        bool keep = true;
        const qint64 days = b.createdAt.daysTo(now);
        switch (b.kind) {
        case BackupKind::Manual: break;
        case BackupKind::BeforeMigration: keep = ++beforeMigration <= 5; break;
        case BackupKind::BeforeRestore: keep = ++beforeRestore <= 3; break;
        case BackupKind::Automatic:
            if (days < 7) {
                keep = true;
            } else if (days < 7 + 4 * 7) {
                int year = 0;
                const int week = b.createdAt.date().weekNumber(&year);
                const QString key = QStringLiteral("%1-%2").arg(year).arg(week);
                keep = !weeks.contains(key) && weeks.size() < 4;
                weeks.insert(key);
            } else if (days < 7 + 4 * 7 + 186) {
                const QString key = b.createdAt.toString(QStringLiteral("yyyy-MM"));
                keep = !months.contains(key) && months.size() < 6;
                months.insert(key);
            } else {
                keep = false;
            }
            break;
        }
        if (!keep && QFile::remove(b.path))
            removed << b.path;
    }
    if (!removed.isEmpty())
        qCInfo(lcApp) << "Rotation des sauvegardes :" << removed.size() << "fichier(s) supprimé(s)";
    return removed;
}

Result<void> BackupService::validate(const QString& file)
{
    if (!QFileInfo(file).isFile())
        return Error::make(Error::Code::NotFound, QStringLiteral("Sauvegarde introuvable : %1").arg(file));
    auto opened = db::Database::open(file, db::Database::Mode::ReadOnly);
    if (!opened)
        return opened.error();
    auto check = opened.value()->checkIntegrity(false);
    if (!check)
        return check.error();
    if (!check.value().isEmpty())
        return Error::make(Error::Code::Database,
                           QStringLiteral("La sauvegarde est endommagée : %1").arg(check.value().first()));
    auto version = opened.value()->queryInt(QStringLiteral("SELECT MAX(version) FROM schema_migrations"));
    if (!version || version.value() <= 0)
        return Error::make(Error::Code::Database, QStringLiteral("Ce fichier n'est pas une base StudyLab."));
    return {};
}

QString BackupService::pendingFile(const AppPaths& paths) { return paths.dataDir() + QStringLiteral("/.restauration"); }

Result<void> BackupService::scheduleRestore(const QString& file)
{
    if (auto v = validate(file); !v)
        return v;
    QFile f(pendingFile(m_paths));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate) || f.write(QFileInfo(file).absoluteFilePath().toUtf8()) < 0)
        return Error::make(Error::Code::Io, QStringLiteral("Impossible de programmer la restauration."));
    return {};
}

std::optional<QString> BackupService::pendingRestore() const
{
    QFile f(pendingFile(m_paths));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    return QString::fromUtf8(f.readAll()).trimmed();
}

void BackupService::cancelPendingRestore() { QFile::remove(pendingFile(m_paths)); }

Result<void> BackupService::restoreNow(const AppPaths& paths, const QString& file)
{
    if (auto v = validate(file); !v)
        return v;
    const QString dbFile = paths.databaseFile();
    if (QFile::exists(dbFile)) {
        // Copie de sécurité de la base remplacée, si elle s'ouvre encore.
        auto current = db::Database::open(dbFile);
        if (current) {
            const QString safety = QDir(paths.backupsDir()).filePath(fileNameFor(BackupKind::BeforeRestore, QDateTime::currentDateTime()));
            QDir().mkpath(paths.backupsDir());
            if (auto r = current.value()->backupTo(safety); !r)
                qCWarning(lcApp) << "Copie de la base avant restauration impossible :" << r.error().message;
        }
    }
    if (auto r = copyOver(file, dbFile); !r)
        return r;
    qCInfo(lcApp) << "Base restaurée depuis" << file;
    return {};
}

Result<QString> BackupService::applyPendingRestore(const AppPaths& paths)
{
    QFile f(pendingFile(paths));
    if (!f.exists())
        return QString();
    if (!f.open(QIODevice::ReadOnly))
        return Error::make(Error::Code::Io, QStringLiteral("Restauration programmée illisible."));
    const QString file = QString::fromUtf8(f.readAll()).trimmed();
    f.close();
    QFile::remove(pendingFile(paths));  // une seule tentative, même en cas d'échec
    if (auto r = restoreNow(paths, file); !r)
        return r.error();
    return file;
}

}  // namespace studylab
