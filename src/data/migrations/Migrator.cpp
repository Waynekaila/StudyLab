#include "data/migrations/Migrator.h"

#include "core/Clock.h"
#include "core/Log.h"
#include "data/sqlite/Database.h"
#include "data/sqlite/Statement.h"
#include "data/sqlite/Transaction.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

namespace studylab::db {

namespace {
const QString kCreateTable = QStringLiteral(
    "CREATE TABLE IF NOT EXISTS schema_migrations ("
    " version INTEGER PRIMARY KEY, name TEXT NOT NULL, applied_at TEXT NOT NULL)");
}

Result<QList<Migration>> Migrator::loadFrom(const QString& directory)
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d{4})_([A-Za-z0-9_\\-]+)\\.sql$"));
    QDir dir(directory);
    if (!dir.exists())
        return Error::make(Error::Code::NotFound, QStringLiteral("Dossier de migrations introuvable : %1").arg(directory));

    QList<Migration> list;
    for (const QString& file : dir.entryList({QStringLiteral("*.sql")}, QDir::Files, QDir::Name)) {
        const auto m = pattern.match(file);
        if (!m.hasMatch())
            return Error::make(Error::Code::InvalidArgument, QStringLiteral("Nom de migration invalide : %1").arg(file));
        QFile f(dir.filePath(file));
        if (!f.open(QIODevice::ReadOnly))
            return Error::make(Error::Code::Io, QStringLiteral("Lecture impossible : %1").arg(f.fileName()));
        list.append(Migration{m.captured(1).toInt(), m.captured(2), QString::fromUtf8(f.readAll())});
    }
    std::sort(list.begin(), list.end(), [](const Migration& a, const Migration& b) { return a.version < b.version; });
    for (qsizetype i = 1; i < list.size(); ++i) {
        if (list[i].version == list[i - 1].version)
            return Error::make(Error::Code::InvalidArgument,
                               QStringLiteral("Deux migrations portent la version %1").arg(list[i].version));
    }
    return list;
}

Migrator::Migrator(QList<Migration> migrations) : m_migrations(std::move(migrations)) {}

int Migrator::latestVersion() const { return m_migrations.isEmpty() ? 0 : m_migrations.last().version; }

Result<int> Migrator::currentVersion(Database& db)
{
    auto exists = db.queryInt(QStringLiteral(
        "SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = 'schema_migrations'"));
    if (!exists)
        return exists.error();
    if (exists.value() == 0)
        return 0;
    auto v = db.queryInt(QStringLiteral("SELECT coalesce(max(version), 0) FROM schema_migrations"));
    if (!v)
        return v.error();
    return int(v.value());
}

Result<int> Migrator::migrate(Database& db, const BeforeMigrateHook& beforeMigrate) const
{
    auto current = currentVersion(db);
    if (!current)
        return current.error();
    const int from = current.value();
    const int to = latestVersion();

    if (from > to) {
        return Error::make(Error::Code::SchemaTooNew,
                           QStringLiteral("Cette base a été créée par une version plus récente de StudyLab "
                                          "(schéma %1, cette version connaît le schéma %2).")
                               .arg(from)
                               .arg(to));
    }
    if (from == to)
        return to;

    if (from > 0 && beforeMigrate) {
        if (auto r = beforeMigrate(from, to); !r)
            return r.error();
    }

    if (auto r = db.exec(kCreateTable); !r)
        return r.error();

    for (const Migration& m : m_migrations) {
        if (m.version <= from)
            continue;
        Transaction tx(db);
        if (!tx.isActive())
            return tx.beginError();
        if (auto r = db.exec(m.sql); !r) {
            return Error::make(Error::Code::Database,
                               QStringLiteral("Migration %1 (%2) : %3").arg(m.version).arg(m.name, r.error().message));
        }
        auto ins = db.prepare(QStringLiteral("INSERT INTO schema_migrations(version, name, applied_at) VALUES (?1, ?2, ?3)"));
        if (!ins)
            return ins.error();
        ins.value().bind(1, m.version).bind(2, m.name).bind(3, nowIsoUtc());
        if (auto r = ins.value().run(); !r)
            return r.error();
        if (auto r = tx.commit(); !r)
            return r.error();
        qCInfo(lcDb) << "Migration appliquée :" << m.version << m.name;
    }

    auto fk = db.queryInt(QStringLiteral("SELECT count(*) FROM pragma_foreign_key_check"));
    if (fk && fk.value() > 0)
        qCWarning(lcDb) << "Violations de clés étrangères après migration :" << fk.value();

    return to;
}

}  // namespace studylab::db
