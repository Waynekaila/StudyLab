#include "data/sqlite/Database.h"

#include "core/Log.h"
#include "data/sqlite/Statement.h"

#include <QFile>

#include <sqlite3.h>

#include <utility>

namespace studylab::db {

Database::Database(sqlite3* db, QString path, Mode mode) : m_db(db), m_path(std::move(path)), m_mode(mode) {}

Database::~Database()
{
    if (m_db) {
        if (m_mode == Mode::ReadWrite)
            sqlite3_exec(m_db, "PRAGMA optimize;", nullptr, nullptr, nullptr);
        sqlite3_close_v2(m_db);
    }
}

Result<std::unique_ptr<Database>> Database::open(const QString& path, Mode mode)
{
    sqlite3* raw = nullptr;
    const int flags = (mode == Mode::ReadOnly ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE))
                      | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_EXRESCODE;
    const QByteArray utf8 = path.toUtf8();
    const int rc = sqlite3_open_v2(utf8.constData(), &raw, flags, nullptr);
    if (rc != SQLITE_OK) {
        const QString msg = raw ? QString::fromUtf8(sqlite3_errmsg(raw)) : QString::fromUtf8(sqlite3_errstr(rc));
        sqlite3_close_v2(raw);
        return Error::make(Error::Code::Database, QStringLiteral("Impossible d'ouvrir la base « %1 » : %2").arg(path, msg));
    }

    std::unique_ptr<Database> db(new Database(raw, path, mode));
    sqlite3_busy_timeout(raw, 5000);

    QString pragmas = QStringLiteral("PRAGMA foreign_keys = ON;");
    if (mode == Mode::ReadWrite && path != QLatin1String(":memory:"))
        pragmas += QStringLiteral("PRAGMA journal_mode = WAL; PRAGMA synchronous = NORMAL;");
    if (auto r = db->exec(pragmas); !r)
        return r.error();

    return db;
}

Result<std::unique_ptr<Database>> Database::openInMemory()
{
    return open(QStringLiteral(":memory:"));
}

Result<void> Database::exec(const QString& sql)
{
    char* err = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    const int rc = sqlite3_exec(m_db, utf8.constData(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        const QString msg = err ? QString::fromUtf8(err) : lastErrorMessage();
        sqlite3_free(err);
        return Error::make(Error::Code::Database, msg);
    }
    return Result<void>::success();
}

Result<Statement> Database::prepare(const QString& sql)
{
    sqlite3_stmt* stmt = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    const int rc = sqlite3_prepare_v3(m_db, utf8.constData(), int(utf8.size()), 0, &stmt, nullptr);
    if (rc != SQLITE_OK)
        return makeError(QStringLiteral("Préparation de la requête"));
    return Statement(stmt);
}

Result<QString> Database::queryString(const QString& sql)
{
    auto stmt = prepare(sql);
    if (!stmt)
        return stmt.error();
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    if (!row.value())
        return Error::make(Error::Code::NotFound, QStringLiteral("Aucun résultat"));
    return stmt.value().columnText(0);
}

Result<qint64> Database::queryInt(const QString& sql)
{
    auto stmt = prepare(sql);
    if (!stmt)
        return stmt.error();
    auto row = stmt.value().step();
    if (!row)
        return row.error();
    if (!row.value())
        return Error::make(Error::Code::NotFound, QStringLiteral("Aucun résultat"));
    return stmt.value().columnInt64(0);
}

qint64 Database::lastInsertRowId() const { return sqlite3_last_insert_rowid(m_db); }
int Database::changes() const { return sqlite3_changes(m_db); }

QStringList Database::compileOptions()
{
    QStringList options;
    for (int i = 0;; ++i) {
        const char* opt = sqlite3_compileoption_get(i);
        if (!opt)
            break;
        options << QString::fromUtf8(opt);
    }
    return options;
}

bool Database::hasCompileOption(const QString& option)
{
    return sqlite3_compileoption_used(option.toUtf8().constData()) != 0;
}

QString Database::sqliteVersion() { return QString::fromUtf8(sqlite3_libversion()); }

Result<QStringList> Database::checkIntegrity(bool full)
{
    auto stmt = prepare(full ? QStringLiteral("PRAGMA integrity_check") : QStringLiteral("PRAGMA quick_check"));
    if (!stmt)
        return stmt.error();
    QStringList problems;
    while (true) {
        auto row = stmt.value().step();
        if (!row)
            return row.error();
        if (!row.value())
            break;
        const QString line = stmt.value().columnText(0);
        if (line != QLatin1String("ok"))
            problems << line;
    }
    return problems;
}

Result<void> Database::backupTo(const QString& targetFile)
{
    if (QFile::exists(targetFile))
        return Error::make(Error::Code::Io, QStringLiteral("Le fichier de sauvegarde existe déjà : %1").arg(targetFile));
    auto stmt = prepare(QStringLiteral("VACUUM INTO ?1"));
    if (!stmt)
        return stmt.error();
    stmt.value().bind(1, targetFile);
    auto r = stmt.value().step();
    if (!r)
        return r.error();
    qCInfo(lcDb) << "Sauvegarde créée :" << targetFile;
    return Result<void>::success();
}

QString Database::lastErrorMessage() const { return QString::fromUtf8(sqlite3_errmsg(m_db)); }

Error Database::makeError(const QString& context) const
{
    return Error::make(Error::Code::Database, QStringLiteral("%1 : %2").arg(context, lastErrorMessage()));
}

}  // namespace studylab::db
